#include "Program.h"

#include "BaseConditions.h"
#include "BaseFunctions.h"
#include "BuiltinResources.h"
#include "ModRegistry.h"
#include "RegisteredMods.h"
#include "Resources.h"
#include "Utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace Program
{
	namespace
	{
		constexpr float kNoAnswer = std::numeric_limits<float>::quiet_NaN();

		[[nodiscard]] bool Holds(float a_value)
		{
			return a_value != 0.f && !std::isnan(a_value);
		}

		[[nodiscard]] bool Compares(float a_left, Conditions::ComparisonOperator a_comparison, float a_right)
		{
			if (std::isnan(a_left) || std::isnan(a_right)) {
				return false;
			}
			switch (a_comparison) {
			case Conditions::ComparisonOperator::kEqual:
				return a_left == a_right;
			case Conditions::ComparisonOperator::kNotEqual:
				return a_left != a_right;
			case Conditions::ComparisonOperator::kGreater:
				return a_left > a_right;
			case Conditions::ComparisonOperator::kGreaterEqual:
				return a_left >= a_right;
			case Conditions::ComparisonOperator::kLess:
				return a_left < a_right;
			case Conditions::ComparisonOperator::kLessEqual:
				return a_left <= a_right;
			default:
				return false;
			}
		}

		[[nodiscard]] std::uint32_t BitsOf(float a_value)
		{
			// -0 and 0 are the same number, and one key
			if (a_value == 0.f) {
				a_value = 0.f;
			}
			std::uint32_t bits = 0;
			std::memcpy(&bits, &a_value, sizeof(bits));
			return bits;
		}
	}

	void Compiled::Clear()
	{
		subjects = { {} };
		probes.clear();
		nodes.clear();
		rules.clear();
		worldProbes.clear();
		actorProbes.clear();
		sets.clear();
		live.clear();
		functionRules.clear();
		rowsSeen = 0;
		rowsUnsupported = 0;
		rowNodes.clear();
	}

	Expr Builder::Intern(Node a_node)
	{
		// Hashed by what it asks, so two rows asking the same thing are one node answered once
		auto key = std::format("{}|{}|{}|{}|{}|{}|{}", static_cast<int>(a_node.op), BitsOf(a_node.value), a_node.probe, static_cast<int>(a_node.comparison), a_node.a, a_node.b, a_node.set);
		for (const auto child : a_node.children) {
			key += std::format(",{}", child);
		}
		if (const auto it = _nodeIndex.find(key); it != _nodeIndex.end()) {
			return { it->second };
		}
		const auto index = static_cast<std::uint32_t>(_out.nodes.size());
		_out.nodes.push_back(std::move(a_node));
		_nodeIndex.emplace(std::move(key), index);
		return { index };
	}

	std::uint32_t Builder::Subject()
	{
		const auto it = std::ranges::find(_out.subjects, _subject);
		if (it != _out.subjects.end()) {
			return static_cast<std::uint32_t>(it - _out.subjects.begin());
		}
		_out.subjects.push_back(_subject);
		return static_cast<std::uint32_t>(_out.subjects.size() - 1);
	}

	Expr Builder::Read(std::string a_key, std::function<float(RE::TESObjectREFR*)> a_read)
	{
		const auto subject = Subject();
		auto key = std::format("{}@{}", a_key, subject);
		std::uint32_t probe = 0;
		if (const auto it = _probeIndex.find(key); it != _probeIndex.end()) {
			probe = it->second;
		} else {
			probe = static_cast<std::uint32_t>(_out.probes.size());
			_out.probes.push_back({ key, false, subject, std::move(a_read) });
			_out.actorProbes.push_back(probe);
			_probeIndex.emplace(std::move(key), probe);
		}
		return Intern({ .op = Op::kProbe, .probe = probe });
	}

	Expr Builder::ReadWorld(std::string a_key, std::function<float()> a_read)
	{
		auto key = std::format("{}@world", a_key);
		std::uint32_t probe = 0;
		if (const auto it = _probeIndex.find(key); it != _probeIndex.end()) {
			probe = it->second;
		} else {
			probe = static_cast<std::uint32_t>(_out.probes.size());
			_out.probes.push_back({ key, true, 0, [read = std::move(a_read)](RE::TESObjectREFR*) { return read(); } });
			_out.worldProbes.push_back(probe);
			_probeIndex.emplace(std::move(key), probe);
		}
		return Intern({ .op = Op::kProbe, .probe = probe });
	}

	Expr Builder::ResourceSet(std::string a_key)
	{
		const auto subject = Subject();
		auto key = std::format("resource|{}@{}", a_key, subject);
		std::uint32_t probe = 0;
		if (const auto it = _probeIndex.find(key); it != _probeIndex.end()) {
			probe = it->second;
		} else {
			probe = static_cast<std::uint32_t>(_out.probes.size());
			Probe one{ .key = key, .subject = subject, .bSet = true };
			one.readSet = [resource = a_key](RE::TESObjectREFR* a_refr, std::vector<std::uint64_t>& a_out) {
				Resources::GetSet(resource, a_refr, a_out);
			};
			_out.probes.push_back(std::move(one));
			_out.actorProbes.push_back(probe);
			_probeIndex.emplace(std::move(key), probe);
		}
		return Intern({ .op = Op::kProbe, .probe = probe });
	}

	Expr Builder::SetTest(Op a_op, Expr a_set, std::vector<std::uint64_t> a_members)
	{
		std::ranges::sort(a_members);
		a_members.erase(std::ranges::unique(a_members).begin(), a_members.end());
		if (a_members.empty()) {
			return a_op == Op::kHasAll ? True() : False();
		}
		// One member is the same question either way: asked as any, so it joins with both
		if (a_members.size() == 1) {
			a_op = Op::kHasAny;
		}
		std::string key;
		for (const auto member : a_members) {
			key += std::format("{},", member);
		}
		std::uint32_t set = 0;
		if (const auto it = _setIndex.find(key); it != _setIndex.end()) {
			set = it->second;
		} else {
			set = static_cast<std::uint32_t>(_out.sets.size());
			_out.sets.push_back(std::move(a_members));
			_setIndex.emplace(std::move(key), set);
		}
		return Intern({ .op = a_op, .probe = _out.nodes[a_set.node].probe, .a = a_set.node, .set = set });
	}

	Expr Builder::HasAny(Expr a_set, std::vector<std::uint64_t> a_members)
	{
		return SetTest(Op::kHasAny, a_set, std::move(a_members));
	}

	Expr Builder::HasAll(Expr a_set, std::vector<std::uint64_t> a_members)
	{
		return SetTest(Op::kHasAll, a_set, std::move(a_members));
	}

	Expr Builder::Meets(Expr a_set, Expr a_other)
	{
		return Intern({ .op = Op::kMeets, .probe = _out.nodes[a_set.node].probe, .a = a_set.node, .b = a_other.node, .set = _out.nodes[a_other.node].probe });
	}

	std::vector<Expr> Builder::Merge(Op a_op, std::vector<Expr> a_children)
	{
		const Op joined = a_op == Op::kAny ? Op::kHasAny : Op::kHasAll;
		std::map<std::uint32_t, std::vector<std::uint64_t>> bySet;  // the set's node, its members
		std::map<std::pair<std::uint32_t, int>, float> bounds;      // the number's node and the comparison, the bound
		std::vector<Expr> out;
		for (const auto child : a_children) {
			const auto node = _out.nodes[child.node];
			// A set test of this kind, or of one member, on a set: joined
			if (node.op == joined || (node.op == Op::kHasAny && _out.sets[node.set].size() == 1)) {
				auto& members = bySet[node.a];
				members.insert(members.end(), _out.sets[node.set].begin(), _out.sets[node.set].end());
				continue;
			}
			// A number against a constant, above or below: under All the strictest bound holds, under Any the loosest
			using Comparison = Conditions::ComparisonOperator;
			const bool bBound = node.op == Op::kCompare && _out.nodes[node.b].op == Op::kConstant &&
			                    (node.comparison == Comparison::kGreater || node.comparison == Comparison::kGreaterEqual || node.comparison == Comparison::kLess || node.comparison == Comparison::kLessEqual);
			if (bBound) {
				const float value = _out.nodes[node.b].value;
				const bool bAbove = node.comparison == Comparison::kGreater || node.comparison == Comparison::kGreaterEqual;
				const bool bHigher = (a_op == Op::kAll) == bAbove;
				const auto key = std::pair{ node.a, static_cast<int>(node.comparison) };
				if (const auto it = bounds.find(key); it != bounds.end()) {
					it->second = bHigher ? (std::max)(it->second, value) : (std::min)(it->second, value);
				} else {
					bounds.emplace(key, value);
				}
				continue;
			}
			out.push_back(child);
		}
		for (auto& [set, members] : bySet) {
			out.push_back(SetTest(joined, Expr{ set }, std::move(members)));
		}
		for (const auto& [key, value] : bounds) {
			out.push_back(Compare(Expr{ key.first }, static_cast<Conditions::ComparisonOperator>(key.second), Constant(value)));
		}
		return out;
	}

	Expr Builder::Constant(float a_value)
	{
		return Intern({ .op = Op::kConstant, .value = a_value });
	}

	Expr Builder::Compare(Expr a_left, Conditions::ComparisonOperator a_comparison, Expr a_right)
	{
		return Intern({ .op = Op::kCompare, .comparison = a_comparison, .a = a_left.node, .b = a_right.node });
	}

	Expr Builder::Truthy(Expr a_value)
	{
		return Intern({ .op = Op::kTruthy, .a = a_value.node });
	}

	Expr Builder::Not(Expr a_value)
	{
		return Intern({ .op = Op::kNot, .a = a_value.node });
	}

	Expr Builder::All(std::vector<Expr> a_children)
	{
		Node node{ .op = Op::kAll };
		for (const auto child : Merge(Op::kAll, std::move(a_children))) {
			node.children.push_back(child.node);
		}
		return Intern(std::move(node));
	}

	Expr Builder::Any(std::vector<Expr> a_children)
	{
		Node node{ .op = Op::kAny };
		for (const auto child : Merge(Op::kAny, std::move(a_children))) {
			node.children.push_back(child.node);
		}
		return Intern(std::move(node));
	}

	Expr Builder::One(std::vector<Expr> a_children)
	{
		Node node{ .op = Op::kOne };
		for (const auto child : a_children) {
			node.children.push_back(child.node);
		}
		return Intern(std::move(node));
	}

	Expr Builder::Resource(std::string a_key)
	{
		return Read(std::format("resource|{}", a_key), [key = a_key](RE::TESObjectREFR* a_refr) {
			return Resources::GetNumber(key, a_refr).value_or(kNoAnswer);
		});
	}

	Expr Builder::ResourceWorld(std::string a_key)
	{
		return ReadWorld(std::format("resource|{}", a_key), [key = a_key] {
			return Resources::GetNumber(key, nullptr).value_or(kNoAnswer);
		});
	}

	Expr Builder::Number(const Components::NumericValue& a_value)
	{
		switch (a_value.GetType()) {
		case Components::NumericValue::Type::kGlobalVariable:
			return ResourceWorld(BuiltinResources::GlobalKey(a_value));
		case Components::NumericValue::Type::kActorValue:
			return Resource(BuiltinResources::ActorValueKey(a_value));
		case Components::NumericValue::Type::kFeed:
			return Resource(a_value.GetArgument());
		default:
			return Constant(a_value.GetValue(nullptr));
		}
	}

	Expr Builder::Compare(Expr a_reading, const Conditions::ComparisonConditionComponent* a_comparison, const Conditions::NumericConditionComponent* a_number)
	{
		return Compare(a_reading, a_comparison->comparisonOperator, Number(a_number->value));
	}

	Expr Builder::Row(const Conditions::ICondition* a_condition)
	{
		if (!a_condition || a_condition->IsDisabled()) {
			return True();
		}
		++_out.rowsSeen;

		// A row pointing at nothing never holds, so NOT of it always does
		Expr answer;
		if (!a_condition->IsValid()) {
			answer = False();
		} else if (const auto* base = dynamic_cast<const Conditions::ConditionBase*>(a_condition)) {
			answer = base->Compile(*this);
		} else {
			// A condition another plugin registered: nothing here can compile it
			++_out.rowsUnsupported;
			answer = False();
		}
		const Expr row = a_condition->IsNegated() ? Not(answer) : answer;
		_out.rowNodes[{ _owner, a_condition }] = row.node;
		return row;
	}

	std::vector<Expr> Builder::Rows(Conditions::ConditionSet* a_set)
	{
		std::vector<Expr> rows;
		if (!a_set) {
			logger::error("program: a row's condition list is missing; it compiles to nothing");
			return rows;
		}
		a_set->ForEach([&](std::unique_ptr<Conditions::ICondition>& a_condition) {
			if (!a_condition->IsDisabled()) {
				rows.push_back(Row(a_condition.get()));
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return rows;
	}

	Expr Builder::All(Conditions::ConditionSet* a_set)
	{
		return All(Rows(a_set));
	}

	Expr Builder::Any(Conditions::ConditionSet* a_set)
	{
		return Any(Rows(a_set));
	}

	Expr Builder::One(Conditions::ConditionSet* a_set)
	{
		return One(Rows(a_set));
	}

	void Builder::PushSubject(Step a_step)
	{
		_subject.push_back(a_step);
	}

	void Builder::PopSubject()
	{
		if (!_subject.empty()) {
			_subject.pop_back();
		}
	}

	void Compile(Compiled& a_out)
	{
		a_out.Clear();

		// One builder for every rule, so a reading or a test two mods share is taken once
		Builder builder(a_out);
		ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
			a_mod->ForEachSubMod([&](SubMod* a_subMod) {
				Rule rule;
				rule.mod = a_mod->GetName();
				rule.subMod = a_subMod->GetName();
				rule.owner = a_subMod;
				rule.bModifier = a_subMod->GetKind() == SubModKind::kModifier;
				builder.SetOwner(a_subMod);
				rule.root = builder.All(a_subMod->GetConditionSet()).node;
				a_out.rules.push_back(rule);

				// Every CONDITION function it runs, however deep: its list a rule beside the clip's or the modifier's
				const std::function<void(Functions::FunctionSet*)> functions = [&](Functions::FunctionSet* a_set) {
					if (!a_set) {
						return;
					}
					a_set->ForEach([&](std::unique_ptr<Functions::IFunction>& a_function) {
						for (std::uint32_t i = 0; i < a_function->GetNumComponents(); ++i) {
							auto* component = a_function->GetComponent(i);
							if (component && component->GetType() == Functions::FunctionComponentType::kCondition) {
								auto* condition = static_cast<Functions::IConditionFunctionComponent*>(component);
								if (auto* set = condition->GetConditions()) {
									Rule own = rule;
									own.conditions = set;
									own.root = builder.All(set).node;
									a_out.functionRules[set] = static_cast<std::uint32_t>(a_out.rules.size());
									a_out.rules.push_back(std::move(own));
								}
								functions(condition->GetFunctions());
							} else if (component && component->GetType() == Functions::FunctionComponentType::kMulti) {
								functions(static_cast<Functions::IMultiFunctionComponent*>(component)->GetFunctions());
							}
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				};
				for (const auto type : { Functions::FunctionSetType::kOnActivate, Functions::FunctionSetType::kOnDeactivate, Functions::FunctionSetType::kOnTrigger }) {
					functions(a_subMod->GetFunctionSet(type));
				}
				if (a_subMod->GetKind() == SubModKind::kClip) {
					for (auto& variant : static_cast<Clip*>(a_subMod)->GetTracks()) {
						for (auto& keyframe : variant.keyframes) {
							functions(keyframe->functions.get());
						}
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		});
		builder.SetOwner(nullptr);

		// The nodes the rules read, through every operand: what an untraced actor answers
		std::vector<std::uint8_t> reached(a_out.nodes.size(), 0);
		std::vector<std::uint32_t> pending;
		for (const auto& rule : a_out.rules) {
			pending.push_back(rule.root);
		}
		while (!pending.empty()) {
			const auto index = pending.back();
			pending.pop_back();
			if (index >= reached.size()) {
				logger::error("program: node {} is past the {} nodes built", index, reached.size());
				continue;
			}
			if (reached[index]) {
				continue;
			}
			reached[index] = 1;
			const auto& node = a_out.nodes[index];
			switch (node.op) {
			case Op::kCompare:
			case Op::kMeets:
				pending.push_back(node.a);
				pending.push_back(node.b);
				break;
			case Op::kTruthy:
			case Op::kNot:
			case Op::kHasAny:
			case Op::kHasAll:
				pending.push_back(node.a);
				break;
			default:
				pending.insert(pending.end(), node.children.begin(), node.children.end());
				break;
			}
		}
		for (std::uint32_t i = 0; i < reached.size(); ++i) {
			if (reached[i]) {
				a_out.live.push_back(i);
			}
		}
	}

	void ResolveSubjects(const Compiled& a_program, RE::TESObjectREFR* a_actor, std::vector<RE::TESObjectREFRPtr>& a_out)
	{
		a_out.resize(a_program.subjects.size());
		for (std::size_t s = 0; s < a_program.subjects.size(); ++s) {
			RE::TESObjectREFRPtr current(a_actor);
			for (const auto& step : a_program.subjects[s]) {
				if (!current) {
					break;
				}
				if (step.kind == Step::Kind::kPlayer) {
					current = RE::TESObjectREFRPtr(RE::PlayerCharacter::GetSingleton());
					continue;
				}
				// The target, as its resource holds it for whoever the step before named
				auto* form = Resources::GetForm(BuiltinResources::TargetKey(step.targetType), current.get());
				current = RE::TESObjectREFRPtr(form ? form->As<RE::TESObjectREFR>() : nullptr);
			}
			a_out[s] = current;
		}
	}

	void Evaluate(const Compiled& a_program, const Facts::WorldFacts& a_world, const Facts::ActorFacts& a_actor, bool a_bEveryNode,
		std::vector<float>& a_nodeValues, std::vector<std::uint8_t>& a_ruleAnswers)
	{
		static const std::vector<std::uint64_t> kNone;
		const auto setOf = [&](std::uint32_t a_probe) -> const std::vector<std::uint64_t>& {
			const auto& sets = a_program.probes[a_probe].bWorld ? a_world.sets : a_actor.sets;
			if (a_probe >= sets.size()) {
				if (static bool bSaid = false; !bSaid) {
					bSaid = true;
					logger::error("program: set probe {} is past the {} sets gathered for this program; it reads as empty", a_probe, sets.size());
				}
				return kNone;
			}
			return sets[a_probe];
		};
		// Each node once, in order: each reads only nodes before it
		const auto count = a_bEveryNode ? a_program.nodes.size() : a_program.live.size();
		for (std::size_t n = 0; n < count; ++n) {
			const auto i = a_bEveryNode ? n : a_program.live[n];
			const auto& node = a_program.nodes[i];
			float value = 0.f;
			switch (node.op) {
			case Op::kConstant:
				value = node.value;
				break;
			case Op::kProbe:
				value = a_program.probes[node.probe].bWorld ? a_world.values[node.probe] : a_actor.values[node.probe];
				break;
			case Op::kHasAny:
				{
					const auto& have = setOf(node.probe);
					const auto& wanted = a_program.sets[node.set];
					auto h = have.begin();
					auto w = wanted.begin();
					while (h != have.end() && w != wanted.end() && *h != *w) {
						*h < *w ? ++h : ++w;
					}
					value = h != have.end() && w != wanted.end() ? 1.f : 0.f;
					break;
				}
			case Op::kHasAll:
				value = std::ranges::includes(setOf(node.probe), a_program.sets[node.set]) ? 1.f : 0.f;
				break;
			case Op::kMeets:
				{
					const auto& have = setOf(node.probe);
					const auto& other = setOf(node.set);
					auto h = have.begin();
					auto o = other.begin();
					while (h != have.end() && o != other.end() && *h != *o) {
						*h < *o ? ++h : ++o;
					}
					value = h != have.end() && o != other.end() ? 1.f : 0.f;
					break;
				}
			case Op::kCompare:
				value = Compares(a_nodeValues[node.a], node.comparison, a_nodeValues[node.b]) ? 1.f : 0.f;
				break;
			case Op::kTruthy:
				value = Holds(a_nodeValues[node.a]) ? 1.f : 0.f;
				break;
			case Op::kNot:
				value = Holds(a_nodeValues[node.a]) ? 0.f : 1.f;
				break;
			case Op::kAll:
				value = std::ranges::all_of(node.children, [&](std::uint32_t a_child) { return Holds(a_nodeValues[a_child]); }) ? 1.f : 0.f;
				break;
			case Op::kAny:
				value = node.children.empty() || std::ranges::any_of(node.children, [&](std::uint32_t a_child) { return Holds(a_nodeValues[a_child]); }) ? 1.f : 0.f;
				break;
			case Op::kOne:
				value = std::ranges::count_if(node.children, [&](std::uint32_t a_child) { return Holds(a_nodeValues[a_child]); }) == 1 ? 1.f : 0.f;
				break;
			}
			a_nodeValues[i] = value;
		}

		for (std::size_t i = 0; i < a_program.rules.size(); ++i) {
			a_ruleAnswers[i] = static_cast<std::uint8_t>(Holds(a_nodeValues[a_program.rules[i].root]));
		}
	}
}

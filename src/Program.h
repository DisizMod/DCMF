#pragma once

#include "Facts.h"

#include "ConditionTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class SubMod;

namespace Components
{
	class NumericValue;
}

namespace Conditions
{
	class ConditionSet;
	class ComparisonConditionComponent;
	class NumericConditionComponent;
}

namespace Program
{
	// Who a reading is taken off: the actor, the player, or a target of the one before, as TARGET and PLAYER nest
	struct Step
	{
		enum class Kind : std::uint8_t
		{
			kPlayer,
			kTarget
		};

		Kind kind = Kind::kPlayer;
		std::int32_t targetType = 0;  // Utils::TargetType, for a target

		[[nodiscard]] bool operator==(const Step&) const = default;
	};

	// One value Gather fetches: off the world once per tick, or off a subject once per actor per tick. The key says
	// what it reads, so two readings of the same thing are one. NaN is no answer: a subject that is not there, or not
	// an actor, where the condition itself would have said false
	struct Probe
	{
		std::string key;
		bool bWorld = false;
		std::uint32_t subject = 0;  // into Compiled::subjects; 0 is the actor itself
		std::function<float(RE::TESObjectREFR*)> read;
		bool bSet = false;  // read into the facts' sets rather than their values
		std::function<void(RE::TESObjectREFR*, std::vector<std::uint64_t>&)> readSet;
	};

	// One step of a test, answered in Evaluate from the probes' values and nothing else
	enum class Op : std::uint8_t
	{
		kConstant,  // value
		kProbe,     // probe's value
		kCompare,   // a, comparison, b; false when either is NaN
		kTruthy,    // a is neither 0 nor NaN
		kNot,       // a
		kAll,       // children; none is true
		kAny,       // children; none is true
		kOne,       // exactly one of children; none is false
		kHasAny,    // the set probe holds any member of set
		kHasAll,    // the set probe holds every member of set
		kMeets      // the set probe and the set probe at set share a member
	};

	struct Node
	{
		Op op = Op::kConstant;
		float value = 0.f;
		std::uint32_t probe = 0;
		Conditions::ComparisonOperator comparison = Conditions::ComparisonOperator::kEqual;
		std::uint32_t a = 0;
		std::uint32_t b = 0;
		std::uint32_t set = 0;  // into Compiled::sets
		std::vector<std::uint32_t> children;
	};

	// A node, as the builder hands it back
	struct Expr
	{
		std::uint32_t node = 0;
	};

	// One clip or modifier's conditions: the node that answers them all
	struct Rule
	{
		std::string mod;
		std::string subMod;
		std::uint32_t root = 0;
		const SubMod* owner = nullptr;  // what holds when the rule does; stale after an edit until the next compile
		bool bModifier = false;         // a clip's rule is answered and not resolved
		const void* conditions = nullptr;  // a CONDITION function's list: answered for it when it runs, never held or played
	};

	struct Compiled
	{
		std::vector<std::vector<Step>> subjects{ {} };  // the actor itself first
		std::vector<Probe> probes;
		std::vector<Node> nodes;  // each after the nodes it reads
		std::vector<Rule> rules;
		std::vector<std::uint32_t> worldProbes;
		std::vector<std::uint32_t> actorProbes;
		std::vector<std::vector<std::uint64_t>> sets;  // the members the set tests ask about, each sorted
		std::vector<std::uint32_t> live;               // the nodes a rule reads, in order; a row merged away is not among them
		std::unordered_map<const void*, std::uint32_t> functionRules;  // a CONDITION function's list, its rule

		std::size_t rowsSeen = 0;
		std::size_t rowsUnsupported = 0;  // a condition from another plugin, which cannot be compiled: never holds

		// By its clip or modifier and the condition, the node a row's answer is, negation included; for the log's trace
		std::map<std::pair<const void*, const void*>, std::uint32_t> rowNodes;

		void Clear();
	};

	// What every condition compiles itself with: readings for Gather, and tests over them for Evaluate. A condition's
	// Compile says what it reads off its subject, the subject being whoever the TARGET or PLAYER around it names
	class Builder
	{
	public:
		explicit Builder(Compiled& a_out) :
			_out(a_out) {}

		// A reading off the subject in force; the key names what it reads, and never the condition asking
		[[nodiscard]] Expr Read(std::string a_key, std::function<float(RE::TESObjectREFR*)> a_read);
		// A reading off the world, the same for every actor
		[[nodiscard]] Expr ReadWorld(std::string a_key, std::function<float()> a_read);

		[[nodiscard]] Expr Constant(float a_value);
		[[nodiscard]] Expr True() { return Constant(1.f); }
		[[nodiscard]] Expr False() { return Constant(0.f); }
		[[nodiscard]] Expr Compare(Expr a_left, Conditions::ComparisonOperator a_comparison, Expr a_right);
		[[nodiscard]] Expr Truthy(Expr a_value);
		[[nodiscard]] Expr Not(Expr a_value);
		[[nodiscard]] Expr All(std::vector<Expr> a_children);
		[[nodiscard]] Expr Any(std::vector<Expr> a_children);
		[[nodiscard]] Expr One(std::vector<Expr> a_children);

		// A registered number resource: off the subject in force, or off the world
		[[nodiscard]] Expr Resource(std::string a_key);
		[[nodiscard]] Expr ResourceWorld(std::string a_key);
		// A number as a component holds it: a constant, or a global, an actor value or a feed as its resource
		[[nodiscard]] Expr Number(const Components::NumericValue& a_value);
		// The shape most conditions have: a reading, a comparison and a number
		[[nodiscard]] Expr Compare(Expr a_reading, const Conditions::ComparisonConditionComponent* a_comparison, const Conditions::NumericConditionComponent* a_number);

		// A form, form set or text set resource off the subject in force
		[[nodiscard]] Expr ResourceSet(std::string a_key);
		// Whether the set holds any, or every, of the members; rows asking one set under the same All or Any are merged
		// into one of these
		[[nodiscard]] Expr HasAny(Expr a_set, std::vector<std::uint64_t> a_members);
		[[nodiscard]] Expr HasAll(Expr a_set, std::vector<std::uint64_t> a_members);
		// Whether two sets share a member: what the actor has against what a feed holds for it
		[[nodiscard]] Expr Meets(Expr a_set, Expr a_other);

		// The clip or modifier being compiled, for the conditions whose state is kept per clip or modifier
		void SetOwner(SubMod* a_owner) { _owner = a_owner; }
		[[nodiscard]] SubMod* GetOwner() const { return _owner; }

		// One row: disabled holds, invalid does not, negated flips, the rest compiles itself
		[[nodiscard]] Expr Row(const Conditions::ICondition* a_condition);
		// A set's enabled rows, all of them or any of them, as ConditionSet answers them
		[[nodiscard]] Expr All(Conditions::ConditionSet* a_set);
		[[nodiscard]] Expr Any(Conditions::ConditionSet* a_set);
		[[nodiscard]] Expr One(Conditions::ConditionSet* a_set);

		// Readings taken inside are taken off the next subject: the player, or a target of the one in force
		void PushSubject(Step a_step);
		void PopSubject();

	private:
		[[nodiscard]] Expr Intern(Node a_node);
		[[nodiscard]] std::uint32_t Subject();
		[[nodiscard]] std::vector<Expr> Rows(Conditions::ConditionSet* a_set);
		[[nodiscard]] Expr SetTest(Op a_op, Expr a_set, std::vector<std::uint64_t> a_members);
		// Siblings asking the same thing as one: set tests on one set joined, bounds on one number kept at the strongest
		[[nodiscard]] std::vector<Expr> Merge(Op a_op, std::vector<Expr> a_children);

		Compiled& _out;
		SubMod* _owner = nullptr;
		std::vector<Step> _subject;
		std::unordered_map<std::string, std::uint32_t> _probeIndex;
		std::unordered_map<std::string, std::uint32_t> _nodeIndex;
		std::unordered_map<std::string, std::uint32_t> _setIndex;
	};

	// Main thread: walks the registered mods and compiles every clip and modifier's conditions
	void Compile(Compiled& a_out);

	// Main thread: the subjects an actor's readings are taken off, the actor first; empty where there is none
	void ResolveSubjects(const Compiled& a_program, RE::TESObjectREFR* a_actor, std::vector<RE::TESObjectREFRPtr>& a_out);

	// Pure. Answers the nodes for one actor from the world's and the actor's readings, then every rule: the live nodes, or
	// every node when the actor is traced, so a row merged away still shows its own answer
	void Evaluate(const Compiled& a_program, const Facts::WorldFacts& a_world, const Facts::ActorFacts& a_actor, bool a_bEveryNode,
		std::vector<float>& a_nodeValues, std::vector<std::uint8_t>& a_ruleAnswers);
}

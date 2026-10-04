#include "ModifierLog.h"

#include "ActorState.h"
#include "ClipPlayer.h"
#include "Program.h"
#include "RegisteredMods.h"
#include "Resolve.h"
#include "Settings.h"
#include "Triggers.h"
#include "apply/Entries.h"

#include <cmath>
#include <unordered_map>

namespace
{
	std::string ModNameOf(const SubMod* a_subMod)
	{
		const auto* mod = a_subMod ? a_subMod->GetParentMod() : nullptr;
		return mod ? std::string(mod->GetName()) : std::string{};
	}

	// A row's answer, as the program reads its nodes: nothing, or no answer, does not hold
	ModifierTrace::Result ResultOf(const Program::Compiled& a_program, const std::vector<float>& a_nodeValues, const SubMod* a_owner, const Conditions::ICondition* a_condition)
	{
		const auto it = a_program.rowNodes.find({ a_owner, a_condition });
		if (it == a_program.rowNodes.end() || it->second >= a_nodeValues.size()) {
			return ModifierTrace::Result::kNone;
		}
		const float value = a_nodeValues[it->second];
		return value != 0.f && !std::isnan(value) ? ModifierTrace::Result::kSuccess : ModifierTrace::Result::kFail;
	}

	// A condition set's rows, each with the rows inside it for AND, OR, XOR and a preset, and what each reads now
	std::vector<ModifierTrace::Condition> TraceSet(const Program::Compiled& a_program, const std::vector<float>& a_nodeValues, const SubMod* a_owner, Conditions::ConditionSet* a_set, RE::TESObjectREFR* a_refr, int a_depth)
	{
		std::vector<ModifierTrace::Condition> out;
		if (!a_set || a_depth > 16) {
			return out;
		}
		a_set->ForEach([&](std::unique_ptr<Conditions::ICondition>& a_condition) {
			ModifierTrace::Condition entry;
			entry.name = a_condition->GetName().data();
			entry.argument = a_condition->GetArgument().data();
			if (a_refr && !a_condition->IsDisabled()) {
				entry.current = a_condition->GetCurrent(a_refr).data();
			}
			if (a_condition->IsDisabled()) {
				entry.result = ModifierTrace::Result::kDisabled;
			} else {
				entry.result = ResultOf(a_program, a_nodeValues, a_owner, a_condition.get());
				for (std::uint32_t i = 0; i < a_condition->GetNumComponents(); ++i) {
					if (const auto* multi = dynamic_cast<const Conditions::IMultiConditionComponent*>(a_condition->GetComponent(i))) {
						auto children = TraceSet(a_program, a_nodeValues, a_owner, multi->GetConditions(), a_refr, a_depth + 1);
						entry.children.insert(entry.children.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
					}
				}
			}
			out.push_back(std::move(entry));
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return out;
	}
}

bool ModifierLogEntry::MatchesRegex(const std::regex& a_regex) const
{
	for (const auto& change : changes) {
		if (std::regex_search(change.modName, a_regex) || std::regex_search(change.subModName, a_regex)) {
			return true;
		}
	}
	return false;
}

ModifierTrace ModifierLog::TraceConditions(const Program::Compiled& a_program, const std::vector<float>& a_nodeValues, const std::vector<std::uint8_t>& a_ruleAnswers, RE::TESObjectREFR* a_refr)
{
	ModifierTrace trace;
	for (std::size_t r = 0; r < a_program.rules.size(); ++r) {
		const auto& rule = a_program.rules[r];
		if (!rule.owner || rule.conditions) {
			continue;
		}
		ModifierTrace::Step step;
		step.owner = rule.owner;
		step.modName = rule.mod;
		step.subModName = rule.subMod;
		step.bClip = !rule.bModifier;
		step.priority = rule.owner->GetPriority();
		auto* conditions = rule.owner->GetConditionSet();
		if (rule.owner->IsDisabled()) {
			step.result = ModifierTrace::Result::kDisabled;
		} else if (!conditions || conditions->IsEmpty()) {
			step.result = ModifierTrace::Result::kNoConditions;
		} else {
			step.result = r < a_ruleAnswers.size() && a_ruleAnswers[r] ? ModifierTrace::Result::kSuccess : ModifierTrace::Result::kFail;
			step.conditions = TraceSet(a_program, a_nodeValues, rule.owner, conditions, a_refr, 0);
		}
		trace.steps.push_back(std::move(step));
	}
	return trace;
}

void ModifierLog::OnTick(ActorState::State& a_state, ModifierTrace a_trace)
{
	using Section = ModifierTrace::Section;
	using Kind = ActorState::ClipReport::Kind;
	const auto refr = a_state.handle.get();
	const RE::FormID formID = refr ? refr->GetFormID() : 0;
	const float now = Resolve::Now();

	// What is active now, a modifier on its way out no longer counted
	const auto resolved = a_state.resolved.With([](const auto& a_resolved) { return a_resolved; });
	std::vector<const SubMod*> active;
	if (resolved) {
		for (const auto& one : resolved->active) {
			if (one.leftAt < 0.f) {
				active.push_back(one.subMod);
			}
		}
	}

	// What the clips' tick did, what plays, and the countdowns
	std::vector<ActorState::ClipReport> report;
	std::vector<ActorState::RunningClip> running;
	std::unordered_map<const void*, float> nextFire;
	a_state.clips.With([&](const ActorState::ClipState& a_clips) {
		report = a_clips.report;
		for (const auto& one : a_clips.running) {
			ActorState::RunningClip copy;
			copy.clip = one.clip;
			copy.priority = one.priority;
			copy.variant = one.variant;
			copy.startedAt = one.startedAt;
			copy.length = one.length;
			copy.bExiting = one.bExiting;
			running.push_back(std::move(copy));
		}
		for (const auto& [trigger, timer] : a_clips.intervals) {
			if (timer.nextFire) {
				nextFire[trigger] = *timer.nextFire;
			}
		}
	});

	// Each step's section and what it is doing
	for (auto& step : a_trace.steps) {
		if (step.result == ModifierTrace::Result::kDisabled) {
			step.section = Section::kDisabled;
		} else if (step.bClip) {
			if (const auto it = std::ranges::find(running, step.owner, &ActorState::RunningClip::clip); it != running.end()) {
				step.section = Section::kPlaying;
				step.state = it->bExiting ? std::format("#{}, exiting", it->variant + 1) : std::format("#{}, {:.2f} / {:.2f} s", it->variant + 1, now - it->startedAt, it->length);
			} else if (step.result == ModifierTrace::Result::kSuccess || step.result == ModifierTrace::Result::kNoConditions) {
				step.section = Section::kBlocked;
				const auto why = std::ranges::find_if(report, [&](const ActorState::ClipReport& a_report) { return a_report.clip == step.owner && (a_report.kind == Kind::kBlocked || a_report.kind == Kind::kEchoIgnored); });
				step.blocked = why != report.end() ? why->reason : std::string("waiting for a trigger");
			} else {
				step.section = Section::kFailed;
			}
		} else if (resolved) {
			if (const auto it = std::ranges::find(resolved->active, step.owner, &ActorState::Active::subMod); it != resolved->active.end()) {
				step.section = Section::kActive;
				const auto* modifier = static_cast<const AppearanceModifier*>(it->subMod);
				step.state = it->leftAt >= 0.f ? "leaving" : modifier->HasStartTransition() && now - it->enteredAt < modifier->GetStartTransitionTime() ? "entering" : "on";
			} else {
				step.section = Section::kFailed;
			}
		}

		// A clip's triggers, the one that fired it marked, an interval's time left
		if (step.bClip && step.owner) {
			const auto fired = std::ranges::find_if(report, [&](const ActorState::ClipReport& a_report) { return a_report.clip == step.owner && a_report.trigger; });
			const_cast<Clip*>(static_cast<const Clip*>(step.owner))->GetTriggerSet()->ForEach([&](std::unique_ptr<Triggers::TriggerBase>& a_trigger) {
				ModifierTrace::Trigger trigger{ std::string(a_trigger->GetTypeName()), a_trigger->GetArgument() };
				trigger.bFired = fired != report.end() && fired->trigger == a_trigger.get();
				if (const auto it = nextFire.find(a_trigger.get()); it != nextFire.end()) {
					trigger.detail = std::format("{:.1f} s left", (std::max)(it->second - now, 0.f));
				}
				step.triggers.push_back(std::move(trigger));
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
	}

	// Under each active modifier and playing clip, its channels: a clip playing has them all, the modifiers under it
	// shut out of them
	auto& entries = Apply::Entries::GetSingleton();
	std::unordered_map<std::uint32_t, const Clip*> byClip;
	for (const auto& one : running) {
		if (!one.bExiting) {
			for (const auto id : ClipPlayer::Claims(one.clip)) {
				byClip.try_emplace(id.raw, one.clip);
			}
		}
	}
	const auto stepOf = [&](const SubMod* a_subMod) -> ModifierTrace::Step* {
		const auto it = std::ranges::find(a_trace.steps, a_subMod, &ModifierTrace::Step::owner);
		return it != a_trace.steps.end() ? &*it : nullptr;
	};
	if (resolved) {
		for (const auto& outcome : Resolve::Explain(resolved->active)) {
			auto* step = stepOf(outcome.owner);
			if (!step) {
				continue;
			}
			Apply::ChannelId id;
			id.raw = outcome.channel;
			ModifierTrace::Channel channel{ entries.Identifier(id) };
			if (const auto clip = byClip.find(outcome.channel); clip != byClip.end()) {
				channel.outcome = ModifierTrace::Outcome::kOverridden;
				channel.detail = std::format("by clip {}", clip->second->GetName());
			} else {
				switch (outcome.kind) {
				case Resolve::ChannelOutcome::Kind::kWins:
					channel.outcome = ModifierTrace::Outcome::kWins;
					break;
				case Resolve::ChannelOutcome::Kind::kBlended:
					channel.outcome = ModifierTrace::Outcome::kBlended;
					channel.detail = std::format("under {} ({})", outcome.by ? outcome.by->GetName() : ""sv, outcome.mode);
					break;
				case Resolve::ChannelOutcome::Kind::kOverridden:
					channel.outcome = ModifierTrace::Outcome::kOverridden;
					channel.detail = std::format("by {}", outcome.by ? outcome.by->GetName() : ""sv);
					break;
				case Resolve::ChannelOutcome::Kind::kHeldBack:
					channel.outcome = ModifierTrace::Outcome::kHeldBack;
					channel.detail = "its group";
					break;
				}
			}
			step->bActive = true;
			step->channels.push_back(std::move(channel));
		}
	}
	for (const auto& one : running) {
		if (auto* step = stepOf(one.clip)) {
			step->bActive = true;
			for (const auto id : ClipPlayer::Claims(one.clip)) {
				step->channels.push_back({ entries.Identifier(id), ModifierTrace::Outcome::kWins, "clip" });
			}
		}
	}
	for (auto& step : a_trace.steps) {
		std::ranges::sort(step.channels, {}, &ModifierTrace::Channel::name);
		step.owner = nullptr;
	}
	// The highest priority first in each section
	std::ranges::stable_sort(a_trace.steps, [](const ModifierTrace::Step& a, const ModifierTrace::Step& b) { return a.priority > b.priority; });
	// The live copy, for the Actor window
	if (_bLive) {
		std::scoped_lock guard(_lock);
		_live = a_trace;
	}
	if (!_bLog) {
		return;
	}

	// Another actor picked: what it has now is where its log starts, not a change
	if (formID != _lastID) {
		_lastID = formID;
		_lastActive = active;
		return;
	}

	ModifierLogEntry entry;
	for (const auto* one : active) {
		if (std::ranges::find(_lastActive, one) == _lastActive.end()) {
			entry.changes.push_back({ ModifierLogEntry::Event::kActivate, ModNameOf(one), std::string(one->GetName()), one->GetPriority() });
		}
	}
	for (const auto* one : _lastActive) {
		if (std::ranges::find(active, one) == active.end()) {
			entry.changes.push_back({ ModifierLogEntry::Event::kDeactivate, ModNameOf(one), std::string(one->GetName()), one->GetPriority() });
		}
	}
	_lastActive = active;
	for (const auto& one : report) {
		ModifierLogEntry::Event event = ModifierLogEntry::Event::kNone;
		switch (one.kind) {
		case Kind::kStart:
			event = ModifierLogEntry::Event::kClipStart;
			break;
		case Kind::kEcho:
			event = ModifierLogEntry::Event::kClipEcho;
			break;
		case Kind::kEnd:
			event = ModifierLogEntry::Event::kClipEnd;
			break;
		case Kind::kInterrupted:
			event = ModifierLogEntry::Event::kClipInterrupted;
			break;
		default:
			break;
		}
		if (event != ModifierLogEntry::Event::kNone) {
			entry.changes.push_back({ event, ModNameOf(one.clip), std::string(one.clip->GetName()), one.clip->GetPriority(), one.variant, one.reason });
		}
	}
	if (entry.changes.empty()) {
		return;
	}

	entry.trace = std::move(a_trace);
	entry.bIsValid = true;

	// The filter decides what is taken; the same change again counted on the newest
	std::scoped_lock guard(_lock);
	if (!filter.empty()) {
		try {
			if (!entry.MatchesRegex(std::regex(filter, std::regex_constants::icase))) {
				return;
			}
		} catch (const std::regex_error&) {
		}
	}
	if (!_log.empty() && _log.front().changes == entry.changes) {
		_log.front().count++;
		_log.front().timeDrawn = 0.f;
		_log.front().trace = std::move(entry.trace);
		return;
	}
	_log.push_front(std::move(entry));
	while (_log.size() > Settings::uModifierLogMaxEntries) {
		_log.pop_back();
	}
}

void ModifierLog::ClampLog()
{
	std::scoped_lock guard(_lock);
	while (_log.size() > Settings::uModifierLogMaxEntries) {
		_log.pop_back();
	}
}

bool ModifierLog::IsLogEmpty() const
{
	std::scoped_lock guard(_lock);
	return _log.empty();
}

void ModifierLog::ForEachLogEntry(const std::function<void(ModifierLogEntry&)>& a_func)
{
	std::scoped_lock guard(_lock);
	for (auto& entry : _log) {
		a_func(entry);
	}
}

void ModifierLog::ClearLog()
{
	std::scoped_lock guard(_lock);
	_log.clear();
}

void ModifierLog::SetLive(bool a_bEnable)
{
	if (_bLive == a_bEnable) {
		return;
	}
	_bLive = a_bEnable;
	if (!a_bEnable) {
		std::scoped_lock guard(_lock);
		_live = {};
	}
}

ModifierTrace ModifierLog::LiveTrace() const
{
	std::scoped_lock guard(_lock);
	return _live;
}

void ModifierLog::SetLog(bool a_bEnable)
{
	_bLog = a_bEnable;
	if (!a_bEnable) {
		ClearLog();
		_lastID = 0;
	}
}

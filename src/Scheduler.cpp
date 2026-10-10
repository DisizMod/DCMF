#include "Scheduler.h"

#include "ActorState.h"
#include "Actors.h"
#include "AnimationGraph.h"
#include "ModifierLog.h"
#include "apply/Entries.h"
#include "Facts.h"
#include "Program.h"
#include "ClipPlayer.h"
#include "Resolve.h"
#include "RegisteredMods.h"
#include "Resources.h"
#include "scan/Scan.h"
#include "Settings.h"
#include "UI/UIManager.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Scheduler
{
	namespace
	{
		// A frame longer than this is a loading screen or a stall, not a frame.
		// Counting it would fire a tick the instant the game came back.
		constexpr float kMaxDelta = 0.25f;



		float g_accumulator = 0.f;
		std::uint64_t g_ticksFired = 0;
		std::uint64_t g_tick = 0;

		Facts::Frame g_frame;

		std::vector<RE::ActorHandle> g_handles;

		// Compiled once, not per tick: walking every mod and interning every
		// row costs the same whether one actor is driven or fifty, so doing it
		// every tick hid the cost of everything else.
		Program::Compiled g_program;
		bool g_needsCompile = true;

		// Reused across actors and frames, so evaluating allocates nothing.
		std::vector<float> g_nodeValues;
		// The inspected reference's rows, answered on its latest tick, for the editor on the render thread
		std::mutex g_answersLock;
		std::map<std::pair<const void*, const void*>, RowAnswer> g_rowAnswers;

		// Every row's answer for the inspected reference, and what a compared row read, from the nodes just answered
		void Inspect(const Facts::WorldFacts& a_world, const Facts::ActorFacts& a_actor)
		{
			const auto holds = [](float a_value) { return a_value != 0.f && !std::isnan(a_value); };
			std::map<std::pair<const void*, const void*>, RowAnswer> rows;
			for (const auto& [key, index] : g_program.rowNodes) {
				RowAnswer answer{ .bHolds = holds(g_nodeValues[index]) };
				// Through a NOT to the comparison, and the reading it made
				auto node = index;
				if (g_program.nodes[node].op == Program::Op::kNot) {
					node = g_program.nodes[node].a;
				}
				if (g_program.nodes[node].op == Program::Op::kCompare) {
					const auto& reading = g_program.nodes[g_program.nodes[node].a];
					if (reading.op == Program::Op::kProbe && !g_program.probes[reading.probe].bSet) {
						const auto& values = g_program.probes[reading.probe].bWorld ? a_world.values : a_actor.values;
						if (reading.probe >= values.size()) {
							if (static bool bSaid = false; !bSaid) {
								bSaid = true;
								logger::error("scheduler: probe {} is past the {} readings gathered for the inspected reference", reading.probe, values.size());
							}
							continue;
						}
						const float value = values[reading.probe];
						answer.current = std::isnan(value) ? "(none)" : std::format("{:g}", value);
					}
				}
				rows.emplace(key, std::move(answer));
			}
			std::scoped_lock guard(g_answersLock);
			g_rowAnswers = std::move(rows);
		}

		// The reference the editor inspects, when no tick answers it as a driven actor: answered for the editor alone
		Facts::ActorFacts g_inspectedFacts;

		// Each actor's answers to the CONDITION functions' lists, by rule, from its latest tick
		std::unordered_map<RE::FormID, std::vector<std::uint8_t>> g_functionAnswers;
		std::vector<std::uint8_t> g_ruleAnswers;

		// Per actor, what its last evaluation read and answered, kept until what it reads changes
		struct Kept
		{
			Facts::ActorFacts facts;
			std::vector<std::uint8_t> answers;
			float evaluatedAt = -std::numeric_limits<float>::infinity();
			bool bPending = false;  // a change waiting for the minimum gap
			bool bAnswered = false;
		};
		std::unordered_map<RE::FormID, Kept> g_kept;
		Facts::WorldFacts g_lastWorld;

		// Readings the same to the bit, a NaN equal to itself
		bool SameReadings(const std::vector<float>& a_values, const std::vector<std::vector<std::uint64_t>>& a_sets, const std::vector<float>& b_values, const std::vector<std::vector<std::uint64_t>>& b_sets)
		{
			return a_values.size() == b_values.size() && std::memcmp(a_values.data(), b_values.data(), a_values.size() * sizeof(float)) == 0 && a_sets == b_sets;
		}

		// How many rules held for the actor drained most recently -- the
		// cheapest proof the evaluation is running and answering.
		std::size_t g_matchedLast = 0;


		// Written on the main thread at the end of Advance, read by the panel
		// on the render thread.
		std::mutex g_timingMutex;
		Timing::Phases g_timings;


		[[nodiscard]] Facts::Frame& Current() { return g_frame; }


		constexpr const char* kRaceMenuOpen = "RaceMenu is open";

		// Why a frame did nothing, so the panel can say which it was.
		[[nodiscard]] const char* CheckGate()
		{
			if (static_cast<TargetMode>(Settings::uTargetMode) == TargetMode::kDisabled) {
				return "target is Disabled";
			}

			if (Settings::bPauseInRaceMenu) {
				if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME)) {
					return kRaceMenuOpen;
				}
			}

			if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
				// Menu Studio shows the character in the inventory: with its patch, the tick runs on there, so an equip blends
				if (!Settings::bPatchMenuStudio || !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
					return "game is paused";
				}
			}

			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return "no player";
			}

			if (!player->Is3DLoaded()) {
				return "player 3D not loaded";
			}

			return nullptr;
		}

		// Before any reading is taken: a row an edit removed must never be read through the program compiled before it,
		// so an edit is compiled on the next frame, tick or not, and the world read again for the probes it now has
		void CompileIfNeeded(Timing::FrameSample& a_sample)
		{
			if (!g_needsCompile) {
				return;
			}
			Timing::Scope scope(a_sample.compile);
			Program::Compile(g_program);
			g_functionAnswers.clear();
			{
				std::scoped_lock guard(g_answersLock);
				g_rowAnswers.clear();
			}
			g_nodeValues.assign(g_program.nodes.size(), 0.f);
			g_ruleAnswers.assign(g_program.rules.size(), 0);
			g_needsCompile = false;
			// Everyone answered again through the new program
			g_kept.clear();
			g_lastWorld = {};
			Actors::CaptureWorld(g_program, Current().world);
			for (auto& actor : Current().actors) {
				actor.Reset();
			}
		}

		void FireTick(Timing::FrameSample& a_sample)
		{
			Timing::Scope scope(a_sample.capture);

			auto& frame = Current();
			frame.Begin(++g_tick);
			Resources::BeginTick();

			// The channel tables take in whatever the scan found before anything compiles against them
			Apply::Entries::GetSingleton().Sync();
			// Then every body preset as channels, once per scan
			Resolve::BuildPresetSliders();
			// The mods on disk, once the scan has named what their channels drive
			RegisteredMod::LoadAll();

			CompileIfNeeded(a_sample);
			Actors::CaptureWorld(g_program, frame.world);

			const Actors::Filter filter{ static_cast<TargetMode>(Settings::uTargetMode) == TargetMode::kAll, Settings::fTargetDistance };
			Actors::Capture(filter, g_handles);
			ActorState::Track(g_handles);

			frame.actors.resize(g_handles.size());

			// What was kept for an actor out of range goes with it
			std::unordered_set<RE::FormID> inRange;
			for (const auto& handle : g_handles) {
				if (const auto actor = handle.get()) {
					inRange.insert(actor->GetFormID());
				}
			}
			std::erase_if(g_kept, [&](const auto& a_entry) { return !inRange.contains(a_entry.first); });
		}


		// Phase-major on purpose. Interleaving the four per actor would read
		// more naturally and would weld the two halves together; kept apart,
		// the middle two become a parallel_for and nothing else moves.
		// On a tick every actor is gathered and resolved; between ticks only those a value may have changed for. An actor
		// is evaluated only when what it read changed, and not again before the minimum gap
		void Drain(Timing::FrameSample& a_sample, std::size_t& a_outDrained, bool a_bTick, const std::unordered_set<RE::FormID>& a_touched, bool a_bWorldTouched)
		{
			auto& frame = Current();

			const std::size_t first = 0;
			const auto last = g_handles.size();
			if (last == 0) {
				return;
			}

			const float now = Resolve::Now();
			const float gap = (std::max)(Settings::fMinEvaluationGap, 0.f) / 1000.f;

			// The world's readings: taken by the tick, again between ticks when one held for every actor changed
			if (!a_bTick && a_bWorldTouched) {
				Actors::CaptureWorld(g_program, frame.world);
			}
			bool bWorldChanged = false;
			if (a_bTick || a_bWorldTouched) {
				bWorldChanged = !SameReadings(g_lastWorld.values, g_lastWorld.sets, frame.world.values, frame.world.sets);
				if (bWorldChanged) {
					g_lastWorld = frame.world;
				}
			}

			// The actors this pass reaches, by index
			std::vector<std::size_t> reached;
			reached.reserve(last);
			for (auto i = first; i < last; ++i) {
				const auto actor = g_handles[i].get();
				const RE::FormID id = actor ? actor->GetFormID() : 0;
				const auto kept = g_kept.find(id);
				if (a_bTick || bWorldChanged || a_touched.contains(id) || kept == g_kept.end() || kept->second.bPending) {
					reached.push_back(i);
				}
			}
			if (reached.empty()) {
				return;
			}

			// Gather -- the last phase that needs the game.
			std::size_t gathered = 0;
			{
				Timing::Scope scope(a_sample.gather);
				for (const auto i : reached) {
					if (Actors::Gather(g_handles[i], g_program, frame.actors[i])) {
						++gathered;
					} else {
						frame.actors[i].Reset();
					}
				}
			}

			// The actor the Modifier & Clip Log watches: its conditions traced as they are answered
			std::optional<std::pair<std::size_t, ModifierTrace>> logged;

			// The reference the editor inspects: every row answered for it, driven or not
			auto* inspectedRefr = UI::UIManager::GetSingleton().GetRefrToEvaluate();
			const RE::FormID inspected = inspectedRefr ? inspectedRefr->GetFormID() : 0;
			bool bInspectedAnswered = false;

			// Evaluate -- pure. Reads the facts gathered above and nothing else,
			// so this loop is the one that becomes a parallel_for.
			std::vector<std::size_t> evaluated;
			{
				Timing::Scope scope(a_sample.evaluate);
				for (const auto i : reached) {
					auto& facts = frame.actors[i];
					// An actor Gather skipped has nothing to answer from, and nothing held
					if (facts.values.size() != g_program.probes.size()) {
						if (const auto actor = g_handles[i].get()) {
							g_kept.erase(actor->GetFormID());
						}
						continue;
					}
					auto& kept = g_kept[facts.formID];
					if (bWorldChanged || !kept.bAnswered || !SameReadings(kept.facts.values, kept.facts.sets, facts.values, facts.sets)) {
						kept.facts = facts;
						kept.bPending = true;
					}
					auto& log = ModifierLog::GetSingleton();
					const bool bTraced = log.ShouldLogActor(facts.formID);
					const bool bInspected = facts.formID == inspected;
					// The log and the editor read every tick's answers
					const bool bForced = a_bTick && (bTraced || bInspected);
					if (!bForced && !(kept.bPending && now - kept.evaluatedAt >= gap)) {
						continue;
					}
					Program::Evaluate(g_program, frame.world, facts, bTraced || bInspected, g_nodeValues, g_ruleAnswers);
					kept.answers = g_ruleAnswers;
					kept.evaluatedAt = now;
					kept.bPending = false;
					kept.bAnswered = true;
					evaluated.push_back(i);
					if (bInspected) {
						Inspect(frame.world, facts);
						bInspectedAnswered = true;
					}
					if (bTraced) {
						logged.emplace(i, ModifierLog::TraceConditions(g_program, g_nodeValues, g_ruleAnswers, g_handles[i].get().get()));
					}
					if (!g_program.functionRules.empty()) {
						g_functionAnswers[facts.formID] = g_ruleAnswers;
					}
				}
			}

			// Not driven: gathered and answered for the editor, nothing applied
			if (a_bTick && inspected && !bInspectedAnswered) {
				if (auto* actor = inspectedRefr->As<RE::Actor>(); actor && Actors::Gather(actor->GetHandle(), g_program, g_inspectedFacts)) {
					Program::Evaluate(g_program, frame.world, g_inspectedFacts, true, g_nodeValues, g_ruleAnswers);
					Inspect(frame.world, g_inspectedFacts);
				}
			}

			if (!evaluated.empty()) {
				g_matchedLast = static_cast<std::size_t>(
					std::count(g_ruleAnswers.begin(), g_ruleAnswers.end(), std::uint8_t{ 1 }));
			}

			// Channels wait for the scan: until it finishes they would not know what they can drive
			if (Scan::Scanner::GetSingleton().IsFinished()) {
				// Resolve -- pure but for the clock: each actor's active set from what held for it when last answered. On a
				// tick every actor, for the transitions and clips the clock alone moves, and a modifier switched off since
				Timing::Scope scope(a_sample.resolve);
				const auto& resolved = a_bTick ? reached : evaluated;
				std::vector<const SubMod*> held;
				std::vector<Clip*> passing;
				for (const auto i : resolved) {
					const auto actor = g_handles[i].get();
					const auto kept = actor ? g_kept.find(actor->GetFormID()) : g_kept.end();
					if (kept == g_kept.end() || !kept->second.bAnswered || kept->second.answers.size() != g_program.rules.size()) {
						continue;
					}
					held.clear();
					passing.clear();
					for (std::size_t r = 0; r < g_program.rules.size(); ++r) {
						const auto& rule = g_program.rules[r];
						if (!kept->second.answers[r] || !rule.owner || rule.owner->IsDisabled() || rule.conditions) {
							continue;
						}
						if (rule.bModifier) {
							held.push_back(rule.owner);
						} else {
							passing.push_back(const_cast<Clip*>(static_cast<const Clip*>(rule.owner)));
						}
					}
					if (const auto state = ActorState::Get(actor->GetHandle())) {
						Resolve::Tick(*state, held, now);
						ClipPlayer::Tick(*state, actor.get(), passing, now);
						if (logged && logged->first == i) {
							ModifierLog::GetSingleton().OnTick(*state, std::move(logged->second));
						}
					}
				}
			}

			a_outDrained = gathered;
		}
	}


	void Recompile()
	{
		g_needsCompile = true;
		Resolve::ForgetDraws();
	}

	std::optional<RowAnswer> AnswerFor(const void* a_owner, const void* a_condition)
	{
		std::scoped_lock guard(g_answersLock);
		const auto it = g_rowAnswers.find({ a_owner, a_condition });
		return it != g_rowAnswers.end() ? std::optional<RowAnswer>(it->second) : std::nullopt;
	}

	bool FunctionConditionsHold(const void* a_conditions, RE::TESObjectREFR* a_refr)
	{
		const auto rule = g_program.functionRules.find(a_conditions);
		const auto answers = a_refr ? g_functionAnswers.find(a_refr->GetFormID()) : g_functionAnswers.end();
		if (rule == g_program.functionRules.end() || answers == g_functionAnswers.end()) {
			return false;
		}
		if (rule->second >= answers->second.size()) {
			logger::error("scheduler: a CONDITION function's rule {} is past the {} answers its actor's tick kept", rule->second, answers->second.size());
			return false;
		}
		return answers->second[rule->second] != 0;
	}

	void Reset()
	{
		g_accumulator = 0.f;
		g_handles.clear();
		g_kept.clear();
	}

	void Advance(float a_realDeltaSeconds)
	{
		Timing::FrameSample sample;

		const char* closed = nullptr;
		{
			Timing::Scope scope(sample.gate);
			closed = CheckGate();
		}

		// Recorded either way. A frame the gate turned away is the most
		// interesting kind when nothing appears to be running.
		if (closed) {
			Reset();
			// RaceMenu reads the actor as it stands and saves it: everyone let go, the inspected reference too
			if (closed == kRaceMenuOpen) {
				ActorState::Track({});
				ActorState::Watch({});
			}
			std::lock_guard<std::mutex> lock(g_timingMutex);
			++g_timings.advances;
			++g_timings.gateClosed;
			g_timings.gateReason = closed;
			g_timings.gate.Add(sample.gate);
			return;
		}

		// Clamped and dropped rather than caught up: a condition is a question
		// about now, and twenty missed answers are worth nothing.
		AnimationGraph::Flush();
		g_accumulator += std::min(a_realDeltaSeconds, kMaxDelta);
		Resolve::AdvanceClock(std::min(a_realDeltaSeconds, kMaxDelta));

		bool bTickFired = false;
		const auto period = 1.f / std::max(Settings::fTickRate, 0.1f);
		if (g_accumulator >= period) {
			g_accumulator = 0.f;

			// A tick falling due while the last one is still draining replaces
			// it. Finishing a stale pass first would only delay the fresh one.
			FireTick(sample);
			++g_ticksFired;
			bTickFired = true;
		}

		// Who a value may have changed for since the last frame
		std::vector<RE::FormID> touchedList;
		bool bWorldTouched = false;
		Resources::TakeTouched(touchedList, bWorldTouched);

		// Everything captured, in the frame the tick fired; between ticks, the actors a value may have changed for and those
		// waiting out the gap. An edit waits for the tick, which compiles it first
		std::size_t drained = 0;
		bool bDrained = false;
		if (bTickFired && !g_handles.empty()) {
			CompileIfNeeded(sample);
			Drain(sample, drained, true, { touchedList.begin(), touchedList.end() }, bWorldTouched);
			bDrained = true;
		} else if (!g_needsCompile && !g_handles.empty() &&
				   (bWorldTouched || !touchedList.empty() || std::ranges::any_of(g_kept, [](const auto& a_entry) { return a_entry.second.bPending; }))) {
			Drain(sample, drained, false, { touchedList.begin(), touchedList.end() }, bWorldTouched);
			bDrained = true;
		}

		const auto tickMs = sample.compile + sample.capture + sample.gather + sample.evaluate +
		                    sample.resolve +
		                    sample.apply;
		const auto frameMs = sample.gate + tickMs;

		{
			std::lock_guard<std::mutex> lock(g_timingMutex);

			++g_timings.advances;
			g_timings.gateReason = nullptr;
			g_timings.ticksFired = g_ticksFired;
			// Every frame: the gate is asked on all of them, and the frame is
			// what this mod cost the frame whether or not a tick was due.
			g_timings.gate.Add(sample.gate);
			g_timings.frame.Add(frameMs);

			// Only on a frame a tick fired or a change was answered, and gated on that rather than on a
			// measured value: a phase too fast to measure still ran.
			if (bDrained) {
				if (sample.compile > 0.f) {
				g_timings.compile.Add(sample.compile);
			}
			g_timings.capture.Add(sample.capture);
				g_timings.gather.Add(sample.gather);
				g_timings.evaluate.Add(sample.evaluate);
				g_timings.resolve.Add(sample.resolve);
				g_timings.apply.Add(sample.apply);
				g_timings.tick.Add(tickMs);
				g_timings.actorsDrained = drained;
			}

			g_timings.actorsCaptured = g_handles.size();
			g_timings.nodes = g_program.nodes.size();
			g_timings.rules = g_program.rules.size();
			g_timings.rowsSeen = g_program.rowsSeen;
			g_timings.rowsUnsupported = g_program.rowsUnsupported;
			g_timings.matched = g_matchedLast;
		}
	}

	Timing::Phases GetTimings()
	{
		std::lock_guard<std::mutex> lock(g_timingMutex);
		return g_timings;
	}

	void ResetTimings()
	{
		std::lock_guard<std::mutex> lock(g_timingMutex);
		g_timings = {};
	}
}

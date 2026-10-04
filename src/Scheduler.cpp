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

		// Per actor, the modifiers whose rules held, and the clips, kept from the evaluate phase for the resolve one
		std::vector<std::vector<const SubMod*>> g_held;
		std::vector<std::vector<Clip*>> g_clipsPassing;

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
				return "game is paused";
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
			// The mods on disk, once the scan has named what their channels drive
			RegisteredMod::LoadAll();

			CompileIfNeeded(a_sample);
			Actors::CaptureWorld(g_program, frame.world);

			const Actors::Filter filter{ static_cast<TargetMode>(Settings::uTargetMode) == TargetMode::kAll, Settings::fTargetDistance };
			Actors::Capture(filter, g_handles);
			ActorState::Track(g_handles);

			frame.actors.resize(g_handles.size());
		}


		// Phase-major on purpose. Interleaving the four per actor would read
		// more naturally and would weld the two halves together; kept apart,
		// the middle two become a parallel_for and nothing else moves.
		void Drain(Timing::FrameSample& a_sample, std::size_t& a_outDrained)
		{
			auto& frame = Current();

			const std::size_t first = 0;
			const auto last = g_handles.size();
			if (last == 0) {
				return;
			}

			// Gather -- the last phase that needs the game.
			std::size_t gathered = 0;
			{
				Timing::Scope scope(a_sample.gather);
				for (auto i = first; i < last; ++i) {
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
			{
				Timing::Scope scope(a_sample.evaluate);
				g_held.resize(last);
				g_clipsPassing.resize(last);
				for (auto i = first; i < last; ++i) {
					// An actor Gather skipped has nothing to answer from
					if (frame.actors[i].values.size() != g_program.probes.size()) {
						g_held[i].clear();
						g_clipsPassing[i].clear();
						continue;
					}
					auto& log = ModifierLog::GetSingleton();
					const bool bTraced = log.ShouldLogActor(frame.actors[i].formID);
					const bool bInspected = frame.actors[i].formID == inspected;
					Program::Evaluate(g_program, frame.world, frame.actors[i], bTraced || bInspected, g_nodeValues, g_ruleAnswers);
					if (bInspected) {
						Inspect(frame.world, frame.actors[i]);
						bInspectedAnswered = true;
					}
					if (bTraced) {
						logged.emplace(i, ModifierLog::TraceConditions(g_program, g_nodeValues, g_ruleAnswers, g_handles[i].get().get()));
					}
					auto& held = g_held[i];
					auto& passing = g_clipsPassing[i];
					held.clear();
					passing.clear();
					if (!g_program.functionRules.empty()) {
						g_functionAnswers[frame.actors[i].formID] = g_ruleAnswers;
					}
					for (std::size_t r = 0; r < g_program.rules.size(); ++r) {
						const auto& rule = g_program.rules[r];
						if (!g_ruleAnswers[r] || !rule.owner || rule.owner->IsDisabled() || rule.conditions) {
							continue;
						}
						if (rule.bModifier) {
							held.push_back(rule.owner);
						} else {
							passing.push_back(const_cast<Clip*>(static_cast<const Clip*>(rule.owner)));
						}
					}
				}
			}

			// Not driven: gathered and answered for the editor, nothing applied
			if (inspected && !bInspectedAnswered) {
				if (auto* actor = inspectedRefr->As<RE::Actor>(); actor && Actors::Gather(actor->GetHandle(), g_program, g_inspectedFacts)) {
					Program::Evaluate(g_program, frame.world, g_inspectedFacts, true, g_nodeValues, g_ruleAnswers);
					Inspect(frame.world, g_inspectedFacts);
				}
			}

			g_matchedLast = static_cast<std::size_t>(
				std::count(g_ruleAnswers.begin(), g_ruleAnswers.end(), std::uint8_t{ 1 }));

			// Channels wait for the scan: until it finishes they would not know what they can drive
			if (Scan::Scanner::GetSingleton().IsFinished()) {
				// Resolve -- pure but for the clock: each actor's active set from what held for it
				{
					Timing::Scope scope(a_sample.resolve);
					const float now = Resolve::Now();
					for (auto i = first; i < last; ++i) {
						const auto actor = g_handles[i].get();
						if (const auto state = actor ? ActorState::Get(actor->GetHandle()) : nullptr) {
							Resolve::Tick(*state, g_held[i], now);
							ClipPlayer::Tick(*state, actor.get(), g_clipsPassing[i], now);
							if (logged && logged->first == i) {
								ModifierLog::GetSingleton().OnTick(*state, std::move(logged->second));
							}
						}
					}
				}

				// Apply -- back on the main thread.
				{
					Timing::Scope scope(a_sample.apply);
				}
			}

			a_outDrained = gathered;
		}
	}


	void Recompile() { g_needsCompile = true; }

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

		// Everything captured, in the frame the tick fired. Spreading it over
		// the frames that follow is a separate decision.
		std::size_t drained = 0;
		if (bTickFired && !g_handles.empty()) {
			CompileIfNeeded(sample);
			Drain(sample, drained);
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

			// Only on a frame a tick fired, and gated on that rather than on a
			// measured value: a phase too fast to measure still ran.
			if (bTickFired) {
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

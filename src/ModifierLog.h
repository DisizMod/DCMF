#pragma once

#include <deque>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

class SubMod;

namespace ActorState
{
	struct State;
}

namespace Program
{
	struct Compiled;
}

// What one tick decided for the reference: every clip and modifier looked at, what its conditions answered and, for
// those playing or active, what became of each channel they set
struct ModifierTrace
{
	enum class Result : std::uint8_t
	{
		kNone,
		kSuccess,
		kFail,
		kDisabled,
		kNoConditions
	};

	enum class Outcome : std::uint8_t
	{
		kWins,        // its value is the one applied, alone or at the top of a mix
		kBlended,     // mixed in under a higher one's blend
		kOverridden,  // shut out by a higher claim, or by a clip playing
		kHeldBack     // left out with its group, a higher one holding one of its channels
	};

	// Where a step is shown: what plays and is active first, then what could not play, then what failed
	enum class Section : std::uint8_t
	{
		kPlaying,
		kActive,
		kBlocked,
		kFailed,
		kDisabled
	};

	struct Condition
	{
		std::string name;
		std::string argument;
		std::string current;  // what it reads on the reference now, as the condition editor shows it
		Result result = Result::kNone;
		std::vector<Condition> children;
	};

	struct Trigger
	{
		std::string name;
		std::string argument;
		bool bFired = false;
		std::string detail;  // an interval's time left
	};

	struct Channel
	{
		std::string name;
		Outcome outcome = Outcome::kWins;
		std::string detail;  // who it is under, and by what
	};

	struct Step
	{
		const SubMod* owner = nullptr;  // only while the entry is made
		std::string modName;
		std::string subModName;
		bool bClip = false;
		bool bActive = false;  // an active modifier or a clip playing
		std::int32_t priority = 0;
		Result result = Result::kNone;
		Section section = Section::kFailed;
		std::string state;    // entering, on or leaving; a clip's variant and time
		std::string blocked;  // why a clip whose conditions hold does not play
		std::vector<Condition> conditions;
		std::vector<Trigger> triggers;
		std::vector<Channel> channels;
	};

	std::vector<Step> steps;
};

// One tick on which the reference's active modifiers or playing clips changed: what changed, and the tick's trace
struct ModifierLogEntry
{
	enum class Event : std::uint8_t
	{
		kNone,
		kActivate,
		kDeactivate,
		kClipStart,
		kClipEnd,
		kClipInterrupted,
		kClipEcho
	};

	struct Change
	{
		Event event = Event::kNone;
		std::string modName;
		std::string subModName;
		std::int32_t priority = 0;
		std::uint16_t variant = 0;
		std::string reason;  // why a clip was interrupted

		[[nodiscard]] bool operator==(const Change&) const = default;
	};

	[[nodiscard]] bool MatchesRegex(const std::regex& a_regex) const;

	std::vector<Change> changes;
	ModifierTrace trace;

	float timeDrawn = 0.f;
	std::uint32_t count = 1;

	bool bIsValid = false;
};

// The Modifier & Clip Log: entries for the selected reference only, and nothing taken while its window is closed
class ModifierLog
{
public:
	static ModifierLog& GetSingleton()
	{
		static ModifierLog singleton;
		return singleton;
	}

	// Main thread: whether this tick's evaluation is traced for the actor, and the trace of its conditions
	[[nodiscard]] bool ShouldLogActor(RE::FormID a_formID) const { return (_bLog || _bLive) && a_formID != 0 && a_formID == _refrID; }
	[[nodiscard]] static ModifierTrace TraceConditions(const Program::Compiled& a_program, const std::vector<float>& a_nodeValues, const std::vector<std::uint8_t>& a_ruleAnswers, RE::TESObjectREFR* a_refr);

	// Main thread, after the modifiers' and the clips' tick: the live trace kept, and an entry when what is active or playing changed
	void OnTick(ActorState::State& a_state, ModifierTrace a_trace);

	void ClampLog();
	[[nodiscard]] bool IsLogEmpty() const;
	void ForEachLogEntry(const std::function<void(ModifierLogEntry&)>& a_func);
	void ClearLog();
	void SetLog(bool a_bEnable);
	// The Actor window's live view: the reference's trace kept each tick while it is open
	void SetLive(bool a_bEnable);
	[[nodiscard]] ModifierTrace LiveTrace() const;
	void SetRefr(RE::FormID a_formID) { _refrID = a_formID; }

	std::string filter = {};

	ModifierLogEntry tracedEntry;

private:
	ModifierLog() = default;
	ModifierLog(const ModifierLog&) = delete;
	ModifierLog(ModifierLog&&) = delete;
	~ModifierLog() = default;

	ModifierLog& operator=(const ModifierLog&) = delete;
	ModifierLog& operator=(ModifierLog&&) = delete;

	mutable std::mutex _lock;
	std::deque<ModifierLogEntry> _log = {};
	bool _bLog = false;
	bool _bLive = false;
	ModifierTrace _live;
	RE::FormID _refrID = 0;

	// What was active on the last tick, for the actor logged
	RE::FormID _lastID = 0;
	std::vector<const SubMod*> _lastActive;
};

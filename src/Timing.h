#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <cstddef>
#include <cstdint>

namespace Timing
{
	// One measured thing over time. The average is exponential rather than a
	// true mean: the question is what a phase costs now, and a session-long
	// mean stops answering it after the first minute.
	class Stat
	{
	public:
		static constexpr float kSmoothing = 0.1f;

		void Add(float a_milliseconds);
		void Reset();

		[[nodiscard]] float Last() const { return _last; }
		[[nodiscard]] float Average() const { return _average; }
		[[nodiscard]] float Worst() const { return _worst; }
		[[nodiscard]] std::uint64_t Samples() const { return _samples; }

	private:
		float _last = 0.f;
		float _average = 0.f;
		float _worst = 0.f;
		std::uint64_t _samples = 0;
	};

	// Adds its lifetime, in milliseconds, to a float. A float rather than a
	// Stat because a phase is measured on the main thread and folded into the
	// Stats once at the end of the frame, under one lock instead of six.
	class Scope
	{
	public:
		explicit Scope(float& a_outMilliseconds) :
			_out(a_outMilliseconds), _began(std::chrono::steady_clock::now()) {}

		~Scope()
		{
			const std::chrono::duration<float, std::milli> elapsed =
				std::chrono::steady_clock::now() - _began;
			_out += elapsed.count();
		}

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		float& _out;
		std::chrono::steady_clock::time_point _began;
	};

	// One frame's readings, before they are folded into the Stats.
	struct FrameSample
	{
		float gate = 0.f;
		float compile = 0.f;
		float capture = 0.f;
		float gather = 0.f;
		float evaluate = 0.f;
		float resolve = 0.f;
		float apply = 0.f;
	};

	// The lifecycle, phase by phase, so a change can be read against the phase
	// it was meant to move rather than against the total.
	struct Phases
	{
		Stat gate;
		Stat compile;  // only on the ticks that rebuild the program
		Stat capture;
		Stat gather;
		Stat evaluate;
		Stat resolve;
		Stat apply;

		// Everything this mod did in one frame, and the sum of the frames that
		// drained one tick. The second is what compares against a legacy pass.
		Stat frame;
		Stat tick;

		// Enough to tell a hook that never fires from a gate that never
		// opens, which a table of zeros cannot.
		std::uint64_t advances = 0;
		std::uint64_t ticksFired = 0;
		std::uint64_t gateClosed = 0;
		const char* gateReason = "not yet called";

		std::size_t actorsCaptured = 0;
		std::size_t actorsDrained = 0;

		// What the compiler made of the mods, and what held for the last actor.
		std::size_t nodes = 0;
		std::size_t rules = 0;
		std::size_t rowsSeen = 0;
		std::size_t rowsUnsupported = 0;
		std::size_t matched = 0;
	};

	// What runs around the scheduler every frame: the actor state, the sampling and the appliers
	struct PerFrame
	{
		Stat total;         // all of this mod's work in the frame hook, the scheduler included
		Stat actorState;    // the fingerprint check, and any tree read it caused
		Stat sample;        // building each actor's channel values
		Stat applyUpdate;   // the Update pass over every actor
		Stat upload;        // the render thread's GPU writes, on the frames it had any
		Stat bodyMorphSum;  // summing a shape's deltas, on the frames a weight changed
		std::uint64_t bodyMorphWrites = 0;
	};

	// Any thread
	void RecordPerFrame(const std::function<void(PerFrame&)>& a_function);
	[[nodiscard]] PerFrame GetPerFrame();
}

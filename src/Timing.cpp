#include "Timing.h"

#include <algorithm>

namespace Timing
{
	void Stat::Add(float a_milliseconds)
	{
		_last = a_milliseconds;
		_worst = std::max(_worst, a_milliseconds);
		_average = _samples == 0 ? a_milliseconds
								 : _average + (a_milliseconds - _average) * kSmoothing;
		++_samples;
	}

	namespace
	{
		std::mutex g_perFrameLock;
		PerFrame g_perFrame;
	}

	void RecordPerFrame(const std::function<void(PerFrame&)>& a_function)
	{
		std::scoped_lock lock(g_perFrameLock);
		a_function(g_perFrame);
	}

	PerFrame GetPerFrame()
	{
		std::scoped_lock lock(g_perFrameLock);
		return g_perFrame;
	}

	void Stat::Reset()
	{
		_last = 0.f;
		_average = 0.f;
		_worst = 0.f;
		_samples = 0;
	}
}

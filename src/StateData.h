#pragma once

namespace Components
{
	enum class ActorValueType : int
	{
		kActorValue,
		kBase,
		kMax,
		kPercentage
	};
}

class IStateData
{
public:
	virtual ~IStateData() = default;
	virtual bool Update([[maybe_unused]] float a_deltaTime) { return false; }
};

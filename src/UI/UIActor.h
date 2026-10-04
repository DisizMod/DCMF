#pragma once
#include "UIWindow.h"

#include "../ActorState.h"

namespace UI
{
	class UIActor : public UIWindow
	{
	protected:
		bool ShouldDrawImpl() const override;
		void DrawImpl() override;
		void OnClose() override;

	private:
		void DrawActiveSubMods();
		void DrawAvailableParts();
		void DrawChannels();
		void DrawPart(const ActorState::Part& a_part) const;
	};
}

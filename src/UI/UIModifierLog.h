#pragma once
#include "../ModifierLog.h"
#include "UIWindow.h"

namespace UI
{
	class UIModifierLog : public UIWindow
	{
	public:
		// One step of a trace, as the log draws it; the Actor window draws its live trace with it too
		static void DrawTraceStep(const ModifierTrace::Step& a_step);

	protected:
		bool ShouldDrawImpl() const override;
		void DrawImpl() override;
		void OnOpen() override;
		void OnClose() override;

	private:
		[[nodiscard]] bool IsInteractable() const;
		void DrawFilterPanel() const;
		void DrawLogEntry(ModifierLogEntry& a_logEntry);

		void DrawTrace(const ModifierTrace& a_trace) const;
		static std::string_view GetTraceResultText(ModifierTrace::Result a_result);
		static void DrawTraceCondition(const ModifierTrace::Condition& a_condition, bool a_bDimSuccess);
		static void DrawTraceChannel(const ModifierTrace::Channel& a_channel);
	};
}

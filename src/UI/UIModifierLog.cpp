#include "UIModifierLog.h"
#include "Settings.h"
#include "UICommon.h"
#include "UIManager.h"

#include "../Utils.h"

#include <array>
#include <imgui_stdlib.h>

namespace UI
{
	bool UIModifierLog::ShouldDrawImpl() const
	{
		return Settings::bEnableModifierLog;
	}

	void UIModifierLog::DrawImpl()
	{
		SetWindowDimensions(Settings::fAnimationLogsOffsetX, Settings::fAnimationLogsOffsetY, Settings::fModifierLogWidth, 0.f, WindowAlignment::kTopLeft, ImVec2(Settings::fModifierLogWidth, -1), ImVec2(Settings::fModifierLogWidth, -1), ImGuiCond_Always);
		ForceSetWidth(Settings::fModifierLogWidth);

		constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

		auto& modifierLog = ModifierLog::GetSingleton();
		auto* refr = UIManager::GetSingleton().GetRefrToEvaluate();
		modifierLog.SetRefr(refr ? refr->GetFormID() : 0);

		if (ImGui::Begin("Modifier & Clip Log", nullptr, windowFlags)) {
			if (refr != nullptr) {
				if (!modifierLog.IsLogEmpty()) {
					// A set height, the older entries scrolled to, as the Actor window's channel list
					if (ImGui::BeginChild("##ModifierLogEntries", ImVec2(0.f, Settings::fModifierLogHeight))) {
						if (ImGui::BeginTable("ModifierLogTable", 1, ImGuiTableFlags_Borders)) {
							if (modifierLog.tracedEntry.bIsValid) {
								DrawLogEntry(modifierLog.tracedEntry);
								DrawTrace(modifierLog.tracedEntry.trace);
							} else {
								modifierLog.ForEachLogEntry([&](ModifierLogEntry& a_logEntry) {
									DrawLogEntry(a_logEntry);
								});
							}
							ImGui::EndTable();
						}
					}
					ImGui::EndChild();
				} else {
					UICommon::TextUnformattedDisabled("No modifier or clip log entries");
				}
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				ImGui::TextWrapped("No reference selected. Type in a FormID in the main window, or select a reference in the console.");
				ImGui::PopStyleColor();
			}
			if (IsInteractable()) {
				DrawFilterPanel();
			}
		}
		ImGui::End();
	}

	void UIModifierLog::OnOpen()
	{
		ModifierLog::GetSingleton().SetLog(true);
	}

	void UIModifierLog::OnClose()
	{
		ModifierLog::GetSingleton().SetLog(false);
	}

	bool UIModifierLog::IsInteractable() const
	{
		// draw filter panel only when the main UI is open
		return UIManager::GetSingleton().bShowMain;
	}

	void UIModifierLog::DrawFilterPanel() const
	{
		auto& modifierLog = ModifierLog::GetSingleton();

		// filtering
		const auto& style = ImGui::GetStyle();
		const float helpMarkerWidth = ImGui::CalcTextSize("(?)").x + style.ItemSpacing.x * 2;
		const float filterWidth = (ImGui::GetContentRegionAvail().x - style.FramePadding.x * 2 - helpMarkerWidth * 2);

		ImGui::SetNextItemWidth(filterWidth);
		ImGui::InputTextWithHint("##filter", "Filter... (Affects new entries)", &modifierLog.filter);
		ImGui::SameLine();
		UICommon::HelpMarker("Type a part of a mod name or a clip or modifier name to filter the log results. You can use regex.");
	}

	void UIModifierLog::DrawLogEntry(ModifierLogEntry& a_logEntry)
	{
		using Event = ModifierLogEntry::Event;
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();

		if (a_logEntry.timeDrawn < Settings::fModifierLogEntryFadeTime) {
			const float alpha = std::lerp(0.f, 0.25f, std::fmax(Settings::fModifierLogEntryFadeTime - a_logEntry.timeDrawn, 0.f) / Settings::fModifierLogEntryFadeTime);
			const ImVec4 color(1.f, 1.f, 1.f, alpha);
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(color));
		}

		const ImGuiIO& io = ImGui::GetIO();
		a_logEntry.timeDrawn += io.DeltaTime;

		// One line per change: what happened, to which, its mod and priority on the right
		bool bFirst = true;
		for (const auto& change : a_logEntry.changes) {
			const bool bClip = change.event != Event::kActivate && change.event != Event::kDeactivate;
			switch (change.event) {
			case Event::kActivate:
				UICommon::TextUnformattedColored(UICommon::LOG_ACTIVATED_COLOR, "Activate");
				break;
			case Event::kDeactivate:
				UICommon::TextUnformattedColored(UICommon::LOG_INTERRUPTED_COLOR, "Deactivate");
				break;
			case Event::kClipStart:
				UICommon::TextUnformattedColored(UICommon::LOG_ACTIVATED_COLOR, "Clip start");
				break;
			case Event::kClipEnd:
				UICommon::TextUnformattedColored(UICommon::LOG_LOOP_COLOR, "Clip end");
				break;
			case Event::kClipInterrupted:
				UICommon::TextUnformattedColored(UICommon::LOG_INTERRUPTED_COLOR, "Interrupted");
				break;
			case Event::kClipEcho:
				UICommon::TextUnformattedColored(UICommon::LOG_ECHO_COLOR, "Echo");
				break;
			default:
				break;
			}
			if (bClip) {
				ImGui::SameLine();
				UICommon::TextUnformattedColored(UICommon::LOG_VARIANT_COLOR, std::format("#{}", change.variant + 1).data());
			}

			if (bFirst && a_logEntry.count > 1) {
				ImGui::SameLine();
				const auto text = std::format("x{} ", a_logEntry.count);
				ImGui::TextUnformatted(text.data());
			}

			const std::string priorityText = std::format("Priority: {} ", change.priority);
			ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(priorityText.data()).x);
			UICommon::TextUnformattedDisabled(priorityText.data());

			UICommon::TextUnformattedDisabled("Mod:");
			ImGui::SameLine();
			ImGui::TextUnformatted(change.modName.data());
			ImGui::SameLine();
			UICommon::TextUnformattedDisabled(bClip ? "Clip:" : "Modifier:");
			ImGui::SameLine();
			ImGui::TextUnformatted(change.subModName.data());

			// trace button
			auto& modifierLog = ModifierLog::GetSingleton();
			if (bFirst && !modifierLog.tracedEntry.bIsValid) {
				const auto& style = ImGui::GetStyle();
				std::string traceButtonText = "Show trace";
				const float traceButtonWidth = ImGui::CalcTextSize(traceButtonText.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
				traceButtonText += "##" + std::to_string(reinterpret_cast<std::uintptr_t>(&a_logEntry));
				ImGui::SameLine(ImGui::GetContentRegionMax().x - traceButtonWidth);
				if (ImGui::SmallButton(traceButtonText.data())) {
					modifierLog.tracedEntry = a_logEntry;
				}
			}

			// Why an interrupted clip let go
			if (!change.reason.empty()) {
				UICommon::TextUnformattedDisabled("Reason:");
				ImGui::SameLine();
				UICommon::TextUnformattedColored(UICommon::LOG_INTERRUPTED_COLOR, change.reason.data());
			}
			bFirst = false;
		}
	}

	namespace
	{
		void SetColors(ModifierTrace::Result a_result, const std::function<void()>& a_function)
		{
			bool bPushedColor = false;

			switch (a_result) {
			case ModifierTrace::Result::kSuccess:
			case ModifierTrace::Result::kNoConditions:
				ImGui::PushStyleColor(ImGuiCol_Text, UICommon::SUCCESS_COLOR);
				bPushedColor = true;
				break;
			case ModifierTrace::Result::kFail:
				ImGui::PushStyleColor(ImGuiCol_Text, UICommon::FAIL_COLOR);
				bPushedColor = true;
				break;
			default:
				break;
			}

			a_function();

			if (bPushedColor) {
				ImGui::PopStyleColor();
			}
		}

		// The trace's own filter: a step shown when its mod or its name has the text
		std::string g_traceFilter;

		bool Shown(const ModifierTrace::Step& a_step)
		{
			if (g_traceFilter.empty()) {
				return true;
			}
			return Utils::ContainsStringIgnoreCase(a_step.modName, g_traceFilter) || Utils::ContainsStringIgnoreCase(a_step.subModName, g_traceFilter);
		}
	}

	void UIModifierLog::DrawTrace(const ModifierTrace& a_trace) const
	{
		using Section = ModifierTrace::Section;
		std::array<std::size_t, 5> counts{};
		for (const auto& step : a_trace.steps) {
			++counts[static_cast<std::size_t>(step.section)];
		}

		// What the tick came to, at a glance
		ImGui::TextUnformatted(std::format("{} playing  ·  {} active  ·  {} blocked  ·  {} failed  ·  {} disabled", counts[0], counts[1], counts[2], counts[3], counts[4]).data());
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::InputTextWithHint("##traceFilter", "Filter the trace by mod, clip or modifier name...", &g_traceFilter);

		if (ImGui::BeginChild("TraceWindow", ImVec2(0, 600), true)) {
			constexpr std::array sections{ std::pair{ Section::kPlaying, "Playing clips" }, std::pair{ Section::kActive, "Active modifiers" }, std::pair{ Section::kBlocked, "Blocked" },
				std::pair{ Section::kFailed, "Failed" }, std::pair{ Section::kDisabled, "Disabled" } };
			for (const auto& [section, title] : sections) {
				std::size_t shown = 0;
				for (const auto& step : a_trace.steps) {
					shown += step.section == section && Shown(step) ? 1 : 0;
				}
				if (shown == 0) {
					continue;
				}
				// What plays, is active or cannot play open; what failed or is off closed
				const bool bOpenFirst = section == Section::kPlaying || section == Section::kActive || section == Section::kBlocked;
				if (ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | (bOpenFirst ? ImGuiTreeNodeFlags_DefaultOpen : 0), "%s (%zu)", title, shown)) {
					for (const auto& step : a_trace.steps) {
						if (step.section == section && Shown(step)) {
							DrawTraceStep(step);
						}
					}
					ImGui::TreePop();
				}
			}
		}

		ImGui::EndChild();

		if (ImGui::Button("Close trace")) {
			auto& modifierLog = ModifierLog::GetSingleton();
			modifierLog.tracedEntry = ModifierLogEntry();
		}
	}

	void UIModifierLog::DrawTraceStep(const ModifierTrace::Step& a_step)
	{
		using Section = ModifierTrace::Section;
		ImGui::PushID(std::format("{}|{}", a_step.modName, a_step.subModName).data());
		bool bNodeOpen = false;
		if (!a_step.conditions.empty() || !a_step.channels.empty() || !a_step.triggers.empty()) {
			bNodeOpen = ImGui::TreeNodeEx("##step", ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
		} else {
			bNodeOpen = UICommon::TreeNodeCollapsedLeaf("##step", ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Bullet, "");
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(a_step.section == Section::kDisabled);

		// Its mod and name, what it is doing, and the conditions' answer on the right
		const std::string modText = std::format("{} | {}", a_step.modName, a_step.subModName);
		if (a_step.section == Section::kPlaying) {
			UICommon::TextUnformattedColored(UICommon::LOG_LOOP_COLOR, modText.data());
		} else if (a_step.section == Section::kActive) {
			UICommon::TextUnformattedColored(UICommon::LOG_ACTIVATED_COLOR, modText.data());
		} else {
			ImGui::TextUnformatted(modText.data());
		}
		ImGui::SameLine();
		UICommon::TextUnformattedDisabled(std::format("P{}", a_step.priority).data());
		if (!a_step.state.empty()) {
			ImGui::SameLine();
			UICommon::TextUnformattedDisabled(a_step.state.data());
		}

		UICommon::SecondColumn(0.85f);
		SetColors(a_step.result, [&]() {
			ImGui::TextUnformatted(GetTraceResultText(a_step.result).data());
		});
		ImGui::EndDisabled();

		// Why a clip whose conditions hold does not play
		if (!a_step.blocked.empty()) {
			ImGui::Indent();
			UICommon::TextUnformattedColored(UICommon::LOG_INTERRUPTED_COLOR, a_step.blocked.data());
			ImGui::Unindent();
		}

		if (bNodeOpen) {
			// A clip's triggers, the one that fired marked
			if (!a_step.triggers.empty()) {
				if (ImGui::TreeNodeEx("Triggers", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen, "Triggers (%zu)", a_step.triggers.size())) {
					for (const auto& trigger : a_step.triggers) {
						UICommon::TreeNodeCollapsedLeaf(&trigger, ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Bullet, "");
						ImGui::SameLine();
						ImGui::TextUnformatted(trigger.name.data());
						if (!trigger.argument.empty()) {
							ImGui::SameLine();
							UICommon::TextUnformattedDisabled(trigger.argument.data());
						}
						UICommon::SecondColumn(0.6f);
						if (trigger.bFired) {
							UICommon::TextUnformattedColored(UICommon::SUCCESS_COLOR, "Fired");
						} else if (!trigger.detail.empty()) {
							UICommon::TextUnformattedDisabled(trigger.detail.data());
						}
					}
					ImGui::TreePop();
				}
			} else if (a_step.bClip) {
				UICommon::TextUnformattedDisabled("No trigger: fires when its conditions come to hold");
			}

			// Its conditions: open straight away for what failed, the passing rows dimmed so the failing stand out
			if (!a_step.conditions.empty()) {
				const bool bFailed = a_step.section == Section::kFailed;
				if (ImGui::TreeNodeEx("Conditions", ImGuiTreeNodeFlags_SpanAvailWidth | (bFailed ? ImGuiTreeNodeFlags_DefaultOpen : 0), "Conditions (%zu)", a_step.conditions.size())) {
					for (std::size_t i = 0; i < a_step.conditions.size(); ++i) {
						ImGui::PushID(static_cast<int>(i));
						DrawTraceCondition(a_step.conditions[i], bFailed);
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
			}

			// Its channels, by what became of them
			if (!a_step.channels.empty()) {
				using Outcome = ModifierTrace::Outcome;
				constexpr std::array outcomes{ std::pair{ Outcome::kWins, "Wins" }, std::pair{ Outcome::kBlended, "Blended" }, std::pair{ Outcome::kOverridden, "Overridden" }, std::pair{ Outcome::kHeldBack, "Held back" } };
				if (ImGui::TreeNodeEx("Channels", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen, "Channels (%zu)", a_step.channels.size())) {
					for (const auto& [outcome, title] : outcomes) {
						const auto count = std::ranges::count(a_step.channels, outcome, &ModifierTrace::Channel::outcome);
						if (count == 0) {
							continue;
						}
						if (ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_SpanAvailWidth | (outcome == Outcome::kHeldBack ? 0 : ImGuiTreeNodeFlags_DefaultOpen), "%s (%zd)", title, count)) {
							for (const auto& channel : a_step.channels) {
								if (channel.outcome == outcome) {
									DrawTraceChannel(channel);
								}
							}
							ImGui::TreePop();
						}
					}
					ImGui::TreePop();
				}
			}

			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	std::string_view UIModifierLog::GetTraceResultText(ModifierTrace::Result a_result)
	{
		switch (a_result) {
		case ModifierTrace::Result::kSuccess:
			return "Success"sv;
		case ModifierTrace::Result::kFail:
			return "Fail"sv;
		case ModifierTrace::Result::kDisabled:
			return "Disabled"sv;
		case ModifierTrace::Result::kNoConditions:
			return "No conditions"sv;
		default:
			return "Unknown"sv;
		}
	}

	void UIModifierLog::DrawTraceCondition(const ModifierTrace::Condition& a_condition, bool a_bDimSuccess)
	{
		bool bConditionOpen = false;
		if (!a_condition.children.empty()) {
			bConditionOpen = ImGui::TreeNodeEx("##condition", ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | (a_bDimSuccess && a_condition.result == ModifierTrace::Result::kFail ? ImGuiTreeNodeFlags_DefaultOpen : 0), "");
		} else {
			bConditionOpen = UICommon::TreeNodeCollapsedLeaf("##condition", ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Bullet, "");
		}

		ImGui::SameLine();

		const bool bDim = a_condition.result == ModifierTrace::Result::kDisabled || (a_bDimSuccess && a_condition.result == ModifierTrace::Result::kSuccess);
		ImGui::BeginDisabled(bDim);

		ImGui::TextUnformatted(a_condition.name.data());
		if (!a_condition.argument.empty()) {
			ImGui::SameLine();
			UICommon::TextUnformattedDisabled(a_condition.argument.data());
		}
		// What it reads now, to see why it answered as it did
		if (!a_condition.current.empty()) {
			UICommon::SecondColumn(0.6f);
			UICommon::TextUnformattedDisabled(a_condition.current.data());
		}

		UICommon::SecondColumn(0.85f);

		SetColors(a_condition.result, [&]() {
			ImGui::TextUnformatted(GetTraceResultText(a_condition.result).data());
		});

		ImGui::EndDisabled();

		if (bConditionOpen) {
			for (std::size_t i = 0; i < a_condition.children.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				DrawTraceCondition(a_condition.children[i], a_bDimSuccess);
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
	}

	void UIModifierLog::DrawTraceChannel(const ModifierTrace::Channel& a_channel)
	{
		UICommon::TreeNodeCollapsedLeaf(&a_channel, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Bullet, "");
		ImGui::SameLine();

		// Applied in the success colour, mixed in the variant colour, shut out greyed
		const bool bOut = a_channel.outcome == ModifierTrace::Outcome::kOverridden || a_channel.outcome == ModifierTrace::Outcome::kHeldBack;
		ImGui::BeginDisabled(bOut);
		ImGui::TextUnformatted(a_channel.name.data());
		UICommon::SecondColumn(0.6f);
		switch (a_channel.outcome) {
		case ModifierTrace::Outcome::kWins:
			UICommon::TextUnformattedColored(UICommon::SUCCESS_COLOR, "Wins");
			break;
		case ModifierTrace::Outcome::kBlended:
			UICommon::TextUnformattedColored(UICommon::LOG_VARIANT_COLOR, "Blended");
			break;
		case ModifierTrace::Outcome::kOverridden:
			ImGui::TextUnformatted("Overridden");
			break;
		case ModifierTrace::Outcome::kHeldBack:
			ImGui::TextUnformatted("Held back");
			break;
		}
		if (!a_channel.detail.empty()) {
			ImGui::SameLine();
			UICommon::TextUnformattedDisabled(a_channel.detail.data());
		}
		ImGui::EndDisabled();
	}
}

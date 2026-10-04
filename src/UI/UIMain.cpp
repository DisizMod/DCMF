#include "UIMain.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "DetectedProblems.h"
#include "Jobs.h"
#include "ModRegistry.h"
#include "Parsing.h"
#include "UICommon.h"
#include "../Channels.h"
#include "../apply/Entries.h"
#include "../Hook.h"
#include "../BodyTypes.h"
#include "../Utils.h"
#include "../apply/HeadPart.h"
#include "MeshThumbs.h"
#include "../scan/Scan.h"
#include "../ClipPlayer.h"
#include "../ModifierLog.h"
#include "../Preview.h"
#include "../Resources.h"
#include "../Resolve.h"
#include "../Scheduler.h"
#include "../Timing.h"
#include <rapidjson/writer.h>

#include <numbers>
#include <rapidjson/stringbuffer.h>
#include "../Triggers.h"
#include "UIManager.h"

namespace UI
{
	bool UIMain::ShouldDrawImpl() const
	{
		return UIManager::GetSingleton().bShowMain;
	}

	void UIMain::DrawImpl()
	{
		// disable idle camera rotation
		if (const auto playerCamera = RE::PlayerCamera::GetSingleton()) {
			playerCamera->GetRuntimeData2().idleTimer = 0.f;
		}

		//ImGui::ShowDemoWindow();

		SetWindowDimensions(0.f, 0.f, 850.f, ImGui::GetIO().DisplaySize.y * 0.85f, WindowAlignment::kCenterLeft);

		const auto title = std::format("{} {}", Plugin::NAME, Plugin::VERSION_STRING);
		if (ImGui::Begin(title.data(), &UIManager::GetSingleton().bShowMain, ImGuiWindowFlags_NoCollapse)) {
			if (ImGui::BeginTable("EvaluateForReference", 2, ImGuiTableFlags_None)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				static char formIDBuf[9] = "";
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);

				if (const auto consoleRefr = Utils::GetConsoleRefr()) {
					ImGui::BeginDisabled();
					std::string formID = std::format("{:08X}", consoleRefr->GetFormID());
					ImGui::InputText("Evaluate for reference", formID.data(), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
					ImGui::EndDisabled();
				} else {
					ImGui::InputTextWithHint("Evaluate for reference", "FormID...", formIDBuf, IM_ARRAYSIZE(formIDBuf), ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase, &ReferenceInputTextCallback);
				}

				ImGui::SameLine();
				UICommon::HelpMarker("Select a reference in the game's console, or type the FormID of a reference to color clips, modifiers and conditions based on their current result for that reference. No need to type the leading zeros. (The player's FormID is 14).");

				if (const auto refr = UIManager::GetSingleton().GetRefrToEvaluate()) {
					ImGui::TableSetColumnIndex(1);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(UICommon::SUCCESS_BG_COLOR));
					ImGui::TextUnformatted(refr->GetDisplayFullName());
				}

				ImGui::EndTable();
			}

			//ImGui::Spacing();

			const auto& style = ImGui::GetStyle();
			const float bottomBarHeight = ImGui::GetTextLineHeight() + style.FramePadding.y * 4 + style.ItemSpacing.y * 4;
			if (ImGui::BeginChild("Tabs", ImVec2(0.f, -bottomBarHeight), true)) {
				if (ImGui::BeginTabBar("TabBar")) {
					if (ImGui::BeginTabItem("Registered Mods")) {
						DrawRegisteredMods();
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Scanned Data")) {
						DrawScan();
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Patches")) {
						DrawPatches();
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Feeds")) {
						DrawFeeds();
						ImGui::EndTabItem();
					}

					ImGui::EndTabBar();
				}
			}
			ImGui::EndChild();

			if (_commentState.bShouldOpen) {
				ImGui::OpenPopup("Edit comment##popup");
				_commentState.bShouldOpen = false;
			}

			if (_commentState.Valid()) {
				ImGui::SetNextWindowSize(ImVec2(280.f, 0.f));
				if (ImGui::BeginPopupModal("Edit comment##popup"), nullptr, ImGuiWindowFlags_NoDecoration) {
					if (ImGui::IsWindowAppearing()) {
						ImGui::SetKeyboardFocusHere();
					}

					float avail = ImGui::GetContentRegionAvail().x;
					float buttonWidth = (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

					bool bEdited = _commentState.HasUnsavedChanges();

					ImGui::SetNextItemWidth(avail);

					if (ImGui::InputTextWithHint("##comment", "Add comment...", &_commentState.buffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
						if (bEdited) {
							_commentState.Save();
							ImGui::CloseCurrentPopup();
						}
					}

					if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.f))) {
						_commentState.Clear();
						ImGui::CloseCurrentPopup();
					}

					ImGui::SameLine();

					ImGui::BeginDisabled(!bEdited);
					if (ImGui::Button("Save", ImVec2(buttonWidth, 0.f))) {
						_commentState.Save();
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();

					ImGui::EndPopup();
				}
			}

			const std::string animationEventLogButtonName = "Actor";
			const float animationEventLogButtonWidth = (ImGui::CalcTextSize(animationEventLogButtonName.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x);
			const std::string modifierLogButtonName = "Modifier & Clip Log";
			const float modifierLogButtonWidth = (ImGui::CalcTextSize(modifierLogButtonName.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x);

			const std::string settingsButtonName = _bShowSettings ? "Settings <" : "Settings >";
			const float settingsButtonWidth = (ImGui::CalcTextSize(settingsButtonName.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x);

			// Bottom bar
			if (ImGui::BeginChild("BottomBar", ImVec2(ImGui::GetContentRegionAvail().x - modifierLogButtonWidth - animationEventLogButtonWidth - settingsButtonWidth, 0.f), true)) {
				ImGui::AlignTextToFramePadding();
				// Status text

				auto& problems = DetectedProblems::GetSingleton();

				const std::string_view problemText = problems.GetProblemMessage();
				using Severity = DetectedProblems::Severity;

				if (problems.GetProblemSeverity() > Severity::kNone) {
					// Problems found
					switch (problems.GetProblemSeverity()) {
					case Severity::kWarning:
						ImGui::PushStyleColor(ImGuiCol_Button, UICommon::WARNING_BUTTON_COLOR);
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UICommon::WARNING_BUTTON_HOVERED_COLOR);
						ImGui::PushStyleColor(ImGuiCol_ButtonActive, UICommon::WARNING_BUTTON_ACTIVE_COLOR);
						break;
					case Severity::kError:
						ImGui::PushStyleColor(ImGuiCol_Button, UICommon::ERROR_BUTTON_COLOR);
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UICommon::ERROR_BUTTON_HOVERED_COLOR);
						ImGui::PushStyleColor(ImGuiCol_ButtonActive, UICommon::ERROR_BUTTON_ACTIVE_COLOR);
						break;
					}

					if (ImGui::Button(problemText.data(), ImGui::GetContentRegionAvail())) {
						if (problems.CheckForProblems()) {
							ImGui::OpenPopup("Problems");
						}
					}

					ImGui::PopStyleColor(3);

					const auto viewport = ImGui::GetMainViewport();
					ImGui::SetNextWindowPos(ImVec2(viewport->Size.x * 0.5f, viewport->Size.y * 0.5f), ImGuiCond_None, ImVec2(0.5f, 0.5f));

					float height = 30.f;
					if (problems.IsOutdated()) {
						height += 100.f;
					}
					if (problems.HasMissingPlugins()) {
						height += 100.f;
						height += problems.NumMissingPlugins() * ImGui::GetTextLineHeightWithSpacing();
					}
					if (problems.HasSubModsWithInvalidEntries()) {
						height += 200.f;
						height += problems.NumSubModsSharingPriority() * ImGui::GetTextLineHeightWithSpacing();
					}
					if (problems.HasSubModsSharingPriority()) {
						height += 200.f;
						height += problems.NumSubModsSharingPriority() * ImGui::GetTextLineHeightWithSpacing();
					}

					const float buttonHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.f;
					height += buttonHeight;

					height = std::clamp(height, 600.f, ImGui::GetIO().DisplaySize.y);

					ImGui::SetNextWindowSize(ImVec2(800.f, height));
					if (ImGui::BeginPopupModal("Problems", nullptr, ImGuiWindowFlags_NoResize)) {
						auto childSize = ImGui::GetContentRegionAvail();
						childSize.y -= buttonHeight;
						if (ImGui::BeginChild("ProblemsContent", childSize)) {
							bool bShouldDrawSeparator = false;
							if (problems.IsOutdated()) {
								ImGui::PushStyleColor(ImGuiCol_Text, UICommon::ERROR_TEXT_COLOR);
								ImGui::TextWrapped("ERROR: At least one registered mod has conditions that require a newer version of DCMF.\nThe mod will not function correctly. Please update DCMF!");
								ImGui::PopStyleColor();

								bShouldDrawSeparator = true;
							}

							if (problems.HasMissingPlugins()) {
								if (bShouldDrawSeparator) {
									ImGui::Spacing();
									ImGui::Spacing();
									ImGui::Separator();
									ImGui::Spacing();
									ImGui::Spacing();
								}
								ImGui::PushStyleColor(ImGuiCol_Text, UICommon::ERROR_TEXT_COLOR);
								ImGui::TextWrapped("ERROR: At least one registered mod has conditions that DCMF does not include itself, but that come from a plugin that is either not installed or outdated.\nThe mod will not function correctly. Please download or update the required plugin!");
								ImGui::PopStyleColor();
								ImGui::Spacing();
								DrawMissingPlugins();

								bShouldDrawSeparator = true;
							}

							if (problems.HasInvalidPlugins()) {
								if (bShouldDrawSeparator) {
									ImGui::Spacing();
									ImGui::Spacing();
									ImGui::Separator();
									ImGui::Spacing();
									ImGui::Spacing();
								}
								ImGui::PushStyleColor(ImGuiCol_Text, UICommon::ERROR_TEXT_COLOR);
								ImGui::TextWrapped("ERROR: At least one registered mod has conditions that DCMF does not include itself, but that come from a plugin that seems to have failed to initialize properly.\nThe mod will not function correctly. Please make sure you have installed the required plugin and all its dependencies correctly!");
								ImGui::PopStyleColor();
								ImGui::Spacing();
								DrawInvalidPlugins();

								bShouldDrawSeparator = true;
							}

							if (problems.HasRegisteredModsWithInvalidEntries()) {
								if (bShouldDrawSeparator) {
									ImGui::Spacing();
									ImGui::Spacing();
									ImGui::Separator();
									ImGui::Spacing();
									ImGui::Spacing();
								}
								ImGui::PushStyleColor(ImGuiCol_Text, UICommon::ERROR_TEXT_COLOR);
								ImGui::TextWrapped("ERROR: At least one registered mod has conditions that are invalid.\nThe mod will not function correctly. Please check the conditions of the registered mod, and check whether there's an update available!");
								ImGui::PopStyleColor();
								ImGui::Spacing();
								DrawRegisteredModsWithInvalidConditions();

								bShouldDrawSeparator = true;
							}

							if (problems.HasSubModsWithInvalidEntries()) {
								if (bShouldDrawSeparator) {
									ImGui::Spacing();
									ImGui::Spacing();
									ImGui::Separator();
									ImGui::Spacing();
									ImGui::Spacing();
								}
								ImGui::PushStyleColor(ImGuiCol_Text, UICommon::ERROR_TEXT_COLOR);
								ImGui::TextWrapped("ERROR: At least one clip or modifier has conditions that are invalid.\nThe mod will not function correctly. Please check the conditions of the registered mod, and check whether there's an update available!");
								ImGui::PopStyleColor();
								ImGui::Spacing();
								DrawSubModsWithInvalidConditions();

								bShouldDrawSeparator = true;
							}

							if (problems.HasSubModsSharingPriority()) {
								if (bShouldDrawSeparator) {
									ImGui::Spacing();
									ImGui::Spacing();
									ImGui::Separator();
									ImGui::Spacing();
									ImGui::Spacing();
								}
								ImGui::PushStyleColor(ImGuiCol_Text, UICommon::WARNING_TEXT_COLOR);
								ImGui::TextWrapped("WARNING: The following mods have conflicting priorities.\nThis might cause unexpected behavior if they drive the same channels.");
								ImGui::PopStyleColor();
								ImGui::Spacing();
								DrawConflictingSubMods();

								bShouldDrawSeparator = true;
							}
						}
						ImGui::EndChild();

						constexpr float buttonWidth = 120.f;
						ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonWidth) * 0.5f);
						ImGui::SetItemDefaultFocus();
						if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
				} else {
					ImGui::TextUnformatted(problemText.data());
				}
			}
			ImGui::EndChild();

			// Modifier & Clip Log button
			ImGui::SameLine(ImGui::GetWindowWidth() - modifierLogButtonWidth - animationEventLogButtonWidth - settingsButtonWidth);
			if (ImGui::Button(modifierLogButtonName.data(), ImVec2(0.f, bottomBarHeight - style.ItemSpacing.y))) {
				Settings::bEnableModifierLog = !Settings::bEnableModifierLog;
				Settings::WriteSettings();
			}

			// Actor window button
			ImGui::SameLine(ImGui::GetWindowWidth() - animationEventLogButtonWidth - settingsButtonWidth);
			if (ImGui::Button(animationEventLogButtonName.data(), ImVec2(0.f, bottomBarHeight - style.ItemSpacing.y))) {
				Settings::bShowActorWindow = !Settings::bShowActorWindow;
				Settings::WriteSettings();
			}

			// Settings button
			ImGui::SameLine(ImGui::GetWindowWidth() - settingsButtonWidth);
			if (ImGui::Button(settingsButtonName.data(), ImVec2(0.f, bottomBarHeight - style.ItemSpacing.y))) {
				_bShowSettings = !_bShowSettings;
			}
		}

		const auto windowContentMax = ImGui::GetWindowContentRegionMax();
		const auto windowPos = ImGui::GetWindowPos();
		const auto settingsPos = ImVec2(windowPos.x + windowContentMax.x + 7.f, windowPos.y + windowContentMax.y + 8.f);

		ImGui::End();

		if (_bShowSettings) {
			DrawSettings(settingsPos);
		}
		if (Settings::bShowTimings) {
			DrawTimings();
		}

		DrawTimeline();
	}

	void UIMain::OnOpen()
	{
		UIManager::GetSingleton().AddInputConsumer();

		auto& detectedProblems = DetectedProblems::GetSingleton();
		detectedProblems.CheckForSubModsSharingPriority();
		detectedProblems.CheckForSubModsWithInvalidEntries();
	}

	void UIMain::OnClose()
	{
		UIManager::GetSingleton().RemoveInputConsumer();
		_commentState.Clear();
	}

	void UIMain::DrawSettings(const ImVec2& a_pos)
	{
		ImGui::SetNextWindowPos(a_pos, ImGuiCond_None, ImVec2(0.f, 1.f));

		if (ImGui::Begin("Settings", &_bShowSettings, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			// UI settings
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("UI Settings");
			ImGui::Spacing();

			if (UICommon::InputKey("Menu key", Settings::uToggleUIKeyData)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("Set the key to toggle the UI.");

			ImGui::Spacing();

			if (ImGui::Checkbox("Show welcome banner", &Settings::bShowWelcomeBanner)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("Enable to show the welcome banner on startup.");

			static float tempScale = Settings::fUIScale;
			ImGui::SliderFloat("UI scale", &tempScale, 0.5f, 2.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SameLine();
			UICommon::HelpMarker("Set the UI scale.");
			ImGui::SameLine();
			ImGui::BeginDisabled(tempScale == Settings::fUIScale);
			if (ImGui::Button("Apply##UIScale")) {
				Settings::fUIScale = tempScale;
				Settings::WriteSettings();
			}
			ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::Separator();

			// General settings
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("General Settings");
			ImGui::Spacing();

			constexpr const char* targetModes[] = { "Disabled", "Player", "All" };
			if (ImGui::SliderInt("Target", reinterpret_cast<int*>(&Settings::uTargetMode), 0, 2, targetModes[Settings::uTargetMode])) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("Disabled: nothing is evaluated and everything driven is let go. Player: the player's face and nobody else's. All: everyone loaded whose race is a person's.");

			ImGui::BeginDisabled(Settings::uTargetMode != 2);
			if (ImGui::SliderFloat("Target distance", &Settings::fTargetDistance, 0.f, 4096.f, Settings::fTargetDistance <= 0.f ? "No limit" : "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
				Settings::WriteSettings();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			UICommon::HelpMarker("How far from the player an actor may be and still be driven. Zero is no limit.");

			if (ImGui::SliderFloat("Default transition time", &Settings::fDefaultTransitionTime, 0.f, 1.f, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("The transition time a modifier uses when its own custom time is off.");

			if (ImGui::SliderFloat("Tick rate", &Settings::fTickRate, 1.f, 20.f, "%.1f per second", ImGuiSliderFlags_AlwaysClamp)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("How often an actor is looked at. Conditions answer questions about AI state, which does not change at frame rate.");

			if (ImGui::Checkbox("Pause in RaceMenu", &Settings::bPauseInRaceMenu)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("While RaceMenu is open every actor is let go, the one being inspected too, so what RaceMenu reads and saves is the character's own look and not a modifier's or a clip's.\n\nWith this off, a face edited in RaceMenu while a face morph is held can lose the edit the next time that morph changes.");

			constexpr uint32_t workerCountMin = 1;
			constexpr uint32_t workerCountMax = 32;
			if (ImGui::SliderScalar("Parsing worker count", ImGuiDataType_U32, &Settings::uParsingWorkerCount, &workerCountMin, &workerCountMax, "%d", ImGuiSliderFlags_AlwaysClamp)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("Number of worker threads used while parsing registered mods on launch. Higher values can be slower due to storage contention. Takes effect after restarting the game.");

			ImGui::Spacing();
			ImGui::Separator();

			// Modifier & Clip Log settings
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Modifier & Clip Log Settings");
			ImGui::Spacing();

			int tempMaxEntries = static_cast<int>(Settings::uModifierLogMaxEntries);
			if (ImGui::SliderInt("Entries kept", &tempMaxEntries, 10, 500)) {
				Settings::uModifierLogMaxEntries = static_cast<uint32_t>(tempMaxEntries);
				ModifierLog::GetSingleton().ClampLog();
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("How many entries the Modifier & Clip Log keeps; the oldest go first.");

			ImGui::Spacing();
			ImGui::Separator();

			// Preview settings
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Preview Settings");
			ImGui::Spacing();

			if (ImGui::Checkbox("Lock spine", &Settings::bPreviewLockSpine)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("While previewing, the spine, neck, head and eyes are held on their rest pose, so the animation does not move what is being judged. The previewed item's own spine, head and gaze channels still apply.");

			if (ImGui::Checkbox("Hold A pose", &Settings::bPreviewHoldAPose)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("While previewing, the actor is held in an A pose: every skeleton bone on its rest pose and the arms turned down, over whatever animation plays. Physics bones are left to their physics. Best previewed standing still.");

			if (ImGui::Checkbox("Clear channels", &Settings::bPreviewClearChannels)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("While previewing, everything else on the actor is cleared, so only the previewed item shows.");

			ImGui::Spacing();
			ImGui::Separator();

			// Spine & headtracking settings
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Spine & Headtracking Settings");
			ImGui::Spacing();

			if (ImGui::SliderFloat("Spine1 share", &Settings::fTrackSpine1Share, 0.f, 1.f, "%.2f")) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("Of the turn a tracking spine makes, how much Spine1 takes; the neck closes whatever the two spine bones leave.");
			if (ImGui::SliderFloat("Spine2 share", &Settings::fTrackSpine2Share, 0.f, 1.f, "%.2f")) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("How much Spine2 takes of what Spine1 left.");
			if (ImGui::SliderFloat("Neck share", &Settings::fTrackNeckShare, 0.f, 1.f, "%.2f")) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("The neck's weight against the two above it; it closes the rest of the spine's turn within its reach.");
			{
				float reach[2]{ Settings::fTrackSpine1Yaw, Settings::fTrackSpine1Pitch };
				if (ImGui::SliderFloat2("Spine1 reach", reach, 0.f, 90.f, "%.0f deg")) {
					Settings::fTrackSpine1Yaw = reach[0];
					Settings::fTrackSpine1Pitch = reach[1];
					Settings::WriteSettings();
				}
				ImGui::SameLine();
				UICommon::HelpMarker("How far Spine1 may turn for a tracked target, off the animation's pose: sideways, then up and down.");
			}
			{
				float reach[2]{ Settings::fTrackSpine2Yaw, Settings::fTrackSpine2Pitch };
				if (ImGui::SliderFloat2("Spine2 reach", reach, 0.f, 90.f, "%.0f deg")) {
					Settings::fTrackSpine2Yaw = reach[0];
					Settings::fTrackSpine2Pitch = reach[1];
					Settings::WriteSettings();
				}
				ImGui::SameLine();
				UICommon::HelpMarker("How far Spine2 may turn for a tracked target.");
			}
			{
				float reach[2]{ Settings::fTrackNeckYaw, Settings::fTrackNeckPitch };
				if (ImGui::SliderFloat2("Neck reach", reach, 0.f, 90.f, "%.0f deg")) {
					Settings::fTrackNeckYaw = reach[0];
					Settings::fTrackNeckPitch = reach[1];
					Settings::WriteSettings();
				}
				ImGui::SameLine();
				UICommon::HelpMarker("How far the neck may turn for a tracked target.");
			}
			{
				float reach[2]{ Settings::fTrackHeadYaw, Settings::fTrackHeadPitch };
				if (ImGui::SliderFloat2("Head reach", reach, 0.f, 90.f, "%.0f deg")) {
					Settings::fTrackHeadYaw = reach[0];
					Settings::fTrackHeadPitch = reach[1];
					Settings::WriteSettings();
				}
				ImGui::SameLine();
				UICommon::HelpMarker("How far the head may turn for a tracked target, from where the spine put it.");
			}
			if (ImGui::SliderFloat("Mode transition", &Settings::fPoseModeTransition, 0.f, 3.f, "%.2f s")) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("How long the spine, the head or the gaze takes to ease from one mode to the next, Offset, Fixed, Track or Engine, instead of snapping.");

			ImGui::Spacing();
			ImGui::Separator();

			// Mfg Fix settings: what the face does on its own, blink and eye movement
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Mfg Fix Settings");
			ImGui::SameLine();
			UICommon::HelpMarker("The face animation this plugin runs in Mfg Fix's place: the blink and the eyes' wander, on every driven actor. A modifier's StopBlink and StopEyeMovement switches hold either off.");
			ImGui::Spacing();

			{
				float lid[2]{ Settings::fBlinkDownTime, Settings::fBlinkUpTime };
				if (ImGui::SliderFloat2("Blink lid time", lid, 0.01f, 0.5f, "%.2f s")) {
					Settings::fBlinkDownTime = lid[0];
					Settings::fBlinkUpTime = lid[1];
					Settings::WriteSettings();
				}
				ImGui::SameLine();
				UICommon::HelpMarker("How long the lid takes to close, then to open. Mfg Fix's are 0.04 and 0.14.");
			}
			{
				float gap[2]{ Settings::fBlinkDelayMin, Settings::fBlinkDelayMax };
				if (ImGui::SliderFloat2("Blink gap", gap, 0.1f, 20.f, "%.1f s")) {
					Settings::fBlinkDelayMin = (std::min)(gap[0], gap[1]);
					Settings::fBlinkDelayMax = (std::max)(gap[0], gap[1]);
					Settings::WriteSettings();
				}
				ImGui::SameLine();
				UICommon::HelpMarker("The shortest and longest wait between blinks, drawn short more often than long. Mfg Fix's are 0.5 and 8.");
			}
			{
				float reach[2]{ Settings::fEyeMovementHeading, Settings::fEyeMovementPitch };
				if (ImGui::SliderFloat2("Eye movement reach", reach, 0.f, 1.f, "%.2f")) {
					Settings::fEyeMovementHeading = reach[0];
					Settings::fEyeMovementPitch = reach[1];
					Settings::WriteSettings();
				}
				ImGui::SameLine();
				UICommon::HelpMarker("How far the eyes wander off their aim, sideways then up and down, as fractions of how far the engine lets them turn. 0 stills them.");
			}
			{
				float gap[2]{ Settings::fEyeMovementGapMin, Settings::fEyeMovementGapMax };
				if (ImGui::SliderFloat2("Eye movement gap", gap, 0.1f, 10.f, "%.1f s")) {
					Settings::fEyeMovementGapMin = (std::min)(gap[0], gap[1]);
					Settings::fEyeMovementGapMax = (std::max)(gap[0], gap[1]);
					Settings::WriteSettings();
				}
				ImGui::SameLine();
				UICommon::HelpMarker("The shortest and longest wait between glances.");
			}
			if (ImGui::SliderFloat("Eye movement speed", &Settings::fEyeMovementSpeed, 1.f, 40.f, "%.0f")) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("How fast a glance settles on its new spot; higher is snappier.");

			ImGui::Spacing();
			ImGui::Separator();

			// Timings
			if (ImGui::Checkbox("Show timings", &Settings::bShowTimings)) {
				Settings::WriteSettings();
			}
			ImGui::SameLine();
			UICommon::HelpMarker("Opens a window with what each phase of a tick costs, the per-frame costs around it, and what the compiler made of the mods.");
		}

		ImGui::End();
	}

	void UIMain::DrawTimings()
	{
		if (ImGui::Begin("Timings", &Settings::bShowTimings, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Phases");
			ImGui::SameLine();
			UICommon::HelpMarker("What each phase of a tick costs. Tick is the whole pass, summed over the frames it was spread across, which is the figure to compare against another pass.");
			ImGui::Spacing();

			{
				const auto timings = Scheduler::GetTimings();

				const auto row = [](const char* a_name, const Timing::Stat& a_stat) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(a_name);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%.3f", a_stat.Last());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%.3f", a_stat.Average());
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%.3f", a_stat.Worst());
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%llu", static_cast<unsigned long long>(a_stat.Samples()));
				};

				if (ImGui::BeginTable("##timings", 5, ImGuiTableFlags_BordersInner | ImGuiTableFlags_SizingFixedFit)) {
					ImGui::TableSetupColumn("Phase (all actors)");
					ImGui::TableSetupColumn("Last (ms)");
					ImGui::TableSetupColumn("Average");
					ImGui::TableSetupColumn("Worst");
					ImGui::TableSetupColumn("Samples");
					ImGui::TableHeadersRow();

					row("Gate", timings.gate);
					row("Compile", timings.compile);
					row("Capture", timings.capture);
					row("Gather", timings.gather);
					row("Evaluate", timings.evaluate);
					row("Resolve", timings.resolve);
					row("Apply", timings.apply);
					row("Frame", timings.frame);
					row("Tick", timings.tick);

					ImGui::EndTable();
				}

				// Around the scheduler, every frame: the actor state and the appliers
				const auto perFrame = Timing::GetPerFrame();
				if (ImGui::BeginTable("##perFrame", 5, ImGuiTableFlags_BordersInner | ImGuiTableFlags_SizingFixedFit)) {
					ImGui::TableSetupColumn("Per frame (all actors)");
					ImGui::TableSetupColumn("Last (ms)");
					ImGui::TableSetupColumn("Average");
					ImGui::TableSetupColumn("Worst");
					ImGui::TableSetupColumn("Samples");
					ImGui::TableHeadersRow();

					row("Total", perFrame.total);
					row("Actor state", perFrame.actorState);
					row("Sample", perFrame.sample);
					row("Apply, update pass", perFrame.applyUpdate);
					row("Body morph sum", perFrame.bodyMorphSum);
					row("Upload", perFrame.upload);

					ImGui::EndTable();
				}
				ImGui::Text("%llu body morph writes", static_cast<unsigned long long>(perFrame.bodyMorphWrites));

				// First: is the driver even running. A table of zeros says nothing on
				// its own.
				if (Hook::Installed()) {
					ImGui::Text("Hook: %s", Hook::Status().data());
				} else {
					UICommon::TextUnformattedColored(UICommon::ERROR_TEXT_COLOR,
						std::format("Hook NOT installed: {}", Hook::Status()).data());
				}
				ImGui::Text("%llu frames seen, %llu ticks fired",
					static_cast<unsigned long long>(timings.advances),
					static_cast<unsigned long long>(timings.ticksFired));
				if (timings.gateReason) {
					UICommon::TextUnformattedColored(UICommon::WARNING_TEXT_COLOR,
						std::format("Doing nothing: {} ({} frames turned away)", timings.gateReason,
							timings.gateClosed).data());
				}

				// Every figure in the table is the whole pass, every actor together.
				// The per-actor number is the one that says whether a crowd is the
				// problem or the rules are.
				const auto drained = timings.actorsDrained > 0 ? timings.actorsDrained : 1;
				ImGui::Text("%zu actors captured -- %.4f ms per actor last tick",
					timings.actorsCaptured, timings.tick.Last() / static_cast<float>(drained));
				ImGui::Text("%zu rules over %zu questions, from %zu rows (%zu not compiled)",
					timings.rules, timings.nodes, timings.rowsSeen, timings.rowsUnsupported);
				ImGui::Text("%zu rules held for the last actor", timings.matched);

				if (ImGui::Button("Recompile")) {
					Scheduler::Recompile();
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset timings")) {
					Scheduler::ResetTimings();
				}
			}
		}
		ImGui::End();
		if (!Settings::bShowTimings) {
			Settings::WriteSettings();
		}
	}

	void UIMain::DrawMissingPlugins()
	{
		if (ImGui::BeginTable("MissingPlugins", 3, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter)) {
			ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Min required", ImGuiTableColumnFlags_WidthFixed, 100.f);
			ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 100.f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			DetectedProblems::GetSingleton().ForEachMissingPlugin([&](auto& a_missingPlugin) {
				auto& missingPluginName = a_missingPlugin.first;
				auto& missingPluginVersion = a_missingPlugin.second;

				const REL::Version currentPluginVersion = ModRegistry::GetSingleton().GetPluginVersion(missingPluginName);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				ImGui::TextUnformatted(missingPluginName.data());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(missingPluginVersion.string("."sv).data());
				ImGui::TableSetColumnIndex(2);
				if (currentPluginVersion > 0) {
					ImGui::TextUnformatted(currentPluginVersion.string("."sv).data());
				} else {
					ImGui::TextUnformatted("Missing");
				}
			});

			ImGui::EndTable();
		}
	}

	void UIMain::DrawInvalidPlugins()
	{
		if (ImGui::BeginTable("InvalidPlugins", 3, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter)) {
			ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Min required", ImGuiTableColumnFlags_WidthFixed, 100.f);
			ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 100.f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			DetectedProblems::GetSingleton().ForEachMissingPlugin([&](auto& a_invalidPlugin) {
				auto& invalidPluginName = a_invalidPlugin.first;
				auto& invalidPluginVersion = a_invalidPlugin.second;

				const REL::Version currentPluginVersion = ModRegistry::GetSingleton().GetPluginVersion(invalidPluginName);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				ImGui::TextUnformatted(invalidPluginName.data());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(invalidPluginVersion.string("."sv).data());
				ImGui::TableSetColumnIndex(2);
				if (currentPluginVersion > 0) {
					ImGui::TextUnformatted(currentPluginVersion.string("."sv).data());
				} else {
					ImGui::TextUnformatted("Missing");
				}
			});

			ImGui::EndTable();
		}
	}

	void UIMain::DrawConflictingSubMods() const
	{
		if (ImGui::BeginTable("ConflictingSubMods", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter)) {
			ImGui::TableSetupColumn("Clip or modifier", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 100.f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			bool bJustStarted = true;
			int32_t prevPriority = 0;

			DetectedProblems::GetSingleton().ForEachSubModSharingPriority([&](const SubMod* a_subMod) {
				const auto parentMod = a_subMod->GetParentMod();

				const auto nodeName = a_subMod->GetName();

				if (bJustStarted) {
					bJustStarted = false;
				} else if (prevPriority != a_subMod->GetPriority()) {
					ImGui::Spacing();
				}
				prevPriority = a_subMod->GetPriority();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				const bool bOpen = ImGui::TreeNode(nodeName.data());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(std::to_string(a_subMod->GetPriority()).data());
				if (bOpen) {
					if (parentMod) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(parentMod->GetName().data());
						UICommon::TextUnformattedEllipsis(a_subMod->GetPath().data());
					}
					ImGui::TreePop();
				}
			});

			ImGui::EndTable();
		}
	}

	void UIMain::DrawRegisteredModsWithInvalidConditions() const
	{
		if (ImGui::BeginTable("RegisteredModsWithInvalidConditions", 1, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter)) {
			ImGui::TableSetupColumn("Registered mod", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			DetectedProblems::GetSingleton().ForEachRegisteredModWithInvalidEntries([&](const RegisteredMod* a_registeredMod) {
				const auto registeredModName = a_registeredMod->GetName();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(registeredModName.data());
			});

			ImGui::EndTable();
		}
	}

	void UIMain::DrawSubModsWithInvalidConditions() const
	{
		if (ImGui::BeginTable("SubModsWithInvalidConditions", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter)) {
			ImGui::TableSetupColumn("Parent mod", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Clip or modifier", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			DetectedProblems::GetSingleton().ForEachSubModWithInvalidEntries([&](const SubMod* a_subMod) {
				const auto subModName = a_subMod->GetName();
				const auto parentModName = a_subMod->GetParentMod()->GetName();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				ImGui::TextUnformatted(parentModName.data());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(subModName.data());
			});

			ImGui::EndTable();
		}
	}

	void UIMain::DrawRegisteredMods()
	{
		static char nameFilterBuf[32] = "";
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
		ImGui::InputTextWithHint("Filter", "Mod/clip or modifier/author name...", nameFilterBuf, IM_ARRAYSIZE(nameFilterBuf));
		ImGui::SameLine();
		UICommon::HelpMarker("Type a part of a mod/clip or modifier/author name to filter the list.");

		const float offset = ImGui::CalcTextSize("Inspect Mode").x + ImGui::CalcTextSize("User Mode").x + ImGui::CalcTextSize("Author Mode").x + ImGui::CalcTextSize("(?)").x + 100.f;
		ImGui::SameLine(ImGui::GetWindowWidth() - offset);

		ImGui::RadioButton("Inspect Mode", reinterpret_cast<int*>(&_editMode), 0);
		ImGui::SameLine();
		ImGui::RadioButton("User Mode", reinterpret_cast<int*>(&_editMode), 1);
		ImGui::SameLine();
		ImGui::RadioButton("Author Mode", reinterpret_cast<int*>(&_editMode), 2);
		ImGui::SameLine();
		UICommon::HelpMarker("Editing in author mode will edit the original config files contained in the mod folders. User mode creates and saves a new configuration file that will override the original one when the settings are reloaded. That won't affect the original file.");

		ImGui::Separator();

		DrawNewEntryPopup();

		if (ImGui::BeginChild("Mods")) {
			ModRegistry::GetSingleton().ForEachSortedRegisteredMod([&](RegisteredMod* a_registeredMod) {
				// Parse names for filtering and duplicate names
				std::unordered_map<std::string, SubModNameFilterResult> subModNameFilterResults{};
				const bool bEntireRegisteredModMatchesFilter = !std::strlen(nameFilterBuf) || Utils::ContainsStringIgnoreCase(a_registeredMod->GetName(), nameFilterBuf) || Utils::ContainsStringIgnoreCase(a_registeredMod->GetAuthor(), nameFilterBuf);
				bool bDisplayMod = bEntireRegisteredModMatchesFilter;

				a_registeredMod->ForEachSubMod([&](const SubMod* a_subMod) {
					SubModNameFilterResult res;
					const auto subModName = a_subMod->GetName();
					res.bDisplay = bEntireRegisteredModMatchesFilter || Utils::ContainsStringIgnoreCase(subModName, nameFilterBuf);

					// if at least one clip or modifier matches filter, display the registered mod
					if (res.bDisplay) {
						bDisplayMod = true;
					}

					auto [it, bInserted] = subModNameFilterResults.try_emplace(subModName.data(), res);
					if (!bInserted) {
						subModNameFilterResults[subModName.data()].bDuplicateName = true;
					}

					return RE::BSVisit::BSVisitControl::kContinue;
				});

				if (bDisplayMod) {
					DrawRegisteredMod(a_registeredMod, subModNameFilterResults);
				}
			});

			// A new mod, named in a popup
			if (_editMode == EditMode::kAuthor) {
				ImGui::Spacing();
				if (ImGui::Button("Register mod")) {
					_newEntryState = { NewEntryState::Kind::kMod, nullptr, {}, true };
				}
			}
		}
		ImGui::EndChild();
	}

	// The name box for a mod, modifier or clip about to be made; Enter or Create makes it and opens it
	void UIMain::DrawNewEntryPopup()
	{
		using Kind = NewEntryState::Kind;
		if (_newEntryState.bShouldOpen) {
			ImGui::OpenPopup("New entry##popup");
			_newEntryState.bShouldOpen = false;
		}
		if (_newEntryState.kind == Kind::kNone) {
			return;
		}

		const char* what = _newEntryState.kind == Kind::kMod ? "mod" : _newEntryState.kind == Kind::kModifier ? "modifier" : "clip";
		ImGui::SetNextWindowSize(ImVec2(280.f, 0.f));
		if (ImGui::BeginPopupModal("New entry##popup", nullptr, ImGuiWindowFlags_NoDecoration)) {
			if (ImGui::IsWindowAppearing()) {
				ImGui::SetKeyboardFocusHere();
			}

			const float avail = ImGui::GetContentRegionAvail().x;
			const float buttonWidth = (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

			const auto create = [&]() {
				const auto& name = _newEntryState.name;
				if (_newEntryState.kind == Kind::kMod) {
					auto mod = std::make_unique<RegisteredMod>(std::format("Data\\SKSE\\Plugins\\DCMF\\{}", name), name, ""sv, ""sv);
					// New, so there is something to save
					mod->SetDirty(true);
					_openMod = ModRegistry::GetSingleton().AddRegisteredMod(mod);
				} else if (auto* parent = _newEntryState.parentMod) {
					std::unique_ptr<SubMod> subMod = _newEntryState.kind == Kind::kModifier ?
					                                     std::unique_ptr<SubMod>(std::make_unique<AppearanceModifier>(parent)) :
					                                     std::unique_ptr<SubMod>(std::make_unique<Clip>(parent));
					subMod->SetName(name);
					subMod->SetPath(parent->GetPath());
					subMod->SetDirty(true);
					_openSubMod = subMod.get();
					_openMod = parent;
					parent->AddSubMod(subMod);
					Scheduler::Recompile();
				}
				_newEntryState = {};
				ImGui::CloseCurrentPopup();
			};

			ImGui::SetNextItemWidth(avail);
			const bool bEntered = ImGui::InputTextWithHint("##name", std::format("New {} name...", what).data(), &_newEntryState.name, ImGuiInputTextFlags_EnterReturnsTrue);
			const bool bNamed = !_newEntryState.name.empty();
			if (bEntered && bNamed) {
				create();
			}

			if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.f))) {
				_newEntryState = {};
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			ImGui::BeginDisabled(!bNamed);
			if (ImGui::Button("Create", ImVec2(buttonWidth, 0.f))) {
				create();
			}
			ImGui::EndDisabled();

			ImGui::EndPopup();
		}
	}

	void UIMain::DrawRegisteredMod(RegisteredMod* a_registeredMod, std::unordered_map<std::string, SubModNameFilterResult>& a_filterResults)
	{
		if (!a_registeredMod) {
			return;
		}

		if (_openMod == a_registeredMod) {
			ImGui::SetNextItemOpen(true);
			_openMod = nullptr;
		}
		bool bNodeOpen = ImGui::TreeNodeEx(a_registeredMod, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

		if (a_registeredMod->IsDirty()) {
			ImGui::SameLine();
			UICommon::TextUnformattedColored(UICommon::DIRTY_COLOR, "*");
		}

		if (a_registeredMod->GetConfigSource() == Parsing::ConfigSource::kUser) {
			ImGui::SameLine();
			UICommon::TextUnformattedColored(UICommon::USER_MOD_COLOR, "(User)");
		}

		// node name
		ImGui::SameLine();
		if (a_registeredMod->HasInvalidConditions(false) || a_registeredMod->HasInvalidFunctions()) {
			UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_registeredMod->GetName().data());
		} else {
			ImGui::TextUnformatted(a_registeredMod->GetName().data());
		}

		if (bNodeOpen) {
			// Mod name
			if (_editMode == EditMode::kAuthor) {
				const std::string nameId = "Mod name##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + "name";
				ImGui::SetNextItemWidth(-150.f);
				std::string tempName(a_registeredMod->GetName());
				if (ImGui::InputTextWithHint(nameId.data(), "Mod name", &tempName)) {
					a_registeredMod->SetName(tempName);
					a_registeredMod->SetDirty(true);
				}
			}

			// Mod author
			const std::string authorId = "Author##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + "author";
			if (_editMode == EditMode::kAuthor) {
				ImGui::SetNextItemWidth(250.f);
				std::string tempAuthor(a_registeredMod->GetAuthor());
				if (ImGui::InputTextWithHint(authorId.data(), "Author", &tempAuthor)) {
					a_registeredMod->SetAuthor(tempAuthor);
					a_registeredMod->SetDirty(true);
				}
			} else if (!a_registeredMod->GetAuthor().empty()) {
				if (ImGui::BeginTable(authorId.data(), 1, ImGuiTableFlags_BordersOuter)) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					UICommon::TextDescriptionRightAligned("Author");
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(a_registeredMod->GetAuthor().data());
					ImGui::EndTable();
				}
			}

			// Mod description
			const std::string descriptionId = "Mod description##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + "description";
			if (_editMode == EditMode::kAuthor) {
				ImGui::SetNextItemWidth(-150.f);
				std::string tempDescription(a_registeredMod->GetDescription());
				if (ImGui::InputTextMultiline(descriptionId.data(), &tempDescription, ImVec2(0, ImGui::GetTextLineHeight() * 5))) {
					a_registeredMod->SetDescription(tempDescription);
					a_registeredMod->SetDirty(true);
				}
				ImGui::Spacing();
			} else if (!a_registeredMod->GetDescription().empty()) {
				if (ImGui::BeginTable(descriptionId.data(), 1, ImGuiTableFlags_BordersOuter)) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					UICommon::TextDescriptionRightAligned("Description");
					UICommon::TextUnformattedWrapped(a_registeredMod->GetDescription().data());
					ImGui::EndTable();
				}
				ImGui::Spacing();
			}

			// Required mods -- the multi-select field, listing every registered mod
			{
				const std::string requiredModsId = "##RequiredMods" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod));
				constexpr auto requiredModsTooltip = "The mods this mod builds on. Their condition presets, trigger presets and reference pools can be used by this mod's clips and modifiers, and their overlay slots are listed in its Overlay Slots to add overlays to. Pick from the registered mods, or add a name by hand; a name that matches nothing registered is shown in orange.";

				std::vector<UICommon::MultiSelectItem> options;
				ModRegistry::GetSingleton().ForEachSortedRegisteredMod([&](RegisteredMod* a_candidate) {
					if (a_candidate != a_registeredMod) {
						options.emplace_back(UICommon::MultiSelectItem{ std::string(a_candidate->GetName()), std::string(a_candidate->GetDescription()) });
					}
				});

				auto& requiredMods = a_registeredMod->GetRequiredMods();
				if (_editMode == EditMode::kAuthor) {
					ImGui::SetNextItemWidth(-220.f);
					if (UICommon::MultiSelectCombo(requiredModsId.data(), requiredMods, options, true, "Add a mod by name...", "No registered mod of this name is loaded.")) {
						a_registeredMod->SetDirty(true);
						a_registeredMod->ResolveConditionPresets();
					}
					ImGui::SameLine();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted("Required mods");
					ImGui::SameLine();
					UICommon::HelpMarker(requiredModsTooltip);
					ImGui::Spacing();
				} else if (!requiredMods.empty()) {
					const auto tableWidth = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("(?)").x - ImGui::GetStyle().FramePadding.x * 2;
					if (ImGui::BeginTable(requiredModsId.data(), 1, ImGuiTableFlags_BordersOuter, ImVec2(tableWidth, 0))) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::AlignTextToFramePadding();
						UICommon::TextDescriptionRightAligned("Required mods");
						for (size_t i = 0; i < requiredMods.size(); ++i) {
							if (i > 0) {
								ImGui::SameLine(0.f, 0.f);
								ImGui::TextUnformatted(", ");
								ImGui::SameLine(0.f, 0.f);
							}
							const bool bKnown = std::ranges::any_of(options, [&](const UICommon::MultiSelectItem& a_option) {
								return a_option.name == requiredMods[i];
							});
							if (bKnown) {
								ImGui::TextUnformatted(requiredMods[i].data());
							} else {
								UICommon::TextUnformattedColored(UICommon::WARNING_TEXT_COLOR, requiredMods[i].data());
								UICommon::AddTooltip("No registered mod of this name is loaded.");
							}
						}
						ImGui::EndTable();
					}
					ImGui::Spacing();
				}
			}

			{
				const std::string modId = std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod));
				// The presets inside are absent in Inspect mode, so the heading does not
				// promise them. The id after ## is unchanged, so the open state carries
				// across a mode switch.
				// What the mod ships for others to use
				if (ImGui::CollapsingHeader(std::format("Resource Definitions##{}definitions", modId).data())) {
					ImGui::Indent();

					// Skin sets
					if (_editMode > EditMode::kNone || a_registeredMod->HasSkinSets()) {
						const std::string setsLabel = std::format("Skin Sets##{}skinSets", modId);
						const ImGuiTreeNodeFlags flags = a_registeredMod->HasSkinSets() ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
						if (ImGui::CollapsingHeader(setsLabel.data(), flags)) {
							ImGui::AlignTextToFramePadding();
							ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

							bool bShouldSort = false;
							a_registeredMod->ForEachSkinSet([&](Skins::Set* a_set) {
								if (DrawSkinSet(a_registeredMod, a_set, bShouldSort)) {
									a_registeredMod->SetDirty(true);
								}
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							if (bShouldSort) {
								a_registeredMod->SortSkinSets();
							}

							if (_editMode > EditMode::kNone) {
								// Add skin set button, naming it in a popup as a condition preset is named
								constexpr auto popupName = "Adding new skin set"sv;
								if (ImGui::Button("Add skin set")) {
									const auto popupPos = ImGui::GetCursorScreenPos();
									ImGui::SetNextWindowPos(popupPos);
									ImGui::OpenPopup(popupName.data());
								}

								if (ImGui::BeginPopupModal(popupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
									std::string setName;
									static std::string setNameError;
									if (ImGui::IsWindowAppearing()) {
										setNameError.clear();
									}
									if (ImGui::InputTextWithHint("##SkinSetName", "Type a unique name...", &setName, ImGuiInputTextFlags_EnterReturnsTrue)) {
										setNameError = setName.size() <= 2 ? "The name needs at least 3 characters."s : a_registeredMod->HasSkinSet(setName) ? "A skin set of this name already exists."s : std::string{};
										if (setNameError.empty()) {
											auto newSet = std::make_unique<Skins::Set>(setName);
											a_registeredMod->AddSkinSet(newSet);
											a_registeredMod->SetDirty(true);
											ImGui::CloseCurrentPopup();
										}
									}
									ImGui::SetItemDefaultFocus();
									ImGui::SameLine();
									if (ImGui::Button("Cancel")) {
										ImGui::CloseCurrentPopup();
									}
									// Why the name was refused
									if (!setNameError.empty()) {
										UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, setNameError.data());
									}
									ImGui::EndPopup();
								}
							}

							ImGui::PopStyleVar();
						}
					}

					// Overlay slots: its own, then each required mod's, a name both define drawn once, as the required one
					std::vector<std::pair<RegisteredMod*, Overlays::Slot*>> requiredSlots;
					a_registeredMod->ForEachRequiredMod([&](RegisteredMod* a_required) {
						a_required->ForEachOverlaySlot([&](Overlays::Slot* a_slot) {
							if (std::ranges::none_of(requiredSlots, [&](const auto& a_listed) { return a_listed.second->GetName() == a_slot->GetName(); })) {
								requiredSlots.emplace_back(a_required, a_slot);
							}
							return RE::BSVisit::BSVisitControl::kContinue;
						});
					});
					if (_editMode > EditMode::kNone || a_registeredMod->HasOverlaySlots() || !requiredSlots.empty()) {
						const std::string slotsLabel = std::format("Overlay Slots##{}overlaySlots", modId);
						const ImGuiTreeNodeFlags flags = a_registeredMod->HasOverlaySlots() ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
						if (ImGui::CollapsingHeader(slotsLabel.data(), flags)) {
							ImGui::AlignTextToFramePadding();
							ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

							bool bShouldSort = false;
							a_registeredMod->ForEachOverlaySlot([&](Overlays::Slot* a_slot) {
								if (std::ranges::any_of(requiredSlots, [&](const auto& a_listed) { return a_listed.second->GetName() == a_slot->GetName(); })) {
									return RE::BSVisit::BSVisitControl::kContinue;
								}
								if (DrawOverlaySlot(a_registeredMod, a_slot, bShouldSort)) {
									a_registeredMod->SetDirty(true);
								}
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							for (const auto& [required, slot] : requiredSlots) {
								if (DrawOverlaySlot(a_registeredMod, slot, bShouldSort, required)) {
									a_registeredMod->SetDirty(true);
								}
							}
							if (bShouldSort) {
								a_registeredMod->SortOverlaySlots();
							}

							if (_editMode > EditMode::kNone) {
								// Add overlay slot button, naming it in a popup as a condition preset is named
								constexpr auto popupName = "Adding new overlay slot"sv;
								if (ImGui::Button("Add overlay slot")) {
									const auto popupPos = ImGui::GetCursorScreenPos();
									ImGui::SetNextWindowPos(popupPos);
									ImGui::OpenPopup(popupName.data());
								}

								if (ImGui::BeginPopupModal(popupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
									std::string slotName;
									static std::string slotNameError;
									if (ImGui::IsWindowAppearing()) {
										slotNameError.clear();
									}
									if (ImGui::InputTextWithHint("##OverlaySlotName", "Type a unique name...", &slotName, ImGuiInputTextFlags_EnterReturnsTrue)) {
										slotNameError = slotName.size() <= 2 ? "The name needs at least 3 characters."s : a_registeredMod->HasOverlaySlot(slotName) ? "An overlay slot of this name already exists."s : std::string{};
										if (slotNameError.empty()) {
											auto newSlot = std::make_unique<Overlays::Slot>(slotName);
											a_registeredMod->AddOverlaySlot(newSlot);
											a_registeredMod->SetDirty(true);
											ImGui::CloseCurrentPopup();
										}
									}
									ImGui::SetItemDefaultFocus();
									ImGui::SameLine();
									if (ImGui::Button("Cancel")) {
										ImGui::CloseCurrentPopup();
									}
									// Why the name was refused
									if (!slotNameError.empty()) {
										UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, slotNameError.data());
									}
									ImGui::EndPopup();
								}
							}

							ImGui::PopStyleVar();
						}
					}

					ImGui::Unindent();
				}

				// What the mod's own clips and modifiers reuse
				if (_editMode > EditMode::kNone || a_registeredMod->HasConditionPresets() || a_registeredMod->HasReferencePools()) {
				if (ImGui::CollapsingHeader(std::format("Presets##{}presets", modId).data())) {
					ImGui::Indent();

					// Reference pool presets, drawn as trigger presets are
					if (_editMode > EditMode::kNone || a_registeredMod->HasReferencePools()) {
						const std::string poolsLabel = "References Pool Presets##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + "referencePools";
						if (ImGui::CollapsingHeader(poolsLabel.data())) {
							ImGui::AlignTextToFramePadding();
							ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

							std::string removed;
							a_registeredMod->ForEachReferencePool([&](ReferencePool* a_pool) {
								if (DrawReferencePool(a_registeredMod, a_pool, removed)) {
									a_registeredMod->SetDirty(true);
								}
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							if (!removed.empty()) {
								a_registeredMod->RemoveReferencePool(removed);
								a_registeredMod->SetDirty(true);
							}

							if (_editMode > EditMode::kNone) {
								// Add reference pool button, named in a popup as a trigger preset is
								constexpr auto popupName = "Adding new reference pool"sv;
								if (ImGui::Button("Add new reference pool")) {
									const auto popupPos = ImGui::GetCursorScreenPos();
									ImGui::SetNextWindowPos(popupPos);
									ImGui::OpenPopup(popupName.data());
								}
								if (ImGui::BeginPopupModal(popupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
									std::string poolName;
									static std::string poolNameError;
									if (ImGui::IsWindowAppearing()) {
										poolNameError.clear();
									}
									if (ImGui::InputTextWithHint("##ReferencePoolName", "Type a unique name...", &poolName, ImGuiInputTextFlags_EnterReturnsTrue)) {
										poolNameError = poolName.size() <= 2 ? "The name needs at least 3 characters."s : a_registeredMod->GetReferencePool(poolName) ? "A reference pool of this name already exists."s : std::string{};
										if (poolNameError.empty()) {
											auto newPool = std::make_unique<ReferencePool>(poolName);
											a_registeredMod->AddReferencePool(newPool);
											a_registeredMod->SetDirty(true);
											ImGui::CloseCurrentPopup();
										}
									}
									ImGui::SetItemDefaultFocus();
									ImGui::SameLine();
									if (ImGui::Button("Cancel")) {
										ImGui::CloseCurrentPopup();
									}
									// Why the name was refused
									if (!poolNameError.empty()) {
										UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, poolNameError.data());
									}
									ImGui::EndPopup();
								}
							}

							ImGui::PopStyleVar();
						}
					}

					// Trigger presets
					if (_editMode > EditMode::kNone || a_registeredMod->HasTriggerPresets()) {
						const std::string triggerPresetsLabel = "Trigger presets##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + "triggerPresets";
						ImGuiTreeNodeFlags flags = a_registeredMod->HasTriggerPresets() ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
						if (ImGui::CollapsingHeader(triggerPresetsLabel.data(), flags)) {
							ImGui::AlignTextToFramePadding();
							ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

							bool bShouldSort = false;
							a_registeredMod->ForEachTriggerPreset([&](Triggers::TriggerPreset* a_preset) {
								if (DrawTriggerPreset(a_registeredMod, a_preset, bShouldSort)) {
									a_registeredMod->SetDirty(true);
								}
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							if (bShouldSort) {
								a_registeredMod->SortTriggerPresets();
							}

							if (_editMode > EditMode::kNone) {
								// Add trigger preset button, named in a popup as a condition preset is
								constexpr auto popupName = "Adding new trigger preset"sv;
								if (ImGui::Button("Add new trigger preset")) {
									const auto popupPos = ImGui::GetCursorScreenPos();
									ImGui::SetNextWindowPos(popupPos);
									ImGui::OpenPopup(popupName.data());
								}
								if (ImGui::BeginPopupModal(popupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
									std::string presetName;
									static std::string presetNameError;
									if (ImGui::IsWindowAppearing()) {
										presetNameError.clear();
									}
									if (ImGui::InputTextWithHint("##TriggerPresetName", "Type a unique name...", &presetName, ImGuiInputTextFlags_EnterReturnsTrue)) {
										presetNameError = presetName.size() <= 2 ? "The name needs at least 3 characters."s : a_registeredMod->HasTriggerPreset(presetName) ? "A trigger preset of this name already exists."s : std::string{};
										if (presetNameError.empty()) {
											auto newPreset = std::make_unique<Triggers::TriggerPreset>(presetName, ""sv);
											a_registeredMod->AddTriggerPreset(newPreset);
											a_registeredMod->SetDirty(true);
											ImGui::CloseCurrentPopup();
										}
									}
									ImGui::SetItemDefaultFocus();
									ImGui::SameLine();
									if (ImGui::Button("Cancel")) {
										ImGui::CloseCurrentPopup();
									}
									// Why the name was refused
									if (!presetNameError.empty()) {
										UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, presetNameError.data());
									}
									ImGui::EndPopup();
								}
							}

							ImGui::PopStyleVar();
						}
					}

					// Condition presets
					if (_editMode > EditMode::kNone || a_registeredMod->HasConditionPresets()) {
						const std::string conditionPresetsLabel = "Condition presets##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + "conditionPresets";
						ImGuiTreeNodeFlags flags = a_registeredMod->HasConditionPresets() ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
						if (ImGui::CollapsingHeader(conditionPresetsLabel.data(), flags)) {
							ImGui::AlignTextToFramePadding();
							ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

							if (a_registeredMod->HasConditionPresets()) {
								bool bShouldSort = false;
								a_registeredMod->ForEachConditionPreset([&](Conditions::ConditionPreset* a_preset) {
									if (DrawConditionPreset(a_registeredMod, a_preset, bShouldSort)) {
										a_registeredMod->SetDirty(true);
									}

									return RE::BSVisit::BSVisitControl::kContinue;
								});

								if (bShouldSort) {
									a_registeredMod->SortConditionPresets();
								}
							}

							if (_editMode > EditMode::kNone) {
								// Add condition preset button
								constexpr auto popupName = "Adding new condition preset"sv;
								if (ImGui::Button("Add new condition preset")) {
									const auto popupPos = ImGui::GetCursorScreenPos();
									ImGui::SetNextWindowPos(popupPos);
									ImGui::OpenPopup(popupName.data());
								}

								if (ImGui::BeginPopupModal(popupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
									std::string conditionPresetName;
									static std::string conditionPresetNameError;
									if (ImGui::IsWindowAppearing()) {
										conditionPresetNameError.clear();
									}
									if (ImGui::InputTextWithHint("##ConditionPresetName", "Type a unique name...", &conditionPresetName, ImGuiInputTextFlags_EnterReturnsTrue)) {
										conditionPresetNameError = conditionPresetName.size() <= 2 ? "The name needs at least 3 characters."s : a_registeredMod->HasConditionPreset(conditionPresetName) ? "A condition preset of this name already exists."s : std::string{};
										if (conditionPresetNameError.empty()) {
											auto newConditionPreset = std::make_unique<Conditions::ConditionPreset>(conditionPresetName, ""sv);
											a_registeredMod->AddConditionPreset(newConditionPreset);
											a_registeredMod->SetDirty(true);
											ImGui::CloseCurrentPopup();
										}
									}
									ImGui::SetItemDefaultFocus();
									ImGui::SameLine();
									if (ImGui::Button("Cancel")) {
										ImGui::CloseCurrentPopup();
									}
									// Why the name was refused
									if (!conditionPresetNameError.empty()) {
										UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, conditionPresetNameError.data());
									}
									ImGui::EndPopup();
								}
							}

							ImGui::PopStyleVar();
						}
					}
					ImGui::Unindent();
				}
				}
			}

			const auto drawSubModsOfKind = [&](SubModKind a_kind) {
				a_registeredMod->ForEachSubMod([&](SubMod* a_subMod) {
					if (a_subMod->GetKind() != a_kind) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					// Filter
					const auto search = a_filterResults.find(a_subMod->GetName().data());
					if (search != a_filterResults.end()) {
						const SubModNameFilterResult& filterResult = search->second;
						if (filterResult.bDisplay) {
							DrawSubMod(a_registeredMod, a_subMod, filterResult.bDuplicateName);
						}
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			};

			// Each list ends with its own add button in author mode, which names the new one in a popup
			ImGui::TextUnformatted("Modifiers:");
			drawSubModsOfKind(SubModKind::kModifier);
			if (_editMode == EditMode::kAuthor && ImGui::Button("Add modifier")) {
				_newEntryState = { NewEntryState::Kind::kModifier, a_registeredMod, {}, true };
			}

			ImGui::TextUnformatted("Clips:");
			drawSubModsOfKind(SubModKind::kClip);
			if (_editMode == EditMode::kAuthor && ImGui::Button("Add clip")) {
				_newEntryState = { NewEntryState::Kind::kClip, a_registeredMod, {}, true };
			}

			// Save mod config
			if (_editMode > EditMode::kNone) {
				const bool bIsDirty = a_registeredMod->IsDirty();
				if (!bIsDirty) {
					ImGui::BeginDisabled();
				}
				ImGui::BeginDisabled(a_registeredMod->HasInvalidConditions(true) || a_registeredMod->HasInvalidFunctions());
				if (ImGui::Button(_editMode == EditMode::kAuthor ? "Save mod config (Author)" : "Save mod config (User)")) {
					a_registeredMod->SaveConfig(_editMode);
				}
				ImGui::EndDisabled();
				if (!bIsDirty) {
					ImGui::EndDisabled();
				}

				// Reload mod config
				ImGui::SameLine();
				UICommon::ButtonWithConfirmationModal("Reload mod config", "Are you sure you want to reload the config?\nThis operation cannot be undone!\n\n"sv, [&]() {
					ModRegistry::GetSingleton().QueueJob<Jobs::ReloadRegisteredModConfigJob>(a_registeredMod);
				});

				// delete user config
				const bool bUserConfigExists = Utils::DoesUserConfigExist(a_registeredMod->GetPath());
				if (!bUserConfigExists) {
					ImGui::BeginDisabled();
				}
				ImGui::SameLine();
				UICommon::ButtonWithConfirmationModal("Delete mod user config", "Are you sure you want to delete the user config?\nThis operation cannot be undone!\n\n"sv, [&]() {
					Utils::DeleteUserConfig(a_registeredMod->GetPath());
					ModRegistry::GetSingleton().QueueJob<Jobs::ReloadRegisteredModConfigJob>(a_registeredMod);
				});
				if (!bUserConfigExists) {
					ImGui::EndDisabled();
				}
			}

			ImGui::TreePop();
		}
	}

	void UIMain::DrawSubMod(RegisteredMod* a_registeredMod, SubMod* a_subMod, bool a_bAddPathToName /*= false*/)
	{
		const bool bIsClip = a_subMod->GetKind() == SubModKind::kClip;
		const auto kindWord = bIsClip ? "clip"sv : "modifier"sv;
		const auto kindWordCap = bIsClip ? "Clip"sv : "Modifier"sv;

		bool bStyleVarPushed = false;
		if (a_subMod->IsDisabled()) {
			auto& style = ImGui::GetStyle();
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.DisabledAlpha);
			bStyleVarPushed = true;
		}

		if (_openSubMod == a_subMod) {
			ImGui::SetNextItemOpen(true);
			_openSubMod = nullptr;
		}
		bool bNodeOpen = ImGui::TreeNodeEx(a_subMod, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

		// Disable checkbox
		ImGui::SameLine();
		if (_editMode > EditMode::kNone) {
			std::string idString = std::format("{}##bDisabled", reinterpret_cast<uintptr_t>(a_subMod));
			ImGui::PushID(idString.data());
			bool bEnabled = !a_subMod->IsDisabled();
			if (ImGui::Checkbox("##disableSubMod", &bEnabled)) {
				a_subMod->SetDisabled(!bEnabled);
				ModRegistry::GetSingleton().QueueJob<Jobs::UpdateSubModJob>(a_subMod, true);
				a_subMod->SetDirty(true);
			}
			UICommon::AddTooltip(std::format("If unchecked, the {} will be disabled and it will not be considered.", kindWord).data());
			ImGui::PopID();
		}

		if (a_subMod->IsDirty()) {
			ImGui::SameLine();
			UICommon::TextUnformattedColored(UICommon::DIRTY_COLOR, "*");
		}

		switch (a_subMod->GetConfigSource()) {
		case Parsing::ConfigSource::kUser:
			ImGui::SameLine();
			UICommon::TextUnformattedColored(UICommon::USER_MOD_COLOR, "(User)");
			break;
		}

		// node name
		ImGui::SameLine();
		if (a_subMod->HasInvalidConditions() || a_subMod->HasInvalidFunctions()) {
			UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_subMod->GetName().data());
		} else {
			ImGui::TextUnformatted(a_subMod->GetName().data());
		}
		ImGui::SameLine();

		if (a_bAddPathToName) {
			UICommon::TextUnformattedDisabled(a_subMod->GetPath().data());
			ImGui::SameLine();
		}

		float cursorPosX = ImGui::GetCursorPosX();

		std::string priorityText = "Priority: " + std::to_string(a_subMod->GetPriority());
		UICommon::SecondColumn(_firstColumnWidthPercent);
		if (ImGui::GetCursorPosX() < cursorPosX) {
			// make sure we don't draw priority text over the name
			ImGui::SetCursorPosX(cursorPosX);
		}
		ImGui::TextUnformatted(priorityText.data());

		if (bStyleVarPushed) {
			ImGui::PopStyleVar();
		}

		// A modifier previewed alone on the reference actor
		if (a_subMod->GetKind() == SubModKind::kModifier) {
			DrawPreviewButton(a_subMod, [a_subMod](RE::TESObjectREFR* a_refr) { Preview::StartModifier(a_refr, a_subMod); });
		}

		if (bNodeOpen) {
			// Clip or modifier name
			{
				if (_editMode == EditMode::kAuthor) {
					std::string subModNameId = std::format("{} name##", kindWordCap) + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "name";
					ImGui::SetNextItemWidth(-150.f);
					std::string tempName(a_subMod->GetName());
					if (ImGui::InputTextWithHint(subModNameId.data(), std::format("{} name", kindWordCap).data(), &tempName)) {
						a_subMod->SetName(tempName);
						a_subMod->SetDirty(true);
					}
				}
			}

			// Clip or modifier description
			{
				std::string subModDescriptionId = std::format("{} description##", kindWordCap) + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "description";
				if (_editMode == EditMode::kAuthor) {
					ImGui::SetNextItemWidth(-150.f);
					std::string tempDescription(a_subMod->GetDescription());
					if (ImGui::InputTextMultiline(subModDescriptionId.data(), &tempDescription, ImVec2(0, ImGui::GetTextLineHeight() * 5))) {
						a_subMod->SetDescription(tempDescription);
						a_subMod->SetDirty(true);
					}
				} else if (!a_subMod->GetDescription().empty()) {
					if (ImGui::BeginTable(subModDescriptionId.data(), 1, ImGuiTableFlags_BordersOuter)) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::AlignTextToFramePadding();
						UICommon::TextDescriptionRightAligned("Description");
						UICommon::TextUnformattedWrapped(a_subMod->GetDescription().data());
						ImGui::EndTable();
					}
				}
			}

			// Clip or modifier priority
			{
				std::string priorityLabel = "Priority##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "priority";
				if (_editMode != EditMode::kNone) {
					int32_t tempPriority = a_subMod->GetPriority();
					if (ImGui::InputInt(priorityLabel.data(), &tempPriority, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue)) {
						a_subMod->SetPriority(tempPriority);
						ModRegistry::GetSingleton().QueueJob<Jobs::UpdateSubModJob>(a_subMod, true);
						a_subMod->SetDirty(true);
					}
				} else {
					if (ImGui::BeginTable(priorityLabel.data(), 1, ImGuiTableFlags_BordersOuter)) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::AlignTextToFramePadding();
						UICommon::TextDescriptionRightAligned("Priority");
						ImGui::TextUnformatted(std::to_string(a_subMod->GetPriority()).data());
						ImGui::EndTable();
					}
				}
			}

			// Interruptible is a clip's: a modifier holds values rather than playing.
			if (bIsClip) {
				std::string interruptibleLabel = "Interruptible##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "interruptible";
				std::string interruptibleTooltip = "If checked, the conditions will be checked every frame and the clip will be switched to another one if needed. Mostly useful for looping animations.";

				if (_editMode != EditMode::kNone) {
					bool tempInterruptible = a_subMod->IsInterruptible();
					if (ImGui::Checkbox(interruptibleLabel.data(), &tempInterruptible)) {
						a_subMod->SetInterruptible(tempInterruptible);
						ModRegistry::GetSingleton().QueueJob<Jobs::UpdateSubModJob>(a_subMod, false);
						a_subMod->SetDirty(true);
					}
					ImGui::SameLine();
					UICommon::HelpMarker(interruptibleTooltip.data());
				} else if (a_subMod->IsInterruptible()) {
					ImGui::BeginDisabled();
					bool tempInterruptible = a_subMod->IsInterruptible();
					ImGui::Checkbox(interruptibleLabel.data(), &tempInterruptible);
					ImGui::EndDisabled();
					ImGui::SameLine();
					UICommon::HelpMarker(interruptibleTooltip.data());
				}

				// Let go mid-run the moment the conditions stop holding, through each channel's exit transition
				auto* clip = static_cast<Clip*>(a_subMod);
				const std::string stopLabel = "Interrupt when conditions fail##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "stopWhenConditionsFail";
				constexpr auto stopTooltip = "If checked, the clip is let go the moment its conditions stop holding, mid-run, through each channel's exit transition. Otherwise it runs to its end."sv;
				if (_editMode != EditMode::kNone) {
					bool tempStop = clip->StopsWhenConditionsFail();
					if (ImGui::Checkbox(stopLabel.data(), &tempStop)) {
						clip->SetStopWhenConditionsFail(tempStop);
						a_subMod->SetDirty(true);
					}
					ImGui::SameLine();
					UICommon::HelpMarker(stopTooltip.data());
				} else if (clip->StopsWhenConditionsFail()) {
					ImGui::BeginDisabled();
					bool tempStop = true;
					ImGui::Checkbox(stopLabel.data(), &tempStop);
					ImGui::EndDisabled();
					ImGui::SameLine();
					UICommon::HelpMarker(stopTooltip.data());
				}
			}

			if (bIsClip && a_subMod->IsInterruptible()) {
				// Interrupted, the channels leave through their exit transitions; with a time of the clip's own
				auto* clip = static_cast<Clip*>(a_subMod);
				const std::string applyExitLabel = "Apply exit transition on interrupt##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "applyExitOnInterrupt";
				constexpr auto applyExitTooltip = "If checked, an interrupted clip lets its channels go through their exit transitions, as if it had ended, instead of handing them over at once."sv;
				if (_editMode != EditMode::kNone) {
					bool tempApply = clip->AppliesExitOnInterrupt();
					if (ImGui::Checkbox(applyExitLabel.data(), &tempApply)) {
						clip->SetApplyExitOnInterrupt(tempApply);
						a_subMod->SetDirty(true);
					}
					ImGui::SameLine();
					UICommon::HelpMarker(applyExitTooltip.data());
				} else if (clip->AppliesExitOnInterrupt()) {
					ImGui::BeginDisabled();
					bool tempApply = true;
					ImGui::Checkbox(applyExitLabel.data(), &tempApply);
					ImGui::EndDisabled();
					ImGui::SameLine();
					UICommon::HelpMarker(applyExitTooltip.data());
				}
				if (clip->AppliesExitOnInterrupt()) {
					const std::string hasTimeLabel = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "hasCustomExitTime";
					const std::string timeLabel = "Custom exit time on interrupt##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "customExitTime";
					constexpr auto timeTooltip = "Sets one exit time for every channel when the clip is interrupted, in place of each channel's own time to rest."sv;
					if (_editMode != EditMode::kNone) {
						bool tempCustom = clip->HasCustomExitTime();
						if (ImGui::Checkbox(hasTimeLabel.data(), &tempCustom)) {
							clip->SetCustomExitTime(tempCustom);
							a_subMod->SetDirty(true);
						}
						ImGui::SameLine();
						ImGui::BeginDisabled(!clip->HasCustomExitTime());
						float tempTime = clip->GetCustomExitTime();
						ImGui::SetNextItemWidth(200.f);
						if (ImGui::SliderFloat(timeLabel.data(), &tempTime, 0.f, 1.f, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
							clip->SetCustomExitTime(tempTime);
							a_subMod->SetDirty(true);
						}
						ImGui::EndDisabled();
						ImGui::SameLine();
						UICommon::HelpMarker(timeTooltip.data());
					} else if (clip->HasCustomExitTime()) {
						ImGui::BeginDisabled();
						float tempTime = clip->GetCustomExitTime();
						ImGui::SetNextItemWidth(200.f);
						ImGui::SliderFloat(timeLabel.data(), &tempTime, 0.f, 1.f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
						ImGui::EndDisabled();
						ImGui::SameLine();
						UICommon::HelpMarker(timeTooltip.data());
					}
				}
			}

			if (!bIsClip) {
				const auto modifier = static_cast<AppearanceModifier*>(a_subMod);
				const auto idSuffix = std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod));

				constexpr float controlWidth = 200.f;

				const auto drawTransition = [&](std::string_view a_name, bool a_bEnabled, const std::function<void(bool)>& a_setEnabled,
											   float a_time, const std::function<void(float)>& a_setTime,
											   bool a_bCustomTime, const std::function<void(bool)>& a_setCustomTime) {
					const auto key = std::string(a_name) + idSuffix;
					const bool bEditable = _editMode != EditMode::kNone;

					// Inspecting, a transition that is off is nothing to show; on, it shows without its checkboxes
					if (!bEditable && !a_bEnabled) {
						return;
					}

					// Only the controls are disabled, never the help markers, so they still answer a hover when inspecting
					const float rowStart = ImGui::GetCursorPosX();

					// The checkbox, label hidden -- the slider beside it names the row.
					if (bEditable) {
						bool tempEnabled = a_bEnabled;
						if (ImGui::Checkbox(("##" + key + "enabled").data(), &tempEnabled)) {
							a_setEnabled(tempEnabled);
							ModRegistry::GetSingleton().QueueJob<Jobs::UpdateSubModJob>(a_subMod, false);
							a_subMod->SetDirty(true);
						}
						ImGui::SameLine();
					}
					const float indent = ImGui::GetCursorPosX();

					ImGui::TextUnformatted(a_name.data());
					ImGui::SameLine();
					UICommon::HelpMarker(std::format("How this modifier {} its values: eased over the given time. A clip to play as it {} is an {} trigger on the clip.",
						a_name == "Start Transition"sv ? "arrives at" : "leaves", a_name == "Start Transition"sv ? "activates" : "deactivates",
						a_name == "Start Transition"sv ? "OnModifierActivated" : "OnModifierDeactivated").data());

					// Second line, indented under the name above; only for a transition that is on
					if (!a_bEnabled) {
						return;
					}
					// Its checkbox under the transition's own, not under the name; none when inspecting.
					if (bEditable) {
						ImGui::SetCursorPosX(rowStart);
						// Off, the default from the settings applies; on, the slider does.
						bool tempCustom = a_bCustomTime;
						if (ImGui::Checkbox(("##" + key + "customTime").data(), &tempCustom)) {
							a_setCustomTime(tempCustom);
							ModRegistry::GetSingleton().QueueJob<Jobs::UpdateSubModJob>(a_subMod, false);
							a_subMod->SetDirty(true);
						}
						ImGui::SameLine();
					} else {
						ImGui::SetCursorPosX(indent);
					}
					ImGui::BeginDisabled(!bEditable || !a_bCustomTime);
					float tempTime = a_bCustomTime ? a_time : Settings::fDefaultTransitionTime;
					ImGui::SetNextItemWidth(controlWidth);
					if (ImGui::SliderFloat(("Custom transition time##" + key + "time").data(), &tempTime, 0.f, 1.f, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
						a_setTime(tempTime);
						ModRegistry::GetSingleton().QueueJob<Jobs::UpdateSubModJob>(a_subMod, false);
						a_subMod->SetDirty(true);
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					UICommon::HelpMarker("How long the modifier takes to get there. Off uses the default transition time from the settings.");
				};

				drawTransition("Start Transition"sv,
					modifier->HasStartTransition(), [&](bool a_v) { modifier->SetStartTransition(a_v); },
					modifier->GetStartTransitionTime(), [&](float a_v) { modifier->SetStartTransitionTime(a_v); },
					modifier->HasCustomStartTransitionTime(), [&](bool a_v) { modifier->SetCustomStartTransitionTime(a_v); });

				drawTransition("End Transition"sv,
					modifier->HasEndTransition(), [&](bool a_v) { modifier->SetEndTransition(a_v); },
					modifier->GetEndTransitionTime(), [&](float a_v) { modifier->SetEndTransitionTime(a_v); },
					modifier->HasCustomEndTransitionTime(), [&](bool a_v) { modifier->SetCustomEndTransitionTime(a_v); });
			}

			if (bIsClip) {
				// Clip or modifier replace on echo
				{
					std::string replaceOnEchoLabel = "Replace on echo##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "replaceOnEcho";
					std::string replaceOnEchoTooltip = "If checked the conditions will be reevaluated on animation echo and the clip will be switched to another one if needed. Disabled by default because of cosmetic issues with several animations, should be only enabled on animations that actually need it.";

					if (_editMode != EditMode::kNone) {
						bool tempReplaceOnEcho = a_subMod->IsReevaluatingOnEcho();
						if (ImGui::Checkbox(replaceOnEchoLabel.data(), &tempReplaceOnEcho)) {
							a_subMod->SetReevaluatingOnEcho(tempReplaceOnEcho);
							ModRegistry::GetSingleton().QueueJob<Jobs::UpdateSubModJob>(a_subMod, false);
							a_subMod->SetDirty(true);
						}
						ImGui::SameLine();
						UICommon::HelpMarker(replaceOnEchoTooltip.data());
					} else if (a_subMod->IsReevaluatingOnEcho()) {
						ImGui::BeginDisabled();
						bool tempReplaceOnEcho = a_subMod->IsReevaluatingOnEcho();
						ImGui::Checkbox(replaceOnEchoLabel.data(), &tempReplaceOnEcho);
						ImGui::EndDisabled();
						ImGui::SameLine();
						UICommon::HelpMarker(replaceOnEchoTooltip.data());
					}
				}
			}

			// Clip triggers: what fires it; none, and its conditions alone do
			if (bIsClip) {
				auto* clip = static_cast<Clip*>(a_subMod);
				if (_editMode != EditMode::kNone || !clip->GetTriggerSet()->IsEmpty()) {
					std::string triggersTreeNodeLabel = "Triggers##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "triggersNode";
					const bool bOpen = ImGui::CollapsingHeader(triggersTreeNodeLabel.data(), ImGuiTreeNodeFlags_DefaultOpen);
					ImGui::SameLine();
					UICommon::HelpMarker("What fires the clip: any of these, while its conditions hold. Leave it empty and the clip fires from its conditions alone.");
					if (bOpen) {
						ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
						ImGui::Indent();
						const ImGuiStyle& style = ImGui::GetStyle();
						ImVec2 pos = ImGui::GetCursorScreenPos();
						pos.x += style.FramePadding.x;
						pos.y += style.FramePadding.y;
						ImGui::PushID(clip->GetTriggerSet());
						DrawTriggerSet(clip->GetTriggerSet(), a_subMod, _editMode, true, pos);
						ImGui::PopID();
						ImGui::Unindent();
						ImGui::Spacing();
						ImGui::PopStyleVar();
					}
				}
			}

			// Clip or modifier conditions
			std::string conditionsTreeNodeLabel = "Conditions##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "conditionsNode";
			if (ImGui::CollapsingHeader(conditionsTreeNodeLabel.data(), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

				ImGui::Indent();
				const ImGuiStyle& style = ImGui::GetStyle();
				ImVec2 pos = ImGui::GetCursorScreenPos();
				pos.x += style.FramePadding.x;
				pos.y += style.FramePadding.y;
				ImGui::PushID(a_subMod->GetConditionSet());
				DrawConditionSet(a_subMod->GetConditionSet(), a_subMod, _editMode, Conditions::ConditionType::kNormal, UIManager::GetSingleton().GetRefrToEvaluate(), true, pos);
				ImGui::PopID();
				ImGui::Unindent();

				ImGui::Spacing();
				ImGui::PopStyleVar();
			}

			// Modifier channels: what it drives, and how
			if (!bIsClip) {
				std::string channelsTreeNodeLabel = "Channels##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "channelsNode";
				if (ImGui::CollapsingHeader(channelsTreeNodeLabel.data(), ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

					ImGui::Indent();
					const ImGuiStyle& style = ImGui::GetStyle();
					ImVec2 pos = ImGui::GetCursorScreenPos();
					pos.x += style.FramePadding.x;
					pos.y += style.FramePadding.y;
					ImGui::PushID(a_subMod->GetChannelSet());
					DrawChannelSet(a_subMod->GetChannelSet(), a_subMod, _editMode, true, pos);
					ImGui::PopID();
					ImGui::Unindent();

					ImGui::Spacing();
					ImGui::PopStyleVar();
				}
			}

			// Clip claims and keyframes
			if (bIsClip) {
				auto* clip = static_cast<Clip*>(a_subMod);
				const std::string idSuffix = std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod));

				// The claimed channels: what the clip touches, a bare list; off, a claim is ignored in every frame
				if (_editMode != EditMode::kNone || !clip->GetChannelSet()->IsEmpty()) {
					const std::string claimedLabel = "Claimed Channels##" + idSuffix + "claimedNode";
					const bool bOpen = ImGui::CollapsingHeader(claimedLabel.data(), ImGuiTreeNodeFlags_DefaultOpen);
					ImGui::SameLine();
					UICommon::HelpMarker("Every channel the clip drives. Each keyframe has a row for each of these; a claim toggled off is ignored in every frame. Add or remove channels here, not in the frames.");
					if (bOpen) {
						ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
						ImGui::Indent();
						const ImGuiStyle& style = ImGui::GetStyle();
						ImVec2 pos = ImGui::GetCursorScreenPos();
						pos.x += style.FramePadding.x;
						pos.y += style.FramePadding.y;
						ImGui::PushID(clip->GetChannelSet());
						DrawChannelSet(clip->GetChannelSet(), a_subMod, _editMode, true, pos, ChannelDrawMode::kBare);
						ImGui::PopID();
						ImGui::Unindent();
						ImGui::Spacing();
						ImGui::PopStyleVar();
					}
				}

				// The keyframes: one variant's directly, several variants each with their own
				auto& tracks = clip->GetTracks();
				const bool bAnyKeyframe = std::ranges::any_of(tracks, [](const ClipVariant& a_variant) { return !a_variant.keyframes.empty(); });
				if (_editMode != EditMode::kNone || bAnyKeyframe) {
					if (tracks.size() == 1) {
						const std::string keyframesLabel = "Keyframes##" + idSuffix + "keyframesNode";
						const bool bOpen = ImGui::CollapsingHeader(keyframesLabel.data(), ImGuiTreeNodeFlags_DefaultOpen);
						ImGui::SameLine();
						UICommon::HelpMarker("The clip's moments in order. A frame holds a value for every claimed channel and the way into it; the time to the next frame that carries the channel is the transition's length.");
						if (bOpen) {
							ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
							ImGui::Indent();
							const ImGuiStyle& style = ImGui::GetStyle();
							ImVec2 pos = ImGui::GetCursorScreenPos();
							pos.x += style.FramePadding.x;
							pos.y += style.FramePadding.y;
							ImGui::PushID(&tracks.front());
							DrawKeyframes(clip, tracks.front(), pos);
							if (_editMode != EditMode::kNone) {
								ImGui::SameLine(0.f, 20.f);
								// A variant is known by its number, added at once
								if (ImGui::Button("Add variant")) {
									clip->AddVariant(std::format("Variant {}", clip->GetTracks().size() + 1));
									a_subMod->SetDirty(true);
								}
							}
							// The frames on the timeline, and the clip played on the reference actor, under its keyframes
							DrawTimelineButton(clip, 0, false);
							if (UIManager::GetSingleton().GetRefrToEvaluate()) {
								ImGui::SameLine();
							}
							DrawPreviewButton(&tracks.front(), [clip](RE::TESObjectREFR* a_refr) { Preview::StartClip(a_refr, clip, 0); }, false);
							ImGui::PopID();
							ImGui::Unindent();
							ImGui::Spacing();
							ImGui::PopStyleVar();
						}
					} else {
						const std::string variantsLabel = "Variants##" + idSuffix + "variantsNode";
						const bool bOpen = ImGui::CollapsingHeader(variantsLabel.data(), ImGuiTreeNodeFlags_DefaultOpen);
						ImGui::SameLine();
						UICommon::HelpMarker("One of these plays each time the clip fires: drawn by weight, or in order. Each has its own keyframes over the same claimed channels.");
						if (bOpen) {
							ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
							ImGui::Indent();
							DrawClipVariants(a_registeredMod, clip);
							ImGui::Unindent();
							ImGui::Spacing();
							ImGui::PopStyleVar();
						}
					}
				}
			}

			// Clip or modifier functions
			auto drawFunctionSet = [&](Functions::FunctionSetType a_functionSetType) {
				std::string functionsTreeNodeLabel;
				std::string helpMarkerText;
				switch (a_functionSetType) {
				case Functions::FunctionSetType::kOnActivate:
					functionsTreeNodeLabel = "On Activate##";
					helpMarkerText = std::format("Functions from this set will run when this {} starts.", kindWord);
					break;
				case Functions::FunctionSetType::kOnDeactivate:
					functionsTreeNodeLabel = "On Deactivate##";
					helpMarkerText = std::format("Functions from this set will run when this {} ends.", kindWord);
					break;
				case Functions::FunctionSetType::kOnTrigger:
					functionsTreeNodeLabel = "On Trigger##";
					helpMarkerText = std::format("Functions from this set will run when a specified animation event is called while this {} is active.", kindWord);
					break;
				}

				ImGuiTreeNodeFlags_ treeNodeFlags = a_subMod->GetFunctionSet(a_functionSetType) ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;

				functionsTreeNodeLabel += std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "functionsNode";
				bool bIsExpanded = ImGui::CollapsingHeader(functionsTreeNodeLabel.data(), treeNodeFlags);
				ImGui::SameLine();
				UICommon::HelpMarker(helpMarkerText.data());
				if (bIsExpanded) {
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

					ImGui::Indent();
					const ImGuiStyle& style = ImGui::GetStyle();
					ImVec2 pos = ImGui::GetCursorScreenPos();
					pos.x += style.FramePadding.x;
					pos.y += style.FramePadding.y;
					ImGui::PushID(a_subMod->GetFunctionSet(a_functionSetType));
					DrawFunctionSet(a_subMod->GetFunctionSet(a_functionSetType), a_subMod, _editMode, a_functionSetType, UIManager::GetSingleton().GetRefrToEvaluate(), true, pos);
					ImGui::PopID();
					ImGui::Unindent();

					ImGui::Spacing();
					ImGui::PopStyleVar();
				}
			};

			ImGuiTreeNodeFlags_ treeNodeFlags = a_subMod->HasAnyFunctionSet() ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;

			if (_editMode != EditMode::kNone || a_subMod->HasAnyFunctionSet()) {
				std::string allFunctionsTreeNodeLabel = "Functions##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + std::to_string(reinterpret_cast<std::uintptr_t>(a_subMod)) + "functionsNode";
				if (ImGui::CollapsingHeader(allFunctionsTreeNodeLabel.data(), treeNodeFlags)) {
					ImGui::Indent();
					if (_editMode != EditMode::kNone || a_subMod->HasFunctionSet(Functions::FunctionSetType::kOnActivate)) {
						drawFunctionSet(Functions::FunctionSetType::kOnActivate);
					}
					if (_editMode != EditMode::kNone || a_subMod->HasFunctionSet(Functions::FunctionSetType::kOnDeactivate)) {
						drawFunctionSet(Functions::FunctionSetType::kOnDeactivate);
					}
					if (!bIsClip && (_editMode != EditMode::kNone || a_subMod->HasFunctionSet(Functions::FunctionSetType::kOnTrigger))) {
						drawFunctionSet(Functions::FunctionSetType::kOnTrigger);
					}
					ImGui::Unindent();
				}
			}

			if (_editMode != EditMode::kNone) {
				// Save clip or modifier config
				bool bIsDirty = a_subMod->IsDirty();
				if (!bIsDirty) {
					ImGui::BeginDisabled();
				}
				ImGui::BeginDisabled(a_subMod->HasInvalidConditions() || a_subMod->HasInvalidFunctions());
				if (ImGui::Button(std::format("Save {} config ({})", kindWord, _editMode == EditMode::kAuthor ? "Author" : "User").data())) {
					a_subMod->SaveConfig(_editMode);
				}
				ImGui::EndDisabled();
				if (!bIsDirty) {
					ImGui::EndDisabled();
				}

				// Reload clip or modifier config
				ImGui::SameLine();
				UICommon::ButtonWithConfirmationModal(std::format("Reload {} config", kindWord), "Are you sure you want to reload the config?\nThis operation cannot be undone!\n\n"sv, [&]() {
					ModRegistry::GetSingleton().QueueJob<Jobs::ReloadSubModConfigJob>(a_subMod);
				});

				// delete user config
				const bool bUserConfigExists = Utils::DoesUserConfigExist(a_subMod->GetPath());
				if (!bUserConfigExists) {
					ImGui::BeginDisabled();
				}
				ImGui::SameLine();
				UICommon::ButtonWithConfirmationModal(std::format("Delete {} user config", kindWord), "Are you sure you want to delete the user config?\nThis operation cannot be undone!\n\n"sv, [&]() {
					Utils::DeleteUserConfig(a_subMod->GetPath());
					ModRegistry::GetSingleton().QueueJob<Jobs::ReloadSubModConfigJob>(a_subMod);
				});
				if (!bUserConfigExists) {
					ImGui::EndDisabled();
				}

				// Delete the clip or modifier, the mod to be saved without it
				if (_editMode == EditMode::kAuthor) {
					ImGui::SameLine();
					UICommon::ButtonWithConfirmationModal(std::format("Delete {}", kindWord), std::format("Are you sure you want to delete this {}?\nThis operation cannot be undone!\n\n", kindWord), [&]() {
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveSubModJob>(a_registeredMod, a_subMod);
					});
				}
			}

			ImGui::Spacing();

			ImGui::TreePop();
		}
	}

	namespace
	{
		struct CategorisedChannel
		{
			std::unique_ptr<Channels::ChannelBase>* channel;
			std::string category;
			int rank;
		};

		// A set's channels as the picker lists them, under their categories; the set's own order within each
		std::vector<CategorisedChannel> ByCategory(Channels::ChannelSet* a_channelSet, UIChannelComboFilter& a_filter)
		{
			const auto& entries = Apply::Entries::GetSingleton();
			std::vector<CategorisedChannel> out;
			a_channelSet->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_channel) {
				std::string category = a_channel->IsGroup() ? UIChannelComboFilter::kGroupCategory : UIChannelComboFilter::CategoryName(entries.Get(static_cast<Channels::Channel*>(a_channel.get())->GetId()));
				const int rank = a_filter.CategoryRank(category);
				out.push_back({ &a_channel, std::move(category), rank });
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			std::ranges::stable_sort(out, {}, &CategorisedChannel::rank);
			return out;
		}

		// A category's heading, a line above it after the first, as legacy separates its lists
		void DrawCategoryHeading(const std::vector<CategorisedChannel>& a_list, std::size_t a_at)
		{
			if (a_at > 0 && a_list[a_at].category == a_list[a_at - 1].category) {
				return;
			}
			if (a_at > 0) {
				ImGui::Separator();
			}
			ImGui::TextDisabled("%s", a_list[a_at].category.data());
		}
	}

	bool UIMain::DrawChannelSet(Channels::ChannelSet* a_channelSet, SubMod* a_parentSubMod, EditMode a_editMode, bool a_bDrawLines, const ImVec2& a_drawStartPos, ChannelDrawMode a_mode)
	{
		if (!a_channelSet) {
			return false;
		}

		bool bSetDirty = false;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();

		ImVec2 vertLineStart = a_drawStartPos;
		vertLineStart.y += style.FramePadding.x;
		vertLineStart.x -= style.IndentSpacing * 0.6f;
		ImVec2 vertLineEnd = vertLineStart;

		const float tooltipWidth = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2;

		if (!a_channelSet->IsEmpty()) {
			const auto list = ByCategory(a_channelSet, _channelComboFilter);
			for (std::size_t i = 0; i < list.size(); ++i) {
				DrawCategoryHeading(list, i);
				const ImRect nodeRect = DrawChannel(*list[i].channel, a_channelSet, a_parentSubMod, a_editMode, bSetDirty, a_mode);
				if (a_bDrawLines) {
					const float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
					constexpr float horLineLength = 10.f;
					drawList->AddLine(ImVec2(vertLineStart.x, midPoint), ImVec2(vertLineStart.x + horLineLength, midPoint), ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
					vertLineEnd.y = midPoint;
				}
			}
		} else {
			DrawBlankChannel(a_channelSet, a_editMode);
		}

		if (a_bDrawLines) {
			drawList->AddLine(vertLineStart, vertLineEnd, ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
		}

		if (a_editMode > EditMode::kNone && a_mode != ChannelDrawMode::kLocked) {
			// Add channel button: the last one picked, else a group, which drives nothing until filled
			if (a_mode == ChannelDrawMode::kBare) {
				// A clip's claims: the channel picker's list open at once, what is picked claimed; a claim has no details to
				// change it in afterwards
				ImGui::PushID(a_channelSet);
				const bool bOpenNow = ImGui::Button("Add channels");
				if (bOpenNow) {
					_claimPickerSet = a_channelSet;
				}
				if (_claimPickerSet == a_channelSet) {
					constexpr auto pickerId = "##Claim channel";
					ImGui::SameLine();
					if (bOpenNow) {
						ImGui::OpenPopupEx(ImGui::GetID(pickerId));
					}
					const auto& channelInfos = _channelComboFilter.GetChannelInfos(UIManager::GetSingleton().GetRefrToEvaluate());
					int selectedItem = -1;
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					if (_channelComboFilter.ComboFilter(pickerId, selectedItem, channelInfos, nullptr, ImGuiComboFlags_HeightLarge, &UIMain::DrawInfoTooltip)) {
						if (selectedItem >= 0 && selectedItem < static_cast<int>(channelInfos.size())) {
							// One this clip already claims is refused, as the channel picker refuses it
							auto* topSet = a_parentSubMod ? a_parentSubMod->GetChannelSet() : a_channelSet;
							const auto& pickedName = channelInfos[selectedItem].name;
							auto newChannel = Channels::CreateChannel(std::string_view(pickedName));
							if (newChannel && (newChannel->IsGroup() || !topSet->Contains(static_cast<Channels::Channel*>(newChannel.get())->GetId()))) {
								_lastAddNewChannelName = pickedName;
								a_channelSet->Add(newChannel, true);
								bSetDirty = true;
							}
						}
					}
					if (!bOpenNow && !ImGui::IsPopupOpen(ImGui::GetID(pickerId), ImGuiPopupFlags_None)) {
						_claimPickerSet = nullptr;
					}
				}
				ImGui::PopID();
			} else if (ImGui::Button("Add new channel")) {
				auto* topSet = a_parentSubMod ? a_parentSubMod->GetChannelSet() : a_channelSet;
				auto newChannel = Channels::CreateChannel(std::string_view(_lastAddNewChannelName.empty() ? Channels::kGroupName : _lastAddNewChannelName));
				if (newChannel && !newChannel->IsGroup() && topSet->Contains(static_cast<Channels::Channel*>(newChannel.get())->GetId())) {
					newChannel = Channels::CreateChannel(Channels::kGroupName);
				}
				a_channelSet->Add(newChannel, true);
				bSetDirty = true;
			}

			// Channel set functions button
			ImGui::SameLine(0.f, 20.f);
			const auto popupId = std::string("Channel set functions##") + std::to_string(reinterpret_cast<uintptr_t>(a_channelSet));
			if (UICommon::PopupToggleButton("Channel set...", popupId.data())) {
				ImGui::OpenPopup(popupId.data());
			}

			if (ImGui::BeginPopupContextItem(popupId.data())) {
				const auto xButtonSize = ImGui::CalcTextSize("Paste channel set").x + style.FramePadding.x * 2 + style.ItemSpacing.x;

				// Copy channels button
				ImGui::BeginDisabled(a_channelSet->IsEmpty() || !a_channelSet->IsValid());
				if (ImGui::Button("Copy channel set", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					_channelSetCopy = Channels::DuplicateChannelSet(a_channelSet);
				}
				ImGui::EndDisabled();

				// Paste channels button: appended, less any channel this modifier already drives
				ImGui::BeginDisabled(!_channelSetCopy);
				if (ImGui::Button("Paste channel set", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					auto* topSet = a_parentSubMod ? a_parentSubMod->GetChannelSet() : a_channelSet;
					const auto duplicatedSet = Channels::DuplicateChannelSet(_channelSetCopy.get());
					duplicatedSet->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_channel) {
						if (a_channel->IsGroup() || !topSet->Contains(static_cast<Channels::Channel*>(a_channel.get())->GetId())) {
							a_channelSet->Add(a_channel, true);
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
					bSetDirty = true;
				}
				ImGui::EndDisabled();
				// Paste tooltip
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
					ImGui::SetNextWindowSize(ImVec2(tooltipWidth, 0));
					ImGui::BeginTooltip();
					DrawChannelSet(_channelSetCopy.get(), nullptr, EditMode::kNone, false, a_drawStartPos);
					ImGui::EndTooltip();
				}

				// Clear channels button
				ImGui::BeginDisabled(a_channelSet->IsEmpty());
				UICommon::ButtonWithConfirmationModal(
					"Clear channel set"sv, "Are you sure you want to clear the channel set?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ImGui::ClosePopupsExceptModals();
						ModRegistry::GetSingleton().QueueJob<Jobs::ClearChannelSetJob>(a_channelSet);
						bSetDirty = true;
					},
					ImVec2(xButtonSize, 0));
				ImGui::EndDisabled();
				ImGui::EndPopup();
			}
		}

		return bSetDirty;
	}

	ImRect UIMain::DrawChannel(std::unique_ptr<Channels::ChannelBase>& a_channel, Channels::ChannelSet* a_channelSet, SubMod* a_parentSubMod, EditMode a_editMode, bool& a_bOutSetDirty, ChannelDrawMode a_mode, const KeyRowState* a_keyRow)
	{
		const bool bBare = a_mode == ChannelDrawMode::kBare;
		const bool bLocked = a_mode == ChannelDrawMode::kLocked;
		// A keyframe's row that is not the frame's to edit: its claim off, held by an earlier frame, or not set
		const bool bKeyQuiet = bLocked && a_keyRow && (a_keyRow->heldBy || a_channel->IsDisabled());
		// A key row whose claim is off behaves as any other, its border saying the clip will not drive it
		const bool bKeyIgnored = bLocked && a_keyRow && a_keyRow->bIgnored;
		ImRect channelRect;
		ImRect nodeRect;

		std::string channelTableId = std::format("{}channelTable", reinterpret_cast<uintptr_t>(a_channel.get()));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		const bool bIsGroup = a_channel->IsGroup();
		if (bIsGroup) {
			ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, UICommon::CONDITION_PRESET_BORDER_COLOR);
		} else if (bKeyIgnored) {
			ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, UICommon::IGNORED_CHANNEL_BORDER_COLOR);
		}

		if (ImGui::BeginTable(channelTableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			if (bIsGroup || bKeyIgnored) {
				ImGui::PopStyleColor();  // ImGuiCol_TableBorderStrong
			}

			const auto nodeName = a_channel->GetName();

			bool bStyleVarPushed = false;
			if (a_channel->IsDisabled() || bKeyQuiet) {
				auto& style = ImGui::GetStyle();
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
				bStyleVarPushed = true;
			}

			// Open node; a bare row never opens, a claim being a name and nothing more, nor a quiet key row
			bool bNodeOpen = false;
			if ((a_editMode > EditMode::kNone || a_channel->GetNumComponents() > 0 || bIsGroup) && (!bBare || bIsGroup) && !bKeyQuiet) {
				if (_reopenChannelSet == a_channelSet && a_channel.get() != _reopenChannelOld) {
					int index = 0;
					bool bFound = false;
					a_channelSet->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
						if (a_entry.get() == a_channel.get()) {
							bFound = true;
							return RE::BSVisit::BSVisitControl::kStop;
						}
						++index;
						return RE::BSVisit::BSVisitControl::kContinue;
					});
					if (bFound && index == _reopenChannelIndex) {
						ImGui::SetNextItemOpen(true);
						_reopenChannelSet = nullptr;
						_reopenChannelOld = nullptr;
					}
				}
				bNodeOpen = ImGui::TreeNodeEx(a_channel.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
			} else if (bKeyQuiet) {
				// A dot in place of the arrow, as legacy's unset rows had: orange for a value on the move through this
				// frame, blue for one held by an earlier frame, the text colour otherwise
				const ImVec4 dot = a_keyRow->heldBy ? ImVec4(0.45f, 0.7f, 1.f, 1.f) : a_keyRow->bMoving ? ImVec4(1.f, 0.75f, 0.4f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
				ImGui::PushStyleColor(ImGuiCol_Text, dot);
				bNodeOpen = ImGui::TreeNodeEx(a_channel.get(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
				ImGui::PopStyleColor();
				bNodeOpen = false;
			} else {
				bNodeOpen = UICommon::TreeNodeCollapsedLeaf(a_channel.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
			}

			// A copy through the file's form, a group with what it holds; and a row set to a copy's values, its own parts
			// (a frame's transitions, a modifier's blend) kept where the copy has none
			const auto duplicate = [](const Channels::ChannelBase* a_source) {
				rapidjson::Document doc(rapidjson::kObjectType);
				rapidjson::Value value(rapidjson::kObjectType);
				a_source->Serialize(&value, &doc.GetAllocator());
				return Channels::CreateChannelFromJson(value);
			};
			const auto pasteValues = [](Channels::ChannelBase* a_row, const Channels::ChannelBase* a_copy) {
				rapidjson::Document doc(rapidjson::kObjectType);
				rapidjson::Value value(rapidjson::kObjectType);
				a_copy->Serialize(&value, &doc.GetAllocator());
				value.RemoveMember("disabled");
				a_row->Parse(value);
				a_row->SetDisabled(false);
			};
			const auto copiedId = [&]() -> std::optional<Apply::ChannelId> {
				return _channelCopy && !_channelCopy->IsGroup() ? std::optional(static_cast<Channels::Channel*>(_channelCopy.get())->GetId()) : std::nullopt;
			};

			// Channel context menu; a keyframe's row is its claim's, not removed here, and a claim's delete is on its header
			if (a_editMode > EditMode::kNone && !bLocked && !bBare) {
				if (ImGui::BeginPopupContextItem()) {
					auto& style = ImGui::GetStyle();
					auto xButtonSize = ImGui::CalcTextSize("Paste channel below").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					if (ImGui::Button("Copy channel", ImVec2(xButtonSize, 0))) {
						_channelCopy = duplicate(a_channel.get());
						ImGui::CloseCurrentPopup();
					}
					// A channel this modifier already drives, copied from here or from a frame, sets that channel's values; any
					// other is pasted below this one
					auto* topSet = a_parentSubMod ? a_parentSubMod->GetChannelSet() : a_channelSet;
					const auto id = copiedId();
					Channels::ChannelBase* existing = nullptr;
					if (id) {
						topSet->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
							if (!a_entry->IsGroup() && static_cast<Channels::Channel*>(a_entry.get())->GetId() == *id) {
								existing = a_entry.get();
								return RE::BSVisit::BSVisitControl::kStop;
							}
							return RE::BSVisit::BSVisitControl::kContinue;
						});
					}
					ImGui::BeginDisabled(!_channelCopy);
					if (existing) {
						if (ImGui::Button("Paste channel values", ImVec2(xButtonSize, 0))) {
							pasteValues(existing, _channelCopy.get());
							a_channelSet->SetDirty(true);
							a_bOutSetDirty = true;
							ImGui::CloseCurrentPopup();
						}
						UICommon::AddTooltip("This modifier already drives that channel: its values are set to the copy's.");
					} else if (ImGui::Button("Paste channel below", ImVec2(xButtonSize, 0))) {
						auto pasted = duplicate(_channelCopy.get());
						ModRegistry::GetSingleton().QueueJob<Jobs::InsertChannelJob>(pasted, a_channelSet, a_channel);
						a_bOutSetDirty = true;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();
					ImGui::Spacing();
					UICommon::ButtonWithConfirmationModal(
						"Delete channel"sv, "Are you sure you want to remove the channel?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveChannelJob>(a_channel, a_channelSet);
							a_bOutSetDirty = true;
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}
			}

			// A frame's row: its values copied, or set to a copy of the same channel from a frame or a modifier, and set
			if (a_editMode > EditMode::kNone && bLocked && a_keyRow && !a_keyRow->heldBy && !a_channel->IsGroup()) {
				if (ImGui::BeginPopupContextItem()) {
					auto& style = ImGui::GetStyle();
					const auto xButtonSize = ImGui::CalcTextSize("Paste channel values").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					if (ImGui::Button("Copy channel values", ImVec2(xButtonSize, 0))) {
						_channelCopy = duplicate(a_channel.get());
						ImGui::CloseCurrentPopup();
					}
					const auto id = copiedId();
					const bool bSame = id && *id == static_cast<Channels::Channel*>(a_channel.get())->GetId();
					ImGui::BeginDisabled(!bSame);
					if (ImGui::Button("Paste channel values", ImVec2(xButtonSize, 0))) {
						pasteValues(a_channel.get(), _channelCopy.get());
						a_channelSet->SetDirty(true);
						a_bOutSetDirty = true;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();
					if (_channelCopy && !bSame) {
						UICommon::AddTooltip("The copy is of another channel.");
					}
					ImGui::EndPopup();
				}
			}

			// Channel description tooltip
			{
				std::string description(a_channel->GetDescription());
				if (!a_channel->IsGroup()) {
					if (const auto own = Apply::Entries::GetSingleton().Get(static_cast<Channels::Channel*>(a_channel.get())->GetId()).description; !own.empty()) {
						description = own;
					}
				}
				DrawInfoTooltip(Info(nodeName, description));
			}

			nodeRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// Disable checkbox; a keyframe's row has a set checkbox instead, checked is set, and none when held or ignored
			ImGui::SameLine();
			if (a_editMode > EditMode::kNone && bLocked && a_keyRow && !a_keyRow->heldBy) {
				if (bStyleVarPushed) {
					ImGui::PopStyleVar();
					bStyleVarPushed = false;
				}
				ImGui::PushID(std::format("{}##bSet", reinterpret_cast<uintptr_t>(a_channel.get())).data());
				bool bSet = !a_channel->IsDisabled();
				if (ImGui::Checkbox("##setRow", &bSet)) {
					if (bSet) {
						// Keyed here at exactly the value the run has: the last set row's, copied over this one, a number at
						// where its move stands
						if (a_keyRow->computedFrom) {
							rapidjson::Document doc(rapidjson::kObjectType);
							rapidjson::Value value(rapidjson::kObjectType);
							a_keyRow->computedFrom->Serialize(&value, &doc.GetAllocator());
							a_channel->Parse(value);
						}
						if (a_keyRow->computedNumber) {
							if (auto* number = dynamic_cast<Channels::NumberChannel*>(a_channel.get())) {
								number->value->value.SetStaticValue(*a_keyRow->computedNumber);
							}
						}
					}
					a_channel->SetDisabled(!bSet);
					a_channelSet->SetDirty(true);
					a_bOutSetDirty = true;
				}
				UICommon::AddTooltip("Checked, this frame sets the channel; unchecked, it leaves it as the run has it.");
				ImGui::PopID();
				if (a_channel->IsDisabled()) {
					auto& style = ImGui::GetStyle();
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
					bStyleVarPushed = true;
				}
			} else if (a_editMode > EditMode::kNone && !bLocked) {
				std::string idString = std::format("{}##bDisabled", reinterpret_cast<uintptr_t>(a_channel.get()));
				ImGui::PushID(idString.data());
				bool bEnabled = !a_channel->IsDisabled();
				if (ImGui::Checkbox("##toggleChannel", &bEnabled)) {
					a_channel->SetDisabled(!bEnabled);
					a_channelSet->SetDirty(true);
					a_bOutSetDirty = true;
				}
				UICommon::AddTooltip("Toggles the channel on/off");
				ImGui::PopID();
			}

			// Channel name
			ImGui::SameLine();
			if (!a_channel->IsValid()) {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, nodeName.data());
			} else if (bIsGroup) {
				UICommon::TextUnformattedColored(UICommon::CONDITION_PRESET_COLOR, nodeName.data());
			} else {
				ImGui::TextUnformatted(nodeName.data());
			}

			ImVec2 cursorPos = ImGui::GetCursorScreenPos();

			// Right column: the argument, or for a claim nothing, and for a key row what stands in the frame's way
			UICommon::SecondColumn(_firstColumnWidthPercent);
			if (bBare && !bIsGroup) {
				ImGui::TextUnformatted("");
			} else if (bLocked && a_keyRow && a_keyRow->heldBy) {
				ImGui::TextUnformatted(std::format("(held by the {:.2f} s frame)", a_keyRow->heldBy->time).data());
				UICommon::AddTooltip("An earlier frame sets this channel and holds it past this frame's time, so this frame cannot set it. Shorten that hold or move this frame.");
			} else if (bLocked && a_keyRow && a_channel->IsDisabled()) {
				ImGui::TextUnformatted(a_keyRow->computed.data());
				UICommon::AddTooltip("Not set on this frame: what the run has here, from the row that last set it or the channel's rest. Set keys it here at exactly this value.");
			} else if (bLocked && a_keyRow && !bIsGroup) {
				// A set key row: its value, then the way in and on its last key the way out, type(strength or space)
				const auto componentNamed = [&](std::string_view a_name) -> Channels::IChannelComponent* {
					for (uint32_t i = 0; i < a_channel->GetNumComponents(); ++i) {
						if (auto* component = a_channel->GetComponent(i); component->GetName() == a_name) {
							return component;
						}
					}
					return nullptr;
				};
				const auto describe = [&](std::string_view a_transition, std::string_view a_strength, std::string_view a_space) -> std::string {
					auto* transition = componentNamed(a_transition);
					if (!transition || !transition->IsShown()) {
						return {};
					}
					const auto type = transition->GetArgument();
					if (auto* strength = componentNamed(a_strength); strength && strength->IsShown()) {
						return std::format("{}({})", type, strength->GetArgument());
					}
					if (auto* space = componentNamed(a_space); space && space->IsShown()) {
						return std::format("{}({})", type, space->GetArgument());
					}
					return type;
				};
				// What the row is set to: the first shown component that says something, a static pick saying nothing
				std::string text;
				for (std::uint32_t i = 0; i < a_channel->GetNumComponents() && text.empty(); ++i) {
					if (auto* component = a_channel->GetComponent(i); component->IsShown()) {
						text = component->GetArgument();
					}
				}
				const auto in = describe("Transition"sv, "Strength"sv, "Colour space"sv);
				const auto out = describe("Exit transition"sv, "Exit strength"sv, "Exit colour space"sv);
				if (!in.empty()) {
					text += "   " + in;
					if (!out.empty()) {
						text += " / " + out;
					}
				}
				ImGui::TextUnformatted(text.data());
			} else {
				const auto argument = a_channel->GetArgument();
				ImGui::TextUnformatted(argument.data());
			}

			channelRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// The header's end: a claim's Delete
			if (a_editMode > EditMode::kNone && bBare) {
				if (bStyleVarPushed) {
					ImGui::PopStyleVar();
					bStyleVarPushed = false;
				}
				const char* label = "Delete";
				const auto& style = ImGui::GetStyle();
				ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(label).x - style.FramePadding.x * 2 - style.ItemSpacing.x);
				ImGui::PushID(a_channel.get());
				if (ImGui::SmallButton(label)) {
					ModRegistry::GetSingleton().QueueJob<Jobs::RemoveChannelJob>(a_channel, a_channelSet);
					a_bOutSetDirty = true;
				}
				ImGui::PopID();
			}

			// Node contents
			if (bNodeOpen) {
				ImGui::Spacing();

				if (a_editMode > EditMode::kNone && !bLocked) {
					// select channel: every entry the load order has; one already driven by this modifier is refused
					const float channelComboWidth = UICommon::FirstColumnWidth(_firstColumnWidthPercent);
					ImGui::SetNextItemWidth(channelComboWidth);

					const auto& channelInfos = _channelComboFilter.GetChannelInfos(UIManager::GetSingleton().GetRefrToEvaluate());

					int selectedItem = -1;
					const Info* currentChannelInfo = nullptr;
					auto it = std::ranges::find_if(channelInfos, [&](const Info& a_info) { return a_info.name == nodeName; });
					if (it != channelInfos.end()) {
						selectedItem = static_cast<int>(std::distance(channelInfos.begin(), it));
						currentChannelInfo = &*it;
					}

					if (_channelComboFilter.ComboFilter("##Channel type", selectedItem, channelInfos, currentChannelInfo, ImGuiComboFlags_HeightLarge, &UIMain::DrawInfoTooltip)) {
						if (selectedItem >= 0 && selectedItem < channelInfos.size()) {
							const auto& pickedName = channelInfos[selectedItem].name;
							auto* topSet = a_parentSubMod ? a_parentSubMod->GetChannelSet() : a_channelSet;
							const auto pickedId = Apply::Entries::GetSingleton().Find(pickedName);
							if (pickedId.IsValid() && topSet->Contains(pickedId, a_channel.get())) {
								// Already driven by this modifier: the pick is refused and the node stays as it was
							} else {
								_lastAddNewChannelName = pickedName;
								_reopenChannelSet = a_channelSet;
								_reopenChannelOld = a_channel.get();
								_reopenChannelIndex = -1;
								int index = 0;
								a_channelSet->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
									if (a_entry.get() == _reopenChannelOld) {
										_reopenChannelIndex = index;
										return RE::BSVisit::BSVisitControl::kStop;
									}
									++index;
									return RE::BSVisit::BSVisitControl::kContinue;
								});
								ModRegistry::GetSingleton().QueueJob<Jobs::ReplaceChannelJob>(a_channel, pickedName, a_channelSet);
								a_bOutSetDirty = true;
							}
						}
					}

					// remove channel button
					UICommon::SecondColumn(_firstColumnWidthPercent);
					UICommon::ButtonWithConfirmationModal("Delete channel"sv, "Are you sure you want to remove the channel?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveChannelJob>(a_channel, a_channelSet);
						a_bOutSetDirty = true;
					});
				}

				if (bIsGroup) {
					auto* group = static_cast<Channels::GroupChannel*>(a_channel.get());
					if (DrawChannelSet(group->channels.get(), a_parentSubMod, a_editMode, true, cursorPos, a_mode)) {
						a_channelSet->SetDirty(true);
						a_bOutSetDirty = true;
					}
				} else if (auto numComponents = a_channel->GetNumComponents(); numComponents > 0) {
					for (uint32_t i = 0; i < numComponents; i++) {
						auto component = a_channel->GetComponent(i);
						if (!component->IsShown()) {
							continue;
						}
						ImGui::Separator();
						// write component name aligned to the right
						const auto componentName = component->GetName();
						UICommon::TextDescriptionRightAligned(componentName.data());
						// show component description on mouseover
						const auto componentDescription = component->GetDescription();
						if (!componentDescription.empty()) {
							UICommon::AddTooltip(componentDescription.data());
						}
						// display component
						if (component->DisplayInUI(a_editMode != EditMode::kNone, _firstColumnWidthPercent)) {
							a_channelSet->SetDirty(true);
							a_bOutSetDirty = true;
						}
					}
				}

				ImGui::Spacing();

				ImGui::TreePop();
			}

			if (bStyleVarPushed) {
				ImGui::PopStyleVar();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		return channelRect;
	}

	ImRect UIMain::DrawBlankChannel(Channels::ChannelSet* a_channelSet, [[maybe_unused]] EditMode a_editMode)
	{
		ImRect channelRect;

		const std::string channelTableId = std::format("{}blankChannelTable", reinterpret_cast<uintptr_t>(a_channelSet));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		if (ImGui::BeginTable(channelTableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			const std::string nodeId = std::format("No channels##{}", reinterpret_cast<uintptr_t>(a_channelSet));
			UICommon::TreeNodeCollapsedLeaf(nodeId.data(), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);

			channelRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();

		return channelRect;
	}

	void UIMain::DrawBodyTypes()
	{
		if (ImGui::Button("Reload")) {
			BodyTypes::Load();
		}
		ImGui::SameLine();
		UICommon::TextUnformattedDisabled(BodyTypes::kPath.data());

		const auto joined = [](const std::vector<std::string>& a_items) {
			std::string out;
			for (const auto& item : a_items) {
				out += out.empty() ? item : ", " + item;
			}
			return out;
		};

		// The rows of one part, as the file has them
		const auto config = BodyTypes::GetConfig();
		const auto drawRows = [&](const char* a_title, const std::vector<BodyTypes::Row>& a_rows, const char* a_matchesHeader) {
			if (!ImGui::CollapsingHeader(std::format("{} ({})###{}", a_title, a_rows.size(), a_title).data(), ImGuiTreeNodeFlags_DefaultOpen)) {
				return;
			}
			if (ImGui::BeginTable(a_title, 3, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.f);
				ImGui::TableSetupColumn("UV", ImGuiTableColumnFlags_WidthFixed, 90.f);
				ImGui::TableSetupColumn(a_matchesHeader, ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				for (const auto& row : a_rows) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(row.name.data());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(row.uv.data());
					ImGui::TableSetColumnIndex(2);
					const auto matches = joined(row.matches);
					ImGui::TextUnformatted(matches.empty() ? "(nothing)" : matches.data());
					if (!matches.empty()) {
						UICommon::AddTooltip(matches.data());
					}
				}
				ImGui::EndTable();
			}
		};
		drawRows("Body types", config.bodies, "Built by any of these BodySlide projects");
		drawRows("Head types", config.heads, "Face model path contains any of");

		// Every scanned naked body and head, and what it came to
		const auto lessText = [](std::string_view a_lhs, std::string_view a_rhs) {
			return std::ranges::lexicographical_compare(a_lhs, a_rhs, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) < std::tolower(static_cast<unsigned char>(b)); });
		};
		auto resolutions = BodyTypes::GetResolutions();
		if (ImGui::CollapsingHeader(std::format("Resolution ({})###Resolution", resolutions.size()).data(), ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::BeginTable("Resolution", 6, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 16))) {
				ImGui::TableSetupColumn("Part", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 60.f);
				ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.f);
				ImGui::TableSetupColumn("UV", ImGuiTableColumnFlags_WidthFixed, 90.f);
				ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();
				if (auto* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsCount > 0) {
					const auto column = sort->Specs[0].ColumnIndex;
					const bool bDescending = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
					const auto less = [&](const BodyTypes::Resolution& a, const BodyTypes::Resolution& b) {
						switch (column) {
						case 1:
							return lessText(a.path, b.path);
						case 2:
							return lessText(a.type, b.type);
						case 3:
							return lessText(a.uv, b.uv);
						case 4:
							return lessText(a.project, b.project);
						case 5:
							return lessText(a.reason, b.reason);
						default:
							return a.part != b.part ? a.part < b.part : lessText(a.path, b.path);
						}
					};
					std::ranges::stable_sort(resolutions, [&](const BodyTypes::Resolution& a, const BodyTypes::Resolution& b) { return bDescending ? less(b, a) : less(a, b); });
				}
				for (const auto& resolution : resolutions) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(Scan::SkinPartName(resolution.part).data());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(resolution.path.data());
					if (!resolution.tri.empty()) {
						UICommon::AddTooltip(std::format("TRI: {}", resolution.tri).data());
					}
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(resolution.type.data());
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(resolution.uv.data());
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(resolution.project.data());
					ImGui::TableSetColumnIndex(5);
					UICommon::TextUnformattedDisabled(resolution.reason.data());
				}
				ImGui::EndTable();
			}
		}
	}

	void UIMain::DrawFeeds()
	{
		// Every resource a plugin flagged as a feed, as it registered it, and what it holds on the reference actor
		std::vector<Resources::Info> feeds;
		for (auto& info : Resources::List()) {
			if (info.bFeed) {
				feeds.push_back(std::move(info));
			}
		}
		if (feeds.empty()) {
			UICommon::TextUnformattedDisabled("No plugin has registered a feed.");
			return;
		}
		auto* refr = UIManager::GetSingleton().GetRefrToEvaluate();
		if (ImGui::BeginTable("Feeds", 7, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.f);
			ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 90.f);
			ImGui::TableSetupColumn("Refresh", ImGuiTableColumnFlags_WidthFixed, 110.f);
			ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 90.f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			const auto formText = [](RE::TESForm* a_form) {
				return a_form ? std::format("{} ({:08X})", a_form->GetName(), a_form->GetFormID()) : std::string("(none)");
			};
			for (const auto& feed : feeds) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(feed.key.data());
				if (!feed.displayName.empty() || !feed.description.empty()) {
					UICommon::AddTooltip(feed.displayName.empty() ? feed.description.data() : feed.description.empty() ? feed.displayName.data() : std::format("{}: {}", feed.displayName, feed.description).data());
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(feed.plugin.data());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(Resources::TypeName(feed.type).data());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(Resources::ScopeName(feed.scope).data());
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(Resources::RefreshName(feed).data());
				ImGui::TableSetColumnIndex(5);
				if (feed.type == Resources::ValueType::Number) {
					ImGui::Text("%.3f", feed.defaultNumber);
				} else {
					ImGui::TextUnformatted(formText(feed.defaultForm ? RE::TESForm::LookupByID(feed.defaultForm) : nullptr).data());
				}
				// What it holds on the reference actor, as last set or gathered
				ImGui::TableSetColumnIndex(6);
				if (feed.type == Resources::ValueType::Number) {
					ImGui::Text("%.3f", Resources::GetNumber(feed.key, refr).value_or(feed.defaultNumber));
				} else {
					ImGui::TextUnformatted(formText(Resources::GetForm(feed.key, refr)).data());
				}
			}
			ImGui::EndTable();
		}
	}

	void UIMain::DrawScan()
	{
		auto& scanner = Scan::Scanner::GetSingleton();
		const auto catalogue = scanner.GetCatalogue();

		ImGui::BeginDisabled(scanner.IsRunning());
		if (ImGui::Button("Rescan")) {
			scanner.Start();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (scanner.IsRunning()) {
			uint32_t done = 0;
			uint32_t total = 0;
			for (const auto& source : scanner.GetProgress()) {
				done += source.done;
				total += source.total;
			}
			ImGui::Text("Scanning... %u/%u", done, total);
		} else if (catalogue) {
			ImGui::Text("Last scan took %.0f ms", catalogue->totalMilliseconds);
		} else {
			ImGui::TextUnformatted("No scan has run yet.");
		}

		static char filterBuf[64] = "";
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
		ImGui::InputTextWithHint("Filter", "Name, file or plugin...", filterBuf, IM_ARRAYSIZE(filterBuf));
		ImGui::SameLine();
		UICommon::HelpMarker("Type a part of a name, a path or a plugin to filter every list below.");

		ImGui::Separator();

		if (!catalogue) {
			return;
		}

		const std::string_view filter = filterBuf;
		const auto matches = [&](std::string_view a_text) {
			return filter.empty() || Utils::ContainsStringIgnoreCase(a_text, filter);
		};
		const auto foundBy = [](const std::vector<Scan::Provenance>& a_provenance, const std::vector<Scan::Provenance>* a_more = nullptr) {
			std::vector<std::string_view> sources;
			const auto add = [&](const std::vector<Scan::Provenance>& a_list) {
				for (const auto& provenance : a_list) {
					if (std::ranges::find(sources, provenance.source) == sources.end()) {
						sources.push_back(provenance.source);
					}
				}
			};
			add(a_provenance);
			if (a_more) {
				add(*a_more);
			}
			std::string out;
			for (const auto source : sources) {
				out += out.empty() ? "" : " + ";
				out += source;
			}
			return out;
		};
		const auto provenanceTooltip = [](const std::vector<Scan::Provenance>& a_provenance) {
			if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
				return;
			}
			std::string text;
			for (const auto& provenance : a_provenance) {
				text += std::format("{}{}: {}", text.empty() ? "" : "\n", provenance.source, provenance.origin);
			}
			UICommon::AddTooltip(text.data());
		};

		// Sorts a table's rows by its sort column, every frame, after its headers
		const auto lessText = [](std::string_view a_lhs, std::string_view a_rhs) {
			return std::ranges::lexicographical_compare(a_lhs, a_rhs, [](char l, char r) {
				return std::tolower(static_cast<unsigned char>(l)) < std::tolower(static_cast<unsigned char>(r));
			});
		};
		const auto sortRows = [](auto& a_rows, const auto& a_less) {
			const auto* specs = ImGui::TableGetSortSpecs();
			if (!specs || specs->SpecsCount == 0) {
				return;
			}
			const auto spec = specs->Specs[0];
			std::ranges::stable_sort(a_rows, [&](const auto& a, const auto& b) {
				return spec.SortDirection == ImGuiSortDirection_Descending ? a_less(spec.ColumnIndex, b, a) : a_less(spec.ColumnIndex, a, b);
			});
		};
		constexpr auto sortableFlags = ImGuiTableFlags_Sortable;

		// Head parts by form, for the sets that name them
		static const Scan::Catalogue* indexed = nullptr;
		static std::unordered_map<RE::FormID, const Scan::HeadPart*> headParts;
		if (indexed != catalogue.get()) {
			indexed = catalogue.get();
			headParts.clear();
			for (const auto& part : catalogue->headParts) {
				headParts.emplace(part.formID, &part);
			}
		}
		const auto headPartName = [&](RE::FormID a_formID) {
			const auto it = headParts.find(a_formID);
			return it != headParts.end() && !it->second->editorID.empty() ? it->second->editorID : std::format("{:08X}", a_formID);
		};
		const auto typeName = [](RE::BGSHeadPart::HeadPartType a_type) {
			switch (a_type) {
			case RE::BGSHeadPart::HeadPartType::kMisc:
				return "Misc";
			case RE::BGSHeadPart::HeadPartType::kFace:
				return "Face";
			case RE::BGSHeadPart::HeadPartType::kEyes:
				return "Eyes";
			case RE::BGSHeadPart::HeadPartType::kHair:
				return "Hair";
			case RE::BGSHeadPart::HeadPartType::kFacialHair:
				return "Facial hair";
			case RE::BGSHeadPart::HeadPartType::kScar:
				return "Scar";
			case RE::BGSHeadPart::HeadPartType::kEyebrows:
				return "Eyebrows";
			default:
				return "?";
			}
		};

		const auto drawMorphs = [&](const char* a_id, const std::vector<Scan::Morph>& a_morphs, const std::vector<Scan::Provenance>& a_setFoundBy) {
			if (ImGui::BeginTable(a_id, 5, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | sortableFlags)) {
				ImGui::TableSetupColumn("Morph", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Shown as", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Slider", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Found by", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				std::vector<const Scan::Morph*> rows;
				for (const auto& morph : a_morphs) {
					rows.push_back(&morph);
				}
				sortRows(rows, [&](int a_column, const Scan::Morph* a, const Scan::Morph* b) {
					switch (a_column) {
					case 1:
						return lessText(a->display, b->display);
					case 2:
						return lessText(a->category, b->category);
					case 3:
						return lessText(a->slider, b->slider);
					default:
						return lessText(a->name, b->name);
					}
				});
				for (const auto* row : rows) {
					const auto& morph = *row;
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(morph.name.data());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(morph.display.data());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(morph.category.data());
					ImGui::TableSetColumnIndex(3);
					if (!morph.slider.empty()) {
						ImGui::Text("%s (%s)", morph.slider.data(), morph.sign < 0 ? "low" : "high");
					}
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(foundBy(a_setFoundBy, &morph.foundBy).data());
					if (!morph.foundBy.empty()) {
						provenanceTooltip(morph.foundBy);
					}
				}
				ImGui::EndTable();
			}
		};

		if (ImGui::BeginChild("Scan")) {
			// Head parts, as the tree their extra parts make
			// A part is a root unless it is an extra part, by flag or because another names it
			static const Scan::Catalogue* treed = nullptr;
			static std::vector<const Scan::HeadPart*> roots;
			if (treed != catalogue.get()) {
				treed = catalogue.get();
				std::unordered_set<RE::FormID> extras;
				for (const auto& part : catalogue->headParts) {
					extras.insert(part.extraParts.begin(), part.extraParts.end());
				}
				roots.clear();
				for (const auto& part : catalogue->headParts) {
					if (!part.bExtraPart && !extras.contains(part.formID)) {
						roots.push_back(&part);
					}
				}
			}
			if (ImGui::CollapsingHeader(std::format("{} ({})###HeadParts", Scan::TargetName(Scan::Target::kHeadParts), roots.size()).data())) {

				std::function<bool(const Scan::HeadPart&, int)> subtreeMatches = [&](const Scan::HeadPart& a_part, int a_depth) {
					if (matches(a_part.editorID) || matches(a_part.name) || matches(a_part.plugin) || matches(a_part.model)) {
						return true;
					}
					if (a_depth > 8) {
						return false;
					}
					for (const auto formID : a_part.extraParts) {
						if (const auto it = headParts.find(formID); it != headParts.end() && subtreeMatches(*it->second, a_depth + 1)) {
							return true;
						}
					}
					return false;
				};

				std::function<void(const Scan::HeadPart&, int)> drawPart = [&](const Scan::HeadPart& a_part, int a_depth) {
					if (!subtreeMatches(a_part, a_depth)) {
						return;
					}
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::PushID(&a_part);
					ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
					if (a_part.extraParts.empty() || a_depth > 8) {
						flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
					}
					const bool bOpen = ImGui::TreeNodeEx(a_part.editorID.empty() ? std::format("{:08X}", a_part.formID).data() : a_part.editorID.data(), flags);
					std::string details = std::format("{:08X}\nModel: {}", a_part.formID, a_part.model);
					for (const auto& tri : a_part.tris) {
						if (!tri.empty()) {
							details += std::format("\nTRI: {}", tri);
						}
					}
					if (!a_part.races.empty()) {
						details += "\nRaces:";
						for (const auto& race : a_part.races) {
							details += std::format(" {}", race);
						}
					}
					UICommon::AddTooltip(details.data());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(a_part.name.data());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(typeName(a_part.type));
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(a_part.bMale && a_part.bFemale ? "Both" : a_part.bMale ? "M" : a_part.bFemale ? "F" : "-");
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(a_part.plugin.data());
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%d", static_cast<int>(std::ranges::count_if(a_part.tris, [](const std::string& a_tri) { return !a_tri.empty(); })));
					if (bOpen && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
						for (const auto formID : a_part.extraParts) {
							if (const auto it = headParts.find(formID); it != headParts.end()) {
								drawPart(*it->second, a_depth + 1);
							}
						}
						ImGui::TreePop();
					}
					ImGui::PopID();
				};

				if (ImGui::BeginTable("HeadParts", 6, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | sortableFlags, ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 20))) {
					ImGui::TableSetupColumn("Editor ID", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.f);
					ImGui::TableSetupColumn("Sex", ImGuiTableColumnFlags_WidthFixed, 50.f);
					ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Morph TRIs", ImGuiTableColumnFlags_WidthFixed, 80.f);
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableHeadersRow();
					const auto triCount = [](const Scan::HeadPart* a_part) {
						return std::ranges::count_if(a_part->tris, [](const std::string& a_tri) { return !a_tri.empty(); });
					};
					const auto sexOrder = [](const Scan::HeadPart* a_part) { return (a_part->bMale ? 1 : 0) + (a_part->bFemale ? 2 : 0); };
					auto sorted = roots;
					sortRows(sorted, [&](int a_column, const Scan::HeadPart* a, const Scan::HeadPart* b) {
						switch (a_column) {
						case 1:
							return lessText(a->name, b->name);
						case 2:
							return a->type < b->type;
						case 3:
							return sexOrder(a) < sexOrder(b);
						case 4:
							return lessText(a->plugin, b->plugin);
						case 5:
							return triCount(a) < triCount(b);
						default:
							return lessText(a->editorID, b->editorID);
						}
					});
					for (const auto* part : sorted) {
						drawPart(*part, 0);
					}
					ImGui::EndTable();
				}
			}

			// Morph names, each opening onto the sets that define it
			const auto drawMorphNames = [&](const char* a_id, const std::vector<Scan::MorphSummary>& a_names, const auto& a_sets, const auto& a_drawSet) {
				std::vector<const Scan::MorphSummary*> shown;
				for (const auto& summary : a_names) {
					if (matches(summary.name) || matches(summary.display) || matches(summary.category)) {
						shown.push_back(&summary);
					}
				}
				if (!ImGui::CollapsingHeader(std::format("{} ({})###{}", a_id, shown.size(), a_id).data())) {
					return;
				}
				if (ImGui::BeginTable(a_id, 5, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | sortableFlags, ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 20))) {
					ImGui::TableSetupColumn("Morph", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
					ImGui::TableSetupColumn("Shown as", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthFixed, 70.f);
					ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Found by", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableHeadersRow();
					sortRows(shown, [&](int a_column, const Scan::MorphSummary* a, const Scan::MorphSummary* b) {
						switch (a_column) {
						case 1:
							return lessText(a->display, b->display);
						case 2:
							return a->min != b->min ? a->min < b->min : a->max < b->max;
						case 3:
							return lessText(a->category, b->category);
						case 4:
							return lessText(a->sources, b->sources);
						default:
							return lessText(a->name, b->name);
						}
					});
					for (const auto* summary : shown) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::PushID(summary);
						const bool bOpen = ImGui::TreeNodeEx(std::format("{}  ({})", summary->name, summary->sets.size()).data(), ImGuiTreeNodeFlags_SpanFullWidth);
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(summary->display.data());
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%g to %g", summary->min, summary->max);
						ImGui::TableSetColumnIndex(3);
						ImGui::TextUnformatted(summary->category.data());
						ImGui::TableSetColumnIndex(4);
						ImGui::TextUnformatted(summary->sources.data());
						provenanceTooltip(summary->foundBy);
						if (bOpen) {
							for (const auto index : summary->sets) {
								a_drawSet(a_sets[index], summary->name);
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			};

			const auto morphIn = [](const auto& a_set, std::string_view a_name) -> const Scan::Morph* {
				for (const auto& morph : a_set.morphs) {
					if (Utils::CompareStringsIgnoreCase(morph.name, a_name)) {
						return &morph;
					}
				}
				return nullptr;
			};

			drawMorphNames(Scan::TargetName(Scan::Target::kFaceMorphs).data(), catalogue->faceMorphNames, catalogue->faceMorphs, [&](const Scan::FaceMorphSet& a_set, std::string_view a_name) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TreeNodeEx(a_set.tri.data(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
				std::string details = std::format("{} vertices", a_set.vertexCount);
				if (!a_set.extends.empty()) {
					details += std::format("\nExtends: {}", a_set.extends);
				}
				details += "\nHead parts:";
				for (const auto formID : a_set.headParts) {
					details += std::format(" {}", headPartName(formID));
				}
				UICommon::AddTooltip(details.data());
				ImGui::TableSetColumnIndex(1);
				if (const auto* morph = morphIn(a_set, a_name); morph && !morph->slider.empty()) {
					ImGui::TextDisabled("%s (%s)", morph->slider.data(), morph->sign < 0 ? "low" : "high");
				}
				ImGui::TableSetColumnIndex(3);
				if (a_set.headParts.empty()) {
					UICommon::TextUnformattedColored(UICommon::WARNING_TEXT_COLOR, "no head part");
				} else {
					ImGui::TextDisabled("%zu head parts", a_set.headParts.size());
				}
				ImGui::TableSetColumnIndex(4);
				ImGui::TextDisabled("%s", foundBy(a_set.foundBy).data());
				provenanceTooltip(a_set.foundBy);
			});

			drawMorphNames(Scan::TargetName(Scan::Target::kBodyMorphs).data(), catalogue->bodyMorphNames, catalogue->bodyMorphs, [&](const Scan::BodyMorphSet& a_set, std::string_view a_name) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				const auto label = a_set.project.empty() ? a_set.key : std::format("{}  [{}]", a_set.project, a_set.key);
				ImGui::TreeNodeEx(label.data(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
				std::string details = a_set.tri.empty() ? std::string{} : std::format("TRI: {}{}", a_set.tri, a_set.bBuilt ? "" : " (not built)");
				if (!a_set.armorAddons.empty()) {
					details += "\nArmor addons:";
					for (const auto formID : a_set.armorAddons) {
						details += std::format(" {:08X}", formID);
					}
				}
				if (!a_set.zaps.empty()) {
					details += "\nZaps:";
					for (const auto& zap : a_set.zaps) {
						details += std::format(" {}", zap);
					}
				}
				if (!details.empty()) {
					UICommon::AddTooltip(details.data());
				}
				ImGui::TableSetColumnIndex(1);
				if (const auto* morph = morphIn(a_set, a_name); morph && !morph->shapes.empty()) {
					std::string shapes;
					for (const auto& shape : morph->shapes) {
						shapes += std::format("{}{}", shapes.empty() ? "" : ", ", shape);
					}
					ImGui::TextDisabled("%s", shapes.data());
				}
				ImGui::TableSetColumnIndex(3);
				if (!a_set.project.empty() && !a_set.bBuilt) {
					UICommon::TextUnformattedColored(UICommon::WARNING_TEXT_COLOR, "not built");
				} else if (!a_set.armorAddons.empty()) {
					ImGui::TextDisabled("%zu armor addons", a_set.armorAddons.size());
				}
				ImGui::TableSetColumnIndex(4);
				ImGui::TextDisabled("%s", foundBy(a_set.foundBy).data());
				provenanceTooltip(a_set.foundBy);
			});

			// BodySlide categories
			std::vector<const Scan::BodySlideCategory*> categories;
			for (const auto& category : catalogue->categories) {
				bool bMatch = matches(category.name);
				for (const auto& slider : category.sliders) {
					bMatch = bMatch || matches(slider.name) || matches(slider.display);
				}
				if (bMatch) {
					categories.push_back(&category);
				}
			}
			if (ImGui::CollapsingHeader(std::format("{} ({})###Categories", Scan::TargetName(Scan::Target::kBodySlideCategories), categories.size()).data())) {
				for (const auto* category : categories) {
					ImGui::PushID(category);
					const bool bOpen = ImGui::TreeNode(std::format("{}  ({} sliders)", category->name, category->sliders.size()).data());
					provenanceTooltip(category->foundBy);
					if (bOpen) {
						drawMorphs("Sliders", category->sliders, category->foundBy);
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
			}

			// BodySlide presets
			std::vector<const Scan::BodyPreset*> presets;
			for (const auto& preset : catalogue->bodyPresets) {
				if (matches(preset.name) || matches(preset.set) || matches(preset.origin)) {
					presets.push_back(&preset);
				}
			}
			if (ImGui::CollapsingHeader(std::format("{} ({})###BodyPresets", Scan::TargetName(Scan::Target::kBodyPresets), presets.size()).data())) {
				if (ImGui::BeginTable("BodyPresets", 4, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | sortableFlags, ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 16))) {
					ImGui::TableSetupColumn("Preset", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
					ImGui::TableSetupColumn("Set", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Sliders", ImGuiTableColumnFlags_WidthFixed, 60.f);
					ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableHeadersRow();
					sortRows(presets, [&](int a_column, const Scan::BodyPreset* a, const Scan::BodyPreset* b) {
						switch (a_column) {
						case 1:
							return lessText(a->set, b->set);
						case 2:
							return a->sliders.size() < b->sliders.size();
						case 3:
							return lessText(a->origin, b->origin);
						default:
							return lessText(a->name, b->name);
						}
					});
					for (const auto* preset : presets) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(preset->name.data());
						std::string groups;
						for (const auto& group : preset->groups) {
							groups += std::format("{}{}", groups.empty() ? "Groups: " : ", ", group);
						}
						if (!groups.empty()) {
							UICommon::AddTooltip(groups.data());
						}
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(preset->set.data());
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%zu", preset->sliders.size());
						ImGui::TableSetColumnIndex(3);
						ImGui::TextUnformatted(preset->origin.data());
					}
					ImGui::EndTable();
				}
			}

			// Skin models
			std::vector<const Scan::SkinModel*> skinModels;
			for (const auto& model : catalogue->skinModels) {
				if (matches(model.model) || std::ranges::any_of(model.shapes, matches) || std::ranges::any_of(model.races, matches)) {
					skinModels.push_back(&model);
				}
			}
			if (ImGui::CollapsingHeader(std::format("{} ({})###SkinModels", Scan::TargetName(Scan::Target::kSkinModels), skinModels.size()).data())) {
				if (ImGui::BeginTable("SkinModels", 5, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | sortableFlags, ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 16))) {
					ImGui::TableSetupColumn("Part", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 60.f);
					ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Shapes", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Sex", ImGuiTableColumnFlags_WidthFixed, 60.f);
					ImGui::TableSetupColumn("Races", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableHeadersRow();
					const auto joined = [](const std::vector<std::string>& a_items) {
						std::string out;
						for (const auto& item : a_items) {
							out += out.empty() ? item : ", " + item;
						}
						return out;
					};
					const auto sexOf = [](const Scan::SkinModel* a_model) {
						return a_model->bMale && a_model->bFemale ? "both"sv : a_model->bMale ? "male"sv : "female"sv;
					};
					sortRows(skinModels, [&](int a_column, const Scan::SkinModel* a, const Scan::SkinModel* b) {
						switch (a_column) {
						case 1:
							return lessText(a->model, b->model);
						case 2:
							return lessText(joined(a->shapes), joined(b->shapes));
						case 3:
							return lessText(sexOf(a), sexOf(b));
						case 4:
							return lessText(joined(a->races), joined(b->races));
						default:
							return a->part != b->part ? a->part < b->part : lessText(a->model, b->model);
						}
					});
					for (const auto* model : skinModels) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(Scan::SkinPartName(model->part).data());
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(model->model.data());
						ImGui::TableSetColumnIndex(2);
						ImGui::TextUnformatted(joined(model->shapes).data());
						ImGui::TableSetColumnIndex(3);
						ImGui::TextUnformatted(sexOf(model).data());
						ImGui::TableSetColumnIndex(4);
						const auto races = joined(model->races);
						ImGui::TextUnformatted(races.data());
						UICommon::AddTooltip(races.data());
					}
					ImGui::EndTable();
				}
			}

			// Body types
			if (ImGui::CollapsingHeader("Body types###BodyTypes")) {
				ImGui::Indent();
				DrawBodyTypes();
				ImGui::Unindent();
			}

			// Problems
			if (ImGui::CollapsingHeader(std::format("Problems ({})###Problems", catalogue->problems.size()).data())) {
				if (ImGui::BeginTable("Problems", 3, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | sortableFlags, ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 16))) {
					ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 160.f);
					ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Problem", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableHeadersRow();
					std::vector<const Scan::Problem*> problems;
					for (const auto& problem : catalogue->problems) {
						if (matches(problem.origin) || matches(problem.message) || matches(problem.source)) {
							problems.push_back(&problem);
						}
					}
					sortRows(problems, [&](int a_column, const Scan::Problem* a, const Scan::Problem* b) {
						switch (a_column) {
						case 1:
							return lessText(a->origin, b->origin);
						case 2:
							return lessText(a->message, b->message);
						default:
							return lessText(a->source, b->source);
						}
					});
					for (const auto* row : problems) {
						const auto& problem = *row;
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(problem.source.data());
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(problem.origin.data());
						ImGui::TableSetColumnIndex(2);
						ImGui::TextUnformatted(problem.message.data());
					}
					ImGui::EndTable();
				}
			}

			// Timings
			if (ImGui::CollapsingHeader("Timings###Timings")) {
				if (ImGui::BeginTable("Timings", 4, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit | sortableFlags)) {
					ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_DefaultSort);
					ImGui::TableSetupColumn("Target");
					ImGui::TableSetupColumn("ms");
					ImGui::TableSetupColumn("Items");
					ImGui::TableHeadersRow();
					std::vector<const Scan::SourceTiming*> timings;
					for (const auto& timing : catalogue->timings) {
						timings.push_back(&timing);
					}
					sortRows(timings, [&](int a_column, const Scan::SourceTiming* a, const Scan::SourceTiming* b) {
						switch (a_column) {
						case 1:
							return a->target < b->target;
						case 2:
							return a->milliseconds < b->milliseconds;
						case 3:
							return a->files < b->files;
						default:
							return lessText(a->source, b->source);
						}
					});
					for (const auto* row : timings) {
						const auto& timing = *row;
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(timing.source.data());
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(Scan::TargetName(timing.target).data());
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%.1f", timing.milliseconds);
						ImGui::TableSetColumnIndex(3);
						ImGui::Text("%u", timing.files);
					}
					ImGui::EndTable();
				}
			}
		}
		ImGui::EndChild();
	}

	bool UIMain::DrawConditionSet(Conditions::ConditionSet* a_conditionSet, SubMod* a_parentSubMod, EditMode a_editMode, Conditions::ConditionType a_conditionType, RE::TESObjectREFR* a_refrToEvaluate, bool a_bDrawLines, const ImVec2& a_drawStartPos)
	{
		if (!a_conditionSet) {
			return false;
		}

		//ImGui::TableNextRow();
		//ImGui::TableSetColumnIndex(0);

		bool bSetDirty = false;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();

		ImVec2 vertLineStart = a_drawStartPos;
		vertLineStart.y += style.FramePadding.x;
		vertLineStart.x -= style.IndentSpacing * 0.6f;
		ImVec2 vertLineEnd = vertLineStart;

		const float tooltipWidth = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2;

		if (!a_conditionSet->IsEmpty()) {
			a_conditionSet->ForEach([&](std::unique_ptr<Conditions::ICondition>& a_condition) {
				const ImRect nodeRect = DrawCondition(a_condition, a_conditionSet, a_parentSubMod, a_editMode, a_conditionType, a_refrToEvaluate, bSetDirty);
				if (a_bDrawLines) {
					const float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
					constexpr float horLineLength = 10.f;
					drawList->AddLine(ImVec2(vertLineStart.x, midPoint), ImVec2(vertLineStart.x + horLineLength, midPoint), ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
					vertLineEnd.y = midPoint;
				}

				return RE::BSVisit::BSVisitControl::kContinue;
			});
		} else {
			DrawBlankCondition(a_conditionSet, a_editMode, a_conditionType);
		}

		if (a_bDrawLines) {
			drawList->AddLine(vertLineStart, vertLineEnd, ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
		}

		if (a_editMode > EditMode::kNone) {
			const bool bIsConditionPreset = a_conditionType == Conditions::ConditionType::kPreset;

			// Add condition button
			if (ImGui::Button("Add new condition")) {
				if (bIsConditionPreset && _lastAddNewConditionName == "PRESET") {
					_lastAddNewConditionName.clear();
				}
				if (_lastAddNewConditionName.empty()) {
					auto isFormCondition = Conditions::CreateCondition("IsForm"sv);
					a_conditionSet->Add(isFormCondition, true);
					bSetDirty = true;
				} else {
					auto newCondition = Conditions::CreateCondition(_lastAddNewConditionName);
					a_conditionSet->Add(newCondition, true);
					bSetDirty = true;
				}
			}

			// Condition set functions button
			ImGui::SameLine(0.f, 20.f);
			const auto popupId = std::string("Condition set functions##") + std::to_string(reinterpret_cast<uintptr_t>(a_conditionSet));
			if (UICommon::PopupToggleButton("Condition set...", popupId.data())) {
				ImGui::OpenPopup(popupId.data());
			}

			if (ImGui::BeginPopupContextItem(popupId.data())) {
				const auto xButtonSize = ImGui::CalcTextSize("Paste condition set").x + style.FramePadding.x * 2 + style.ItemSpacing.x;

				// Copy conditions button
				ImGui::BeginDisabled(a_conditionSet->IsEmpty() || !a_conditionSet->IsValid());
				if (ImGui::Button("Copy condition set", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					_conditionSetCopy = DuplicateConditionSet(a_conditionSet);
				}
				ImGui::EndDisabled();

				// Paste conditions button
				const bool bPasteEnabled = _conditionSetCopy && !(bIsConditionPreset && ConditionSetContainsPreset(_conditionSetCopy.get()));
				ImGui::BeginDisabled(!bPasteEnabled);
				if (ImGui::Button("Paste condition set", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					const auto duplicatedSet = DuplicateConditionSet(_conditionSetCopy.get());
					a_conditionSet->Append(duplicatedSet.get());
					bSetDirty = true;
				}
				ImGui::EndDisabled();
				// Paste tooltip
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
					ImGui::SetNextWindowSize(ImVec2(tooltipWidth, 0));
					ImGui::BeginTooltip();
					DrawConditionSet(_conditionSetCopy.get(), nullptr, EditMode::kNone, a_conditionType, nullptr, false, a_drawStartPos);
					ImGui::EndTooltip();
				}

				// Clear conditions button
				ImGui::BeginDisabled(a_conditionSet->IsEmpty());
				UICommon::ButtonWithConfirmationModal(
					"Clear condition set"sv, "Are you sure you want to clear the condition set?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ImGui::ClosePopupsExceptModals();
						ModRegistry::GetSingleton().QueueJob<Jobs::ClearConditionSetJob>(a_conditionSet);
						bSetDirty = true;
					},
					ImVec2(xButtonSize, 0));
				ImGui::EndDisabled();
				ImGui::EndPopup();
			}
		}

		return bSetDirty;
	}

	bool UIMain::DrawFunctionSet(Functions::FunctionSet* a_functionSet, SubMod* a_parentSubMod, EditMode a_editMode, Functions::FunctionSetType a_functionSetType, RE::TESObjectREFR* a_refrToEvaluate, bool a_bDrawLines, const ImVec2& a_drawStartPos)
	{
		bool bSetDirty = false;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();

		ImVec2 vertLineStart = a_drawStartPos;
		vertLineStart.y += style.FramePadding.x;
		vertLineStart.x -= style.IndentSpacing * 0.6f;
		ImVec2 vertLineEnd = vertLineStart;

		const float tooltipWidth = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2;

		if (a_functionSet && !a_functionSet->IsEmpty()) {
			a_functionSet->ForEach([&](std::unique_ptr<Functions::IFunction>& a_function) {
				const ImRect nodeRect = DrawFunction(a_function, a_functionSet, a_parentSubMod, a_editMode, a_functionSetType, a_refrToEvaluate, bSetDirty);
				if (a_bDrawLines) {
					const float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
					constexpr float horLineLength = 10.f;
					drawList->AddLine(ImVec2(vertLineStart.x, midPoint), ImVec2(vertLineStart.x + horLineLength, midPoint), ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
					vertLineEnd.y = midPoint;
				}

				return RE::BSVisit::BSVisitControl::kContinue;
			});
		} else {
			DrawBlankFunction(a_functionSet, a_parentSubMod, a_editMode, a_functionSetType);
		}

		if (a_bDrawLines) {
			drawList->AddLine(vertLineStart, vertLineEnd, ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
		}

		if (a_editMode > EditMode::kNone) {
			// Add function button
			if (ImGui::Button("Add new function")) {
				if (!a_functionSet) {
					a_functionSet = a_parentSubMod->CreateOrGetFunctionSet(a_functionSetType);
				}
				if (_lastAddNewFunctionName.empty()) {
					auto defaultFunction = Functions::CreateFunction("PlaySound"sv);
					a_functionSet->Add(defaultFunction, true);
					bSetDirty = true;
				} else {
					auto newFunction = Functions::CreateFunction(_lastAddNewFunctionName);
					a_functionSet->Add(newFunction, true);
					bSetDirty = true;
				}
			}

			// Function set functions button
			ImGui::SameLine(0.f, 20.f);
			const auto popupId = std::string("Function set functions##") + std::to_string(reinterpret_cast<uintptr_t>(a_parentSubMod)) + std::to_string(static_cast<uint8_t>(a_functionSetType)) + std::to_string(reinterpret_cast<uintptr_t>(a_functionSet));
			if (UICommon::PopupToggleButton("Function set...", popupId.data())) {
				ImGui::OpenPopup(popupId.data());
			}

			if (ImGui::BeginPopupContextItem(popupId.data())) {
				const auto xButtonSize = ImGui::CalcTextSize("Paste function set").x + style.FramePadding.x * 2 + style.ItemSpacing.x;

				// Copy functions button
				ImGui::BeginDisabled(!a_functionSet || a_functionSet->IsEmpty() || !a_functionSet->IsValid());
				if (ImGui::Button("Copy function set", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					_functionSetCopy = DuplicateFunctionSet(a_functionSet);
				}
				ImGui::EndDisabled();

				// Paste functions button
				const bool bPasteEnabled = _functionSetCopy != nullptr;
				ImGui::BeginDisabled(!bPasteEnabled);
				if (ImGui::Button("Paste function set", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					const auto duplicatedSet = DuplicateFunctionSet(_functionSetCopy.get());
					if (!a_functionSet) {
						a_functionSet = a_parentSubMod->CreateOrGetFunctionSet(a_functionSetType);
					}
					a_functionSet->Append(duplicatedSet.get());
					bSetDirty = true;
				}
				ImGui::EndDisabled();
				// Paste tooltip
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
					ImGui::SetNextWindowSize(ImVec2(tooltipWidth, 0));
					ImGui::BeginTooltip();
					DrawFunctionSet(_functionSetCopy.get(), nullptr, EditMode::kNone, a_functionSetType, nullptr, false, a_drawStartPos);
					ImGui::EndTooltip();
				}

				// Clear functions button
				ImGui::BeginDisabled(!a_functionSet || a_functionSet->IsEmpty());
				UICommon::ButtonWithConfirmationModal(
					"Clear function set"sv, "Are you sure you want to clear the function set?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ImGui::ClosePopupsExceptModals();
						ModRegistry::GetSingleton().QueueJob<Jobs::ClearFunctionSetJob>(a_functionSet);
						bSetDirty = true;
					},
					ImVec2(xButtonSize, 0));
				ImGui::EndDisabled();
				ImGui::EndPopup();
			}
		}

		return bSetDirty;
	}

	ImRect UIMain::DrawCondition(std::unique_ptr<Conditions::ICondition>& a_condition, Conditions::ConditionSet* a_conditionSet, SubMod* a_parentSubMod, EditMode a_editMode, Conditions::ConditionType a_conditionType, RE::TESObjectREFR* a_refrToEvaluate, bool& a_bOutSetDirty)
	{
		ImRect conditionRect;

		// As the inspected reference's latest tick answered the row; a row it did not answer shows no mark
		const auto answer = a_refrToEvaluate ? Scheduler::AnswerFor(a_parentSubMod, a_condition.get()) : std::nullopt;
		const auto evalResult = !answer ? ConditionEvaluateResult::kNone : answer->bHolds ? ConditionEvaluateResult::kSuccess : ConditionEvaluateResult::kFailure;

		//ImGui::BeginGroup();
		ImRect nodeRect;

		std::string conditionTableId = std::format("{}conditionTable", reinterpret_cast<uintptr_t>(a_condition.get()));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		const bool bIsConditionPreset = a_condition->GetConditionType() == Conditions::ConditionType::kPreset;
		const bool bHasSharedState = Utils::ConditionHasStateComponentWithSharedScope(a_condition.get());
		if (bIsConditionPreset) {
			ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, UICommon::CONDITION_PRESET_BORDER_COLOR);
		} else if (bHasSharedState) {
			ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, UICommon::CONDITION_SHARED_STATE_BORDER_COLOR);
		}

		if (ImGui::BeginTable(conditionTableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			//ImGui::AlignTextToFramePadding();

			if (bIsConditionPreset || bHasSharedState) {
				ImGui::PopStyleColor();  // ImGuiCol_TableBorderStrong
			}

			const auto conditionName = a_condition->GetName();
			std::string nodeName = conditionName.data();
			if (a_condition->IsNegated()) {
				nodeName.insert(0, "NOT ");
			}

			bool bStyleVarPushed = false;
			if (a_condition->IsDisabled()) {
				auto& style = ImGui::GetStyle();
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
				bStyleVarPushed = true;
			}

			float tooltipWidth = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2;
			ImVec2 nodePos = ImGui::GetCursorScreenPos();

			// Open node
			bool bNodeOpen = false;
			if (a_editMode > EditMode::kNone || a_condition->GetNumComponents() > 0) {
				if (_reopenSet == a_conditionSet && a_condition.get() != _reopenOld) {
					int index = 0;
					bool bFound = false;
					a_conditionSet->ForEach([&](std::unique_ptr<Conditions::ICondition>& a_entry) {
						if (a_entry.get() == a_condition.get()) {
							bFound = true;
							return RE::BSVisit::BSVisitControl::kStop;
						}
						++index;
						return RE::BSVisit::BSVisitControl::kContinue;
					});
					if (bFound && index == _reopenIndex) {
						ImGui::SetNextItemOpen(true);
						_reopenSet = nullptr;
						_reopenOld = nullptr;
					}
				}
				bNodeOpen = ImGui::TreeNodeEx(a_condition.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
			} else {
				bNodeOpen = UICommon::TreeNodeCollapsedLeaf(a_condition.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
				//bNodeOpen = ImGui::TreeNodeEx(a_condition.get(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
			}

			// Condition context menu
			if (a_editMode > EditMode::kNone) {
				if (ImGui::BeginPopupContextItem()) {
					// copy button
					auto& style = ImGui::GetStyle();
					auto xButtonSize = ImGui::CalcTextSize("Paste condition below").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					ImGui::BeginDisabled(!a_condition->IsValid());
					if (ImGui::Button("Copy condition", ImVec2(xButtonSize, 0))) {
						_conditionCopy = DuplicateCondition(a_condition);
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();

					// paste button
					const bool bPasteEnabled = _conditionCopy && !(a_conditionType == Conditions::ConditionType::kPreset && ConditionContainsPreset(_conditionCopy.get()));
					ImGui::BeginDisabled(!bPasteEnabled);
					if (ImGui::Button("Paste condition below", ImVec2(xButtonSize, 0))) {
						auto duplicate = DuplicateCondition(_conditionCopy);
						ModRegistry::GetSingleton().QueueJob<Jobs::InsertConditionJob>(duplicate, a_conditionSet, a_condition);
						a_bOutSetDirty = true;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();
					// paste tooltip
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
						ImGui::SetNextWindowSize(ImVec2(tooltipWidth, 0));
						ImGui::BeginTooltip();
						bool bDummy = false;
						DrawCondition(_conditionCopy, a_conditionSet, nullptr, EditMode::kNone, a_conditionType, nullptr, bDummy);
						ImGui::EndTooltip();
					}

					// comment button
					ImGui::Spacing();

					if (ImGui::Button("Edit comment", ImVec2(xButtonSize, 0))) {
						_commentState.Set(a_condition.get(), a_conditionSet);
						ImGui::CloseCurrentPopup();
					}

					ImGui::Spacing();

					// delete button
					UICommon::ButtonWithConfirmationModal(
						"Delete condition"sv, "Are you sure you want to remove the condition?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveConditionJob>(a_condition, a_conditionSet);
							a_bOutSetDirty = true;
						},
						ImVec2(xButtonSize, 0));

					ImGui::EndPopup();
				}
			}

			// Condition description tooltip
			DrawInfoTooltip(a_condition.get());

			nodeRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// Drag & Drop source
			if (a_editMode > EditMode::kNone && a_condition->IsValid()) {
				if (BeginDragDropSourceEx(ImGuiDragDropFlags_SourceNoHoldToOpenOthers, ImVec2(tooltipWidth, 0))) {
					DragConditionPayload payload(a_condition, a_conditionSet);
					ImGui::SetDragDropPayload("DND_CONDITION", &payload, sizeof(DragConditionPayload));
					bool bDummy = false;
					DrawCondition(a_condition, a_conditionSet, nullptr, EditMode::kNone, a_conditionType, nullptr, bDummy);
					ImGui::EndDragDropSource();
				}
			}

			// Drag & Drop target - tree node
			if (a_editMode > EditMode::kNone) {
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload("DND_CONDITION", ImGuiDragDropFlags_AcceptPeekOnly)) {
						DragConditionPayload payload = *static_cast<DragConditionPayload*>(imguiPayload->Data);

						const ImGuiStyle& style = ImGui::GetStyle();
						if (!bNodeOpen) {
							// Draw our own preview of the drop because we want to draw a line either above or below the condition
							float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
							const auto upperHalf = ImRect(nodeRect.Min.x, nodeRect.Min.y, nodeRect.Max.x, midPoint);
							const auto lowerHalf = ImRect(nodeRect.Min.x, midPoint, nodeRect.Max.x, nodeRect.Max.y);

							bool bInsertAfter = ImGui::IsMouseHoveringRect(lowerHalf.Min, lowerHalf.Max);

							ImDrawList* drawList = ImGui::GetWindowDrawList();
							auto lineY = bInsertAfter ? nodeRect.Max.y + style.ItemSpacing.y * 0.5f : nodeRect.Min.y - style.ItemSpacing.y * 0.5f;
							drawList->AddLine(ImVec2(nodeRect.Min.x, lineY), ImVec2(nodeRect.Max.x, lineY), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 3.f);

							if (imguiPayload->IsDelivery()) {
								ModRegistry::GetSingleton().QueueJob<Jobs::MoveConditionJob>(payload.condition, payload.conditionSet, a_condition, a_conditionSet, bInsertAfter);
							}
						} else {
							// Draw our own preview of the drop because we want to draw a line above the condition if we're hovering over the upper half of the node. Ignore anything below, we have the invisible button for that
							float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
							const auto upperHalf = ImRect(nodeRect.Min.x, nodeRect.Min.y, nodeRect.Max.x, midPoint);

							if (ImGui::IsMouseHoveringRect(upperHalf.Min, upperHalf.Max)) {
								ImDrawList* drawList = ImGui::GetWindowDrawList();
								drawList->AddLine(ImVec2(nodeRect.Min.x, nodeRect.Min.y - style.ItemSpacing.y * 0.5f), ImVec2(nodeRect.Max.x, nodeRect.Min.y - style.ItemSpacing.y * 0.5f), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 3.f);

								if (imguiPayload->IsDelivery()) {
									ModRegistry::GetSingleton().QueueJob<Jobs::MoveConditionJob>(payload.condition, payload.conditionSet, a_condition, a_conditionSet, false);
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
			}

			// Disable checkbox
			ImGui::SameLine();
			if (a_editMode > EditMode::kNone) {
				std::string idString = std::format("{}##bDisabled", reinterpret_cast<uintptr_t>(a_condition.get()));
				ImGui::PushID(idString.data());
				bool bEnabled = !a_condition->IsDisabled();
				if (ImGui::Checkbox("##toggleCondition", &bEnabled)) {
					a_condition->SetDisabled(!bEnabled);
					a_conditionSet->SetDirty(true);
					a_bOutSetDirty = true;
				}
				UICommon::AddTooltip("Toggles the condition on/off");
				ImGui::PopID();
			}

			// Condition name
			ImGui::SameLine();
			if (a_condition->IsValid()) {
				auto requiredPluginName = a_condition->GetRequiredPluginName();
				if (!requiredPluginName.empty()) {
					UICommon::TextUnformattedColored(UICommon::CUSTOM_CONDITION_COLOR, nodeName.data());
				} else if (a_condition->GetConditionType() == Conditions::ConditionType::kPreset) {
					UICommon::TextUnformattedColored(UICommon::CONDITION_PRESET_COLOR, nodeName.data());
				} else {
					ImGui::TextUnformatted(nodeName.data());
				}
			} else {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, nodeName.data());
			}

			ImVec2 cursorPos = ImGui::GetCursorScreenPos();

			// Condition comment
			ImGui::SameLine();
			if (!a_condition->GetComment().empty()) {
				std::string comment = a_condition->GetComment().c_str();
				float maxCommentWidth = (ImGui::GetWindowContentRegionMax().x * _firstColumnWidthPercent) - ImGui::GetCursorPos().x;
				UICommon::TextUnformattedEllipsisColored(UICommon::COMMENT_COLOR, std::format("{}", comment).data(), nullptr, maxCommentWidth);
			}

			// Right column, argument text
			UICommon::SecondColumn(_firstColumnWidthPercent);
			const auto argument = a_condition->GetArgument();
			ImGui::TextUnformatted(argument.data());

			//ImGui::TableSetColumnIndex(0);

			// Evaluate success/failure indicator
			if (a_refrToEvaluate) {
				UICommon::DrawConditionEvaluateResult(evalResult);
			}

			conditionRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// Node contents
			if (bNodeOpen) {
				ImGui::Spacing();

				if (a_editMode > EditMode::kNone) {
					// negate checkbox
					bool bNOT = a_condition->IsNegated();
					if (ImGui::Checkbox("Negate", &bNOT)) {
						a_condition->SetNegated(bNOT);
						a_conditionSet->SetDirty(true);
						a_bOutSetDirty = true;
					}
					UICommon::AddTooltip("Negates the condition");

					// select condition type
					ImGui::SameLine();
					const float conditionComboWidth = UICommon::FirstColumnWidth(_firstColumnWidthPercent);
					ImGui::SetNextItemWidth(conditionComboWidth);

					const auto& conditionInfos = _conditionComboFilter.GetConditionInfos(a_conditionType);
					if (conditionInfos.empty()) {
						_conditionComboFilter.CacheInfos();
					}

					int selectedItem = -1;
					const Info* currentConditionInfo = nullptr;

					auto it = std::ranges::find_if(conditionInfos, [&](const Info& a_conditionInfo) {
						return a_conditionInfo.name == std::string_view(conditionName);
					});

					if (it != conditionInfos.end()) {
						selectedItem = static_cast<int>(std::distance(conditionInfos.begin(), it));
						currentConditionInfo = &*it;
					}

					if (_conditionComboFilter.ComboFilter("##Condition type", selectedItem, conditionInfos, currentConditionInfo, ImGuiComboFlags_HeightLarge, &UIMain::DrawInfoTooltip)) {
						if (selectedItem >= 0 && selectedItem < conditionInfos.size() && ModRegistry::GetSingleton().HasConditionFactory(conditionInfos[selectedItem].name)) {
							_lastAddNewConditionName = conditionInfos[selectedItem].name;
							_reopenSet = a_conditionSet;
							_reopenOld = a_condition.get();
							_reopenIndex = -1;
							int index = 0;
							a_conditionSet->ForEach([&](std::unique_ptr<Conditions::ICondition>& a_entry) {
								if (a_entry.get() == _reopenOld) {
									_reopenIndex = index;
									return RE::BSVisit::BSVisitControl::kStop;
								}
								++index;
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							ModRegistry::GetSingleton().QueueJob<Jobs::ReplaceConditionJob>(a_condition, conditionInfos[selectedItem].name, a_conditionSet);
							a_bOutSetDirty = true;
						}
					}

					// remove condition button
					UICommon::SecondColumn(_firstColumnWidthPercent);

					UICommon::ButtonWithConfirmationModal("Delete condition"sv, "Are you sure you want to remove the condition?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveConditionJob>(a_condition, a_conditionSet);
						a_bOutSetDirty = true;
					});
				}

				if (auto numComponents = a_condition->GetNumComponents(); numComponents > 0) {
					for (uint32_t i = 0; i < numComponents; i++) {
						auto component = a_condition->GetComponent(i);
						const bool bIsMultiConditionComponent = component->GetType() == Conditions::ConditionComponentType::kMulti;
						const bool bIsConditionPresetComponent = component->GetType() == Conditions::ConditionComponentType::kPreset;
						if (bIsMultiConditionComponent || bIsConditionPresetComponent) {
							const auto multiConditionComponent = static_cast<Conditions::IMultiConditionComponent*>(component);
							// draw conditions
							auto conditionType = a_conditionType == Conditions::ConditionType::kPreset || bIsConditionPresetComponent ? Conditions::ConditionType::kPreset : Conditions::ConditionType::kNormal;
							if (DrawConditionSet(multiConditionComponent->GetConditions(), a_parentSubMod, bIsConditionPresetComponent ? EditMode::kNone : a_editMode, conditionType, multiConditionComponent->GetShouldDrawEvaluateResultForChildConditions() ? a_condition->GetRefrToEvaluate(a_refrToEvaluate) : nullptr, true, cursorPos)) {
								a_conditionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
							// display component
							if (component->DisplayInUI(a_editMode != EditMode::kNone, _firstColumnWidthPercent)) {
								a_conditionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
						} else {
							ImGui::Separator();
							// write component name aligned to the right
							const auto componentName = component->GetName();
							UICommon::TextDescriptionRightAligned(componentName.data());
							// show component description on mouseover
							const auto componentDescription = component->GetDescription();
							if (!componentDescription.empty()) {
								UICommon::AddTooltip(componentDescription.data());
							}
							// display component
							if (component->DisplayInUI(a_editMode != EditMode::kNone, _firstColumnWidthPercent)) {
								a_conditionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
						}
					}
				}

				// The number the row compared, as the inspected reference's latest tick read it
				if (answer && !answer->current.empty()) {
					ImGui::Separator();
					UICommon::TextUnformattedDisabled("Current:");
					ImGui::SameLine();
					ImGui::TextUnformatted(answer->current.data());
				}

				ImGui::Spacing();

				ImGui::TreePop();
			}

			if (bStyleVarPushed) {
				ImGui::PopStyleVar();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		auto rectMax = ImGui::GetItemRectMax();

		auto width = ImGui::GetItemRectSize().x;
		auto groupEnd = ImGui::GetCursorPos();
		const ImGuiStyle& style = ImGui::GetStyle();
		ImVec2 invisibleButtonStart = groupEnd;
		invisibleButtonStart.y -= style.ItemSpacing.y;
		ImGui::SetCursorPos(invisibleButtonStart);
		std::string conditionInvisibleDragAreaId = std::format("{}conditionInvisibleDragArea", reinterpret_cast<uintptr_t>(a_condition.get()));
		ImGui::InvisibleButton(conditionInvisibleDragAreaId.data(), ImVec2(width, style.ItemSpacing.y));

		// Drag & Drop target - invisible button
		if (a_editMode > EditMode::kNone) {
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload("DND_CONDITION", ImGuiDragDropFlags_AcceptPeekOnly)) {
					DragConditionPayload payload = *static_cast<DragConditionPayload*>(imguiPayload->Data);
					// Draw our own preview of the drop because we want to draw a line below the condition

					ImDrawList* drawList = ImGui::GetWindowDrawList();
					drawList->AddLine(ImVec2(nodeRect.Min.x, rectMax.y + style.ItemSpacing.y * 0.5f), ImVec2(nodeRect.Max.x, rectMax.y + style.ItemSpacing.y * 0.5f), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 3.f);

					if (imguiPayload->IsDelivery()) {
						ModRegistry::GetSingleton().QueueJob<Jobs::MoveConditionJob>(payload.condition, payload.conditionSet, a_condition, a_conditionSet, true);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		ImGui::SetCursorPos(groupEnd);

		//ImGui::EndGroup();

		return conditionRect;
	}

	ImRect UIMain::DrawFunction(std::unique_ptr<Functions::IFunction>& a_function, Functions::FunctionSet* a_functionSet, SubMod* a_parentSubMod, EditMode a_editMode, Functions::FunctionSetType a_functionSetType, RE::TESObjectREFR* a_refrToEvaluate, bool& a_bOutSetDirty)
	{
		ImRect functionRect;

		ImRect nodeRect;

		std::string functionTableId = std::format("{}functionTable", reinterpret_cast<uintptr_t>(a_function.get()));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));

		if (ImGui::BeginTable(functionTableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			//ImGui::AlignTextToFramePadding();

			const auto functionName = a_function->GetName();
			std::string nodeName = functionName.data();

			bool bStyleVarPushed = false;
			if (a_function->IsDisabled()) {
				auto& style = ImGui::GetStyle();
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
				bStyleVarPushed = true;
			}

			float tooltipWidth = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2;
			ImVec2 nodePos = ImGui::GetCursorScreenPos();

			// Open node
			bool bNodeOpen = false;
			if (a_editMode > EditMode::kNone || a_function->GetNumComponents() > 0) {
				bNodeOpen = ImGui::TreeNodeEx(a_function.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
			} else {
				bNodeOpen = UICommon::TreeNodeCollapsedLeaf(a_function.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
				//bNodeOpen = ImGui::TreeNodeEx(a_condition.get(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
			}

			// Function context menu
			if (a_editMode > EditMode::kNone) {
				if (ImGui::BeginPopupContextItem()) {
					// copy button
					auto& style = ImGui::GetStyle();
					auto xButtonSize = ImGui::CalcTextSize("Paste function below").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					ImGui::BeginDisabled(!a_function->IsValid());
					if (ImGui::Button("Copy function", ImVec2(xButtonSize, 0))) {
						_functionCopy = DuplicateFunction(a_function);
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();

					// paste button
					const bool bPasteEnabled = _functionCopy != nullptr;
					ImGui::BeginDisabled(!bPasteEnabled);
					if (ImGui::Button("Paste function below", ImVec2(xButtonSize, 0))) {
						auto duplicate = DuplicateFunction(_functionCopy);
						if (!a_functionSet) {
							a_functionSet = a_parentSubMod->CreateOrGetFunctionSet(a_functionSetType);
						}
						ModRegistry::GetSingleton().QueueJob<Jobs::InsertFunctionJob>(duplicate, a_functionSet, a_function);
						a_bOutSetDirty = true;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();

					// paste tooltip
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
						ImGui::SetNextWindowSize(ImVec2(tooltipWidth, 0));
						ImGui::BeginTooltip();
						bool bDummy = false;
						DrawFunction(_functionCopy, a_functionSet, nullptr, EditMode::kNone, a_functionSetType, nullptr, bDummy);
						ImGui::EndTooltip();
					}

					// comment button
					ImGui::Spacing();

					if (ImGui::Button("Edit comment", ImVec2(xButtonSize, 0))) {
						_commentState.Set(a_function.get(), a_functionSet);
						ImGui::CloseCurrentPopup();
					}

					ImGui::Spacing();

					// delete button
					UICommon::ButtonWithConfirmationModal(
						"Delete function"sv, "Are you sure you want to remove the function?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveFunctionJob>(a_function, a_functionSet);
							a_bOutSetDirty = true;
						},
						ImVec2(xButtonSize, 0));

					ImGui::EndPopup();
				}
			}

			// Function description tooltip
			DrawInfoTooltip(a_function.get());

			nodeRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// Drag & Drop source
			if (a_editMode > EditMode::kNone && a_function->IsValid()) {
				if (BeginDragDropSourceEx(ImGuiDragDropFlags_SourceNoHoldToOpenOthers, ImVec2(tooltipWidth, 0))) {
					DragFunctionPayload payload(a_function, a_functionSet);
					ImGui::SetDragDropPayload("DND_FUNCTION", &payload, sizeof(DragConditionPayload));
					bool bDummy = false;
					DrawFunction(a_function, a_functionSet, nullptr, EditMode::kNone, a_functionSetType, nullptr, bDummy);
					ImGui::EndDragDropSource();
				}
			}

			// Drag & Drop target - tree node
			if (a_editMode > EditMode::kNone) {
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload("DND_FUNCTION", ImGuiDragDropFlags_AcceptPeekOnly)) {
						DragFunctionPayload payload = *static_cast<DragFunctionPayload*>(imguiPayload->Data);

						const ImGuiStyle& style = ImGui::GetStyle();
						if (!bNodeOpen) {
							// Draw our own preview of the drop because we want to draw a line either above or below the function
							float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
							const auto upperHalf = ImRect(nodeRect.Min.x, nodeRect.Min.y, nodeRect.Max.x, midPoint);
							const auto lowerHalf = ImRect(nodeRect.Min.x, midPoint, nodeRect.Max.x, nodeRect.Max.y);

							bool bInsertAfter = ImGui::IsMouseHoveringRect(lowerHalf.Min, lowerHalf.Max);

							ImDrawList* drawList = ImGui::GetWindowDrawList();
							auto lineY = bInsertAfter ? nodeRect.Max.y + style.ItemSpacing.y * 0.5f : nodeRect.Min.y - style.ItemSpacing.y * 0.5f;
							drawList->AddLine(ImVec2(nodeRect.Min.x, lineY), ImVec2(nodeRect.Max.x, lineY), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 3.f);

							if (imguiPayload->IsDelivery()) {
								ModRegistry::GetSingleton().QueueJob<Jobs::MoveFunctionJob>(payload.function, payload.functionSet, a_function, a_functionSet, bInsertAfter);
							}
						} else {
							// Draw our own preview of the drop because we want to draw a line above the function if we're hovering over the upper half of the node. Ignore anything below, we have the invisible button for that
							float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
							const auto upperHalf = ImRect(nodeRect.Min.x, nodeRect.Min.y, nodeRect.Max.x, midPoint);

							if (ImGui::IsMouseHoveringRect(upperHalf.Min, upperHalf.Max)) {
								ImDrawList* drawList = ImGui::GetWindowDrawList();
								drawList->AddLine(ImVec2(nodeRect.Min.x, nodeRect.Min.y - style.ItemSpacing.y * 0.5f), ImVec2(nodeRect.Max.x, nodeRect.Min.y - style.ItemSpacing.y * 0.5f), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 3.f);

								if (imguiPayload->IsDelivery()) {
									ModRegistry::GetSingleton().QueueJob<Jobs::MoveFunctionJob>(payload.function, payload.functionSet, a_function, a_functionSet, false);
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
			}

			// Disable checkbox
			ImGui::SameLine();
			if (a_editMode > EditMode::kNone) {
				std::string idString = std::format("{}##bDisabled", reinterpret_cast<uintptr_t>(a_function.get()));
				ImGui::PushID(idString.data());
				bool bEnabled = !a_function->IsDisabled();
				if (ImGui::Checkbox("##toggleFunction", &bEnabled)) {
					a_function->SetDisabled(!bEnabled);
					a_functionSet->SetDirty(true);
					a_bOutSetDirty = true;
				}
				UICommon::AddTooltip("Toggles the function on/off");
				ImGui::PopID();
			}

			// Function name
			ImGui::SameLine();
			if (a_function->IsValid()) {
				auto requiredPluginName = a_function->GetRequiredPluginName();
				if (!requiredPluginName.empty()) {
					UICommon::TextUnformattedColored(UICommon::CUSTOM_CONDITION_COLOR, nodeName.data());
				} else {
					ImGui::TextUnformatted(nodeName.data());
				}
			} else {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, nodeName.data());
			}

			ImVec2 cursorPos = ImGui::GetCursorScreenPos();

			// Function comment
			ImGui::SameLine();
			if (!a_function->GetComment().empty()) {
				std::string comment = a_function->GetComment().c_str();
				float maxCommentWidth = (ImGui::GetWindowContentRegionMax().x * _firstColumnWidthPercent) - ImGui::GetCursorPos().x;
				UICommon::TextUnformattedEllipsisColored(UICommon::COMMENT_COLOR, std::format("{}", comment).data(), nullptr, maxCommentWidth);
			}

			// Right column, argument text
			UICommon::SecondColumn(_firstColumnWidthPercent);
			const auto argument = a_function->GetArgument();
			ImGui::TextUnformatted(argument.data());

			//ImGui::TableSetColumnIndex(0);

			functionRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// Node contents
			if (bNodeOpen) {
				ImGui::Spacing();

				if (a_editMode > EditMode::kNone) {
					// select function type
					//ImGui::SameLine();
					const float functionComboWidth = UICommon::FirstColumnWidth(_firstColumnWidthPercent);
					ImGui::SetNextItemWidth(functionComboWidth);

					const auto& functionInfos = _functionComboFilter.GetFunctionInfos();
					if (functionInfos.empty()) {
						_functionComboFilter.CacheInfos();
					}

					int selectedItem = -1;
					const Info* currentFunctionInfo = nullptr;

					auto it = std::ranges::find_if(functionInfos, [&](const Info& a_functionInfo) {
						return a_functionInfo.name == std::string_view(functionName);
					});

					if (it != functionInfos.end()) {
						selectedItem = static_cast<int>(std::distance(functionInfos.begin(), it));
						currentFunctionInfo = &*it;
					}

					if (_functionComboFilter.ComboFilter("##Function type", selectedItem, functionInfos, currentFunctionInfo, ImGuiComboFlags_HeightLarge, &UIMain::DrawInfoTooltip)) {
						if (selectedItem >= 0 && selectedItem < functionInfos.size() && ModRegistry::GetSingleton().HasFunctionFactory(functionInfos[selectedItem].name)) {
							_lastAddNewFunctionName = functionInfos[selectedItem].name;
							ModRegistry::GetSingleton().QueueJob<Jobs::ReplaceFunctionJob>(a_function, functionInfos[selectedItem].name, a_functionSet);
							a_bOutSetDirty = true;
						}
					}

					// remove function button
					UICommon::SecondColumn(_firstColumnWidthPercent);

					UICommon::ButtonWithConfirmationModal("Delete function"sv, "Are you sure you want to remove the function?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveFunctionJob>(a_function, a_functionSet);
						a_bOutSetDirty = true;
					});
				}

				if (auto numComponents = a_function->GetNumComponents(); numComponents > 0) {
					for (uint32_t i = 0; i < numComponents; i++) {
						auto component = a_function->GetComponent(i);
						const bool bIsMultiFunctionComponent = component->GetType() == Functions::FunctionComponentType::kMulti;
						const bool bIsConditionComponent = component->GetType() == Functions::FunctionComponentType::kCondition;
						if (bIsMultiFunctionComponent) {
							const auto multiFunctionComponent = static_cast<Functions::IMultiFunctionComponent*>(component);
							// draw functions
							if (DrawFunctionSet(multiFunctionComponent->GetFunctions(), a_parentSubMod, a_editMode, Functions::FunctionSetType::kNone, a_refrToEvaluate, true, cursorPos)) {
								a_functionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
							// display component
							if (component->DisplayInUI(a_editMode != EditMode::kNone, _firstColumnWidthPercent)) {
								a_functionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
						} else if (bIsConditionComponent) {
							const auto conditionFunctionComponent = static_cast<Functions::IConditionFunctionComponent*>(component);
							// draw conditions
							if (DrawConditionSet(conditionFunctionComponent->GetConditions(), a_parentSubMod, a_editMode, Conditions::ConditionType::kNormal, a_refrToEvaluate, true, cursorPos)) {
								a_functionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
							ImGui::Separator();
							// draw functions
							if (DrawFunctionSet(conditionFunctionComponent->GetFunctions(), a_parentSubMod, a_editMode, Functions::FunctionSetType::kNone, a_refrToEvaluate, true, cursorPos)) {
								a_functionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
							// display component
							if (component->DisplayInUI(a_editMode != EditMode::kNone, _firstColumnWidthPercent)) {
								a_functionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
						} else {
							ImGui::Spacing();
							// write component name aligned to the right
							const auto componentName = component->GetName();
							UICommon::TextDescriptionRightAligned(componentName.data());
							// show component description on mouseover
							const auto componentDescription = component->GetDescription();
							if (!componentDescription.empty()) {
								UICommon::AddTooltip(componentDescription.data());
							}
							// display component
							if (component->DisplayInUI(a_editMode != EditMode::kNone, _firstColumnWidthPercent)) {
								a_functionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
						}
					}
				}

				// triggers
				if (a_functionSetType == Functions::FunctionSetType::kOnTrigger) {
					std::string triggersLabel = std::format("Triggers##{}", reinterpret_cast<uintptr_t>(a_function.get()));
					bool bIsOpen = ImGui::CollapsingHeader(triggersLabel.data(), ImGuiTreeNodeFlags_DefaultOpen);
					ImGui::SameLine();
					UICommon::HelpMarker("Animation events with an optional payload that will trigger this function.");
					if (bIsOpen) {
						ImGui::Indent();
						uint32_t i = 0;
						a_function->ForEachTrigger([&](const auto& a_trigger) {
							// draw trigger
							ImGui::TextUnformatted(a_trigger.event.data());
							if (a_trigger.payload.length() > 0) {
								ImGui::SameLine(0.f, 0.f);
								ImGui::PushStyleColor(ImGuiCol_Text, UICommon::EVENT_LOG_PAYLOAD_COLOR);
								ImGui::TextUnformatted(".");
								ImGui::SameLine(0.f, 0.f);
								UICommon::TextUnformattedEllipsis(a_trigger.payload.data());
								ImGui::PopStyleColor();
							}

							// remove trigger button
							UICommon::SecondColumn(_firstColumnWidthPercent);
							std::string buttonLabel = std::format("Delete trigger##{}{}", reinterpret_cast<uintptr_t>(a_function.get()), i++);
							if (ImGui::Button(buttonLabel.data())) {
								ModRegistry::GetSingleton().QueueJob<Jobs::RemoveTriggerJob>(a_function, a_functionSet, a_trigger);
								a_functionSet->SetDirty(true);
								a_bOutSetDirty = true;
							}
							return RE::BSVisit::BSVisitControl::kContinue;
						});

						// add new trigger
						if (a_editMode > EditMode::kNone) {
							constexpr auto popupName = "Adding new trigger"sv;
							std::string buttonLabel = std::format("Add new trigger##{}", reinterpret_cast<uintptr_t>(a_function.get()));
							if (ImGui::Button(buttonLabel.data())) {
								const auto popupPos = ImGui::GetCursorScreenPos();
								ImGui::SetNextWindowPos(popupPos);
								ImGui::OpenPopup(popupName.data());
							}

							if (ImGui::BeginPopupModal(popupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
								static std::string eventBuffer;
								static std::string payloadBuffer;
								ImGui::InputTextWithHint("##NewTriggerEvent", "Event name", &eventBuffer, ImGuiInputTextFlags_CharsNoBlank);
								ImGui::SameLine();
								ImGui::TextUnformatted(".");
								ImGui::SameLine();
								ImGui::InputTextWithHint("##NewTriggerPayload", "Payload (optional)", &payloadBuffer, ImGuiInputTextFlags_CharsNoBlank);
								std::string addButtonLabel = std::format("Add trigger##{}", reinterpret_cast<uintptr_t>(a_function.get()));
								ImGui::BeginDisabled(eventBuffer.empty());
								if (ImGui::Button(addButtonLabel.data())) {
									auto trigger = Functions::Trigger(eventBuffer.data(), payloadBuffer.data());
									ModRegistry::GetSingleton().QueueJob<Jobs::AddTriggerJob>(a_function, a_functionSet, trigger);
									a_functionSet->SetDirty(true);
									a_bOutSetDirty = true;
									eventBuffer.clear();
									payloadBuffer.clear();
									ImGui::CloseCurrentPopup();
								}
								ImGui::EndDisabled();
								ImGui::SetItemDefaultFocus();
								ImGui::SameLine();
								if (ImGui::Button("Cancel")) {
									eventBuffer.clear();
									payloadBuffer.clear();
									ImGui::CloseCurrentPopup();
								}
								ImGui::EndPopup();
							}
						}

						ImGui::Unindent();
					}
				}

				ImGui::Spacing();

				ImGui::TreePop();
			}

			if (bStyleVarPushed) {
				ImGui::PopStyleVar();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		auto rectMax = ImGui::GetItemRectMax();

		auto width = ImGui::GetItemRectSize().x;
		auto groupEnd = ImGui::GetCursorPos();
		const ImGuiStyle& style = ImGui::GetStyle();
		ImVec2 invisibleButtonStart = groupEnd;
		invisibleButtonStart.y -= style.ItemSpacing.y;
		ImGui::SetCursorPos(invisibleButtonStart);
		std::string functionInvisibleDragAreaId = std::format("{}functionInvisibleDragArea", reinterpret_cast<uintptr_t>(a_function.get()));
		ImGui::InvisibleButton(functionInvisibleDragAreaId.data(), ImVec2(width, style.ItemSpacing.y));

		// Drag & Drop target - invisible button
		if (a_editMode > EditMode::kNone) {
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload("DND_FUNCTION", ImGuiDragDropFlags_AcceptPeekOnly)) {
					DragFunctionPayload payload = *static_cast<DragFunctionPayload*>(imguiPayload->Data);
					// Draw our own preview of the drop because we want to draw a line below the condition

					ImDrawList* drawList = ImGui::GetWindowDrawList();
					drawList->AddLine(ImVec2(nodeRect.Min.x, rectMax.y + style.ItemSpacing.y * 0.5f), ImVec2(nodeRect.Max.x, rectMax.y + style.ItemSpacing.y * 0.5f), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 3.f);

					if (imguiPayload->IsDelivery()) {
						ModRegistry::GetSingleton().QueueJob<Jobs::MoveFunctionJob>(payload.function, payload.functionSet, a_function, a_functionSet, true);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		ImGui::SetCursorPos(groupEnd);

		//ImGui::EndGroup();

		return functionRect;
	}

	ImRect UIMain::DrawBlankCondition(Conditions::ConditionSet* a_conditionSet, EditMode a_editMode, Conditions::ConditionType a_conditionType)
	{
		ImRect conditionRect;

		const std::string conditionTableId = std::format("{}blankConditionTable", reinterpret_cast<uintptr_t>(a_conditionSet));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		if (ImGui::BeginTable(conditionTableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			const float tooltipWidth = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2;

			const std::string nodeId = std::format("No conditions##{}", reinterpret_cast<uintptr_t>(a_conditionSet));
			UICommon::TreeNodeCollapsedLeaf(nodeId.data(), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);

			if (a_editMode > EditMode::kNone) {
				// Paste condition context menu
				if (ImGui::BeginPopupContextItem()) {
					// paste button
					const bool bPasteEnabled = _conditionCopy && !(a_conditionType == Conditions::ConditionType::kPreset && ConditionContainsPreset(_conditionCopy.get()));
					ImGui::BeginDisabled(!bPasteEnabled);
					if (ImGui::Button("Paste condition below")) {
						auto duplicate = DuplicateCondition(_conditionCopy);
						ModRegistry::GetSingleton().QueueJob<Jobs::InsertConditionJob>(duplicate, a_conditionSet, nullptr);
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();
					// paste tooltip
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
						ImGui::SetNextWindowSize(ImVec2(tooltipWidth, 0));
						ImGui::BeginTooltip();
						bool bDummy = false;
						DrawCondition(_conditionCopy, a_conditionSet, nullptr, EditMode::kNone, Conditions::ConditionType::kNormal, nullptr, bDummy);
						ImGui::EndTooltip();
					}

					ImGui::EndPopup();
				}

				// Drag & Drop target - blank condition set
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload("DND_CONDITION")) {
						DragConditionPayload payload = *static_cast<DragConditionPayload*>(imguiPayload->Data);
						ModRegistry::GetSingleton().QueueJob<Jobs::MoveConditionJob>(payload.condition, payload.conditionSet, nullptr, a_conditionSet, true);
					}
					ImGui::EndDragDropTarget();
				}
			}

			conditionRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		return conditionRect;
	}

	ImRect UIMain::DrawBlankFunction(Functions::FunctionSet* a_functionSet, SubMod* a_parentSubMod, EditMode a_editMode, Functions::FunctionSetType a_functionSetType)
	{
		ImRect functionRect;

		auto id = reinterpret_cast<uintptr_t>(a_parentSubMod) + static_cast<uint8_t>(a_functionSetType) + reinterpret_cast<uintptr_t>(a_functionSet);
		const std::string functionTableId = std::format("{}blankFunctionTable", id);
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		if (ImGui::BeginTable(functionTableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			const float tooltipWidth = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2;

			const std::string nodeId = std::format("No functions##{}", id);
			UICommon::TreeNodeCollapsedLeaf(nodeId.data(), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);

			if (a_editMode > EditMode::kNone) {
				// Paste function context menu
				if (ImGui::BeginPopupContextItem()) {
					// paste button
					const bool bPasteEnabled = _functionCopy != nullptr;
					ImGui::BeginDisabled(!bPasteEnabled);
					if (ImGui::Button("Paste function below")) {
						auto duplicate = DuplicateFunction(_functionCopy);
						if (!a_functionSet) {
							a_functionSet = a_parentSubMod->CreateOrGetFunctionSet(a_functionSetType);
						}
						ModRegistry::GetSingleton().QueueJob<Jobs::InsertFunctionJob>(duplicate, a_functionSet, nullptr);
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();
					// paste tooltip
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
						ImGui::SetNextWindowSize(ImVec2(tooltipWidth, 0));
						ImGui::BeginTooltip();
						bool bDummy = false;
						DrawFunction(_functionCopy, a_functionSet, nullptr, EditMode::kNone, a_functionSetType, nullptr, bDummy);
						ImGui::EndTooltip();
					}

					ImGui::EndPopup();
				}

				// Drag & Drop target - blank function set
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload("DND_FUNCTION")) {
						DragFunctionPayload payload = *static_cast<DragFunctionPayload*>(imguiPayload->Data);
						if (!a_functionSet) {
							a_functionSet = a_parentSubMod->CreateOrGetFunctionSet(a_functionSetType);
						}
						ModRegistry::GetSingleton().QueueJob<Jobs::MoveFunctionJob>(payload.function, payload.functionSet, nullptr, a_functionSet, true);
					}
					ImGui::EndDragDropTarget();
				}
			}

			functionRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		return functionRect;
	}

	bool UIMain::DrawTriggerSet(Triggers::TriggerSet* a_triggerSet, SubMod* a_parentSubMod, EditMode a_editMode, bool a_bDrawLines, const ImVec2& a_drawStartPos, RegisteredMod* a_presetMod)
	{
		if (!a_triggerSet) {
			return false;
		}
		bool bSetDirty = false;
		auto* mod = a_presetMod ? a_presetMod : a_parentSubMod ? a_parentSubMod->GetParentMod() : nullptr;
		const bool bInPreset = a_presetMod != nullptr;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();
		ImVec2 vertLineStart = a_drawStartPos;
		vertLineStart.y += style.FramePadding.x;
		vertLineStart.x -= style.IndentSpacing * 0.6f;
		ImVec2 vertLineEnd = vertLineStart;
		const float tooltipWidth = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2;

		if (!a_triggerSet->IsEmpty()) {
			a_triggerSet->ForEach([&](std::unique_ptr<Triggers::TriggerBase>& a_trigger) {
				// A modifier trigger lists its mod's modifiers and those of the mods the clip requires
				for (std::uint32_t i = 0; i < a_trigger->GetNumComponents(); ++i) {
					if (auto* component = dynamic_cast<Triggers::ModifierTriggerComponent*>(a_trigger->GetComponent(i))) {
						component->mod = mod;
						component->clip = a_parentSubMod;
					}
				}
				const ImRect nodeRect = DrawTrigger(a_trigger, a_triggerSet, a_editMode, bSetDirty, mod, bInPreset);
				if (a_bDrawLines) {
					const float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
					constexpr float horLineLength = 10.f;
					drawList->AddLine(ImVec2(vertLineStart.x, midPoint), ImVec2(vertLineStart.x + horLineLength, midPoint), ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
					vertLineEnd.y = midPoint;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		} else {
			// The blank row: the clip fires from its conditions alone
			const std::string nodeId = std::format("No triggers##{}", reinterpret_cast<uintptr_t>(a_triggerSet));
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			if (ImGui::BeginTable("blankTriggerTable", 1, ImGuiTableFlags_BordersOuter)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				UICommon::TreeNodeCollapsedLeaf(nodeId.data(), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
				ImGui::EndTable();
			}
			ImGui::PopStyleVar();
		}
		if (a_bDrawLines) {
			drawList->AddLine(vertLineStart, vertLineEnd, ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
		}

		if (a_editMode > EditMode::kNone) {
			// Add trigger button: the last type picked, else OnHit
			if (ImGui::Button("Add new trigger")) {
				const bool bLastIsPreset = _lastAddNewTriggerName == "PRESET";
				std::unique_ptr<Triggers::TriggerBase> trigger = Triggers::CreateTrigger(_lastAddNewTriggerName.empty() || (bInPreset && bLastIsPreset) ? "OnHit"sv : std::string_view(_lastAddNewTriggerName));
				if (trigger) {
					a_triggerSet->Add(trigger, true);
					bSetDirty = true;
				}
			}

			// Trigger set functions button
			ImGui::SameLine(0.f, 20.f);
			const auto popupId = std::string("Trigger set functions##") + std::to_string(reinterpret_cast<uintptr_t>(a_triggerSet));
			if (UICommon::PopupToggleButton("Trigger set...", popupId.data())) {
				ImGui::OpenPopup(popupId.data());
			}
			if (ImGui::BeginPopupContextItem(popupId.data())) {
				const auto xButtonSize = ImGui::CalcTextSize("Paste trigger set").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
				ImGui::BeginDisabled(a_triggerSet->IsEmpty() || !a_triggerSet->IsValid());
				if (ImGui::Button("Copy trigger set", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					_triggerSetCopy = Triggers::DuplicateTriggerSet(a_triggerSet);
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(!_triggerSetCopy);
				if (ImGui::Button("Paste trigger set", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					const auto duplicatedSet = Triggers::DuplicateTriggerSet(_triggerSetCopy.get());
					a_triggerSet->Append(duplicatedSet.get());
					bSetDirty = true;
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && _triggerSetCopy) {
					ImGui::SetNextWindowSize(ImVec2(tooltipWidth, 0));
					ImGui::BeginTooltip();
					DrawTriggerSet(_triggerSetCopy.get(), nullptr, EditMode::kNone, false, a_drawStartPos, mod);
					ImGui::EndTooltip();
				}
				ImGui::BeginDisabled(a_triggerSet->IsEmpty());
				UICommon::ButtonWithConfirmationModal(
					"Clear trigger set"sv, "Are you sure you want to clear the trigger set?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ImGui::ClosePopupsExceptModals();
						ModRegistry::GetSingleton().QueueJob<Jobs::ClearTriggerSetJob>(a_triggerSet);
						bSetDirty = true;
					},
					ImVec2(xButtonSize, 0));
				ImGui::EndDisabled();
				ImGui::EndPopup();
			}
		}
		(void)a_parentSubMod;
		return bSetDirty;
	}

	ImRect UIMain::DrawTrigger(std::unique_ptr<Triggers::TriggerBase>& a_trigger, Triggers::TriggerSet* a_triggerSet, EditMode a_editMode, bool& a_bOutSetDirty, RegisteredMod* a_mod, bool a_bInPreset)
	{
		// A PRESET trigger picks from its mod's presets
		if (auto* presetTrigger = dynamic_cast<Triggers::PresetTrigger*>(a_trigger.get())) {
			presetTrigger->preset->mod = a_mod;
		}
		ImRect triggerRect;
		ImRect nodeRect;
		const std::string tableId = std::format("{}triggerTable", reinterpret_cast<uintptr_t>(a_trigger.get()));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		if (ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			bool bStyleVarPushed = false;
			if (a_trigger->IsDisabled()) {
				auto& style = ImGui::GetStyle();
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
				bStyleVarPushed = true;
			}

			bool bNodeOpen = false;
			if (a_editMode > EditMode::kNone || a_trigger->GetNumComponents() > 0) {
				bNodeOpen = ImGui::TreeNodeEx(a_trigger.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
			} else {
				bNodeOpen = UICommon::TreeNodeCollapsedLeaf(a_trigger.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
			}

			// Trigger context menu
			if (a_editMode > EditMode::kNone) {
				if (ImGui::BeginPopupContextItem()) {
					auto& style = ImGui::GetStyle();
					auto xButtonSize = ImGui::CalcTextSize("Delete trigger").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					UICommon::ButtonWithConfirmationModal(
						"Delete trigger"sv, "Are you sure you want to remove the trigger?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveTriggerRowJob>(a_trigger, a_triggerSet);
							a_bOutSetDirty = true;
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}
			}
			DrawInfoTooltip(Info(std::string(a_trigger->GetTypeName()), a_trigger->GetDescription()));
			nodeRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// Disable checkbox
			ImGui::SameLine();
			if (a_editMode > EditMode::kNone) {
				ImGui::PushID(std::format("{}##bDisabled", reinterpret_cast<uintptr_t>(a_trigger.get())).data());
				bool bEnabled = !a_trigger->IsDisabled();
				if (ImGui::Checkbox("##toggleTrigger", &bEnabled)) {
					a_trigger->SetDisabled(!bEnabled);
					a_triggerSet->SetDirty(true);
					a_bOutSetDirty = true;
				}
				UICommon::AddTooltip("Toggles the trigger on/off");
				ImGui::PopID();
			}

			// Trigger name
			ImGui::SameLine();
			const auto typeName = std::string(a_trigger->GetTypeName());
			if (!a_trigger->IsValid()) {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, typeName.data());
			} else {
				ImGui::TextUnformatted(typeName.data());
			}

			// Right column, argument text
			UICommon::SecondColumn(_firstColumnWidthPercent);
			const auto argument = a_trigger->GetArgument();
			ImGui::TextUnformatted(argument.data());
			triggerRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// When it last fired on the reference actor
			if (auto* refr = UIManager::GetSingleton().GetRefrToEvaluate()) {
				if (const auto state = ActorState::Get(refr->GetHandle())) {
					const auto firedAt = state->clips.With([&](const ActorState::ClipState& a_clips) -> std::optional<float> {
						const auto it = a_clips.firedAt.find(a_trigger.get());
						return it != a_clips.firedAt.end() ? std::optional<float>(it->second) : std::nullopt;
					});
					ImGui::SameLine();
					if (firedAt) {
						ImGui::TextDisabled("fired %.1f s ago", Resolve::Now() - *firedAt);
					} else {
						ImGui::TextDisabled("never fired");
					}
				}
			}

			if (bNodeOpen) {
				ImGui::Spacing();
				if (a_editMode > EditMode::kNone) {
					// select trigger type: replaced in place, the components starting over
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					ImGui::PushID(a_trigger.get());
					if (ImGui::BeginCombo("##Trigger type", typeName.data())) {
						for (const auto& name : Triggers::GetTriggerTypeNames()) {
							// A preset cannot hold a preset
							if (a_bInPreset && name == "PRESET") {
								continue;
							}
							const bool bIsCurrent = name == typeName;
							if (ImGui::Selectable(name.data(), bIsCurrent) && !bIsCurrent) {
								if (auto replacement = Triggers::CreateTrigger(name)) {
									_lastAddNewTriggerName = name;
									if (auto* presetTrigger = dynamic_cast<Triggers::PresetTrigger*>(replacement.get())) {
										presetTrigger->preset->mod = a_mod;
									}
									a_trigger = std::move(replacement);
									a_triggerSet->SetDirty(true);
									a_bOutSetDirty = true;
								}
							}
							if (bIsCurrent) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
					ImGui::PopID();

					// remove trigger button
					UICommon::SecondColumn(_firstColumnWidthPercent);
					UICommon::ButtonWithConfirmationModal("Delete trigger"sv, "Are you sure you want to remove the trigger?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveTriggerRowJob>(a_trigger, a_triggerSet);
						a_bOutSetDirty = true;
					});
				}

				if (a_trigger) {
					const auto numComponents = a_trigger->GetNumComponents();
					for (uint32_t i = 0; i < numComponents; i++) {
						auto component = a_trigger->GetComponent(i);
						ImGui::Separator();
						const auto componentName = component->GetName();
						UICommon::TextDescriptionRightAligned(componentName.data());
						const auto componentDescription = component->GetDescription();
						if (!componentDescription.empty()) {
							UICommon::AddTooltip(componentDescription.data());
						}
						if (component->DisplayInUI(a_editMode != EditMode::kNone, _firstColumnWidthPercent)) {
							a_triggerSet->SetDirty(true);
							a_bOutSetDirty = true;
						}
					}
				}
				ImGui::Spacing();
				ImGui::TreePop();
			}

			if (bStyleVarPushed) {
				ImGui::PopStyleVar();
			}
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		return nodeRect.Min.x == 0.f && nodeRect.Max.x == 0.f ? triggerRect : nodeRect;
	}

	void UIMain::DrawKeyframeBody(Clip* a_clip, ClipVariant& a_variant, Keyframe& a_keyframe)
	{
		const bool bEditable = _editMode != EditMode::kNone;
		const ImGuiStyle& style = ImGui::GetStyle();
		Keyframe* keyframe = &a_keyframe;

		// Time, from the clip's start; the delete beside it
		if (bEditable) {
			ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
			float time = keyframe->time;
			if (ImGui::InputFloat("##time", &time, 0.05f, 0.5f, "%.2f s")) {
				keyframe->time = (std::max)(time, 0.f);
				a_clip->SetDirty(true);
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				a_clip->SortKeyframes(a_variant);
			}
			UICommon::AddTooltip("When the frame is reached, from the clip's start. The frames are kept in time order.");
			UICommon::SecondColumn(_firstColumnWidthPercent);
			UICommon::ButtonWithConfirmationModal("Delete frame"sv, "Are you sure you want to remove this keyframe?\nThis operation cannot be undone!\n\n"sv, [&]() {
				ModRegistry::GetSingleton().QueueJob<Jobs::RemoveKeyframeJob>(a_clip, &a_variant, keyframe);
			});
		}

		// Hold: the values this frame sets stay set for so long, and no frame within that time can set them;
		// the run itself goes on to the next frame
		if (bEditable) {
			if (ImGui::Checkbox("##hold", &keyframe->bHold)) {
				a_clip->SetDirty(true);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!keyframe->bHold);
			ImGui::SetNextItemWidth(200.f);
			if (ImGui::SliderFloat("Hold", &keyframe->holdTime, 0.f, 10.f, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
				a_clip->SetDirty(true);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			UICommon::HelpMarker("The values this frame sets are held for this long, and no later frame within that time can set them. The run does not wait: a frame setting other channels can follow at once.");
		} else if (keyframe->bHold) {
			ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(200.f);
			ImGui::SliderFloat("Hold", &keyframe->holdTime, 0.f, 10.f, "%.2f s");
			ImGui::EndDisabled();
		}

		// Channels: a row per claim, set or not, with its state at this frame
		const bool bChannelsOpen = ImGui::CollapsingHeader(std::format("Channels##{}frameChannels", reinterpret_cast<uintptr_t>(keyframe)).data(), ImGuiTreeNodeFlags_DefaultOpen);
		ImGui::SameLine();
		UICommon::HelpMarker("A row for every claimed channel. Checked keys the channel on this frame at the value the run has there; an unchecked row shows that value greyed. A row held by an earlier frame's hold cannot be set here. Add or remove channels in Claimed Channels.");
		if (bChannelsOpen) {
			ImGui::Indent();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 vertLineStart = ImGui::GetCursorScreenPos();
			vertLineStart.x += style.FramePadding.x - style.IndentSpacing * 0.6f;
			vertLineStart.y += style.FramePadding.y + style.FramePadding.x;
			ImVec2 vertLineEnd = vertLineStart;
			bool bSetDirty = false;
			std::vector<std::unique_ptr<Channels::ChannelBase>> fresh;  // rests kept alive for the frame
			std::vector<CategorisedChannel> list;
			{
				list = ByCategory(keyframe->channels.get(), _channelComboFilter);
				std::erase_if(list, [](const CategorisedChannel& a_entry) { return (*a_entry.channel)->IsGroup(); });
			}
			for (std::size_t i = 0; i < list.size(); ++i) {
				auto& row = *list[i].channel;
				DrawCategoryHeading(list, i);
				const auto id = static_cast<Channels::Channel*>(row.get())->GetId();
				KeyRowState state;
				{
					a_clip->GetChannelSet()->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_claim) {
						if (!a_claim->IsGroup() && static_cast<Channels::Channel*>(a_claim.get())->GetId() == id) {
							state.bIgnored = a_claim->IsDisabled();
							return RE::BSVisit::BSVisitControl::kStop;
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
				{
					state.heldBy = a_clip->HeldBy(a_variant, *keyframe, id);
				}
				Clip::SetRow before;
				{
					before = a_clip->LastSetBefore(a_variant, *keyframe, id);
				}
				state.computedFrom = before.row;
				if (before.row) {
					state.computed = before.row->GetArgument();
					// A number on its way: where the player has it at this time, from the rest as the timeline draws it
					if (dynamic_cast<const Channels::NumberChannel*>(before.row)) {
						const auto entry = Apply::Entries::GetSingleton().Get(id);
						const float rest = entry.neutral < entry.low ? entry.start : entry.neutral;
						const auto [value, phase] = ClipPlayer::NumberAt(a_variant, id, keyframe->time, rest, rest, nullptr);
						state.bMoving = phase == ClipPlayer::Phase::kMoving || phase == ClipPlayer::Phase::kExiting;
						state.computedNumber = value;
						state.computed = std::format("{:.3}{}", value, state.bMoving ? " (moving)" : "");
					}
				} else {
					if (auto rest = Channels::CreateChannel(id)) {
						state.computed = rest->GetArgument() + " (rest)";
						fresh.push_back(std::move(rest));
					}
				}
				ImRect nodeRect;
				{
					nodeRect = DrawChannel(row, keyframe->channels.get(), a_clip, _editMode, bSetDirty, ChannelDrawMode::kLocked, &state);
				}
				const float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
				constexpr float horLineLength = 10.f;
				drawList->AddLine(ImVec2(vertLineStart.x, midPoint), ImVec2(vertLineStart.x + horLineLength, midPoint), ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
				vertLineEnd.y = midPoint;
			}
			drawList->AddLine(vertLineStart, vertLineEnd, ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
			if (bSetDirty) {
				a_clip->SetDirty(true);
			}

			// The values copied, pasted onto the rows of the same channels, or every row unset
			if (bEditable) {
				const auto popupId = std::string("Keyframe channels##") + std::to_string(reinterpret_cast<uintptr_t>(keyframe));
				if (UICommon::PopupToggleButton("Channels...", popupId.data())) {
					ImGui::OpenPopup(popupId.data());
				}
				if (ImGui::BeginPopupContextItem(popupId.data())) {
					const auto xButtonSize = ImGui::CalcTextSize("Paste channel values").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					if (ImGui::Button("Copy channel values", ImVec2(xButtonSize, 0))) {
						ImGui::CloseCurrentPopup();
						_channelSetCopy = Channels::DuplicateChannelSet(keyframe->channels.get());
					}
					ImGui::BeginDisabled(!_channelSetCopy);
					if (ImGui::Button("Paste channel values", ImVec2(xButtonSize, 0))) {
						ImGui::CloseCurrentPopup();
						rapidjson::Document doc(rapidjson::kObjectType);
						const rapidjson::Value copied = _channelSetCopy->Serialize(doc.GetAllocator());
						for (const auto& value : copied.GetArray()) {
							auto pasted = Channels::CreateChannelFromJson(value);
							if (!pasted || pasted->IsGroup()) {
								continue;
							}
							const auto id = static_cast<Channels::Channel*>(pasted.get())->GetId();
							keyframe->channels->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_row) {
								if (!a_row->IsGroup() && static_cast<Channels::Channel*>(a_row.get())->GetId() == id) {
									a_row->Parse(value);
									a_row->SetDisabled(pasted->IsDisabled());
									return RE::BSVisit::BSVisitControl::kStop;
								}
								return RE::BSVisit::BSVisitControl::kContinue;
							});
						}
						a_clip->SetDirty(true);
					}
					ImGui::EndDisabled();
					UICommon::ButtonWithConfirmationModal(
						"Unset all"sv, "Unset every row of this frame?\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							keyframe->channels->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_row) {
								a_row->SetDisabled(true);
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							a_clip->SetDirty(true);
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}
			}
			ImGui::Unindent();
		}

		// Functions: run once as the clock passes the frame
		if (bEditable || (keyframe->functions && !keyframe->functions->IsEmpty())) {
			const bool bFunctionsOpen = ImGui::CollapsingHeader(std::format("Functions##{}frameFunctions", reinterpret_cast<uintptr_t>(keyframe)).data(), keyframe->functions->IsEmpty() ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::SameLine();
			UICommon::HelpMarker("Run once per run of the clip, the moment the clock passes the frame's time.");
			if (bFunctionsOpen) {
			ImGui::Indent();
			ImVec2 pos = ImGui::GetCursorScreenPos();
			pos.x += style.FramePadding.x;
			pos.y += style.FramePadding.y;
			ImGui::PushID(keyframe->functions.get());
			if (DrawFunctionSet(keyframe->functions.get(), a_clip, _editMode, Functions::FunctionSetType::kOnActivate, UIManager::GetSingleton().GetRefrToEvaluate(), true, pos)) {
				a_clip->SetDirty(true);
			}
			ImGui::PopID();
			ImGui::Unindent();
			}
		}
	}

	void UIMain::DrawKeyframes(Clip* a_clip, ClipVariant& a_variant, const ImVec2& a_drawStartPos)
	{
		const bool bEditable = _editMode != EditMode::kNone;
		const ImGuiStyle& style = ImGui::GetStyle();
		(void)a_drawStartPos;

		for (auto& keyframe : a_variant.keyframes) {
			ImGui::PushID(keyframe.get());
			// The frame as a group is drawn: a bordered block, the node with the time for a name
			const std::string tableId = std::format("{}frameTable", reinterpret_cast<uintptr_t>(keyframe.get()));
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			if (ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				const bool bNodeOpen = ImGui::TreeNodeEx(keyframe.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");
				ImGui::SameLine();
				ImGui::TextUnformatted(std::format("{:.2f} s Frame", keyframe->time).data());
				UICommon::SecondColumn(_firstColumnWidthPercent);
				std::size_t set = 0;
				std::size_t rows = 0;
				keyframe->channels->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_row) {
					++rows;
					set += a_row->IsDisabled() ? 0 : 1;
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				ImGui::TextUnformatted(std::format("{} of {} set{}", set, rows, keyframe->bHold ? std::format(", held {:.2f} s", keyframe->holdTime) : "").data());
				{
					const auto variant = static_cast<std::uint16_t>(&a_variant - a_clip->GetTracks().data());
					DrawPreviewButton(keyframe.get(), [a_clip, variant, frame = keyframe.get()](RE::TESObjectREFR* a_refr) { Preview::StartFrame(a_refr, a_clip, variant, frame); });
				}

				if (bNodeOpen) {
					ImGui::Spacing();
					DrawKeyframeBody(a_clip, a_variant, *keyframe);
					ImGui::Spacing();
					ImGui::TreePop();
				}
				ImGui::EndTable();
			}
			ImGui::PopStyleVar();
			ImGui::PopID();
		}

		if (bEditable) {
			// Add keyframe: half a second after the last
			if (ImGui::Button("Add keyframe")) {
				const float last = a_variant.keyframes.empty() ? -0.5f : a_variant.keyframes.back()->time;
				a_clip->AddKeyframe(a_variant, last + 0.5f);
				a_clip->SetDirty(true);
			}

			// Keyframes functions button: copied and pasted as JSON, the rows landing on the claims of the same names
			ImGui::SameLine(0.f, 20.f);
			const auto popupId = std::string("Keyframes functions##") + std::to_string(reinterpret_cast<uintptr_t>(&a_variant));
			if (UICommon::PopupToggleButton("Keyframes...", popupId.data())) {
				ImGui::OpenPopup(popupId.data());
			}
			if (ImGui::BeginPopupContextItem(popupId.data())) {
				const auto xButtonSize = ImGui::CalcTextSize("Paste keyframes").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
				ImGui::BeginDisabled(a_variant.keyframes.empty());
				if (ImGui::Button("Copy keyframes", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					rapidjson::Document doc(rapidjson::kArrayType);
					for (const auto& keyframe : a_variant.keyframes) {
						doc.PushBack(keyframe->Serialize(doc.GetAllocator()), doc.GetAllocator());
					}
					rapidjson::StringBuffer buffer;
					rapidjson::Writer writer(buffer);
					doc.Accept(writer);
					_keyframesCopy = buffer.GetString();
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(_keyframesCopy.empty());
				if (ImGui::Button("Paste keyframes", ImVec2(xButtonSize, 0))) {
					ImGui::CloseCurrentPopup();
					rapidjson::Document doc;
					doc.Parse(_keyframesCopy.data());
					if (doc.IsArray()) {
						for (const auto& value : doc.GetArray()) {
							a_clip->AddKeyframeFromJson(a_variant, value);
						}
						a_clip->SetDirty(true);
					}
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(a_variant.keyframes.empty());
				UICommon::ButtonWithConfirmationModal(
					"Clear keyframes"sv, "Are you sure you want to remove every keyframe?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ImGui::ClosePopupsExceptModals();
						for (const auto& keyframe : a_variant.keyframes) {
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveKeyframeJob>(a_clip, &a_variant, keyframe.get());
						}
					},
					ImVec2(xButtonSize, 0));
				ImGui::EndDisabled();
				ImGui::EndPopup();
			}
		}
	}

	void UIMain::DrawClipVariants(RegisteredMod* a_registeredMod, Clip* a_clip)
	{
		const bool bEditable = _editMode != EditMode::kNone;
		auto& variants = a_clip->GetVariants();
		auto& tracks = a_clip->GetTracks();
		const auto variantMode = variants.GetVariantMode();
		const ImGuiStyle& style = ImGui::GetStyle();
		(void)a_registeredMod;

		// The mode, as the animation tab has it
		if (bEditable) {
			const std::string label = "Variant Mode##" + std::to_string(reinterpret_cast<std::uintptr_t>(&variants)) + "variantMode";
			const std::string current = variantMode == VariantMode::kRandom ? "Random" : "Sequential";
			int tempVariantMode = static_cast<int>(variantMode);
			ImGui::SetNextItemWidth(200.f);
			if (ImGui::SliderInt(label.data(), &tempVariantMode, 0, 1, current.data(), ImGuiSliderFlags_NoInput)) {
				variants.SetVariantMode(static_cast<VariantMode>(tempVariantMode));
				a_clip->SetDirty(true);
			}
			ImGui::SameLine();
			UICommon::HelpMarker("Random: one drawn by weight each time the clip fires. Sequential: the next in order each time.");
			bool tempReset = variants.ShouldResetRandomOnLoopOrEcho();
			if (ImGui::Checkbox("Reset random on loop or echo", &tempReset)) {
				variants.SetShouldResetRandomOnLoopOrEcho(tempReset);
				a_clip->SetDirty(true);
			}
			ImGui::SameLine();
			UICommon::HelpMarker("If enabled, a looping clip draws again on each loop rather than keeping the variant it started with.");
		} else {
			UICommon::TextUnformattedDisabled("Variant Mode:");
			ImGui::SameLine();
			ImGui::TextUnformatted(variantMode == VariantMode::kRandom ? "Random" : "Sequential");
		}

		// One collapsible per variant, its row as the animation tab's: toggle, name, weight or order, play once
		for (uint16_t index = 0; index < tracks.size(); ++index) {
			auto* variant = variants.GetVariant(index);
			auto& track = tracks[index];
			if (!variant) {
				continue;
			}
			ImGui::PushID(&track);
			const std::string tableId = std::format("{}variantTable", reinterpret_cast<uintptr_t>(&track));
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			if (ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				bool bStyleVarPushed = false;
				if (variant->IsDisabled()) {
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
					bStyleVarPushed = true;
				}
				const bool bNodeOpen = ImGui::TreeNodeEx(&track, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

				// context menu: delete, never the last one
				if (bEditable && tracks.size() > 1) {
					if (ImGui::BeginPopupContextItem()) {
						const auto xButtonSize = ImGui::CalcTextSize("Delete variant").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
						UICommon::ButtonWithConfirmationModal(
							"Delete variant"sv, "Are you sure you want to remove this variant and its keyframes?\nThis operation cannot be undone!\n\n"sv, [&]() {
								ImGui::ClosePopupsExceptModals();
								ModRegistry::GetSingleton().QueueJob<Jobs::RemoveClipVariantJob>(a_clip, index);
							},
							ImVec2(xButtonSize, 0));
						ImGui::EndPopup();
					}
				}

				ImGui::SameLine();
				if (bEditable) {
					bool bEnabled = !variant->IsDisabled();
					if (ImGui::Checkbox("##toggleVariant", &bEnabled)) {
						variant->SetDisabled(!bEnabled);
						variants.UpdateVariantCache();
						a_clip->SetDirty(true);
					}
					UICommon::AddTooltip("Toggles the variant on/off");
					ImGui::SameLine();
				}

				// The order with its arrows, or the weight, then play once; the variant's number after them
				if (variantMode == VariantMode::kSequential || variant->ShouldPlayOnce()) {
					if (bEditable) {
						ImGui::BeginDisabled(index == 0);
						if (ImGui::ArrowButton("Move variant up", ImGuiDir_Up)) {
							variants.SwapVariants(index, index - 1);
							std::swap(tracks[index], tracks[index - 1]);
							a_clip->SetDirty(true);
						}
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::BeginDisabled(index + 1 >= tracks.size());
						if (ImGui::ArrowButton("Move variant down", ImGuiDir_Down)) {
							variants.SwapVariants(index, index + 1);
							std::swap(tracks[index], tracks[index + 1]);
							a_clip->SetDirty(true);
						}
						ImGui::EndDisabled();
					}
				} else if (variantMode == VariantMode::kRandom) {
					float weight = variant->GetWeight();
					ImGui::SetNextItemWidth(100.f);
					if (bEditable) {
						if (ImGui::SliderFloat("Weight", &weight, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
							variant->SetWeight(weight);
							variants.UpdateVariantCache();
							a_clip->SetDirty(true);
						}
					} else {
						ImGui::TextUnformatted(std::format("Weight: {:.2f}", weight).data());
					}
				}
				ImGui::SameLine();
				if (bEditable) {
					bool tempPlayOnce = variant->ShouldPlayOnce();
					if (ImGui::Checkbox(variantMode == VariantMode::kRandom ? "Play first and once" : "Play once", &tempPlayOnce)) {
						variant->SetPlayOnce(tempPlayOnce);
						variants.UpdateVariantCache();
						a_clip->SetDirty(true);
					}
				} else if (variant->ShouldPlayOnce()) {
					ImGui::TextUnformatted("[Play once]");
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(std::format("#{}", index + 1).data());
				DrawTimelineButton(a_clip, index, true);
				DrawPreviewButton(&track, [a_clip, index](RE::TESObjectREFR* a_refr) { Preview::StartClip(a_refr, a_clip, index); });

				if (bNodeOpen) {
					ImGui::Spacing();
					ImGui::Indent();
					ImVec2 pos = ImGui::GetCursorScreenPos();
					pos.x += style.FramePadding.x;
					pos.y += style.FramePadding.y;
					DrawKeyframes(a_clip, track, pos);
					ImGui::Unindent();
					ImGui::Spacing();
					ImGui::TreePop();
				}

				if (bStyleVarPushed) {
					ImGui::PopStyleVar();
				}
				ImGui::EndTable();
			}
			ImGui::PopStyleVar();
			ImGui::PopID();
		}

		if (bEditable) {
			// A variant is known by its number, added at once
			if (ImGui::Button("Add variant")) {
				a_clip->AddVariant(std::format("Variant {}", a_clip->GetTracks().size() + 1));
				a_clip->SetDirty(true);
			}
		}
	}

	void UIMain::DrawPreviewButton(const void* a_item, const std::function<void(RE::TESObjectREFR*)>& a_start, bool a_bInHeader)
	{
		auto* refr = UIManager::GetSingleton().GetRefrToEvaluate();
		if (!refr) {
			return;
		}
		const bool bPreviewing = Preview::IsPreviewing(a_item);
		const char* label = bPreviewing ? "Stop" : "Preview";
		const auto& style = ImGui::GetStyle();
		if (a_bInHeader) {
			ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(label).x - style.FramePadding.x * 2 - style.ItemSpacing.x);
		}
		ImGui::PushID(a_item);
		if (a_bInHeader ? ImGui::SmallButton(label) : ImGui::Button(label)) {
			if (bPreviewing) {
				Preview::Stop();
			} else {
				a_start(refr);
			}
		}
		ImGui::PopID();
		UICommon::AddTooltip(bPreviewing ? "Stop the preview." : "Preview on the selected reference, above every modifier and clip. Clear channels in the settings decides whether the rest of the actor resolves as usual underneath. Holding a value in the Actor window stops it.");
	}

	void UIMain::DrawPatches()
	{
		ImGui::TextDisabled("Fixes for where the engine or another mod does not fit what a mod drives. Each applies to the actors a mod is driving.");
		ImGui::Spacing();

		// One patch drawn as a skin set is: the node, its toggle, its name and its status on the right; open, its description, its rows and a reset
		const auto drawPatch = [&](const char* a_id, const char* a_name, bool& a_bEnabled, bool& a_bChosen, bool a_bFound, const char* a_found, const char* a_notFound, const char* a_description, const std::function<void()>& a_rows, const std::function<void()>& a_reset) {
			ImGui::PushID(a_id);
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			if (ImGui::BeginTable("patchTable", 1, ImGuiTableFlags_BordersOuter)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				bool bStyleVarPushed = false;
				if (!a_bEnabled) {
					auto& style = ImGui::GetStyle();
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
					bStyleVarPushed = true;
				}

				const bool bNodeOpen = ImGui::TreeNodeEx("patch", ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

				ImGui::SameLine();
				if (ImGui::Checkbox("##togglePatch", &a_bEnabled)) {
					a_bChosen = true;
					Settings::WriteSettings();
				}
				UICommon::AddTooltip("Toggles the patch on/off. Your choice is kept over what the load order says.");

				ImGui::SameLine();
				ImGui::TextUnformatted(a_name);

				// right column, where it stands
				UICommon::SecondColumn(_firstColumnWidthPercent);
				if (a_bChosen) {
					ImGui::TextUnformatted(a_bEnabled ? "On by your choice" : "Off by your choice");
				} else if (a_bFound) {
					UICommon::TextUnformattedColored(UICommon::SUCCESS_COLOR, a_found);
				} else {
					UICommon::TextUnformattedDisabled(a_notFound);
				}

				if (bNodeOpen) {
					ImGui::Spacing();
					UICommon::TextUnformattedWrapped(a_description);
					ImGui::Spacing();
					if (a_rows) {
						a_rows();
					}

					// back to what the load order says and the values it shipped with
					ImGui::Spacing();
					if (ImGui::Button("Reset to defaults")) {
						a_bChosen = false;
						a_bEnabled = a_bFound;
						if (a_reset) {
							a_reset();
						}
						Settings::WriteSettings();
					}
					ImGui::Spacing();
					ImGui::TreePop();
				}

				if (bStyleVarPushed) {
					ImGui::PopStyleVar();
				}

				ImGui::EndTable();
			}
			ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding
			ImGui::PopID();
		};

		// One row per weight: the slider in the first column, its name on the right
		const auto weight = [&](const char* a_label, const char* a_id, float& a_value, float a_max, const char* a_help) {
			ImGui::Separator();
			ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
			if (ImGui::SliderFloat(a_id, &a_value, 0.f, a_max, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
				Settings::WriteSettings();
			}
			UICommon::SecondColumn(_firstColumnWidthPercent);
			ImGui::TextUnformatted(a_label);
			ImGui::SameLine();
			UICommon::HelpMarker(a_help);
		};

		drawPatch("ubeEyes", "UBE: eyes turn by their rotation morphs", Settings::bPatchUbeEyeRotation, Settings::bPatchUbeEyeRotationChosen, Settings::bUbeInstalled, "UBE_AllRace.esp found", "UBE_AllRace.esp not found",
			"UBE's eyes barely follow the engine's Look morphs: the eye TRI the engine loads moves them about half as far as UBE's RaceMenu extension TRI does. With this on, every Look held on a UBE head, from a channel, a slider or the gaze, also moves the extension's own Look morph at its weight below, and sideways UBE's Left/Right_Eye_Rotation_In/Out turn as well, at the rotation strength. On every driven actor whose head has the rotation morphs; put back like every other morph when the actor is let go. On by itself when UBE_AllRace.esp is in the load order, until you toggle it.",
			[&]() {
				constexpr auto lookHelp = "The Look morph's weight at a full look that way; 1 is the morph as RaceMenu's slider has it.";
				weight("Left", "##ubeEyesLeft", Settings::fPatchUbeEyeLeft, 4.f, lookHelp);
				weight("Right", "##ubeEyesRight", Settings::fPatchUbeEyeRight, 4.f, lookHelp);
				weight("Up", "##ubeEyesUp", Settings::fPatchUbeEyeUp, 4.f, lookHelp);
				weight("Down", "##ubeEyesDown", Settings::fPatchUbeEyeDown, 4.f, lookHelp);
				weight("Rotation strength", "##ubeEyesRotation", Settings::fPatchUbeEyeRotationStrength, 1.f, "The eye rotation pair's weight at a full sideways look; 1 is UBE's full rotation.");
				ImGui::Separator();
			},
			[]() {
				Settings::fPatchUbeEyeLeft = 0.7f;
				Settings::fPatchUbeEyeRight = 0.7f;
				Settings::fPatchUbeEyeUp = 0.8f;
				Settings::fPatchUbeEyeDown = 2.f;
				Settings::fPatchUbeEyeRotationStrength = 0.05f;
			});

		drawPatch("smpBody", "SMP: collision follows the body's morphs", Settings::bPatchSmpBodyCollision, Settings::bPatchSmpBodyCollisionChosen, Settings::bSmpInstalled, "hdtSMP64.dll found", "hdtSMP64.dll not found",
			"SMP builds its collision from the body mesh once, when the actor is dressed, and never sees a body morph or a body preset. With this on, the body SMP collides with follows what the modifiers make of it, preset included, and SMP rebuilds the actor's physics each time that changes, keeping its bones where they are. It waits for every modifier to finish its transition first; clips and the Actor window's holds are left out, so a clip's passing shape never moves the collision. On by itself when FSMP is loaded, until you toggle it.",
			nullptr, nullptr);
	}

	bool UIMain::DrawSkinSet(RegisteredMod* a_registeredMod, Skins::Set* a_set, bool& a_bOutWasRenamed)
	{
		bool bSetDirty = false;
		const bool bEditable = _editMode != EditMode::kNone;

		// ---- the set, drawn as an overlay is: the node, its toggle, its name and what it states on the right ----
		const std::string tableId = std::format("{}skinSetTable", reinterpret_cast<uintptr_t>(a_set));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		if (ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			bool bStyleVarPushed = false;
			if (a_set->IsDisabled()) {
				auto& style = ImGui::GetStyle();
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
				bStyleVarPushed = true;
			}

			const bool bNodeOpen = ImGui::TreeNodeEx(a_set, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

			// context menu
			if (bEditable) {
				if (ImGui::BeginPopupContextItem()) {
					const std::string buttonText = "Delete skin set";
					const auto& style = ImGui::GetStyle();
					const auto xButtonSize = ImGui::CalcTextSize(buttonText.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					UICommon::ButtonWithConfirmationModal(
						buttonText, "Are you sure you want to remove this skin set?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveSkinSetJob>(a_registeredMod, a_set->GetName());
							bSetDirty = true;
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}
			}

			// Disable checkbox
			ImGui::SameLine();
			if (bEditable) {
				std::string idString = std::format("{}##bDisabled", reinterpret_cast<uintptr_t>(a_set));
				ImGui::PushID(idString.data());
				bool bEnabled = !a_set->IsDisabled();
				if (ImGui::Checkbox("##toggleSkinSet", &bEnabled)) {
					a_set->SetDisabled(!bEnabled);
					bSetDirty = true;
				}
				UICommon::AddTooltip("Toggles the skin set on/off");
				ImGui::PopID();
			}

			// set name
			ImGui::SameLine();
			if (a_set->IsValid()) {
				ImGui::TextUnformatted(a_set->GetName().data());
			} else {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_set->GetName().data());
			}

			// right column, what it states
			UICommon::SecondColumn(_firstColumnWidthPercent);
			ImGui::TextUnformatted(a_set->StatedText().data());

			if (bNodeOpen) {
				ImGui::Spacing();
				// rename / delete skin set
				if (bEditable) {
					const std::string nameId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_set)) + "name";
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					std::string tempName(a_set->GetName());
					if (ImGui::InputText(nameId.data(), &tempName, ImGuiInputTextFlags_EnterReturnsTrue)) {
						if (tempName.size() > 2 && !a_registeredMod->HasSkinSet(tempName)) {
							a_set->SetName(tempName);
							a_bOutWasRenamed = true;
							bSetDirty = true;
						}
					}

					UICommon::SecondColumn(_firstColumnWidthPercent);

					UICommon::ButtonWithConfirmationModal("Delete skin set"sv, "Are you sure you want to remove this skin set?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveSkinSetJob>(a_registeredMod, a_set->GetName());
						bSetDirty = true;
					});

					ImGui::Spacing();
				}

				// description
				const std::string descriptionId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_set)) + "description";
				if (bEditable) {
					std::string tempDescription(a_set->GetDescription());
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					if (ImGui::InputText(descriptionId.data(), &tempDescription)) {
						a_set->SetDescription(tempDescription);
						bSetDirty = true;
					}
					UICommon::SecondColumn(_firstColumnWidthPercent);
					UICommon::TextUnformattedDisabled("Skin set description");
					ImGui::Spacing();
				} else if (!a_set->GetDescription().empty()) {
					UICommon::TextUnformattedWrapped(a_set->GetDescription().data());
					ImGui::Spacing();
				}

				// Whether a map is on disk, loose or in an archive, asked once per path
				static std::unordered_map<std::string, bool> onDisk;
				const auto found = [&](const std::string& a_path) {
					auto it = onDisk.find(a_path);
					if (it == onDisk.end()) {
						std::string full = a_path;
						std::ranges::replace(full, '/', '\\');
						if (full.size() <= 9 || _strnicmp(full.c_str(), "textures\\", 9) != 0) {
							full = "textures\\" + full;
						}
						it = onDisk.emplace(a_path, RE::BSResourceNiBinaryStream(full).good()).first;
					}
					return it->second;
				};

				// One component per part and map, as an overlay's layouts are drawn: its name on the right, the
				// enabling checkbox and, checked, the path in the first column, answered in the second by its file
				// name or that it is not on disk
				for (std::uint8_t p = 0; p < static_cast<std::uint8_t>(Scan::SkinPart::kTotal); ++p) {
					const auto part = static_cast<Scan::SkinPart>(p);
					for (std::uint8_t m = 0; m < static_cast<std::uint8_t>(Skins::Map::kTotal); ++m) {
						const auto map = static_cast<Skins::Map>(m);
						auto& texture = a_set->GetTextures(part).Get(map);
						if (!bEditable && !texture.bEnabled) {
							continue;
						}
						const std::string label = std::format("{} {}", Scan::SkinPartName(part), Skins::MapName(map));
						ImGui::PushID(label.data());
						ImGui::Separator();
						UICommon::TextDescriptionRightAligned(label.data());
						if (bEditable) {
							if (ImGui::Checkbox("##enabled", &texture.bEnabled)) {
								bSetDirty = true;
							}
							ImGui::SameLine();
							ImGui::TextUnformatted(label.data());
						}
						if (texture.bEnabled) {
							if (bEditable) {
								const char* hint = map == Skins::Map::kDiffuse ? (part == Scan::SkinPart::kHead ? "Diffuse, kept: the face draws its bake..." : "Diffuse, relative to Data/Textures...") :
								                   map == Skins::Map::kNormal   ? "Normal map (_msn)..." :
								                   map == Skins::Map::kSpecular ? "Specular (_s)..." :
								                                                  "Subsurface (_sk)...";
								ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
								if (ImGui::InputTextWithHint("##path", hint, &texture.path)) {
									bSetDirty = true;
								}
							} else {
								ImGui::TextUnformatted(texture.path.empty() ? "(no texture)" : texture.path.data());
							}
							if (!texture.path.empty()) {
								UICommon::SecondColumn(_firstColumnWidthPercent);
								ImGui::TextUnformatted(found(texture.path) ? std::filesystem::path(texture.path).filename().string().data() : "(Not found)");
							}
						}
						ImGui::PopID();
					}
				}

				ImGui::Spacing();
				ImGui::TreePop();
			}

			if (bStyleVarPushed) {
				ImGui::PopStyleVar();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		return bSetDirty;
	}

	bool UIMain::DrawOverlaySlot(RegisteredMod* a_registeredMod, Overlays::Slot* a_slot, bool& a_bOutWasRenamed, RegisteredMod* a_from)
	{
		bool bSetDirty = false;
		// A required mod's slot is its own to name, place and fill; this mod only adds overlays to it
		const bool bLocked = a_from != nullptr;
		const bool bEditable = _editMode != EditMode::kNone && !bLocked;
		auto* ownAdditions = bLocked ? a_registeredMod->GetOverlaySlot(a_slot->GetName()) : nullptr;
		const auto part = a_slot->GetPart();
		const auto layouts = Overlays::LayoutsOf(part);

		// One overlay of the pool, drawn as a condition is: the node, its toggle, its name and its layouts on the
		// right; open, the rename and delete row, then a component per layout
		const auto drawOverlay = [&](Overlays::Item& a_item, std::string& a_outRemove, bool a_bEditable) -> ImRect {
			ImRect overlayRect;
			const std::string tableId = std::format("{}overlayTable", reinterpret_cast<uintptr_t>(&a_item));
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			if (ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				bool bStyleVarPushed = false;
				if (a_item.bDisabled) {
					auto& style = ImGui::GetStyle();
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
					bStyleVarPushed = true;
				}

				const bool bNodeOpen = ImGui::TreeNodeEx(&a_item, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

				// Overlay context menu
				if (a_bEditable) {
					if (ImGui::BeginPopupContextItem()) {
						auto& style = ImGui::GetStyle();
						const auto xButtonSize = ImGui::CalcTextSize("Delete overlay").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
						UICommon::ButtonWithConfirmationModal(
							"Delete overlay"sv, "Are you sure you want to remove the overlay?\nThis operation cannot be undone!\n\n"sv, [&]() {
								ImGui::ClosePopupsExceptModals();
								a_outRemove = a_item.name;
							},
							ImVec2(xButtonSize, 0));
						ImGui::EndPopup();
					}
				}

				// Disable checkbox
				ImGui::SameLine();
				if (a_bEditable) {
					std::string idString = std::format("{}##bDisabled", reinterpret_cast<uintptr_t>(&a_item));
					ImGui::PushID(idString.data());
					bool bEnabled = !a_item.bDisabled;
					if (ImGui::Checkbox("##toggleOverlay", &bEnabled)) {
						a_item.bDisabled = !bEnabled;
						bSetDirty = true;
					}
					UICommon::AddTooltip("Toggles the overlay on/off");
					ImGui::PopID();
				}

				// Overlay name
				ImGui::SameLine();
				if (a_item.IsValid()) {
					ImGui::TextUnformatted(a_item.name.data());
				} else {
					UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_item.name.data());
				}

				// Right column, the layouts it is painted for
				UICommon::SecondColumn(_firstColumnWidthPercent);
				ImGui::TextUnformatted(a_item.LayoutsText().data());

				overlayRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				// Node contents
				if (bNodeOpen) {
					ImGui::Spacing();

					if (a_bEditable) {
						// rename
						ImGui::PushID(&a_item);
						ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
						std::string tempName(a_item.name);
						if (ImGui::InputText("##name", &tempName, ImGuiInputTextFlags_EnterReturnsTrue)) {
							if (!tempName.empty() && !a_slot->HasItem(tempName)) {
								a_item.name = tempName;
								bSetDirty = true;
							}
						}

						// remove overlay button
						UICommon::SecondColumn(_firstColumnWidthPercent);
						UICommon::ButtonWithConfirmationModal("Delete overlay"sv, "Are you sure you want to remove the overlay?\nThis operation cannot be undone!\n\n"sv, [&]() {
							a_outRemove = a_item.name;
						});
						ImGui::PopID();
					}

					// One component per layout the part has, a placeholder until it is ticked, then any the data names
					// that no body type does, flagged: its name on the right, the enabling checkbox and, checked, the two
					// paths in the first column with the paint on that layout's figure in the second
					const float card = ImGui::GetTextLineHeight() * 5.f;
					const auto drawTexture = [&](Overlays::Texture& a_texture, bool a_bKnown) {
						if (!a_bEditable && !a_texture.bEnabled) {
							return;
						}
						ImGui::PushID(a_texture.uv.data());
						ImGui::Separator();
						UICommon::TextDescriptionRightAligned(a_texture.uv.data());
						const float blockTop = ImGui::GetCursorPosY();
						if (a_bEditable) {
							if (ImGui::Checkbox("##enabled", &a_texture.bEnabled)) {
								// Ticking a placeholder is what creates its entry
								a_item.TextureFor(a_texture.uv).bEnabled = a_texture.bEnabled;
								bSetDirty = true;
							}
							ImGui::SameLine();
						}
						if (a_bKnown) {
							if (a_bEditable) {
								ImGui::TextUnformatted(a_texture.uv.data());
							}
						} else {
							UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_bEditable ? a_texture.uv.data() : "Unknown layout");
							UICommon::AddTooltip("No body type names this layout, so the texture serves no actor");
						}
						if (a_texture.bEnabled) {
							const float firstColumnWidth = UICommon::FirstColumnWidth(_firstColumnWidthPercent);
							if (a_bEditable) {
								ImGui::SetNextItemWidth(firstColumnWidth);
								if (ImGui::InputTextWithHint("##diffuse", "Texture, relative to Data/Textures...", &a_texture.diffuse)) {
									bSetDirty = true;
								}
								ImGui::SetNextItemWidth(firstColumnWidth);
								if (ImGui::InputTextWithHint("##normal", "Normal map, optional...", &a_texture.normal)) {
									bSetDirty = true;
								}
							} else {
								ImGui::TextUnformatted(a_texture.diffuse.empty() ? "(no texture)" : a_texture.diffuse.data());
								if (!a_texture.normal.empty()) {
									ImGui::TextUnformatted(a_texture.normal.data());
								}
							}
							// The preview at the second column, level with the block's top; the block grows to it
							const float blockBottom = ImGui::GetCursorPosY();
							ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x * _firstColumnWidthPercent, blockTop));
							MeshThumbs::DrawOverlayCard(a_texture.diffuse, part, a_texture.uv, card);
							ImGui::SetCursorPosY((std::max)(blockBottom, blockTop + card + ImGui::GetStyle().ItemSpacing.y));
						}
						ImGui::PopID();
					};
					for (const auto& uv : layouts) {
						if (auto* texture = a_item.FindTexture(uv)) {
							drawTexture(*texture, true);
						} else {
							Overlays::Texture placeholder{ .uv = uv };
							drawTexture(placeholder, true);
						}
					}
					for (auto& texture : a_item.textures) {
						if (std::ranges::none_of(layouts, [&](const std::string& a_uv) { return Utils::CompareStringsIgnoreCase(a_uv, texture.uv); })) {
							drawTexture(texture, false);
						}
					}

					ImGui::Spacing();
					ImGui::TreePop();
				}

				if (bStyleVarPushed) {
					ImGui::PopStyleVar();
				}

				ImGui::EndTable();
			}
			ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding
			return overlayRect;
		};

		// ---- the slot, as a condition preset is drawn ----
		const std::string slotTableId = std::format("{}overlaySlotTable", reinterpret_cast<uintptr_t>(a_slot));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		if (ImGui::BeginTable(slotTableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			const bool bNodeOpen = ImGui::TreeNodeEx(a_slot, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

			// context menu
			if (bEditable) {
				if (ImGui::BeginPopupContextItem()) {
					const std::string buttonText = "Delete overlay slot";
					const auto& style = ImGui::GetStyle();
					const auto xButtonSize = ImGui::CalcTextSize(buttonText.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					UICommon::ButtonWithConfirmationModal(
						buttonText, "Are you sure you want to remove this overlay slot?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveOverlaySlotJob>(a_registeredMod, a_slot->GetName());
							bSetDirty = true;
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}
			}

			// slot name
			ImGui::SameLine();
			if (a_slot->IsValid()) {
				ImGui::TextUnformatted(a_slot->GetName().data());
			} else {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_slot->GetName().data());
			}

			// right column, the part and the pool's size, and the mod a required slot comes from
			UICommon::SecondColumn(_firstColumnWidthPercent);
			const auto count = a_slot->GetItems().size() + (ownAdditions ? ownAdditions->GetItems().size() : 0);
			ImGui::TextUnformatted(std::format("{}, {} overlay{}{}", Scan::SkinPartName(part), count, count == 1 ? "" : "s", bLocked ? std::format(", from {}", a_from->GetName()) : std::string{}).data());

			if (bNodeOpen) {
				const ImGuiStyle& style = ImGui::GetStyle();

				ImGui::Spacing();
				// rename / delete overlay slot
				if (bEditable) {
					const std::string nameId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_slot)) + "name";
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					std::string tempName(a_slot->GetName());
					if (ImGui::InputText(nameId.data(), &tempName, ImGuiInputTextFlags_EnterReturnsTrue)) {
						if (tempName.size() > 2 && !a_registeredMod->HasOverlaySlot(tempName)) {
							a_registeredMod->RenameOverlaySlot(a_slot, tempName);
							a_bOutWasRenamed = true;
							bSetDirty = true;
						}
					}

					UICommon::SecondColumn(_firstColumnWidthPercent);

					UICommon::ButtonWithConfirmationModal("Delete overlay slot"sv, "Are you sure you want to remove this overlay slot?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveOverlaySlotJob>(a_registeredMod, a_slot->GetName());
						bSetDirty = true;
					});

					ImGui::Spacing();
				}

				// description
				const std::string descriptionId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_slot)) + "description";
				if (bEditable) {
					std::string tempDescription(a_slot->GetDescription());
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					if (ImGui::InputText(descriptionId.data(), &tempDescription)) {
						a_slot->SetDescription(tempDescription);
						bSetDirty = true;
					}
					UICommon::SecondColumn(_firstColumnWidthPercent);
					UICommon::TextUnformattedDisabled("Overlay slot description");
					ImGui::Spacing();
				} else if (!a_slot->GetDescription().empty()) {
					UICommon::TextUnformattedWrapped(a_slot->GetDescription().data());
					ImGui::Spacing();
				}

				// the part: a component row, the combo in the first column
				ImGui::Separator();
				UICommon::TextDescriptionRightAligned("Part");
				UICommon::AddTooltip("Where the layer sits: the head under the face node, or the skin in the body, hands or feet slot. Decides which layouts an overlay can be painted for.");
				if (bEditable) {
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					ImGui::PushID(a_slot);
					if (ImGui::BeginCombo("##part", Scan::SkinPartName(part).data())) {
						for (std::uint8_t p = 0; p < static_cast<std::uint8_t>(Scan::SkinPart::kTotal); ++p) {
							const auto candidate = static_cast<Scan::SkinPart>(p);
							const bool bIsCurrent = candidate == part;
							if (ImGui::Selectable(Scan::SkinPartName(candidate).data(), bIsCurrent) && !bIsCurrent) {
								a_slot->SetPart(candidate);
								bSetDirty = true;
							}
							if (bIsCurrent) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
					ImGui::PopID();
				} else {
					ImGui::TextUnformatted(Scan::SkinPartName(part).data());
				}
				ImGui::Spacing();

				// the pool, drawn as a condition set: the rows with their tree lines, then the add button
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				ImVec2 vertLineStart = ImGui::GetCursorScreenPos();
				vertLineStart.x += style.FramePadding.x - style.IndentSpacing * 0.6f;
				vertLineStart.y += style.FramePadding.y + style.FramePadding.x;
				ImVec2 vertLineEnd = vertLineStart;

				std::string itemToRemove;
				std::string ownItemToRemove;
				ImGui::PushID(a_slot);
				if (a_slot->GetItems().empty() && (!ownAdditions || ownAdditions->GetItems().empty())) {
					const std::string nodeId = std::format("No overlays##{}", reinterpret_cast<uintptr_t>(a_slot));
					ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
					if (ImGui::BeginTable("blankOverlayTable", 1, ImGuiTableFlags_BordersOuter)) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						UICommon::TreeNodeCollapsedLeaf(nodeId.data(), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
						ImGui::EndTable();
					}
					ImGui::PopStyleVar();
				} else {
					const auto drawItems = [&](Overlays::Slot& a_items, std::string& a_outRemove, bool a_bItemsEditable) {
						for (auto& item : a_items.GetItems()) {
							const ImRect nodeRect = drawOverlay(item, a_outRemove, a_bItemsEditable);
							const float midPoint = (nodeRect.Min.y + nodeRect.Max.y) / 2.f;
							constexpr float horLineLength = 10.f;
							drawList->AddLine(ImVec2(vertLineStart.x, midPoint), ImVec2(vertLineStart.x + horLineLength, midPoint), ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
							vertLineEnd.y = midPoint;
						}
					};
					drawItems(*a_slot, itemToRemove, bEditable);
					if (ownAdditions) {
						drawItems(*ownAdditions, ownItemToRemove, _editMode != EditMode::kNone);
					}
					drawList->AddLine(vertLineStart, vertLineEnd, ImGui::GetColorU32(UICommon::TREE_LINE_COLOR));
				}
				if (!itemToRemove.empty()) {
					a_slot->RemoveItem(itemToRemove);
					bSetDirty = true;
				}
				if (!ownItemToRemove.empty() && ownAdditions) {
					ownAdditions->RemoveItem(ownItemToRemove);
					bSetDirty = true;
				}

				if (_editMode != EditMode::kNone) {
					// Add overlay button, named in a popup as a condition preset is
					constexpr auto popupName = "Adding new overlay"sv;
					if (ImGui::Button("Add new overlay")) {
						const auto popupPos = ImGui::GetCursorScreenPos();
						ImGui::SetNextWindowPos(popupPos);
						ImGui::OpenPopup(popupName.data());
					}
					if (ImGui::BeginPopupModal(popupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						std::string itemName;
						static std::string itemNameError;
						if (ImGui::IsWindowAppearing()) {
							itemNameError.clear();
						}
						if (ImGui::InputTextWithHint("##OverlayName", "Type a unique name...", &itemName, ImGuiInputTextFlags_EnterReturnsTrue)) {
							const bool bTaken = a_slot->HasItem(itemName) || (ownAdditions && ownAdditions->HasItem(itemName));
							itemNameError = itemName.empty() ? "The name cannot be empty."s : bTaken ? "An overlay of this name already exists."s : std::string{};
							if (itemNameError.empty()) {
								if (!bLocked) {
									a_slot->AddItem(itemName);
								} else {
									// Added to a required mod's slot: kept in this mod's slot of the same name, on the same part
									if (!ownAdditions) {
										auto added = std::make_unique<Overlays::Slot>(a_slot->GetName());
										added->SetPart(a_slot->GetPart());
										a_registeredMod->AddOverlaySlot(added);
										ownAdditions = a_registeredMod->GetOverlaySlot(a_slot->GetName());
									}
									ownAdditions->AddItem(itemName);
								}
								bSetDirty = true;
								ImGui::CloseCurrentPopup();
							}
						}
						ImGui::SetItemDefaultFocus();
						ImGui::SameLine();
						if (ImGui::Button("Cancel")) {
							ImGui::CloseCurrentPopup();
						}
						// Why the name was refused
						if (!itemNameError.empty()) {
							UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, itemNameError.data());
						}
						ImGui::EndPopup();
					}
				}
				ImGui::PopID();

				ImGui::TreePop();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		return bSetDirty;
	}

	bool UIMain::DrawConditionPreset(RegisteredMod* a_registeredMod, Conditions::ConditionPreset* a_conditionPreset, bool& a_bOutWasPresetRenamed)
	{
		bool bSetDirty = false;

		const std::string conditionPresetTableId = std::format("{}conditionPresetTable", reinterpret_cast<uintptr_t>(a_conditionPreset));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, UICommon::CONDITION_PRESET_BORDER_COLOR);
		if (ImGui::BeginTable(conditionPresetTableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGui::PopStyleColor();  // ImGuiCol_TableBorderStrong

			const bool bNodeOpen = ImGui::TreeNodeEx(a_conditionPreset, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

			// set dirty on all clips and modifiers that have the preset
			auto setDirtyOnContainingSubMods = [&](Conditions::ConditionPreset* conditionPreset) {
				a_registeredMod->ForEachSubMod([&](SubMod* a_subMod) {
					if (ConditionSetContainsPreset(a_subMod->GetConditionSet(), conditionPreset)) {
						a_subMod->SetDirty(true);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			};

			// context menu
			if (_editMode != EditMode::kNone) {
				if (ImGui::BeginPopupContextItem()) {
					// delete button
					const std::string buttonText = "Delete condition preset";
					const auto& style = ImGui::GetStyle();
					const auto xButtonSize = ImGui::CalcTextSize(buttonText.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					UICommon::ButtonWithConfirmationModal(
						buttonText, "Are you sure you want to remove this condition preset?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveConditionPresetJob>(a_registeredMod, a_conditionPreset->GetName());
							setDirtyOnContainingSubMods(a_conditionPreset);
							bSetDirty = true;
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}
			}

			// condition preset name
			ImGui::SameLine();
			if (!a_conditionPreset->IsEmpty() && a_conditionPreset->IsValid()) {
				ImGui::TextUnformatted(a_conditionPreset->GetName().data());
			} else {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_conditionPreset->GetName().data());
			}

			// right column, condition count text
			UICommon::SecondColumn(_firstColumnWidthPercent);
			ImGui::TextUnformatted(a_conditionPreset->NumText().data());

			if (bNodeOpen) {
				const ImGuiStyle& style = ImGui::GetStyle();

				ImGui::Spacing();
				// rename / delete condition preset
				if (_editMode != EditMode::kNone) {
					const std::string nameId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_registeredMod)) + "name";
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					std::string tempName(a_conditionPreset->GetName());
					if (ImGui::InputText(nameId.data(), &tempName, ImGuiInputTextFlags_EnterReturnsTrue)) {
						if (tempName.size() > 2 && !a_registeredMod->HasConditionPreset(tempName)) {
							a_conditionPreset->SetName(tempName);
							setDirtyOnContainingSubMods(a_conditionPreset);
							a_bOutWasPresetRenamed = true;
							bSetDirty = true;
						}
					}

					UICommon::SecondColumn(_firstColumnWidthPercent);

					UICommon::ButtonWithConfirmationModal("Delete condition preset"sv, "Are you sure you want to remove this condition preset?\nThis operation cannot be undone!\n\n"sv, [&]() {
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveConditionPresetJob>(a_registeredMod, a_conditionPreset->GetName());
						setDirtyOnContainingSubMods(a_conditionPreset);
						bSetDirty = true;
					});

					ImGui::Spacing();
				}

				// description
				const std::string descriptionId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_conditionPreset)) + "description";
				if (_editMode != EditMode::kNone) {
					std::string tempDescription(a_conditionPreset->GetDescription());
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					if (ImGui::InputText(descriptionId.data(), &tempDescription)) {
						a_conditionPreset->SetDescription(tempDescription);
						a_registeredMod->SetDirty(true);
					}
					UICommon::SecondColumn(_firstColumnWidthPercent);
					UICommon::TextUnformattedDisabled("Condition preset description");
					ImGui::Spacing();
				} else if (!a_conditionPreset->GetDescription().empty()) {
					UICommon::TextUnformattedWrapped(a_conditionPreset->GetDescription().data());
					ImGui::Spacing();
				}

				ImVec2 pos = ImGui::GetCursorScreenPos();
				pos.x += style.FramePadding.x;
				pos.y += style.FramePadding.y;
				ImGui::PushID(a_conditionPreset);
				if (DrawConditionSet(a_conditionPreset, nullptr, _editMode, Conditions::ConditionType::kPreset, UIManager::GetSingleton().GetRefrToEvaluate(), true, pos)) {
					bSetDirty = true;
				}
				ImGui::PopID();

				ImGui::TreePop();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		return bSetDirty;
	}

	bool UIMain::DrawReferencePool(RegisteredMod* a_registeredMod, ReferencePool* a_pool, std::string& a_outRemoved)
	{
		bool bSetDirty = false;
		const auto& entries = Apply::Entries::GetSingleton();
		const auto channelId = entries.Find(a_pool->GetChannel());

		const std::string tableId = std::format("{}referencePoolTable", reinterpret_cast<uintptr_t>(a_pool));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, UICommon::CONDITION_PRESET_BORDER_COLOR);
		if (ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGui::PopStyleColor();  // ImGuiCol_TableBorderStrong

			const bool bNodeOpen = ImGui::TreeNodeEx(a_pool, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

			// context menu
			if (_editMode != EditMode::kNone) {
				if (ImGui::BeginPopupContextItem()) {
					const std::string buttonText = "Delete reference pool";
					const auto& style = ImGui::GetStyle();
					const auto xButtonSize = ImGui::CalcTextSize(buttonText.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					UICommon::ButtonWithConfirmationModal(
						buttonText, "Are you sure you want to remove this reference pool?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							a_outRemoved = a_pool->GetName();
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}
			}

			// pool name, in the invalid colour without a channel or an entry
			ImGui::SameLine();
			if (channelId.IsValid() && !a_pool->entries.entries.empty()) {
				ImGui::TextUnformatted(a_pool->GetName().data());
			} else {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_pool->GetName().data());
			}

			// right column, the channel and the entry count
			UICommon::SecondColumn(_firstColumnWidthPercent);
			const auto count = a_pool->entries.entries.size();
			ImGui::TextUnformatted(std::format("{}  {} {}", a_pool->GetChannel().empty() ? "(no channel)"sv : a_pool->GetChannel(), count, count == 1 ? "entry" : "entries").data());

			if (bNodeOpen) {
				ImGui::Spacing();
				// rename / delete reference pool
				if (_editMode != EditMode::kNone) {
					const std::string nameId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_pool)) + "name";
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					std::string tempName(a_pool->GetName());
					if (ImGui::InputText(nameId.data(), &tempName, ImGuiInputTextFlags_EnterReturnsTrue)) {
						if (tempName.size() > 2 && !a_registeredMod->GetReferencePool(tempName)) {
							a_pool->SetName(tempName);
							bSetDirty = true;
						}
					}

					UICommon::SecondColumn(_firstColumnWidthPercent);

					UICommon::ButtonWithConfirmationModal("Delete reference pool"sv, "Are you sure you want to remove this reference pool?\nThis operation cannot be undone!\n\n"sv, [&]() {
						a_outRemoved = a_pool->GetName();
					});

					ImGui::Spacing();
				}

				// description
				const std::string descriptionId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_pool)) + "description";
				if (_editMode != EditMode::kNone) {
					std::string tempDescription(a_pool->GetDescription());
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					if (ImGui::InputText(descriptionId.data(), &tempDescription)) {
						a_pool->SetDescription(tempDescription);
						bSetDirty = true;
					}
					UICommon::SecondColumn(_firstColumnWidthPercent);
					UICommon::TextUnformattedDisabled("Reference pool description");
					ImGui::Spacing();
				} else if (!a_pool->GetDescription().empty()) {
					UICommon::TextUnformattedWrapped(a_pool->GetDescription().data());
					ImGui::Spacing();
				}

				// channel: the channel picker, its reference channels only; fixed while the pool has entries
				if (_editMode != EditMode::kNone) {
					std::vector<Info> referenceInfos;
					for (const auto& info : _poolChannelComboFilter.GetChannelInfos(UIManager::GetSingleton().GetRefrToEvaluate())) {
						const auto id = entries.Find(info.name);
						if (id.IsValid() && (entries.Get(id).type == Apply::ValueType::kReference || entries.Get(id).type == Apply::ValueType::kBlendedPick)) {
							referenceInfos.push_back(info);
						}
					}
					int selectedItem = -1;
					const Info* currentInfo = nullptr;
					if (const auto it = std::ranges::find_if(referenceInfos, [&](const Info& a_info) { return a_info.name == a_pool->GetChannel(); }); it != referenceInfos.end()) {
						selectedItem = static_cast<int>(std::distance(referenceInfos.begin(), it));
						currentInfo = &*it;
					}
					ImGui::BeginDisabled(!a_pool->entries.entries.empty());
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					ImGui::PushID(a_pool);
					if (_poolChannelComboFilter.ComboFilter("##Pool channel", selectedItem, referenceInfos, currentInfo, ImGuiComboFlags_HeightLarge, &UIMain::DrawInfoTooltip)) {
						if (selectedItem >= 0 && selectedItem < static_cast<int>(referenceInfos.size())) {
							a_pool->SetChannel(referenceInfos[selectedItem].name);
							bSetDirty = true;
						}
					}
					ImGui::PopID();
					ImGui::EndDisabled();
					UICommon::SecondColumn(_firstColumnWidthPercent);
					ImGui::TextUnformatted("Channel");
					if (!a_pool->entries.entries.empty()) {
						ImGui::SameLine();
						UICommon::HelpMarker("The entries are values of this channel: remove them to change it.");
					}
					ImGui::Spacing();
				}

				// entries, each the channel's own value
				if (channelId.IsValid()) {
					if (a_pool->entries.DisplayInUI(_editMode != EditMode::kNone, _firstColumnWidthPercent, Channels::ValueFactoryFor(channelId), nullptr)) {
						bSetDirty = true;
					}
				} else {
					UICommon::TextUnformattedDisabled("Pick the channel the pool is for.");
				}

				ImGui::TreePop();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		return bSetDirty;
	}

	bool UIMain::DrawTriggerPreset(RegisteredMod* a_registeredMod, Triggers::TriggerPreset* a_triggerPreset, bool& a_bOutWasPresetRenamed)
	{
		bool bSetDirty = false;

		const std::string tableId = std::format("{}triggerPresetTable", reinterpret_cast<uintptr_t>(a_triggerPreset));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, UICommon::CONDITION_PRESET_BORDER_COLOR);
		if (ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGui::PopStyleColor();  // ImGuiCol_TableBorderStrong

			const bool bNodeOpen = ImGui::TreeNodeEx(a_triggerPreset, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

			// set dirty on all clips that use the preset
			const auto setDirtyOnContainingClips = [&] {
				a_registeredMod->ForEachSubMod([&](SubMod* a_subMod) {
					if (a_subMod->GetKind() == SubModKind::kClip && Triggers::TriggerSetContainsPreset(static_cast<Clip*>(a_subMod)->GetTriggerSet(), a_triggerPreset)) {
						a_subMod->SetDirty(true);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			};

			// context menu
			if (_editMode != EditMode::kNone) {
				if (ImGui::BeginPopupContextItem()) {
					const std::string buttonText = "Delete trigger preset";
					const auto& style = ImGui::GetStyle();
					const auto xButtonSize = ImGui::CalcTextSize(buttonText.data()).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					UICommon::ButtonWithConfirmationModal(
						buttonText, "Are you sure you want to remove this trigger preset?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							setDirtyOnContainingClips();
							ModRegistry::GetSingleton().QueueJob<Jobs::RemoveTriggerPresetJob>(a_registeredMod, a_triggerPreset->GetName());
							bSetDirty = true;
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}
			}

			// trigger preset name
			ImGui::SameLine();
			if (!a_triggerPreset->IsEmpty() && a_triggerPreset->IsValid()) {
				ImGui::TextUnformatted(a_triggerPreset->GetName().data());
			} else {
				UICommon::TextUnformattedColored(UICommon::INVALID_CONDITION_COLOR, a_triggerPreset->GetName().data());
			}

			// right column, trigger count text
			UICommon::SecondColumn(_firstColumnWidthPercent);
			ImGui::TextUnformatted(a_triggerPreset->NumText().data());

			if (bNodeOpen) {
				const ImGuiStyle& style = ImGui::GetStyle();

				ImGui::Spacing();
				// rename / delete trigger preset
				if (_editMode != EditMode::kNone) {
					const std::string nameId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_triggerPreset)) + "name";
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					std::string tempName(a_triggerPreset->GetName());
					if (ImGui::InputText(nameId.data(), &tempName, ImGuiInputTextFlags_EnterReturnsTrue)) {
						if (tempName.size() > 2 && !a_registeredMod->HasTriggerPreset(tempName)) {
							// The clips name it by name: they follow the rename
							const std::string oldName(a_triggerPreset->GetName());
							a_registeredMod->ForEachSubMod([&](SubMod* a_subMod) {
								if (a_subMod->GetKind() == SubModKind::kClip) {
									static_cast<Clip*>(a_subMod)->GetTriggerSet()->ForEach([&](std::unique_ptr<Triggers::TriggerBase>& a_trigger) {
										if (auto* presetTrigger = dynamic_cast<Triggers::PresetTrigger*>(a_trigger.get()); presetTrigger && presetTrigger->preset->presetName == oldName) {
											presetTrigger->preset->presetName = tempName;
											a_subMod->SetDirty(true);
										}
										return RE::BSVisit::BSVisitControl::kContinue;
									});
								}
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							a_triggerPreset->SetName(tempName);
							a_bOutWasPresetRenamed = true;
							bSetDirty = true;
						}
					}

					UICommon::SecondColumn(_firstColumnWidthPercent);

					UICommon::ButtonWithConfirmationModal("Delete trigger preset"sv, "Are you sure you want to remove this trigger preset?\nThis operation cannot be undone!\n\n"sv, [&]() {
						setDirtyOnContainingClips();
						ModRegistry::GetSingleton().QueueJob<Jobs::RemoveTriggerPresetJob>(a_registeredMod, a_triggerPreset->GetName());
						bSetDirty = true;
					});

					ImGui::Spacing();
				}

				// description
				const std::string descriptionId = "##" + std::to_string(reinterpret_cast<std::uintptr_t>(a_triggerPreset)) + "description";
				if (_editMode != EditMode::kNone) {
					std::string tempDescription(a_triggerPreset->GetDescription());
					ImGui::SetNextItemWidth(UICommon::FirstColumnWidth(_firstColumnWidthPercent));
					if (ImGui::InputText(descriptionId.data(), &tempDescription)) {
						a_triggerPreset->SetDescription(tempDescription);
						a_registeredMod->SetDirty(true);
					}
					UICommon::SecondColumn(_firstColumnWidthPercent);
					UICommon::TextUnformattedDisabled("Trigger preset description");
					ImGui::Spacing();
				} else if (!a_triggerPreset->GetDescription().empty()) {
					UICommon::TextUnformattedWrapped(a_triggerPreset->GetDescription().data());
					ImGui::Spacing();
				}

				ImVec2 pos = ImGui::GetCursorScreenPos();
				pos.x += style.FramePadding.x;
				pos.y += style.FramePadding.y;
				ImGui::PushID(a_triggerPreset);
				if (DrawTriggerSet(a_triggerPreset, nullptr, _editMode, true, pos, a_registeredMod)) {
					bSetDirty = true;
				}
				ImGui::PopID();

				ImGui::TreePop();
			}

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();  // ImGuiStyleVar_CellPadding

		return bSetDirty;
	}

	void UIMain::DrawInfoTooltip(const Info& a_info, ImGuiHoveredFlags a_flags)
	{
		if (ImGui::IsItemHovered(a_flags)) {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8, 8 });
			if (ImGui::BeginTooltip()) {
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 50.0f);
				ImGui::TextUnformatted(a_info.description.data());
				if (!a_info.requiredPluginName.empty()) {
					ImGui::TextUnformatted("Source plugin:");
					ImGui::SameLine();
					UICommon::TextUnformattedColored(a_info.textColor, a_info.requiredPluginName.data());
					ImGui::SameLine();
					UICommon::TextUnformattedDisabled(a_info.requiredVersion.string("."sv).data());
					if (!a_info.requiredPluginAuthor.empty()) {
						ImGui::SameLine();
						ImGui::TextUnformatted("by");
						ImGui::SameLine();
						UICommon::TextUnformattedColored(a_info.textColor, a_info.requiredPluginAuthor.data());
					}
				}
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::PopStyleVar();
		}
	}

	int UIMain::ReferenceInputTextCallback(struct ImGuiInputTextCallbackData* a_data)
	{
		RE::FormID formID;
		auto [ptr, ec]{ std::from_chars(a_data->Buf, a_data->Buf + a_data->BufTextLen, formID, 16) };
		if (ec == std::errc()) {
			UIManager::GetSingleton().SetRefrToEvaluate(RE::TESForm::LookupByID<RE::TESObjectREFR>(formID));
		} else {
			UIManager::GetSingleton().SetRefrToEvaluate(nullptr);
		}

		return 0;
	}

	bool UIMain::BeginDragDropSourceEx(ImGuiDragDropFlags a_flags /*= 0*/, ImVec2 a_tooltipSize /*= ImVec2(0, 0)*/)
	{
		using namespace ImGui;

		ImGuiContext& g = *GImGui;
		ImGuiWindow* window = g.CurrentWindow;

		// FIXME-DRAGDROP: While in the common-most "drag from non-zero active id" case we can tell the mouse button,
		// in both SourceExtern and id==0 cases we may requires something else (explicit flags or some heuristic).
		ImGuiMouseButton mouse_button = ImGuiMouseButton_Left;

		bool source_drag_active = false;
		ImGuiID source_id = 0;
		ImGuiID source_parent_id = 0;
		if (!(a_flags & ImGuiDragDropFlags_SourceExtern)) {
			source_id = g.LastItemData.ID;
			if (source_id != 0) {
				// Common path: items with ID
				if (g.ActiveId != source_id)
					return false;
				if (g.ActiveIdMouseButton != -1)
					mouse_button = g.ActiveIdMouseButton;
				if (g.IO.MouseDown[mouse_button] == false || window->SkipItems)
					return false;
				g.ActiveIdAllowOverlap = false;
			} else {
				// Uncommon path: items without ID
				if (g.IO.MouseDown[mouse_button] == false || window->SkipItems)
					return false;
				if ((g.LastItemData.StatusFlags & ImGuiItemStatusFlags_HoveredRect) == 0 && (g.ActiveId == 0 || g.ActiveIdWindow != window))
					return false;

				// If you want to use BeginDragDropSource() on an item with no unique identifier for interaction, such as Text() or Image(), you need to:
				// A) Read the explanation below, B) Use the ImGuiDragDropFlags_SourceAllowNullID flag.
				if (!(a_flags & ImGuiDragDropFlags_SourceAllowNullID)) {
					IM_ASSERT(0);
					return false;
				}

				// Magic fallback to handle items with no assigned ID, e.g. Text(), Image()
				// We build a throwaway ID based on current ID stack + relative AABB of items in window.
				// THE IDENTIFIER WON'T SURVIVE ANY REPOSITIONING/RESIZINGG OF THE WIDGET, so if your widget moves your dragging operation will be canceled.
				// We don't need to maintain/call ClearActiveID() as releasing the button will early out this function and trigger !ActiveIdIsAlive.
				// Rely on keeping other window->LastItemXXX fields intact.
				source_id = g.LastItemData.ID = window->GetIDFromRectangle(g.LastItemData.Rect);
				KeepAliveID(source_id);
				const bool is_hovered = ItemHoverable(g.LastItemData.Rect, source_id, g.LastItemData.ItemFlags);
				if (is_hovered && g.IO.MouseClicked[mouse_button]) {
					SetActiveID(source_id, window);
					FocusWindow(window);
				}
				if (g.ActiveId == source_id)  // Allow the underlying widget to display/return hovered during the mouse release frame, else we would get a flicker.
					g.ActiveIdAllowOverlap = is_hovered;
			}
			if (g.ActiveId != source_id)
				return false;
			source_parent_id = window->IDStack.back();
			source_drag_active = IsMouseDragging(mouse_button);

			// Disable navigation and key inputs while dragging + cancel existing request if any
			SetActiveIdUsingAllKeyboardKeys();
		} else {
			window = nullptr;
			source_id = ImHashStr("#SourceExtern");
			source_drag_active = true;
		}

		if (source_drag_active) {
			if (!g.DragDropActive) {
				IM_ASSERT(source_id != 0);
				ClearDragDrop();
				ImGuiPayload& payload = g.DragDropPayload;
				payload.SourceId = source_id;
				payload.SourceParentId = source_parent_id;
				g.DragDropActive = true;
				g.DragDropSourceFlags = a_flags;
				g.DragDropMouseButton = mouse_button;
				if (payload.SourceId == g.ActiveId)
					g.ActiveIdNoClearOnFocusLoss = true;
			}
			g.DragDropSourceFrameCount = g.FrameCount;
			g.DragDropWithinSource = true;

			if (!(a_flags & ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
				// Target can request the Source to not display its tooltip (we use a dedicated flag to make this request explicit)
				// We unfortunately can't just modify the source flags and skip the call to BeginTooltip, as caller may be emitting contents.
				SetNextWindowSize(a_tooltipSize);  // <--- ADDED
				BeginTooltipEx(ImGuiTooltipFlags_None, ImGuiWindowFlags_None);
				if (g.DragDropAcceptIdPrev && (g.DragDropAcceptFlagsPrev & ImGuiDragDropFlags_AcceptNoPreviewTooltip)) {
					ImGuiWindow* tooltip_window = g.CurrentWindow;
					tooltip_window->Hidden = tooltip_window->SkipItems = true;
					tooltip_window->HiddenFramesCanSkipItems = 1;
				}
			}

			if (!(a_flags & ImGuiDragDropFlags_SourceNoDisableHover) && !(a_flags & ImGuiDragDropFlags_SourceExtern))
				g.LastItemData.StatusFlags &= ~ImGuiItemStatusFlags_HoveredRect;

			return true;
		}
		return false;
	}

	bool UIMain::ConditionContainsPreset(Conditions::ICondition* a_condition, Conditions::ConditionPreset* a_conditionPreset) const
	{
		if (a_condition == nullptr) {
			return false;
		}

		//if (a_condition->GetConditionType() == Conditions::ConditionType::kPreset) {
		//	return true;
		//}

		if (const auto numComponents = a_condition->GetNumComponents(); numComponents > 0) {
			for (uint32_t i = 0; i < numComponents; i++) {
				const auto component = a_condition->GetComponent(i);
				if (component->GetType() == Conditions::ConditionComponentType::kPreset) {
					if (a_conditionPreset == nullptr) {
						return true;
					}
					// check if equals the given condition preset
					const auto conditionPresetComponent = static_cast<Conditions::ConditionPresetComponent*>(component);
					if (conditionPresetComponent->conditionPreset == a_conditionPreset) {
						return true;
					}
				}

				if (component->GetType() == Conditions::ConditionComponentType::kMulti) {
					const auto multiConditionComponent = static_cast<Conditions::IMultiConditionComponent*>(component);
					if (const auto conditionSet = multiConditionComponent->GetConditions()) {
						if (ConditionSetContainsPreset(conditionSet, a_conditionPreset)) {
							return true;
						}
					}
				}
			}
		}

		return false;
	}

	bool UIMain::ConditionSetContainsPreset(Conditions::ConditionSet* a_conditionSet, Conditions::ConditionPreset* a_conditionPreset) const
	{
		if (a_conditionSet == nullptr) {
			return false;
		}

		const auto result = a_conditionSet->ForEach([&](std::unique_ptr<Conditions::ICondition>& a_condition) {
			if (ConditionContainsPreset(a_condition.get(), a_conditionPreset)) {
				return RE::BSVisit::BSVisitControl::kStop;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		return result == RE::BSVisit::BSVisitControl::kStop;
	}
}

#include "UIWelcomeBanner.h"

#include "Settings.h"
#include "UICommon.h"
#include "UIManager.h"

namespace UI
{
	void UIWelcomeBanner::Display()
	{
		_fLingerTime = DISPLAY_TIME;
		_bFirstFrame = true;
	}

	bool UIWelcomeBanner::ShouldDrawImpl() const
	{
		if (Settings::bShowWelcomeBanner) {
			return _fLingerTime > 0.f;
		}

		return false;
	}

	void UIWelcomeBanner::DrawImpl()
	{
		// Its bottom just above Open Animation Replacer's banner, which sits 200 below the top margin, centred
		constexpr float kOarBannerTop = 20.f + 200.f;
		constexpr float kGap = 8.f;
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, kOarBannerTop - kGap), ImGuiCond_Always, ImVec2(0.5f, 1.f));

		constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

		float alpha = 1.f;
		if (_fLingerTime < Settings::fWelcomeBannerFadeTime) {
			alpha = std::lerp(0.f, 1.f, _fLingerTime / Settings::fWelcomeBannerFadeTime);
		} else if (_fLingerTime > DISPLAY_TIME - Settings::fWelcomeBannerFadeTime) {
			alpha = std::lerp(0.f, 1.f, (DISPLAY_TIME - _fLingerTime) / Settings::fWelcomeBannerFadeTime);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
		if (ImGui::Begin("Dynamic Character Modifier Framework##Welcome", nullptr, windowFlags)) {
			const auto titleText = std::format("{} {}", Plugin::NAME, Plugin::VERSION_STRING);
			constexpr auto textA = "Press"sv;
			std::string keyNameText;
			if (UIManager::GetSingleton().GetSuppressMenuHotkey()) {
				keyNameText = UICommon::GetKeyName(UIManager::GetSingleton().GetAlternativeKeyData());
			} else {
				keyNameText = UICommon::GetKeyName(Settings::uToggleUIKeyData);
			}
			constexpr auto textB = "to open the in-game UI."sv;
			const auto windowWidth = ImGui::GetWindowSize().x;
			const auto titleTextWidth = ImGui::CalcTextSize(titleText.data()).x;
			ImGui::SetCursorPosX((windowWidth - titleTextWidth) * 0.5f);
			ImGui::TextUnformatted(titleText.data());
			ImGui::Separator();

			ImGui::TextUnformatted(textA.data());
			ImGui::SameLine();
			UICommon::TextUnformattedColored(UICommon::KEY_TEXT_COLOR, keyNameText.data());
			ImGui::SameLine();
			ImGui::TextUnformatted(textB.data());
		}
		ImGui::PopStyleVar();
		ImGui::End();

		if (_bFirstFrame) {
			_bFirstFrame = false;
		} else {
			const ImGuiIO& io = ImGui::GetIO();
			_fLingerTime -= io.DeltaTime;
		}
	}
}

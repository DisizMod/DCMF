#include "UIScanProgress.h"

#include "scan/Scan.h"
#include "Settings.h"

namespace UI
{
	bool UIScanProgress::ShouldDrawImpl() const
	{
		return Scan::Scanner::GetSingleton().IsRunning() || _fLingerTime > 0.f;
	}

	void UIScanProgress::DrawImpl()
	{
		auto& scanner = Scan::Scanner::GetSingleton();

		if (scanner.IsRunning()) {
			_fLingerTime = Settings::fAnimationQueueLingerTime + Settings::fQueueFadeTime;
		} else {
			const ImGuiIO& io = ImGui::GetIO();
			_fLingerTime -= io.DeltaTime;
		}

		const auto progress = scanner.GetProgress();
		uint32_t done = 0;
		uint32_t total = 0;
		for (const auto& source : progress) {
			done += source.done;
			total += source.total;
		}

		constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

		// Below the animation queue's corner, which it would otherwise share
		constexpr float PAD = 10.0f;
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 workPos = viewport->WorkPos;
		const ImVec2 workSize = viewport->WorkSize;
		ImVec2 windowPos, windowPosPivot;
		windowPos.x = workPos.x + workSize.x - PAD;
		windowPos.y = workPos.y + PAD + 80.f;
		windowPosPivot.x = 1.0f;
		windowPosPivot.y = 0.0f;
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, windowPosPivot);

		const float alpha = std::fmin(_fLingerTime / Settings::fQueueFadeTime, 1.f);
		ImGui::SetNextWindowBgAlpha(0.25f);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
		if (ImGui::Begin("ScanProgress", nullptr, windowFlags)) {
			const auto text = scanner.IsRunning() ? "Scanning body parts..."sv : "Scan finished"sv;
			const auto windowWidth = ImGui::GetWindowSize().x;
			const auto titleTextWidth = ImGui::CalcTextSize(text.data()).x;
			ImGui::SetCursorPosX((windowWidth - titleTextWidth) * 0.5f);
			ImGui::TextUnformatted(text.data());

			const float percent = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.f;
			const std::string percentStr = std::format("{}/{}", done, total);
			ImGui::ProgressBar(scanner.IsRunning() ? percent : 1.f, ImVec2(200, 0), percentStr.data());
		}
		ImGui::PopStyleVar();
		ImGui::End();
	}
}

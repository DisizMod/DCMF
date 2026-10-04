#include "UICommon.h"
#include "UIManager.h"
#include <Windows.h>
#include <dinput.h>

#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <unordered_map>
#include <optional>
#include <mutex>

namespace UI::UICommon
{
	void TextUnformattedColored(const ImVec4& a_col, const char* a_text, const char* a_textEnd)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, a_col);
		ImGui::TextUnformatted(a_text, a_textEnd);
		ImGui::PopStyleColor();
	}

	void TextUnformattedDisabled(const char* a_text, const char* a_textEnd)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextUnformatted(a_text, a_textEnd);
		ImGui::PopStyleColor();
	}

	void TextUnformattedWrapped(const char* a_text, const char* a_textEnd)
	{
		if (GImGui->CurrentWindow->DC.TextWrapPos < 0.0f) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(a_text, a_textEnd);
			ImGui::PopTextWrapPos();
		} else {
			ImGui::TextUnformatted(a_text, a_textEnd);
		}
	}

	bool TextUnformattedEllipsisNoTooltip(const char* a_text, const char* a_textEnd, float a_maxWidth, const ImVec4& a_col)
	{
		// Accept null ranges
		if (a_text == a_textEnd) {
			a_text = a_textEnd = "";
		}

		// Calculate length
		if (a_textEnd == nullptr) {
			a_textEnd = a_text + strlen(a_text);
		}

		// Calculate max width
		if (a_maxWidth <= 0.f) {
			a_maxWidth = ImGui::GetContentRegionAvail().x;
		}

		static constexpr auto ellipsis = "..."sv;

		const float ellipsisWidth = ImGui::CalcTextSize(ellipsis.data(), nullptr).x;
		float textWidth = ImGui::CalcTextSize(a_text, a_textEnd).x;

		if (textWidth <= a_maxWidth) {
			if (a_col.w != 0.f) {
				TextUnformattedColored(a_col, a_text, a_textEnd);
			} else {
				ImGui::TextUnformatted(a_text, a_textEnd);
			}

			return false;
		}

		const std::string fullText(a_text, a_textEnd);

		while (textWidth + ellipsisWidth > a_maxWidth && a_textEnd != a_text) {
			a_textEnd--;
			textWidth = ImGui::CalcTextSize(a_text, a_textEnd).x;
		}

		std::string shortenedText(a_text, a_textEnd);
		shortenedText.append(ellipsis);

		if (a_col.w != 0.f) {
			TextUnformattedColored(a_col, shortenedText.data(), shortenedText.data() + shortenedText.length());
		} else {
			ImGui::TextUnformatted(shortenedText.data(), shortenedText.data() + shortenedText.length());
		}

		return true;
	}

	bool TextUnformattedEllipsis(const char* a_text, const char* a_textEnd, float a_maxWidth)
	{
		// Accept null ranges
		if (a_text == a_textEnd) {
			a_text = a_textEnd = "";
		}

		// Calculate length
		if (a_textEnd == nullptr) {
			a_textEnd = a_text + strlen(a_text);
		}

		const std::string fullText(a_text, a_textEnd);

		if (TextUnformattedEllipsisNoTooltip(a_text, a_textEnd, a_maxWidth)) {
			AddTooltip(fullText.data(), ImGuiHoveredFlags_DelayShort);
			return true;
		}

		return false;
	}

	bool TextUnformattedEllipsisColored(const ImVec4& a_col, const char* a_text, const char* a_textEnd, float a_maxWidth)
	{
		// Accept null ranges
		if (a_text == a_textEnd) {
			a_text = a_textEnd = "";
		}

		// Calculate length
		if (a_textEnd == nullptr) {
			a_textEnd = a_text + strlen(a_text);
		}

		const std::string fullText(a_text, a_textEnd);

		if (TextUnformattedEllipsisNoTooltip(a_text, a_textEnd, a_maxWidth, a_col)) {
			AddTooltip(fullText.data(), ImGuiHoveredFlags_DelayShort, a_col);
			return true;
		}

		return false;
	}

	bool TextUnformattedEllipsisShort(const char* a_fullText, const char* a_shortText, const char* a_shortTextEnd, float a_maxWidth)
	{
		// Accept null ranges
		if (a_shortText == a_shortTextEnd) {
			a_shortText = a_shortTextEnd = "";
		}

		// Calculate length
		if (a_shortTextEnd == nullptr) {
			a_shortTextEnd = a_shortText + strlen(a_shortText);
		}

		const bool bResult = TextUnformattedEllipsisNoTooltip(a_shortText, a_shortTextEnd, a_maxWidth);

		AddTooltip(a_fullText, ImGuiHoveredFlags_DelayShort);

		return bResult;
	}

	bool TextUnformattedEllipsisShort(const char* a_shortText, const char* a_fullText, float a_maxWidth)
	{
		const char* shortTextEnd = a_shortText + strlen(a_shortText);

		// Calculate max width
		if (a_maxWidth <= 0.f) {
			a_maxWidth = ImGui::GetContentRegionAvail().x;
		}

		static constexpr auto ellipsis = "..."sv;

		const float ellipsisWidth = ImGui::CalcTextSize(ellipsis.data(), nullptr).x;
		float textWidth = ImGui::CalcTextSize(a_shortText, shortTextEnd).x;

		if (textWidth <= a_maxWidth) {
			ImGui::TextUnformatted(a_shortText, shortTextEnd);
			return false;
		}

		while (textWidth + ellipsisWidth > a_maxWidth && shortTextEnd != a_shortText) {
			shortTextEnd--;
			textWidth = ImGui::CalcTextSize(a_shortText, shortTextEnd).x;
		}

		std::string shortenedText(a_shortText, shortTextEnd);
		shortenedText.append(ellipsis);

		ImGui::TextUnformatted(shortenedText.data(), shortenedText.data() + shortenedText.length());
		AddTooltip(a_fullText, ImGuiHoveredFlags_DelayShort);

		return true;
	}

	float PickerButtonWidth()
	{
		return ImGui::GetFrameHeight() * 2.f + ImGui::GetStyle().ItemInnerSpacing.x;
	}

	bool Picker(const char* a_id, const std::function<std::vector<PickerItem>()>& a_items,
		std::uint64_t a_current, std::uint64_t& a_outValue)
	{
		bool bPicked = false;

		ImGui::PushID(a_id);
		if (ImGui::Button("Pick", ImVec2(PickerButtonWidth(), 0.f))) {
			ImGui::OpenPopup("##Picker");
		}

		if (ImGui::BeginPopup("##Picker")) {
			static std::string search;
			if (ImGui::IsWindowAppearing()) {
				search.clear();
			}
			const float width = ImGui::GetFontSize() * 30.f;
			ImGui::SetNextItemWidth(width);
			ImGui::InputTextWithHint("##search", "Search...", &search);

			const auto lower = [](std::string a_text) {
				std::ranges::transform(a_text, a_text.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
				return a_text;
			};
			const auto needle = lower(search);

			if (ImGui::BeginChild("##list", ImVec2(width, ImGui::GetFontSize() * 16.f))) {
				// Built once when the popup opens, not every frame it stays open: a
				// form list is the whole load order's worth of one record type.
				static std::vector<PickerItem> items;
				static std::vector<std::string> lowered;
				if (ImGui::IsWindowAppearing()) {
					items = a_items();
					lowered.clear();
					lowered.reserve(items.size());
					for (const auto& item : items) {
						lowered.push_back(lower(item.label));
					}
				}

				// Grouped items stay in the order given; a heading is drawn each time
				// the group changes, open while searching so the matches show.
				std::string_view group;
				bool bGroupOpen = true;
				for (std::size_t i = 0; i < items.size(); ++i) {
					const auto& item = items[i];
					if (!needle.empty() && lowered[i].find(needle) == std::string::npos) {
						continue;
					}

					if (item.group != group) {
						if (!group.empty() && bGroupOpen) {
							ImGui::TreePop();
						}
						group = item.group;
						bGroupOpen = true;
						if (!group.empty()) {
							if (!needle.empty()) {
								ImGui::SetNextItemOpen(true);
							}
							bGroupOpen = ImGui::TreeNode(item.group.data());
						}
					}

					if (!bGroupOpen) {
						continue;
					}

					ImGui::PushID(static_cast<int>(item.value));
					if (!item.warning.empty()) {
						ImGui::PushStyleColor(ImGuiCol_Text, ERROR_TEXT_COLOR);
					}
					if (ImGui::Selectable(item.label.data(), item.value == a_current)) {
						a_outValue = item.value;
						bPicked = true;
						ImGui::CloseCurrentPopup();
					}
					if (!item.warning.empty()) {
						ImGui::PopStyleColor();
						AddTooltip(item.warning.data());
					}
					ImGui::PopID();
				}

				if (!group.empty() && bGroupOpen) {
					ImGui::TreePop();
				}
			}
			ImGui::EndChild();
			ImGui::EndPopup();
		}
		ImGui::PopID();

		return bPicked;
	}

	bool AsyncPicker(const char* a_id, const std::function<std::vector<AsyncPickerGroup>()>& a_groups,
		std::function<std::vector<PickerItem>(std::uint64_t)> a_items, std::uint64_t a_current, std::uint64_t& a_outValue)
	{
		// Items by picker and group: absent is not asked yet, empty optional is on its way. Written by the main
		// thread's task, read here on the render thread
		struct Cache
		{
			std::mutex lock;
			std::unordered_map<std::string, std::unordered_map<std::uint64_t, std::optional<std::vector<PickerItem>>>> items;
		};
		static Cache cache;

		bool bPicked = false;

		ImGui::PushID(a_id);
		if (ImGui::Button("Pick", ImVec2(PickerButtonWidth(), 0.f))) {
			ImGui::OpenPopup("##AsyncPicker");
		}

		if (ImGui::BeginPopup("##AsyncPicker")) {
			const float width = ImGui::GetFontSize() * 30.f;
			if (ImGui::BeginChild("##list", ImVec2(width, ImGui::GetFontSize() * 16.f))) {
				// The groups, built once when the popup opens
				static std::vector<AsyncPickerGroup> groups;
				if (ImGui::IsWindowAppearing()) {
					groups = a_groups();
				}

				// A section's node round its groups, closed, as a group is round its items
				const std::string* section = nullptr;
				bool bSectionOpen = true;
				for (const auto& group : groups) {
					if (!section || *section != group.section) {
						if (section && !section->empty() && bSectionOpen) {
							ImGui::TreePop();
						}
						section = &group.section;
						bSectionOpen = section->empty() || ImGui::TreeNode(section->data());
					}
					if (!bSectionOpen) {
						continue;
					}
					ImGui::PushID(static_cast<int>(group.key));
					if (ImGui::TreeNode(group.label.data())) {
						std::optional<std::vector<PickerItem>> items;
						bool bAsk = false;
						{
							std::scoped_lock guard(cache.lock);
							auto& ofPicker = cache.items[a_id];
							if (const auto it = ofPicker.find(group.key); it == ofPicker.end()) {
								ofPicker.emplace(group.key, std::nullopt);
								bAsk = true;
							} else {
								items = it->second;
							}
						}
						// First opened: asked for on the main thread, where the engine's files may be read
						if (bAsk) {
							if (auto* tasks = SKSE::GetTaskInterface()) {
								tasks->AddTask([id = std::string(a_id), key = group.key, fetch = a_items] {
									auto fetched = fetch(key);
									std::scoped_lock guard(cache.lock);
									cache.items[id][key] = std::move(fetched);
								});
							}
						}
						if (!items) {
							TextUnformattedDisabled("Loading...");
						} else if (items->empty()) {
							TextUnformattedDisabled("(nothing)");
						} else {
							for (const auto& item : *items) {
								ImGui::PushID(static_cast<int>(item.value));
								if (ImGui::Selectable(item.label.data(), item.value == a_current)) {
									a_outValue = item.value;
									bPicked = true;
									ImGui::CloseCurrentPopup();
								}
								// A line wider than the list is read whole on hover
								if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::CalcTextSize(item.label.data()).x > ImGui::GetContentRegionAvail().x) {
									ImGui::BeginTooltip();
									ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.f);
									ImGui::TextUnformatted(item.label.data());
									ImGui::PopTextWrapPos();
									ImGui::EndTooltip();
								}
								ImGui::PopID();
							}
						}
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				if (section && !section->empty() && bSectionOpen) {
					ImGui::TreePop();
				}
			}
			ImGui::EndChild();
			ImGui::EndPopup();
		}
		ImGui::PopID();

		return bPicked;
	}

	bool MultiSelectCombo(const char* a_id, std::vector<std::string>& a_selected,
		const std::vector<MultiSelectItem>& a_options, bool a_bEditable,
		const char* a_manualEntryHint, const char* a_unknownTooltip)
	{
		bool bEdited = false;

		const auto isKnown = [&a_options](const std::string& a_entry) {
			return std::ranges::any_of(a_options, [&a_entry](const MultiSelectItem& a_item) {
				return a_item.name == a_entry;
			});
		};

		const std::string manualPopupName = std::string("Adding entry") + a_id;

		ImGui::PushID(a_id);
		ImGui::BeginDisabled(!a_bEditable);

		bool bOpenManualEntry = false;

		const bool bComboOpen = ImGui::BeginCombo("", "", ImGuiComboFlags_CustomPreview);

		if (ImGui::BeginComboPreview()) {
			if (a_selected.empty()) {
				TextUnformattedDisabled("none");
			} else {
				auto* drawList = ImGui::GetWindowDrawList();
				const auto& style = ImGui::GetStyle();
				const float rounding = ImGui::GetTextLineHeight() * 0.35f;

				// One chip, drawn where the cursor is.
				const auto drawChip = [&](const char* a_text, bool a_bKnown, const char* a_tooltip) {
					const ImVec2 textSize = ImGui::CalcTextSize(a_text);
					const ImVec2 chipMin = ImGui::GetCursorScreenPos();
					const ImVec2 chipMax(chipMin.x + textSize.x + style.FramePadding.x * 2.f,
						chipMin.y + textSize.y + style.FramePadding.y);

					drawList->AddRectFilled(chipMin, chipMax,
						ImGui::GetColorU32(a_bKnown ? MULTISELECT_CHIP_COLOR : MULTISELECT_CHIP_UNKNOWN_COLOR), rounding);

					ImGui::Dummy(ImVec2(chipMax.x - chipMin.x, chipMax.y - chipMin.y));
					if (a_tooltip && ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", a_tooltip);
					}

					drawList->AddText(ImVec2(chipMin.x + style.FramePadding.x, chipMin.y + style.FramePadding.y * 0.5f),
						ImGui::GetColorU32(ImGuiCol_Text), a_text);
				};

				const auto shown = std::min(a_selected.size(), static_cast<size_t>(MULTISELECT_VISIBLE_CHIPS));
				for (size_t i = 0; i < shown; ++i) {
					if (i > 0) {
						ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
					}
					const auto& entry = a_selected[i];
					const bool bKnown = isKnown(entry);
					drawChip(entry.data(), bKnown, bKnown ? nullptr : a_unknownTooltip);
				}

				if (a_selected.size() > shown) {
					std::string rest;
					bool bAnyUnknown = false;
					for (size_t i = shown; i < a_selected.size(); ++i) {
						rest += (rest.empty() ? "" : "\n") + a_selected[i];
						bAnyUnknown = bAnyUnknown || !isKnown(a_selected[i]);
					}
					if (bAnyUnknown && a_unknownTooltip) {
						rest += "\n\n";
						rest += a_unknownTooltip;
					}

					const auto more = std::format("+{}", a_selected.size() - shown);
					ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
					drawChip(more.data(), !bAnyUnknown, rest.data());
				}
			}
			ImGui::EndComboPreview();
		}

		if (bComboOpen) {
			if (ImGui::Selectable(a_manualEntryHint, false)) {
				bOpenManualEntry = true;
			}
			ImGui::Separator();

			for (const auto& item : a_options) {
				bool bSelected = std::ranges::any_of(a_selected, [&item](const std::string& a_entry) {
					return a_entry == item.name;
				});

				ImGui::PushID(item.name.data());
				if (ImGui::Checkbox(item.name.data(), &bSelected)) {
					if (bSelected) {
						a_selected.emplace_back(item.name);
					} else {
						std::erase(a_selected, item.name);
					}
					bEdited = true;
				}
				if (!item.description.empty()) {
					AddTooltip(item.description.data());
				}
				ImGui::PopID();
			}

			if (a_options.empty()) {
				TextUnformattedDisabled("nothing to pick");
			}

			ImGui::EndCombo();
		}

		if (bOpenManualEntry) {
			ImGui::OpenPopup(manualPopupName.data());
		}

		if (ImGui::BeginPopupModal(manualPopupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			static std::string entryName;
			if (ImGui::InputTextWithHint("##MultiSelectEntry", "Type a name...", &entryName, ImGuiInputTextFlags_EnterReturnsTrue)) {
				if (!entryName.empty() && std::ranges::none_of(a_selected, [](const std::string& a_entry) {
						return a_entry == entryName;
					})) {
					a_selected.emplace_back(entryName);
					bEdited = true;
				}
				entryName.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				entryName.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::EndDisabled();
		ImGui::PopID();

		return bEdited;
	}

	void DrawConditionEvaluateResult(ConditionEvaluateResult a_result)
	{
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 15.f);
		ImVec2 indicatorCenter = ImGui::GetCursorScreenPos();
		const float offset = ImGui::GetTextLineHeightWithSpacing() * 0.25f;
		indicatorCenter.y += offset * 2.f;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		switch (a_result) {
		case ConditionEvaluateResult::kSuccess:
			{
				// draw a tick mark
				const ImVec2 points[3] = { ImVec2(indicatorCenter.x - offset, indicatorCenter.y), ImVec2(indicatorCenter.x - offset * 0.5f, indicatorCenter.y + offset), ImVec2(indicatorCenter.x + offset, indicatorCenter.y - offset) };
				drawList->AddPolyline(points, 3, ImGui::GetColorU32(SUCCESS_COLOR), ImDrawFlags_None, 2.f);
				break;
			}
		case ConditionEvaluateResult::kFailure:
			{
				// draw an X
				drawList->AddLine(indicatorCenter - ImVec2(offset, offset), indicatorCenter + ImVec2(offset, offset), ImGui::GetColorU32(FAIL_COLOR), 2.f);
				drawList->AddLine(indicatorCenter - ImVec2(offset, -offset), indicatorCenter + ImVec2(offset, -offset), ImGui::GetColorU32(FAIL_COLOR), 2.f);
				break;
			}
		case ConditionEvaluateResult::kNone:
			break;
		}
		ImGui::NewLine();
	}

	void DrawWarningIcon()
	{
		static constexpr float SQRT3 = 1.73205080757f;

		const float offsetX = (ImGui::GetTextLineHeight() * 2.f / SQRT3) * 0.5f;
		const float offsetY = ImGui::GetTextLineHeight() * 0.5f;
		const auto screenPos = ImGui::GetCursorScreenPos();

		ImVec2 iconCenter = ImGui::GetCursorScreenPos();
		iconCenter.x += offsetX;
		iconCenter.y += ImGui::GetTextLineHeight() * 0.5f;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		// draw triangle
		const ImVec2 p0 = ImVec2(iconCenter.x - offsetX, iconCenter.y + offsetY);
		const ImVec2 p1 = ImVec2(iconCenter.x, iconCenter.y - offsetY);
		const ImVec2 p2 = ImVec2(iconCenter.x + offsetX, iconCenter.y + offsetY);
		drawList->AddTriangleFilled(p0, p1, p2, ImGui::GetColorU32(YELLOW_COLOR));

		// draw an exclamation point inside
		const ImVec2 dotPos = ImVec2(iconCenter.x, iconCenter.y + offsetY * 0.70f);
		drawList->AddCircleFilled(dotPos, offsetX * 0.15f, ImGui::GetColorU32(BLACK_COLOR));
		const ImVec2 rectMin = ImVec2(iconCenter.x - offsetX * 0.1f, iconCenter.y + offsetY * 0.3f);
		const ImVec2 rectMax = ImVec2(iconCenter.x + offsetX * 0.1f, iconCenter.y - offsetY * 0.45f);
		drawList->AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(BLACK_COLOR));

		ImGui::SetCursorScreenPos(ImVec2(screenPos.x + (offsetX * 2.f) + ImGui::GetStyle().ItemSpacing.x, screenPos.y));
	}

	void ButtonWithConfirmationModal(std::string_view a_label, std::string_view a_confirmation, const std::function<void()>& a_func, const ImVec2& a_buttonSize /*= ImVec2(0, 0)*/)
	{
		using namespace ImGui;
		const auto& style = GetStyle();

		const auto popupName = std::format("{}?", a_label);
		if (Button(a_label.data(), a_buttonSize)) {
			if (GetIO().KeyCtrl) {
				// skip confirmation
				a_func();
			} else {
				auto popupPos = GetCursorScreenPos();

				const auto textHeight = CalcTextSize(a_confirmation.data()).y + style.FramePadding.y * 2.f;
				const auto popupHeight = textHeight + GetFrameHeightWithSpacing() * 3;

				if (popupPos.y + popupHeight > GetIO().DisplaySize.y) {
					popupPos.y -= (popupPos.y + popupHeight) - GetIO().DisplaySize.y;
				}
				SetNextWindowPos(popupPos);

				OpenPopup(popupName.data());
			}
		}
		AddTooltip("Hold CTRL to skip the confirmation popup");

		if (BeginPopupModal(popupName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			Text(a_confirmation.data());
			Separator();

			constexpr float buttonWidth = 120.f;
			SetCursorPosX((GetWindowSize().x - (buttonWidth * 2.f) - GetStyle().ItemSpacing.x) * 0.5f);

			if (Button("OK", ImVec2(buttonWidth, 0))) {
				CloseCurrentPopup();
				a_func();
			}
			SetItemDefaultFocus();
			SameLine();
			if (Button("Cancel", ImVec2(buttonWidth, 0))) {
				CloseCurrentPopup();
			}
			EndPopup();
		}
	}

	bool ArrowButtonText(const char* a_label, ImGuiDir a_dir, bool a_bArrowOnRight, const ImVec2& a_sizeArg /*= ImVec2(0, 0)*/, ImGuiButtonFlags a_flags /*= ImGuiButtonFlags_None*/)
	{
		using namespace ImGui;

		ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
			return false;

		const ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID(a_label);
		ImVec2 label_size = CalcTextSize(a_label, nullptr, true);
		ImVec2 text_size = label_size;
		// add arrow size to label size
		float arrowSize = GetFrameHeight();
		if (a_bArrowOnRight) {
			arrowSize -= style.FramePadding.x;
		}
		label_size.x += arrowSize;

		ImVec2 pos = window->DC.CursorPos;
		if ((a_flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset)  // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
			pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
		const ImVec2 size = CalcItemSize(a_sizeArg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

		const ImRect bb(pos, pos + size);
		ItemSize(size, style.FramePadding.y);
		if (!ItemAdd(bb, id))
			return false;

		if (g.LastItemData.ItemFlags & ImGuiItemFlags_ButtonRepeat)
			a_flags |= ImGuiItemFlags_ButtonRepeat;

		bool hovered, held;
		const bool pressed = ButtonBehavior(bb, id, &hovered, &held, a_flags);

		// Render
		const ImU32 bg_col = GetColorU32((held && hovered) ?
											 ImGuiCol_ButtonActive :
										 hovered ?
											 ImGuiCol_ButtonHovered :
											 ImGuiCol_Button);
		const ImU32 text_col = GetColorU32(ImGuiCol_Text);
		RenderNavHighlight(bb, id);
		RenderFrame(bb.Min, bb.Max, bg_col, true, g.Style.FrameRounding);

		if (g.LogEnabled)
			LogSetNextTextDecoration("[", "]");

		if (a_bArrowOnRight) {
			ImVec2 pos_max = bb.Max - style.FramePadding;
			pos_max.x -= arrowSize;
			RenderTextClipped(bb.Min + style.FramePadding, pos_max, a_label, nullptr, &text_size, style.ButtonTextAlign, &bb);
			ImVec2 arrowPos = ImVec2(bb.Max.x, bb.Min.y);
			arrowPos.x -= arrowSize;
			RenderArrow(window->DrawList, arrowPos + ImVec2(0.f, ImMax(0.0f, (size.y - g.FontSize) * 0.5f)), text_col, a_dir);
		} else {
			RenderArrow(window->DrawList, bb.Min + ImVec2(style.FramePadding.x, ImMax(0.0f, (size.y - g.FontSize) * 0.5f)), text_col, a_dir);
			ImVec2 textPos = bb.Min;
			textPos.x += arrowSize;
			RenderTextClipped(textPos + style.FramePadding, bb.Max - style.FramePadding, a_label, nullptr, &text_size, style.ButtonTextAlign, &bb);
		}

		IMGUI_TEST_ENGINE_ITEM_INFO(id, a_label, g.LastItemData.StatusFlags);
		return pressed;
	}

	bool PopupToggleButton(const char* a_label, const char* a_popupId, const ImVec2& a_sizeArg /*= ImVec2(0, 0)*/)
	{
		const auto dir = ImGui::IsPopupOpen(a_popupId) ? ImGuiDir_Down : ImGuiDir_Right;
		return ArrowButtonText(a_label, dir, false, a_sizeArg, ImGuiButtonFlags_None);
	}

	bool InputText(const char* a_label, std::string* a_str, int a_maxLength, ImGuiInputTextFlags a_flags /*= 0*/, ImGuiInputTextCallback a_callback /*= NULL*/, void* a_userData /*= NULL*/)
	{
		if (a_str->size() < a_maxLength) {
			return ImGui::InputText(a_label, a_str, a_flags, a_callback, a_userData);
		}
		return ImGui::InputText(a_label, (char*)a_str->c_str(), a_maxLength + 1, a_flags, a_callback, a_userData);
	}

	bool InputTextMultiline(const char* a_label, std::string* a_str, int a_maxLength, const ImVec2& a_size /*= ImVec2(0, 0)*/, ImGuiInputTextFlags a_flags /*= 0*/, ImGuiInputTextCallback a_callback /*= NULL*/, void* a_userData /*= NULL*/)
	{
		if (a_str->size() < a_maxLength) {
			return ImGui::InputTextMultiline(a_label, a_str, a_size, a_flags, a_callback, a_userData);
		}
		return ImGui::InputTextMultiline(a_label, (char*)a_str->c_str(), a_maxLength + 1, a_size, a_flags, a_callback, a_userData);
	}

	bool InputTextWithHint(const char* a_label, const char* a_hint, std::string* a_str, int a_maxLength, ImGuiInputTextFlags a_flags /*= 0*/, ImGuiInputTextCallback a_callback /*= NULL*/, void* a_userData /*= NULL*/)
	{
		if (a_str->size() < a_maxLength) {
			return ImGui::InputTextWithHint(a_label, a_hint, a_str, a_flags, a_callback, a_userData);
		}
		return ImGui::InputTextWithHint(a_label, a_hint, (char*)a_str->c_str(), a_maxLength + 1, a_flags, a_callback, a_userData);
	}

#define IM_VK_KEYPAD_ENTER (VK_RETURN + 256)

	std::string GetKeyName(const uint32_t a_keycode)
	{
		uint32_t key = MapVirtualKeyEx(a_keycode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));

		switch (a_keycode) {
		case DIK_LEFTARROW:
			key = VK_LEFT;
			break;
		case DIK_RIGHTARROW:
			key = VK_RIGHT;
			break;
		case DIK_UPARROW:
			key = VK_UP;
			break;
		case DIK_DOWNARROW:
			key = VK_DOWN;
			break;
		case DIK_DELETE:
			key = VK_DELETE;
			break;
		case DIK_END:
			key = VK_END;
			break;
		case DIK_HOME:
			key = VK_HOME;
			break;  // pos1
		case DIK_PRIOR:
			key = VK_PRIOR;
			break;  // page up
		case DIK_NEXT:
			key = VK_NEXT;
			break;  // page down
		case DIK_INSERT:
			key = VK_INSERT;
			break;
		case DIK_NUMPAD0:
			key = VK_NUMPAD0;
			break;
		case DIK_NUMPAD1:
			key = VK_NUMPAD1;
			break;
		case DIK_NUMPAD2:
			key = VK_NUMPAD2;
			break;
		case DIK_NUMPAD3:
			key = VK_NUMPAD3;
			break;
		case DIK_NUMPAD4:
			key = VK_NUMPAD4;
			break;
		case DIK_NUMPAD5:
			key = VK_NUMPAD5;
			break;
		case DIK_NUMPAD6:
			key = VK_NUMPAD6;
			break;
		case DIK_NUMPAD7:
			key = VK_NUMPAD7;
			break;
		case DIK_NUMPAD8:
			key = VK_NUMPAD8;
			break;
		case DIK_NUMPAD9:
			key = VK_NUMPAD9;
			break;
		case DIK_DECIMAL:
			key = VK_DECIMAL;
			break;
		case DIK_NUMPADENTER:
			key = IM_VK_KEYPAD_ENTER;
			break;
		case DIK_RMENU:
			key = VK_RMENU;
			break;  // right alt
		case DIK_RCONTROL:
			key = VK_RCONTROL;
			break;  // right control
		case DIK_LWIN:
			key = VK_LWIN;
			break;  // left win
		case DIK_RWIN:
			key = VK_RWIN;
			break;  // right win
		case DIK_APPS:
			key = VK_APPS;
			break;
		default:
			break;
		}

		if (key >= 256)
			return std::string();

		static const char* keyboard_keys_german[256] = {
			"", "Left Mouse", "Right Mouse", "Cancel", "Middle Mouse", "X1 Mouse", "X2 Mouse", "", "Backspace", "Tab", "", "", "Clear", "Eingabe", "", "",
			"Shift", "Strg", "Alt", "Pause", "Caps Lock", "", "", "", "", "", "", "Escape", "", "", "", "",
			"Leertaste", "Bild auf", "Bild ab", "Ende", "Pos 1", "Left Arrow", "Up Arrow", "Right Arrow", "Down Arrow", "Select", "", "", "Druck", "Einfg", "Entf", "Hilfe",
			"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
			"", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
			"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Windows", "Right Windows", "Apps", "", "Sleep",
			"Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad *", "Numpad +", "", "Numpad -", "Numpad ,", "Numpad /",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
			"F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "", "", "", "", "", "", "", "",
			"Num Lock", "Rollen", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
			"Shift links", "Shift rechts", "Strg links", "Strg rechts", "Alt links", "Alt rechts", "Browser Back", "Browser Forward", "Browser Refresh", "Browser Stop", "Browser Search", "Browser Favorites", "Browser Home", "Volume Mute", "Volume Down", "Volume Up",
			"Next Track", "Previous Track", "Media Stop", "Media Play/Pause", "Mail", "Media Select", "Launch App 1", "Launch App 2", "", "", "Ü", "OEM +", "OEM ,", "OEM -", "OEM .", "OEM #",
			"Ö", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
			"", "", "", "", "", "", "", "", "", "", "", "OEM ß", "OEM ^", "OEM ´", "Ä", "OEM 8",
			"", "", "OEM <", "", "", "", "", "", "", "", "", "", "", "", "", "",
			"", "", "", "", "", "", "Attn", "CrSel", "ExSel", "Erase EOF", "Play", "Zoom", "", "PA1", "OEM Clear", ""
		};
		static const char* keyboard_keys_international[256] = {
			"", "Left Mouse", "Right Mouse", "Cancel", "Middle Mouse", "X1 Mouse", "X2 Mouse", "", "Backspace", "Tab", "", "", "Clear", "Enter", "", "",
			"Shift", "Control", "Alt", "Pause", "Caps Lock", "", "", "", "", "", "", "Escape", "", "", "", "",
			"Space", "Page Up", "Page Down", "End", "Home", "Left Arrow", "Up Arrow", "Right Arrow", "Down Arrow", "Select", "", "", "Print Screen", "Insert", "Delete", "Help",
			"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
			"", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
			"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Windows", "Right Windows", "Apps", "", "Sleep",
			"Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad *", "Numpad +", "", "Numpad -", "Numpad Decimal", "Numpad /",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
			"F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "", "", "", "", "", "", "", "",
			"Num Lock", "Scroll Lock", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
			"Left Shift", "Right Shift", "Left Control", "Right Control", "Left Menu", "Right Menu", "Browser Back", "Browser Forward", "Browser Refresh", "Browser Stop", "Browser Search", "Browser Favorites", "Browser Home", "Volume Mute", "Volume Down", "Volume Up",
			"Next Track", "Previous Track", "Media Stop", "Media Play/Pause", "Mail", "Media Select", "Launch App 1", "Launch App 2", "", "", "OEM ;", "OEM +", "OEM ,", "OEM -", "OEM .", "OEM /",
			"OEM ~", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
			"", "", "", "", "", "", "", "", "", "", "", "OEM [", "OEM \\", "OEM ]", "OEM '", "OEM 8",
			"", "", "OEM <", "", "", "", "", "", "", "", "", "", "", "", "", "",
			"", "", "", "", "", "", "Attn", "CrSel", "ExSel", "Erase EOF", "Play", "Zoom", "", "PA1", "OEM Clear", ""
		};

		const LANGID language = LOWORD(GetKeyboardLayout(0));

		return ((language & 0xFF) == LANG_GERMAN) ?
		           keyboard_keys_german[key] :
		           keyboard_keys_international[key];
	}

	std::string GetKeyName(const uint32_t a_key[4])
	{
		return (a_key[1] ? "Ctrl + " : std::string()) + (a_key[2] ? "Shift + " : std::string()) + (a_key[3] ? "Alt + " : std::string()) + GetKeyName(a_key[0]);
	}

	bool InputKey(const char* a_label, uint32_t a_key[4])
	{
		bool ret = false;
		char buf[48];
		buf[0] = '\0';
		if (a_key[0] || a_key[1] || a_key[2] || a_key[3]) {
			buf[GetKeyName(a_key).copy(buf, sizeof(buf) - 1)] = '\0';
		}
		ImGui::InputTextWithHint(a_label, "Click to set keyboard shortcut", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_NoHorizontalScroll);

		if (ImGui::IsItemActive()) {
			const auto lastKeyPressed = UIManager::GetSingleton().GetLastKeyPressed();
			if (lastKeyPressed != 0) {
				const auto& io = ImGui::GetIO();

				uint32_t newKey[4];
				newKey[0] = lastKeyPressed;
				newKey[1] = io.KeyCtrl;
				newKey[2] = io.KeyShift;
				newKey[3] = io.KeyAlt;

				if (newKey[0] != a_key[0] || newKey[1] != a_key[1] || newKey[2] != a_key[2] || newKey[3] != a_key[3]) {
					a_key[0] = newKey[0];
					a_key[1] = newKey[1];
					a_key[2] = newKey[2];
					a_key[3] = newKey[3];
					ret = true;
				}
			}
		} else {
			AddTooltip("Click in the field and press any key to change the shortcut to that key.");
		}

		return ret;
	}

	void ScaleAllSizes(ImGuiStyle& a_style, float a_scaleFactor)
	{
		a_style.WindowPadding = ImFloor(a_style.WindowPadding * a_scaleFactor);
		a_style.WindowRounding = ImFloor(a_style.WindowRounding * a_scaleFactor);
		a_style.WindowMinSize = ImFloor(a_style.WindowMinSize * a_scaleFactor);
		a_style.ChildRounding = ImFloor(a_style.ChildRounding * a_scaleFactor);
		a_style.PopupRounding = ImFloor(a_style.PopupRounding * a_scaleFactor);
		a_style.FramePadding = ImFloor(a_style.FramePadding * a_scaleFactor);
		a_style.FrameRounding = ImFloor(a_style.FrameRounding * a_scaleFactor);
		a_style.ItemSpacing = ImFloor(a_style.ItemSpacing * a_scaleFactor);
		a_style.ItemInnerSpacing = ImFloor(a_style.ItemInnerSpacing * a_scaleFactor);
		a_style.CellPadding = ImFloor(a_style.CellPadding * a_scaleFactor);
		a_style.TouchExtraPadding = ImFloor(a_style.TouchExtraPadding * a_scaleFactor);
		a_style.IndentSpacing = ImFloor(a_style.IndentSpacing * a_scaleFactor);
		a_style.ColumnsMinSpacing = ImFloor(a_style.ColumnsMinSpacing * a_scaleFactor);
		a_style.ScrollbarSize = ImFloor(a_style.ScrollbarSize * a_scaleFactor);
		a_style.ScrollbarRounding = ImFloor(a_style.ScrollbarRounding * a_scaleFactor);
		a_style.GrabMinSize = ImFloor(a_style.GrabMinSize * a_scaleFactor);
		a_style.GrabRounding = ImFloor(a_style.GrabRounding * a_scaleFactor);
		a_style.LogSliderDeadzone = ImFloor(a_style.LogSliderDeadzone * a_scaleFactor);
		a_style.TabRounding = ImFloor(a_style.TabRounding * a_scaleFactor);
		a_style.TabCloseButtonMinWidthUnselected = (a_style.TabCloseButtonMinWidthUnselected != FLT_MAX) ? ImFloor(a_style.TabCloseButtonMinWidthUnselected * a_scaleFactor) : FLT_MAX;
		a_style.DisplayWindowPadding = ImFloor(a_style.DisplayWindowPadding * a_scaleFactor);
		a_style.DisplaySafeAreaPadding = ImFloor(a_style.DisplaySafeAreaPadding * a_scaleFactor);
	}

	bool FuzzySearch(char const* a_pattern, char const* a_haystack, int& a_outScore, unsigned char a_matches[], int a_maxMatches, int& a_outMatches)
	{
		int recursionCount = 0;
		int recursionLimit = 10;
		a_outMatches = 0;
		return FuzzySearchRecursive(a_pattern, a_haystack, a_outScore, a_haystack, nullptr, a_matches, a_maxMatches, a_outMatches, recursionCount, recursionLimit);
	}

	bool FuzzySearchRecursive(const char* a_pattern, const char* a_src, int& a_outScore, const char* a_strBegin, const unsigned char a_srcMatches[], unsigned char a_newMatches[], int a_maxMatches, int& a_nextMatch, int& a_recursionCount, int a_recursionLimit)
	{
		// Count recursions
		++a_recursionCount;
		if (a_recursionCount >= a_recursionLimit) {
			return false;
		}

		// Detect end of strings
		if (*a_pattern == '\0' || *a_src == '\0') {
			return false;
		}

		// Recursion params
		bool recursiveMatch = false;
		unsigned char bestRecursiveMatches[256];
		int bestRecursiveScore = 0;

		// Loop through a_pattern and str looking for a match
		bool firstMatch = true;
		while (*a_pattern != '\0' && *a_src != '\0') {
			// Found match
			if (tolower(*a_pattern) == tolower(*a_src)) {
				// Supplied matches buffer was too short
				if (a_nextMatch >= a_maxMatches) {
					return false;
				}

				// "Copy-on-Write" a_srcMatches into matches
				if (firstMatch && a_srcMatches) {
					memcpy(a_newMatches, a_srcMatches, a_nextMatch);
					firstMatch = false;
				}

				// Recursive call that "skips" this match
				unsigned char recursiveMatches[256];
				int recursiveScore;
				int recursiveNextMatch = a_nextMatch;
				if (FuzzySearchRecursive(a_pattern, a_src + 1, recursiveScore, a_strBegin, a_newMatches, recursiveMatches, sizeof(recursiveMatches), recursiveNextMatch, a_recursionCount, a_recursionLimit)) {
					// Pick the best recursive score
					if (!recursiveMatch || recursiveScore > bestRecursiveScore) {
						memcpy(bestRecursiveMatches, recursiveMatches, 256);
						bestRecursiveScore = recursiveScore;
					}
					recursiveMatch = true;
				}

				// Advance
				a_newMatches[a_nextMatch++] = (unsigned char)(a_src - a_strBegin);
				++a_pattern;
			}
			++a_src;
		}

		// Determine if full a_pattern was matched
		bool matched = *a_pattern == '\0';

		// Calculate score
		if (matched) {
			const int sequentialBonus = 15;   // bonus for adjacent matches
			const int separatorBonus = 30;    // bonus if match occurs after a separator
			const int camelBonus = 30;        // bonus if match is uppercase and prev is lower
			const int firstLetterBonus = 15;  // bonus if the first letter is matched

			const int leadingLetterPenalty = -5;      // penalty applied for every letter in str before the first match
			const int maxLeadingLetterPenalty = -15;  // maximum penalty for leading letters
			const int unmatchedLetterPenalty = -1;    // penalty for every letter that doesn't matter

			// Iterate str to end
			while (*a_src != '\0') {
				++a_src;
			}

			// Initialize score
			a_outScore = 100;

			// Apply leading letter penalty
			int penalty = leadingLetterPenalty * a_newMatches[0];
			if (penalty < maxLeadingLetterPenalty) {
				penalty = maxLeadingLetterPenalty;
			}
			a_outScore += penalty;

			// Apply unmatched penalty
			int unmatched = (int)(a_src - a_strBegin) - a_nextMatch;
			a_outScore += unmatchedLetterPenalty * unmatched;

			// Apply ordering bonuses
			for (int i = 0; i < a_nextMatch; ++i) {
				unsigned char currIdx = a_newMatches[i];

				if (i > 0) {
					unsigned char prevIdx = a_newMatches[i - 1];

					// Sequential
					if (currIdx == (prevIdx + 1))
						a_outScore += sequentialBonus;
				}

				// Check for bonuses based on neighbor character value
				if (currIdx > 0) {
					// Camel case
					char neighbor = a_strBegin[currIdx - 1];
					char curr = a_strBegin[currIdx];
					if (::islower(neighbor) && ::isupper(curr)) {
						a_outScore += camelBonus;
					}

					// Separator
					bool neighborSeparator = neighbor == '_' || neighbor == ' ';
					if (neighborSeparator) {
						a_outScore += separatorBonus;
					}
				} else {
					// First letter
					a_outScore += firstLetterBonus;
				}
			}
		}

		// Return best result
		if (recursiveMatch && (!matched || bestRecursiveScore > a_outScore)) {
			// Recursive score is better than "this"
			memcpy(a_newMatches, bestRecursiveMatches, a_maxMatches);
			a_outScore = bestRecursiveScore;
			return true;
		} else if (matched) {
			// "this" score is better than recursive
			return true;
		} else {
			// no match
			return false;
		}
	}

	void SetScrollToComboItemJump(ImGuiWindow* a_listbox_window, int a_index)
	{
		const ImGuiContext& g = *GImGui;
		float spacing_y = ImMax(a_listbox_window->WindowPadding.y, g.Style.ItemSpacing.y);
		float temp_pos = (g.FontSize + g.Style.ItemSpacing.y) * a_index;
		float new_pos = ImLerp(temp_pos - spacing_y, temp_pos + g.FontSize + g.Style.ItemSpacing.y + spacing_y, 0.5f) - a_listbox_window->Scroll.y;
		ImGui::SetScrollFromPosY(a_listbox_window, new_pos + 2.50f, 0.5f);
	}

	void SetScrollToComboItemUp(ImGuiWindow* a_listbox_window, int a_index)
	{
		const ImGuiContext& g = *GImGui;
		float item_pos = (g.FontSize + g.Style.ItemSpacing.y) * a_index;
		float diff = item_pos - a_listbox_window->Scroll.y;
		if (diff < 0.0f) {
			a_listbox_window->Scroll.y += diff - 1.0f;
		}
	}

	void SetScrollToComboItemDown(ImGuiWindow* a_listbox_window, int a_index)
	{
		const ImGuiContext& g = *GImGui;
		const float item_pos_lower = (g.FontSize + g.Style.ItemSpacing.y) * (a_index + 1);
		const float diff = item_pos_lower - a_listbox_window->Size.y - a_listbox_window->Scroll.y;
		if (diff > 0.0f) {
			a_listbox_window->Scroll.y += diff + 1.0f;
		}
	}

	bool TreeNodeCollapsedLeaf(const char* a_label, ImGuiTreeNodeFlags a_flags)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		return TreeNodeCollapsedLeafBehavior(window->GetID(a_label), a_flags, a_label);
	}

	bool TreeNodeCollapsedLeaf(const void* a_ptrId, ImGuiTreeNodeFlags a_flags, const char* a_label)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		return TreeNodeCollapsedLeafBehavior(window->GetID(a_ptrId), a_flags, a_label);
	}

	bool TreeNodeCollapsedLeafBehavior(ImGuiID a_id, ImGuiTreeNodeFlags a_flags, const char* a_label)
	{
		using namespace ImGui;

		const ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const bool display_frame = (a_flags & ImGuiTreeNodeFlags_Framed) != 0;
		const ImVec2 padding = (display_frame || (a_flags & ImGuiTreeNodeFlags_FramePadding)) ? style.FramePadding : ImVec2(style.FramePadding.x, ImMin(window->DC.CurrLineTextBaseOffset, style.FramePadding.y));

		const char* label_end = FindRenderedTextEnd(a_label);
		const ImVec2 label_size = CalcTextSize(a_label, label_end, false);

		// We vertically grow up to current line height up the typical widget height.
		const float frame_height = ImMax(ImMin(window->DC.CurrLineSize.y, g.FontSize + style.FramePadding.y * 2), label_size.y + padding.y * 2);
		ImRect frame_bb;
		frame_bb.Min.x = (a_flags & ImGuiTreeNodeFlags_SpanFullWidth) ? window->WorkRect.Min.x : window->DC.CursorPos.x;
		frame_bb.Min.y = window->DC.CursorPos.y;
		frame_bb.Max.x = window->WorkRect.Max.x;
		frame_bb.Max.y = window->DC.CursorPos.y + frame_height;
		if (display_frame) {
			// Framed header expand a little outside the default padding, to the edge of InnerClipRect
			// (FIXME: May remove this at some point and make InnerClipRect align with WindowPadding.x instead of WindowPadding.x*0.5f)
			frame_bb.Min.x -= IM_TRUNC(window->WindowPadding.x * 0.5f - 1.0f);
			frame_bb.Max.x += IM_TRUNC(window->WindowPadding.x * 0.5f);
		}

		const float text_offset_x = g.FontSize + (display_frame ? padding.x * 3 : padding.x * 2);           // Collapser arrow width + Spacing
		const float text_offset_y = ImMax(padding.y, window->DC.CurrLineTextBaseOffset);                    // Latch before ItemSize changes it
		const float text_width = g.FontSize + (label_size.x > 0.0f ? label_size.x + padding.x * 2 : 0.0f);  // Include collapser
		ImVec2 text_pos(window->DC.CursorPos.x + text_offset_x, window->DC.CursorPos.y + text_offset_y);
		ItemSize(ImVec2(text_width, frame_height), padding.y);

		// For regular tree nodes, we arbitrary allow to click past 2 worth of ItemSpacing
		ImRect interact_bb = frame_bb;
		if (!display_frame && (a_flags & (ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_SpanFullWidth)) == 0)
			interact_bb.Max.x = frame_bb.Min.x + text_width + style.ItemSpacing.x * 2.0f;

		const bool item_add = ItemAdd(interact_bb, a_id);
		g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HasDisplayRect;
		g.LastItemData.DisplayRect = frame_bb;

		if (!item_add) {
			IMGUI_TEST_ENGINE_ITEM_INFO(g.LastItemData.ID, label, g.LastItemData.StatusFlags);
			return false;
		}

		ImGuiButtonFlags button_flags = ImGuiTreeNodeFlags_None;
		if (a_flags & ImGuiTreeNodeFlags_AllowOverlap)
			button_flags |= ImGuiButtonFlags_AllowOverlap;

		if (window != g.HoveredWindow)
			button_flags |= ImGuiButtonFlags_NoKeyModsAllowed;

		if (a_flags & ImGuiTreeNodeFlags_OpenOnDoubleClick)
			button_flags |= ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnDoubleClick;
		else
			button_flags |= ImGuiButtonFlags_PressedOnClickRelease;

		const bool selected = (a_flags & ImGuiTreeNodeFlags_Selected) != 0;
		const bool was_selected = selected;

		bool hovered, held;
		ButtonBehavior(interact_bb, a_id, &hovered, &held, button_flags);

		if (a_flags & ImGuiTreeNodeFlags_AllowOverlap)
			SetNextItemAllowOverlap();

		// In this branch, TreeNodeBehavior() cannot toggle the selection so this will never trigger.
		if (selected != was_selected)  //-V547
			g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_ToggledSelection;

		// Render
		const ImU32 text_col = GetColorU32(ImGuiCol_Text);
		const ImGuiNavRenderCursorFlags nav_highlight_flags = ImGuiNavRenderCursorFlags_Compact;
		if (display_frame) {
			// Framed type
			const ImU32 bg_col = GetColorU32((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered :
																								   ImGuiCol_Header);
			RenderFrame(frame_bb.Min, frame_bb.Max, bg_col, true, style.FrameRounding);
			RenderNavHighlight(frame_bb, a_id, nav_highlight_flags);
			if (a_flags & ImGuiTreeNodeFlags_Bullet)
				RenderBullet(window->DrawList, ImVec2(text_pos.x - text_offset_x * 0.60f, text_pos.y + g.FontSize * 0.5f), text_col);
			else  // Leaf without bullet, left-adjusted text
				text_pos.x -= text_offset_x;
			if (a_flags & ImGuiTreeNodeFlags_ClipLabelForTrailingButton)
				frame_bb.Max.x -= g.FontSize + style.FramePadding.x;

			if (g.LogEnabled)
				LogSetNextTextDecoration("###", "###");
			RenderTextClipped(text_pos, frame_bb.Max, a_label, label_end, &label_size);
		} else {
			// Unframed typed for tree nodes
			if (hovered || selected) {
				const ImU32 bg_col = GetColorU32((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered :
																									   ImGuiCol_Header);
				RenderFrame(frame_bb.Min, frame_bb.Max, bg_col, false);
			}
			RenderNavHighlight(frame_bb, a_id, nav_highlight_flags);
			if (a_flags & ImGuiTreeNodeFlags_Bullet)
				RenderBullet(window->DrawList, ImVec2(text_pos.x - text_offset_x * 0.5f, text_pos.y + g.FontSize * 0.5f), text_col);
			if (g.LogEnabled)
				LogSetNextTextDecoration(">", nullptr);
			RenderText(text_pos, a_label, label_end, false);
		}

		IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
		return false;
	}
}

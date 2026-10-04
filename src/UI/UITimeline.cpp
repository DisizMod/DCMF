#include "UIMain.h"

#include <imgui_internal.h>

#include "Jobs.h"
#include "UICommon.h"
#include "UIManager.h"
#include "../BaseChannels.h"
#include "../Channels.h"
#include "../ClipPlayer.h"
#include "../Functions.h"
#include "../Preview.h"
#include "../Resolve.h"
#include "../Timing.h"
#include "../apply/Entries.h"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace UI
{
	namespace
	{
		// The sheet's fixed widths: the label column, and the room past the end
		constexpr float kLabelWidth = 190.f;
		constexpr float kTailWidth = 24.f;

		// The dots' colours in the frame rows: moving and held
		constexpr ImU32 kMovingColour = IM_COL32(255, 191, 102, 255);
		constexpr ImU32 kHeldColour = IM_COL32(115, 179, 255, 255);
		constexpr ImU32 kCurveColour = IM_COL32(200, 200, 200, 255);

		// A little room past the end, and never shorter than a second
		float SpanOf(float a_length)
		{
			return (std::max)(a_length, 1.f) + 0.25f;
		}

		// The ruler's tick spacing for the zoom: the smallest that keeps ticks this far apart
		float TickStep(float a_pixelsPerSecond)
		{
			constexpr float kSteps[]{ 0.05f, 0.1f, 0.25f, 0.5f, 1.f, 2.f, 5.f, 10.f, 30.f };
			for (const auto step : kSteps) {
				if (step * a_pixelsPerSecond >= 60.f) {
					return step;
				}
			}
			return 60.f;
		}

		// A dragged time lands on a fifth of a tick, or anywhere with Ctrl
		float Snapped(float a_time, float a_pixelsPerSecond)
		{
			a_time = (std::max)(a_time, 0.f);
			if (ImGui::GetIO().KeyCtrl) {
				return a_time;
			}
			const float snap = TickStep(a_pixelsPerSecond) / 5.f;
			return std::round(a_time / snap) * snap;
		}

		float RestOf(const Apply::Entry& a_entry)
		{
			return a_entry.neutral < a_entry.low ? a_entry.start : a_entry.neutral;
		}

		Channels::Channel* RowOf(Keyframe& a_keyframe, Apply::ChannelId a_id)
		{
			Channels::Channel* found = nullptr;
			a_keyframe.channels->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_row) {
				if (!a_row->IsGroup() && static_cast<Channels::Channel*>(a_row.get())->GetId() == a_id) {
					found = static_cast<Channels::Channel*>(a_row.get());
					return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return found;
		}

		// A row of the sheet: a claimed channel, or the heading of the category the rows under it are in
		struct SheetRow
		{
			Apply::ChannelId id;
			std::string heading;
			bool bIgnored = false;
			int rank = 0;
		};

		void CollectClaims(Channels::ChannelSet* a_set, bool a_bOff, std::vector<SheetRow>& a_out)
		{
			a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
				if (a_entry->IsGroup()) {
					CollectClaims(static_cast<Channels::GroupChannel*>(a_entry.get())->channels.get(), a_bOff || a_entry->IsDisabled(), a_out);
				} else if (const auto id = static_cast<Channels::Channel*>(a_entry.get())->GetId(); id.IsValid()) {
					a_out.push_back({ id, {}, a_bOff || a_entry->IsDisabled() });
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// A keyframe's JSON onto a frame already there: its hold, its rows by channel, its functions; the time stays
		void PasteOnto(Clip* a_clip, Keyframe& a_keyframe, const std::string& a_json)
		{
			rapidjson::Document doc;
			doc.Parse(a_json.data());
			if (!doc.IsObject()) {
				return;
			}
			const auto hold = doc.FindMember("hold");
			a_keyframe.bHold = hold != doc.MemberEnd() && hold->value.IsNumber();
			if (a_keyframe.bHold) {
				a_keyframe.holdTime = hold->value.GetFloat();
			}
			if (const auto channels = doc.FindMember("channels"); channels != doc.MemberEnd() && channels->value.IsArray()) {
				for (const auto& value : channels->value.GetArray()) {
					if (auto pasted = Channels::CreateChannelFromJson(value); pasted && !pasted->IsGroup()) {
						if (auto* row = RowOf(a_keyframe, static_cast<Channels::Channel*>(pasted.get())->GetId())) {
							row->Parse(value);
							row->SetDisabled(pasted->IsDisabled());
						}
					}
				}
			}
			a_keyframe.functions = std::make_unique<Functions::FunctionSet>(a_clip, Functions::FunctionSetType::kOnActivate);
			if (const auto functions = doc.FindMember("functions"); functions != doc.MemberEnd() && functions->value.IsArray()) {
				for (auto& value : functions->value.GetArray()) {
					if (auto function = Functions::CreateFunctionFromJson(value, a_keyframe.functions.get())) {
						a_keyframe.functions->Add(function);
					}
				}
			}
		}
	}

	void UIMain::DrawDockBottomButton(bool& a_bDockBottom, float& a_dockHeight)
	{
		// In the title bar, left of the close box and its size: an outline with a bar along its bottom
		auto* window = ImGui::GetCurrentWindow();
		if (!window) {
			return;
		}
		const auto& style = ImGui::GetStyle();
		const ImRect title = window->TitleBarRect();
		const float size = ImGui::GetFontSize();
		const float closeX = title.Max.x - style.FramePadding.x - size;
		const ImVec2 min(closeX - style.ItemInnerSpacing.x - size, title.Min.y + style.FramePadding.y);
		const ImRect box(min, ImVec2(min.x + size, min.y + size));

		ImGui::PushClipRect(title.Min, title.Max, false);
		const ImGuiID id = window->GetID("##dockBottom");
		ImGui::ItemAdd(box, id);
		bool bHovered = false;
		bool bHeld = false;
		if (ImGui::ButtonBehavior(box, id, &bHovered, &bHeld)) {
			a_bDockBottom = true;
			a_dockHeight = window->Size.y;
		}
		if (bHovered || bHeld) {
			window->DrawList->AddRectFilled(box.Min, box.Max, ImGui::GetColorU32(bHeld ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), style.FrameRounding);
		}
		const ImU32 colour = ImGui::GetColorU32(ImGuiCol_Text);
		const float inset = size * 0.2f;
		const ImVec2 a(box.Min.x + inset, box.Min.y + inset);
		const ImVec2 b(box.Max.x - inset, box.Max.y - inset);
		window->DrawList->AddRect(a, b, colour, 0.f, 0, 1.f);
		window->DrawList->AddRectFilled(ImVec2(a.x, b.y - (b.y - a.y) * 0.35f), b, colour);
		ImGui::PopClipRect();
		if (bHovered) {
			ImGui::SetTooltip("The screen's full width, on its bottom edge");
		}
	}

	void UIMain::DrawTimelineButton(Clip* a_clip, std::uint16_t a_variant, bool a_bInHeader)
	{
		const auto& style = ImGui::GetStyle();
		const bool bPreview = UIManager::GetSingleton().GetRefrToEvaluate() != nullptr;
		if (a_bInHeader) {
			// Beside the header's Preview, which takes the row's end
			const float previewWidth = bPreview ? ImGui::CalcTextSize("Preview").x + style.FramePadding.x * 2 + style.ItemSpacing.x : 0.f;
			ImGui::SameLine(ImGui::GetContentRegionMax().x - previewWidth - ImGui::CalcTextSize("Timeline").x - style.FramePadding.x * 2 - style.ItemSpacing.x);
		}
		ImGui::PushID(a_variant);
		if (a_bInHeader ? ImGui::SmallButton("Timeline") : ImGui::Button("Timeline")) {
			if (_timeline.clip != a_clip || _timeline.variant != a_variant) {
				_timeline.selected = nullptr;
				_timeline.bFrameOpen = false;
			}
			_timeline.clip = a_clip;
			_timeline.variant = a_variant;
			_timeline.bOpen = true;
			ImGui::SetWindowFocus("Timeline");
		}
		ImGui::PopID();
		UICommon::AddTooltip("Opens these frames on the timeline, where each channel's values are drawn over time and a frame is dragged to retime it.");
	}

	void UIMain::DrawTimeline()
	{
		auto& state = _timeline;
		if (!state.bOpen || !state.clip) {
			return;
		}

		// The clip still loaded: found among the mods, or the timeline closes
		bool bFound = false;
		ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
			a_mod->ForEachSubMod([&](SubMod* a_subMod) {
				if (a_subMod == state.clip) {
					bFound = true;
					return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		});
		if (!bFound) {
			state = {};
			return;
		}
		auto* clip = state.clip;
		auto& tracks = clip->GetTracks();
		if (state.variant >= tracks.size()) {
			state.variant = 0;
		}
		auto& track = tracks[state.variant];
		const bool bEditable = _editMode != EditMode::kNone;
		auto* refr = UIManager::GetSingleton().GetRefrToEvaluate();

		ImGui::SetNextWindowSize(ImVec2(900.f, 380.f), ImGuiCond_FirstUseEver);
		// Docked at the bottom when asked: the screen's width, its own height, on the screen's bottom edge
		if (state.bDockBottom) {
			state.bDockBottom = false;
			const auto display = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowPos(ImVec2(0.f, display.y), ImGuiCond_Always, ImVec2(0.f, 1.f));
			ImGui::SetNextWindowSize(ImVec2(display.x, state.dockHeight), ImGuiCond_Always);
		}
		const bool bShown = ImGui::Begin("Timeline", &state.bOpen, ImGuiWindowFlags_NoSavedSettings);
		DrawDockBottomButton(state.bDockBottom, state.dockHeight);
		if (bShown) {
			// What is shown, and how it is looked at
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(std::format("{} / {}", clip->GetParentMod() ? clip->GetParentMod()->GetName() : ""sv, clip->GetName()).data());
			if (tracks.size() > 1) {
				ImGui::SameLine();
				UICommon::TextUnformattedDisabled(std::format("variant #{}", state.variant + 1).data());
			}
			ImGui::SameLine();
			UICommon::TextUnformattedDisabled(std::format("{} frame{}", track.keyframes.size(), track.keyframes.size() == 1 ? "" : "s").data());

			ImGui::SameLine(0.f, 24.f);
			ImGui::Checkbox("Scroll", &state.bScroll);
			ImGui::SameLine();
			UICommon::HelpMarker("Off, the whole clip fits the window. On, it keeps its zoom and scrolls: Ctrl+wheel zooms about the mouse, the wheel pans. A dragged frame lands on a fifth of a ruler tick; hold Ctrl for none.");

			if (refr) {
				ImGui::SameLine(0.f, 24.f);
				const bool bPlaying = Preview::IsPreviewing(&track);
				if (ImGui::SmallButton(bPlaying ? "Stop" : "Play")) {
					if (bPlaying) {
						Preview::Stop();
					} else {
						Preview::StartClip(refr, clip, state.variant);
					}
				}
				UICommon::AddTooltip("Plays these frames on the selected reference over and over, as the variant's Preview does.");
				if (Preview::HeldAt(clip, state.variant)) {
					ImGui::SameLine();
					if (ImGui::SmallButton("Release")) {
						Preview::Stop();
					}
					UICommon::AddTooltip("Lets go of the pose the ruler holds on the reference.");
				}
			}

			DrawTimelineSheet(clip, track, bEditable);
		}
		ImGui::End();

		DrawTimelineFrame(clip, track);
	}

	void UIMain::DrawTimelineSheet(Clip* a_clip, ClipVariant& a_track, bool a_bEditable)
	{
		auto& state = _timeline;
		auto& entries = Apply::Entries::GetSingleton();
		auto* refr = UIManager::GetSingleton().GetRefrToEvaluate();
		const auto& style = ImGui::GetStyle();
		const float lineHeight = ImGui::GetTextLineHeight();
		const float rulerHeight = lineHeight + 14.f;
		const float numericHeight = lineHeight * 2.4f;
		const float otherHeight = lineHeight + 8.f;
		const float headingHeight = lineHeight + 4.f;
		const float length = ClipPlayer::LengthOf(a_track);
		const float span = SpanOf(length);

		// A frame no longer in the variant is let go
		const auto has = [&](const Keyframe* a_keyframe) {
			return a_keyframe && std::ranges::any_of(a_track.keyframes, [&](const auto& a_entry) { return a_entry.get() == a_keyframe; });
		};
		if (!has(state.selected)) {
			state.selected = nullptr;
			state.bFrameOpen = false;
		}
		if (!has(state.dragging)) {
			state.dragging = nullptr;
		}

		// The rows: every claim, under the channel lists' categories
		std::vector<SheetRow> claims;
		CollectClaims(a_clip->GetChannelSet(), false, claims);
		for (auto& claim : claims) {
			claim.heading = UIChannelComboFilter::CategoryName(entries.Get(claim.id));
			claim.rank = _channelComboFilter.CategoryRank(claim.heading);
		}
		std::ranges::stable_sort(claims, {}, &SheetRow::rank);
		std::vector<SheetRow> rows;
		for (std::size_t i = 0; i < claims.size(); ++i) {
			if (i == 0 || claims[i].heading != claims[i - 1].heading) {
				rows.push_back({ {}, claims[i].heading });
			}
			rows.push_back(claims[i]);
		}
		const auto folded = [&](const std::string& a_heading) { return std::ranges::find(state.folded, a_heading) != state.folded.end(); };
		const auto heightOf = [&](const SheetRow& a_row) {
			if (!a_row.id.IsValid()) {
				return headingHeight;
			}
			return entries.Get(a_row.id).type == Apply::ValueType::kNumber ? numericHeight : otherHeight;
		};
		float sheetHeight = rulerHeight;
		{
			std::string heading;
			for (const auto& row : rows) {
				if (!row.id.IsValid()) {
					heading = row.heading;
				} else if (folded(heading)) {
					continue;
				}
				sheetHeight += heightOf(row);
			}
		}


		// Fitted, the zoom is whatever puts the whole clip in the window
		const float available = ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 2.f - style.ScrollbarSize;
		if (!state.bScroll) {
			state.pixelsPerSecond = std::clamp((available - kLabelWidth - kTailWidth) / span, 10.f, 4000.f);
		}

		const float childHeight = (std::min)(sheetHeight + style.ScrollbarSize + 6.f, ImGui::GetContentRegionAvail().y);
		if (!ImGui::BeginChild("##sheet", ImVec2(0.f, childHeight), ImGuiChildFlags_Borders, (state.bScroll ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None) | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::EndChild();
			return;
		}

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 childPos = ImGui::GetWindowPos();
		const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
		const float wheel = ImGui::GetIO().MouseWheel;
		if (bHovered && wheel != 0.f) {
			if (state.bScroll && ImGui::GetIO().KeyCtrl) {
				// Zoomed about the mouse, the time under it kept where it is
				const float mouseTime = (ImGui::GetMousePos().x - (origin.x + kLabelWidth)) / state.pixelsPerSecond;
				state.pixelsPerSecond = std::clamp(state.pixelsPerSecond * (wheel > 0.f ? 1.25f : 0.8f), 10.f, 4000.f);
				ImGui::SetScrollX((std::max)(mouseTime * state.pixelsPerSecond + kLabelWidth - (ImGui::GetMousePos().x - childPos.x), 0.f));
			} else if (state.bScroll) {
				ImGui::SetScrollX((std::max)(ImGui::GetScrollX() - wheel * 60.f, 0.f));
			} else {
				ImGui::SetScrollY((std::max)(ImGui::GetScrollY() - wheel * 40.f, 0.f));
			}
		}

		const float contentWidth = kLabelWidth + span * state.pixelsPerSecond + kTailWidth;
		ImGui::Dummy(ImVec2(contentWidth, sheetHeight));

		auto* draw = ImGui::GetWindowDrawList();
		const float x0 = origin.x + kLabelWidth;
		const auto xOf = [&](float a_time) { return x0 + a_time * state.pixelsPerSecond; };
		const auto timeOf = [&](float a_x) { return (a_x - x0) / state.pixelsPerSecond; };
		const float labelX = childPos.x;
		const float sheetRight = origin.x + contentWidth;
		const float sheetBottom = origin.y + sheetHeight;

		const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
		const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const ImU32 colRuler = ImGui::GetColorU32(ImGuiCol_FrameBg);
		const ImU32 colRow = ImGui::GetColorU32(ImGuiCol_TableRowBgAlt);
		const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Border);
		const ImU32 colFrame = IM_COL32(200, 200, 200, 70);
		const ImU32 colHandle = IM_COL32(220, 220, 220, 255);
		const ImU32 colSelected = IM_COL32(255, 190, 90, 255);
		const ImU32 colPlayhead = IM_COL32(255, 80, 80, 255);
		const ImU32 colScrub = IM_COL32(90, 170, 255, 255);

		// ---- the ruler, at the top of what shows however far the rows are scrolled: drawn on a layer above them
		const float rulerTop = origin.y + ImGui::GetScrollY();
		const float rulerBottom = rulerTop + rulerHeight;
		const float rowsTop = origin.y + rulerHeight;
		ImDrawListSplitter layers;
		layers.Split(draw, 2);
		layers.SetCurrentChannel(draw, 1);
		// Right of the labels, which the corner covers
		const ImVec2 clipMax(childPos.x + ImGui::GetWindowWidth(), childPos.y + ImGui::GetWindowHeight());
		draw->PushClipRect(ImVec2(labelX + kLabelWidth, childPos.y), clipMax, true);
		draw->AddRectFilled(ImVec2(x0 - kLabelWidth, rulerTop), ImVec2(sheetRight, rulerBottom), colRuler);
		const float step = TickStep(state.pixelsPerSecond);
		for (float t = 0.f; t <= span + 1e-3f; t += step) {
			const float x = xOf(t);
			draw->AddLine(ImVec2(x, rulerBottom - 6.f), ImVec2(x, rulerBottom), colDim);
			draw->AddText(ImVec2(x + 3.f, rulerTop + 2.f), colDim, std::format("{:.2f}", t).data());
		}
		// The clip's end
		draw->AddLine(ImVec2(xOf(length), rulerTop), ImVec2(xOf(length), sheetBottom), colGrid, 2.f);

		// Scrubbing: pressed or dragged on the ruler, the pose at that time is held on the reference
		ImGui::SetCursorScreenPos(ImVec2(x0, rulerTop));
		ImGui::SetNextItemAllowOverlap();
		ImGui::InvisibleButton("##scrub", ImVec2((std::max)(span * state.pixelsPerSecond, 1.f), rulerHeight));
		if (ImGui::IsItemActive() && !state.dragging && refr) {
			Preview::HoldAt(refr, a_clip, state.variant, std::clamp(timeOf(ImGui::GetMousePos().x), 0.f, length));
		}
		if (ImGui::IsItemHovered() && !state.dragging) {
			ImGui::SetTooltip(refr ? "Press or drag to hold the pose at this time on the reference." : "Select a reference to scrub the pose.");
		}

		// ---- frame handles, and their holds
		bool bResort = false;
		for (auto& keyframe : a_track.keyframes) {
			const float x = xOf(keyframe->time);
			const bool bSelected = state.selected == keyframe.get();
			draw->AddLine(ImVec2(x, rulerBottom), ImVec2(x, sheetBottom), colFrame);
			ImGui::PushID(keyframe.get());

			// The hold: a bar from the frame to where it ends, its end dragged to change it
			if (keyframe->bHold) {
				const float end = xOf(keyframe->time + keyframe->holdTime);
				draw->AddRectFilled(ImVec2(x, rulerBottom - 5.f), ImVec2(end, rulerBottom - 1.f), kHeldColour);
				ImGui::SetCursorScreenPos(ImVec2(end - 4.f, rulerBottom - 8.f));
				ImGui::InvisibleButton("##holdEnd", ImVec2(8.f, 8.f));
				if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
					ImGui::SetTooltip("Hold %.2f s", keyframe->holdTime);
				}
				if (ImGui::IsItemActivated()) {
					state.dragging = keyframe.get();
					state.bDraggingHold = true;
					state.dragFrom = keyframe->holdTime;
				}
				if (ImGui::IsItemActive() && state.dragging == keyframe.get() && state.bDraggingHold && a_bEditable) {
					const float hold = Snapped(state.dragFrom + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x / state.pixelsPerSecond, state.pixelsPerSecond);
					if (hold != keyframe->holdTime) {
						keyframe->holdTime = hold;
						a_clip->SetDirty(true);
					}
				}
				if (ImGui::IsItemDeactivated() && state.dragging == keyframe.get()) {
					state.dragging = nullptr;
					state.bDraggingHold = false;
				}
			}

			ImGui::SetCursorScreenPos(ImVec2(x - 6.f, rulerTop));
			ImGui::InvisibleButton("##handle", ImVec2(12.f, rulerHeight - 8.f));
			const bool bHandleHovered = ImGui::IsItemHovered();
			const bool bHandleActive = ImGui::IsItemActive();
			if (ImGui::IsItemActivated()) {
				state.dragging = keyframe.get();
				state.bDraggingHold = false;
				state.dragFrom = keyframe->time;
				state.bDragged = false;
			}
			if (bHandleActive && state.dragging == keyframe.get() && !state.bDraggingHold && a_bEditable) {
				const float delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x;
				if (std::fabs(delta) > 2.f || state.bDragged) {
					state.bDragged = true;
					const float time = Snapped(state.dragFrom + delta / state.pixelsPerSecond, state.pixelsPerSecond);
					if (time != keyframe->time) {
						keyframe->time = time;
						a_clip->SetDirty(true);
					}
					ImGui::SetTooltip("%.2f s%s", time, ImGui::GetIO().KeyCtrl ? "" : "  (Ctrl: no snap)");
				}
			}
			if (ImGui::IsItemDeactivated() && state.dragging == keyframe.get() && !state.bDraggingHold) {
				// Put back in time order once let go: a list sorted under the mouse pulls the handle away
				if (state.bDragged) {
					bResort = true;
				} else {
					state.selected = keyframe.get();
					state.bFrameOpen = true;
				}
				state.dragging = nullptr;
				state.bDragged = false;
			}

			const ImU32 colour = bSelected ? colSelected : bHandleHovered || bHandleActive ? colText : colHandle;
			draw->AddTriangleFilled(ImVec2(x - 6.f, rulerTop + 2.f), ImVec2(x + 6.f, rulerTop + 2.f), ImVec2(x, rulerTop + 11.f), colour);
			draw->AddLine(ImVec2(x, rulerTop + 11.f), ImVec2(x, rulerBottom), colour, 2.f);
			if (bHandleHovered && !state.bDragged) {
				ImGui::SetTooltip("Frame at %.2f s%s\nClick to edit, drag to move, right click for more.", keyframe->time, keyframe->bHold ? std::format(", held {:.2f} s", keyframe->holdTime).data() : "");
			}
			ImGui::PopID();
		}
		if (bResort) {
			a_clip->SortKeyframes(a_track);
		}

		// ---- the rows, under the ruler and cut off where it starts
		draw->PopClipRect();
		layers.SetCurrentChannel(draw, 0);
		draw->PushClipRect(ImVec2(childPos.x, rulerBottom), clipMax, true);
		float y = rowsTop;
		int rowIndex = 0;
		std::string heading;
		for (const auto& row : rows) {
			if (!row.id.IsValid()) {
				// A category's heading: a band across the sheet, clicked to fold its rows away
				heading = row.heading;
				draw->AddRectFilled(ImVec2(x0 - kLabelWidth, y), ImVec2(sheetRight, y + headingHeight), ImGui::GetColorU32(ImGuiCol_Header, 0.35f));
				if (y >= rulerBottom) {
					ImGui::SetCursorScreenPos(ImVec2(labelX, y));
					ImGui::PushID(row.heading.data());
					if (ImGui::InvisibleButton("##fold", ImVec2(kLabelWidth, headingHeight))) {
						if (folded(row.heading)) {
							std::erase(state.folded, row.heading);
						} else {
							state.folded.push_back(row.heading);
						}
					}
					ImGui::PopID();
				}
				y += headingHeight;
				continue;
			}
			if (folded(heading)) {
				continue;
			}
			const auto entry = entries.Get(row.id);
			const float height = heightOf(row);
			const float top = y;
			const float bottom = y + height;
			const float centre = (top + bottom) * 0.5f;
			y = bottom;
			if (rowIndex++ % 2 == 1) {
				draw->AddRectFilled(ImVec2(x0 - kLabelWidth, top), ImVec2(sheetRight, bottom), colRow);
			}
			draw->AddLine(ImVec2(x0 - kLabelWidth, bottom), ImVec2(sheetRight, bottom), colGrid);
			const float alpha = row.bIgnored ? 0.4f : 1.f;
			const auto faded = [&](ImU32 a_colour) {
				ImVec4 colour = ImGui::ColorConvertU32ToFloat4(a_colour);
				colour.w *= alpha;
				return ImGui::ColorConvertFloat4ToU32(colour);
			};

			const float rest = RestOf(entry);

			// Between the keys: what the run has the channel at, coloured by where it is; the keys read once for the row
			const auto keys = ClipPlayer::KeysOf(a_track, row.id);

			// A number's curve, drawn across the channel's own range, the same whatever the keys do
			struct Sample
			{
				float x;
				float value;
				ClipPlayer::Phase phase;
			};
			std::vector<Sample> samples;
			float low = entry.low;
			float high = entry.high;
			if (entry.type == Apply::ValueType::kNumber) {
				for (float x = x0; x <= xOf(length) + 0.5f; x += 3.f) {
					const float t = (std::min)(timeOf(x), length);
					const auto [value, phase] = ClipPlayer::NumberAt(keys, t, rest, rest, refr);
					if (phase == ClipPlayer::Phase::kNone) {
						break;
					}
					samples.push_back({ x, value, phase });
				}
				if (high - low < 1e-3f) {
					high = low + 1.f;
				}
			}
			const auto yOf = [&](float a_value) { return bottom - 4.f - (a_value - low) / (high - low) * (height - 8.f); };

			if (entry.type == Apply::ValueType::kNumber) {
				draw->AddLine(ImVec2(x0, yOf(rest)), ImVec2(xOf(span), yOf(rest)), colGrid);
				ImVec2 last{};
				bool bFirst = true;
				int segment = 0;
				for (const auto& [x, value, phase] : samples) {
					const ImVec2 point(x, yOf(value));
					if (!bFirst) {
						// In and out are from and to whatever is underneath: dashed
						const bool bDashed = phase == ClipPlayer::Phase::kEntering || phase == ClipPlayer::Phase::kExiting;
						const ImU32 colour = phase == ClipPlayer::Phase::kHeld ? kHeldColour : phase == ClipPlayer::Phase::kMoving ? kMovingColour : kCurveColour;
						if (!bDashed || segment % 2 == 0) {
							draw->AddLine(last, point, faded(colour), phase == ClipPlayer::Phase::kHeld ? 3.f : 1.5f);
						}
						++segment;
					}
					last = point;
					bFirst = false;
				}
				// Still set on the last frame: handed back at once at the end
				if (const auto [atEnd, endPhase] = ClipPlayer::NumberAt(keys, length, rest, rest, refr); endPhase == ClipPlayer::Phase::kSteady) {
					draw->AddLine(ImVec2(xOf(length), yOf(atEnd)), ImVec2(xOf(length), yOf(std::clamp(rest, low, high))), faded(kCurveColour), 1.f);
				}
			} else if (entry.type == Apply::ValueType::kColour) {
				// From and back to the reference actor's own colour, as a run with no modifier's colour goes
				const auto own = ClipPlayer::OwnColour(refr, row.id);
				for (float x = x0; x < xOf(length); x += 4.f) {
					const auto [value, phase] = ClipPlayer::ColourAt(keys, timeOf(x), own, own);
					if (value) {
						draw->AddRectFilled(ImVec2(x, centre - 6.f), ImVec2(x + 4.f, centre + 6.f), faded(ImGui::ColorConvertFloat4ToU32(ImVec4(value->red, value->green, value->blue, 1.f))));
					}
					if (phase == ClipPlayer::Phase::kHeld) {
						draw->AddLine(ImVec2(x, centre + 8.f), ImVec2(x + 4.f, centre + 8.f), faded(kHeldColour), 2.f);
					}
				}
			} else {
				// Blocks while a row is in force, each named by its value
				Channels::Channel* current = nullptr;
				float from = 0.f;
				const auto close = [&](float a_to) {
					if (!current) {
						return;
					}
					draw->AddRectFilled(ImVec2(xOf(from), centre - 7.f), ImVec2(xOf(a_to), centre + 7.f), faded(IM_COL32(120, 200, 255, 60)));
					draw->PushClipRect(ImVec2(xOf(from), top), ImVec2(xOf(a_to), bottom), true);
					draw->AddText(ImVec2(xOf(from) + 8.f, centre - lineHeight * 0.5f), faded(colText), current->GetArgument().data());
					draw->PopClipRect();
				};
				for (float x = x0; x <= xOf(length) + 0.5f; x += 3.f) {
					const float t = (std::min)(timeOf(x), length);
					auto* inForce = ClipPlayer::StepAt(keys, t);
					if (inForce != current) {
						close(t);
						current = inForce;
						from = t;
					}
				}
				close(length);
			}

			// The keys: a diamond on each frame that sets the channel, hollow where an earlier hold bars it
			for (auto& keyframe : a_track.keyframes) {
				auto* key = RowOf(*keyframe, row.id);
				if (!key || key->IsDisabled()) {
					continue;
				}
				const bool bBarred = a_clip->HeldBy(a_track, *keyframe, row.id) != nullptr;
				const float x = xOf(keyframe->time);
				float cy = centre;
				if (auto* number = dynamic_cast<Channels::NumberChannel*>(key); number && entry.type == Apply::ValueType::kNumber) {
					cy = yOf(Resolve::NumberValue(number, refr));
				}
				// Under the ruler: neither drawn nor clicked through it
				if (cy - 6.f < rulerBottom) {
					continue;
				}
				ImGui::PushID(keyframe.get());
				ImGui::PushID(static_cast<int>(row.id.raw));
				ImGui::SetCursorScreenPos(ImVec2(x - 6.f, cy - 6.f));
				ImGui::InvisibleButton("##key", ImVec2(12.f, 12.f));
				const bool bKeyHovered = ImGui::IsItemHovered();
				if (ImGui::IsItemClicked()) {
					state.selected = keyframe.get();
					state.bFrameOpen = true;
				}
				const ImU32 colour = faded(state.selected == keyframe.get() ? colSelected : bKeyHovered ? colText : colHandle);
				const ImVec2 points[4]{ ImVec2(x, cy - 5.f), ImVec2(x + 5.f, cy), ImVec2(x, cy + 5.f), ImVec2(x - 5.f, cy) };
				if (bBarred) {
					draw->AddPolyline(points, 4, colour, ImDrawFlags_Closed, 1.5f);
				} else {
					draw->AddConvexPolyFilled(points, 4, colour);
				}
				if (bKeyHovered) {
					ImGui::SetTooltip("%s\n%s at %.2f s%s", entries.Identifier(row.id).data(), key->GetArgument().data(), keyframe->time, bBarred ? "\nBarred by an earlier frame's hold: not played." : "");
				}
				ImGui::PopID();
				ImGui::PopID();
			}
		}
		if (claims.empty()) {
			draw->AddText(ImVec2(x0 + 8.f, rulerBottom + 8.f), colDim, "The clip claims no channel.");
		}

		// ---- the playhead: a preview playing, or the clip running on the reference; and the scrub line. Across the
		// ruler too, on its layer
		draw->PopClipRect();
		layers.SetCurrentChannel(draw, 1);
		draw->PushClipRect(ImVec2(labelX + kLabelWidth, childPos.y), clipMax, true);
		std::optional<float> playhead = Preview::Playhead(a_clip, state.variant);
		if (!playhead && refr) {
			if (const auto actorState = ActorState::Get(refr->GetHandle())) {
				playhead = actorState->clips.With([&](const ActorState::ClipState& a_clips) -> std::optional<float> {
					for (const auto& running : a_clips.running) {
						if (running.clip == a_clip && running.variant == state.variant && !running.bExiting) {
							return Resolve::Now() - running.startedAt;
						}
					}
					return std::nullopt;
				});
			}
		}
		if (playhead) {
			const float x = xOf(std::clamp(*playhead, 0.f, length));
			draw->AddLine(ImVec2(x, rulerTop), ImVec2(x, sheetBottom), colPlayhead, 2.f);
		}
		if (const auto held = Preview::HeldAt(a_clip, state.variant)) {
			const float x = xOf(*held);
			draw->AddLine(ImVec2(x, rulerTop), ImVec2(x, sheetBottom), colScrub, 2.f);
		}

		// ---- right click: on a frame, copy, paste and delete; elsewhere, a new frame or a paste there
		if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			const float mouseX = ImGui::GetMousePos().x;
			state.menuFrame = nullptr;
			for (const auto& keyframe : a_track.keyframes) {
				if (std::fabs(xOf(keyframe->time) - mouseX) <= 6.f) {
					state.menuFrame = keyframe.get();
					break;
				}
			}
			state.menuTime = Snapped(timeOf(mouseX), state.pixelsPerSecond);
			ImGui::OpenPopup("##timelineMenu");
		}
		if (ImGui::BeginPopup("##timelineMenu")) {
			Keyframe* frame = nullptr;
			for (auto& keyframe : a_track.keyframes) {
				if (keyframe.get() == state.menuFrame) {
					frame = keyframe.get();
				}
			}
			if (frame) {
				if (ImGui::MenuItem("Copy")) {
					rapidjson::Document doc(rapidjson::kObjectType);
					const rapidjson::Value value = frame->Serialize(doc.GetAllocator());
					rapidjson::StringBuffer buffer;
					rapidjson::Writer writer(buffer);
					value.Accept(writer);
					_frameCopy = buffer.GetString();
				}
				if (ImGui::MenuItem("Paste", nullptr, false, a_bEditable && !_frameCopy.empty())) {
					PasteOnto(a_clip, *frame, _frameCopy);
					a_clip->SetDirty(true);
				}
				if (ImGui::MenuItem("Delete", nullptr, false, a_bEditable)) {
					ModRegistry::GetSingleton().QueueJob<Jobs::RemoveKeyframeJob>(a_clip, &a_track, frame);
				}
			} else {
				if (ImGui::MenuItem(std::format("New frame at {:.2f} s", state.menuTime).data(), nullptr, false, a_bEditable)) {
					state.selected = a_clip->AddKeyframe(a_track, state.menuTime);
					state.bFrameOpen = true;
					a_clip->SetDirty(true);
				}
				if (ImGui::MenuItem("Paste", nullptr, false, a_bEditable && !_frameCopy.empty())) {
					rapidjson::Document doc;
					doc.Parse(_frameCopy.data());
					if (auto* pasted = a_clip->AddKeyframeFromJson(a_track, doc)) {
						pasted->time = state.menuTime;
						a_clip->SortKeyframes(a_track);
						a_clip->SetDirty(true);
					}
				}
			}
			ImGui::EndPopup();
		}

		// ---- the labels, over the rows where the sheet cannot scroll them sideways; the corner over the ruler
		draw->PopClipRect();
		draw->AddRectFilled(ImVec2(labelX, rulerTop), ImVec2(labelX + kLabelWidth, rulerBottom), colRuler);
		draw->AddLine(ImVec2(labelX + kLabelWidth, rulerTop), ImVec2(labelX + kLabelWidth, rulerBottom), colGrid);
		draw->AddText(ImVec2(labelX + 10.f, rulerTop + 4.f), colDim, "seconds");
		layers.SetCurrentChannel(draw, 0);
		draw->PushClipRect(ImVec2(childPos.x, rulerBottom), clipMax, true);
		draw->AddRectFilled(ImVec2(labelX, rowsTop), ImVec2(labelX + kLabelWidth, sheetBottom), ImGui::GetColorU32(ImGuiCol_ChildBg, 1.f));
		draw->AddLine(ImVec2(labelX + kLabelWidth, rowsTop), ImVec2(labelX + kLabelWidth, sheetBottom), colGrid);
		y = rowsTop;
		heading.clear();
		for (const auto& row : rows) {
			if (!row.id.IsValid()) {
				heading = row.heading;
				draw->AddRectFilled(ImVec2(labelX, y), ImVec2(labelX + kLabelWidth, y + headingHeight), ImGui::GetColorU32(ImGuiCol_Header, 0.35f));
				const ImU32 colHeading = ImGui::GetColorU32(ImGuiCol_HeaderActive);
				const float half = lineHeight * 0.3f;
				const float cx = labelX + 10.f + half;
				const float cy = y + headingHeight * 0.5f;
				if (folded(row.heading)) {
					draw->AddTriangleFilled(ImVec2(cx - half * 0.7f, cy - half), ImVec2(cx - half * 0.7f, cy + half), ImVec2(cx + half * 0.9f, cy), colHeading);
				} else {
					draw->AddTriangleFilled(ImVec2(cx - half, cy - half * 0.7f), ImVec2(cx + half, cy - half * 0.7f), ImVec2(cx, cy + half * 0.9f), colHeading);
				}
				draw->AddText(ImVec2(labelX + 10.f + lineHeight, y + 2.f), colHeading, row.heading.data());
				y += headingHeight;
				continue;
			}
			if (folded(heading)) {
				continue;
			}
			const float height = heightOf(row);
			const auto entry = entries.Get(row.id);
			draw->PushClipRect(ImVec2(labelX, y), ImVec2(labelX + kLabelWidth - 4.f, y + height), true);
			draw->AddText(ImVec2(labelX + 10.f + lineHeight, y + (height - lineHeight) * 0.5f), row.bIgnored ? colDim : colText, entry.display.data());
			draw->PopClipRect();
			y += height;
		}
		draw->PopClipRect();
		layers.Merge(draw);

		ImGui::EndChild();
	}

	void UIMain::DrawTimelineFrame(Clip* a_clip, ClipVariant& a_track)
	{
		auto& state = _timeline;
		if (!state.bFrameOpen || !state.selected) {
			return;
		}
		Keyframe* frame = nullptr;
		for (auto& keyframe : a_track.keyframes) {
			if (keyframe.get() == state.selected) {
				frame = keyframe.get();
			}
		}
		if (!frame) {
			state.bFrameOpen = false;
			return;
		}

		// The frame in a window of its own, drawn as its block in the clip is
		ImGui::SetNextWindowSize(ImVec2(640.f, 520.f), ImGuiCond_FirstUseEver);
		const auto title = std::format("Frame at {:.2f} s - {}###timelineFrame", frame->time, a_clip->GetName());
		if (ImGui::Begin(title.data(), &state.bFrameOpen, ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::PushID(frame);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
			DrawKeyframeBody(a_clip, a_track, *frame);
			ImGui::PopStyleVar();
			ImGui::PopID();
		}
		ImGui::End();
	}
}

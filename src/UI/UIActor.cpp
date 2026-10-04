#include "UIActor.h"

#include <imgui_stdlib.h>

#include "Settings.h"
#include "UICommon.h"
#include "UIMain.h"
#include "UIManager.h"
#include "UIComboFilter.h"
#include "UIModifierLog.h"

#include "ClipPlayer.h"
#include "ModifierLog.h"
#include "ModRegistry.h"
#include "Pose.h"
#include "RegisteredMods.h"
#include "Resolve.h"
#include "SkinSets.h"
#include "Utils.h"
#include "apply/Entries.h"
#include "apply/HeadPart.h"
#include "scan/Scan.h"

namespace UI
{
	namespace
	{
		// The window's holds as a channel set, each row at its held value, for a modifier's Paste channel set
		std::unique_ptr<Channels::ChannelSet> HeldAsChannels(const ActorState::Manual& a_manual, const Scan::Catalogue& a_catalogue)
		{
			auto& entries = Apply::Entries::GetSingleton();
			auto set = std::make_unique<Channels::ChannelSet>();
			const auto add = [&](std::uint32_t a_raw, const std::function<void(Channels::Channel*, const Apply::Entry&)>& a_fill) {
				Apply::ChannelId id;
				id.raw = a_raw;
				auto channel = Channels::CreateChannel(id);
				if (!channel) {
					return;
				}
				a_fill(static_cast<Channels::Channel*>(channel.get()), entries.Get(id));
				set->Add(channel);
			};
			for (const auto& [raw, value] : a_manual.values) {
				add(raw, [&](Channels::Channel* a_channel, const Apply::Entry& a_entry) {
					if (auto* number = dynamic_cast<Channels::NumberChannel*>(a_channel)) {
						number->value->value.SetStaticValue(value);
					} else if (auto* mode = dynamic_cast<Channels::ModeChannel*>(a_channel)) {
						const auto at = static_cast<std::size_t>((std::max)(value, 0.f));
						mode->value->value = at < a_entry.modes.size() ? a_entry.modes[at] : std::string{};
					} else if (auto* toggle = dynamic_cast<Channels::SwitchChannel*>(a_channel)) {
						toggle->value->bValue = value > 0.5f;
					}
				});
			}
			for (const auto& [raw, rgb] : a_manual.colours) {
				add(raw, [&](Channels::Channel* a_channel, const Apply::Entry&) {
					if (auto* colour = dynamic_cast<Channels::ColourChannel*>(a_channel)) {
						colour->value->value.SetValue({ rgb[0], rgb[1], rgb[2] });
					}
				});
			}
			for (const auto& [raw, formID] : a_manual.references) {
				add(raw, [&](Channels::Channel* a_channel, const Apply::Entry&) {
					if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(a_channel)) {
						headPart->value->form.SetValue(RE::TESForm::LookupByID<RE::BGSHeadPart>(static_cast<RE::FormID>(formID)));
					} else if (auto* form = dynamic_cast<Channels::FormChannel*>(a_channel)) {
						form->value->form.SetValue(RE::TESForm::LookupByID<RE::TESObjectREFR>(static_cast<RE::FormID>(formID)));
					}
				});
			}
			for (const auto& [raw, text] : a_manual.texts) {
				add(raw, [&](Channels::Channel* a_channel, const Apply::Entry&) {
					if (auto* reference = dynamic_cast<Channels::ReferenceChannel*>(a_channel)) {
						reference->value->value = text;
					}
				});
			}
			if (a_manual.bodyPreset && *a_manual.bodyPreset < a_catalogue.bodyPresets.size()) {
				add(entries.Require(Apply::Kind::kBodyMorph, "preset"sv).raw, [&](Channels::Channel* a_channel, const Apply::Entry&) {
					if (auto* reference = dynamic_cast<Channels::ReferenceChannel*>(a_channel)) {
						reference->value->value = a_catalogue.bodyPresets[*a_manual.bodyPreset].name;
					}
				});
			}
			return set;
		}

		// A channel set held on the actor, each row at its value now: a feed read once, a random draw left out
		void HoldChannels(Channels::ChannelSet* a_set, ActorState::Manual& a_manual, RE::TESObjectREFR* a_refr, const Scan::Catalogue& a_catalogue)
		{
			auto& entries = Apply::Entries::GetSingleton();
			a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
				if (a_entry->IsDisabled()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (a_entry->IsGroup()) {
					HoldChannels(static_cast<Channels::GroupChannel*>(a_entry.get())->channels.get(), a_manual, a_refr, a_catalogue);
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* channel = static_cast<Channels::Channel*>(a_entry.get());
				const auto id = channel->GetId();
				if (!id.IsValid()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				const auto entry = entries.Get(id);
				if (auto* number = dynamic_cast<Channels::NumberChannel*>(channel)) {
					a_manual.values[id.raw] = number->value->value.GetValue(a_refr);
				} else if (auto* mode = dynamic_cast<Channels::ModeChannel*>(channel)) {
					if (const auto at = std::ranges::find(entry.modes, mode->value->value); at != entry.modes.end()) {
						a_manual.values[id.raw] = static_cast<float>(at - entry.modes.begin());
					}
				} else if (auto* toggle = dynamic_cast<Channels::SwitchChannel*>(channel)) {
					a_manual.values[id.raw] = toggle->value->bValue ? 1.f : 0.f;
				} else if (auto* colour = dynamic_cast<Channels::ColourChannel*>(channel)) {
					const auto& rgb = colour->value->value.GetValue();
					a_manual.colours[id.raw] = { rgb.red, rgb.green, rgb.blue };
				} else if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(channel)) {
					if (const auto* part = headPart->value->bRandom ? nullptr : headPart->value->form.GetValue(a_refr)) {
						a_manual.references[id.raw] = part->GetFormID();
					}
				} else if (auto* form = dynamic_cast<Channels::FormChannel*>(channel)) {
					if (const auto* held = form->value->form.GetValue()) {
						a_manual.references[id.raw] = held->GetFormID();
					}
				} else if (auto* reference = dynamic_cast<Channels::ReferenceChannel*>(channel); reference && !reference->value->bRandom && !reference->value->value.empty()) {
					if (entry.type == Apply::ValueType::kBlendedPick) {
						for (std::size_t i = 0; i < a_catalogue.bodyPresets.size(); ++i) {
							if (a_catalogue.bodyPresets[i].name == reference->value->value) {
								a_manual.bodyPreset = i;
							}
						}
					} else {
						a_manual.texts[id.raw] = reference->value->value;
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// Legacy's on/off: a rounded track with a knob that slides across over a few frames
		bool ToggleSwitch(const char* a_label, bool* a_on)
		{
			const auto height = ImGui::GetFrameHeight();
			const auto width = height * 1.8f;
			const auto radius = height * 0.5f;
			const auto origin = ImGui::GetCursorScreenPos();
			auto* draw = ImGui::GetWindowDrawList();
			const auto& style = ImGui::GetStyle();

			ImGui::InvisibleButton(a_label, ImVec2{ width, height });
			const auto flipped = ImGui::IsItemClicked();
			if (flipped) {
				*a_on = !*a_on;
			}
			const auto hovered = ImGui::IsItemHovered();

			const auto t = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetItemID(), *a_on ? 1.f : 0.f);
			const auto want = *a_on ? 1.f : 0.f;
			if (*t != want) {
				const auto step = ImGui::GetIO().DeltaTime * 8.f;
				*t = *t < want ? (std::min)(*t + step, want) : (std::max)(*t - step, want);
			}

			const auto onColour = style.Colors[hovered ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered];
			const auto offColour = style.Colors[hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg];
			const ImVec4 mixed{ offColour.x + (onColour.x - offColour.x) * *t, offColour.y + (onColour.y - offColour.y) * *t,
				offColour.z + (onColour.z - offColour.z) * *t, offColour.w + (onColour.w - offColour.w) * *t };
			draw->AddRectFilled(origin, ImVec2{ origin.x + width, origin.y + height }, ImGui::GetColorU32(mixed), radius);
			draw->AddCircleFilled(ImVec2{ origin.x + radius + *t * (width - height), origin.y + radius }, radius - 2.f, IM_COL32(255, 255, 255, 255));
			return flipped;
		}
	}

	bool UIActor::ShouldDrawImpl() const
	{
		return Settings::bShowActorWindow;
	}

	void UIActor::DrawImpl()
	{
		// First opened against the right edge of the screen; moved and sized freely after, and remembered
		const auto& display = ImGui::GetIO().DisplaySize;
		constexpr float width = 640.f;
		ImGui::SetNextWindowPos(ImVec2((std::max)(display.x - width - 8.f, 0.f), Settings::fAnimationLogsOffsetY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(width, 760.f), ImGuiCond_FirstUseEver);

		constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

		if (ImGui::Begin("Actor", nullptr, windowFlags)) {
			if (auto* refr = UIManager::GetSingleton().GetRefrToEvaluate()) {
				ImGui::Text("%s  %08X", refr->GetDisplayFullName(), refr->GetFormID());
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				ImGui::TextWrapped("No actor selected. Select one in the console, or type a FormID at the top of the main window.");
				ImGui::PopStyleColor();
			}
			ImGui::Separator();

			// The live trace costs a trace of the actor each tick: taken only while this section is open
			const bool bActiveOpen = ImGui::CollapsingHeader("Active Modifiers and Clips");
			ModifierLog::GetSingleton().SetLive(bActiveOpen);
			if (bActiveOpen) {
				DrawActiveSubMods();
			}

			if (ImGui::CollapsingHeader("Available Parts")) {
				DrawAvailableParts();
			}

			if (ImGui::CollapsingHeader("Channels", ImGuiTreeNodeFlags_DefaultOpen)) {
				DrawChannels();
			}
		}
		ImGui::End();
	}

	void UIActor::OnClose()
	{
		ModifierLog::GetSingleton().SetLive(false);
	}

	void UIActor::DrawActiveSubMods()
	{
		auto* refr = UIManager::GetSingleton().GetRefrToEvaluate();
		const auto handle = refr ? refr->GetHandle() : RE::ObjectRefHandle{};
		ActorState::Watch(handle);
		ModifierLog::GetSingleton().SetRefr(refr ? refr->GetFormID() : 0);
		if (!refr) {
			return;
		}

		// The clips playing and the modifiers active, as the log's trace draws them, refreshed each tick
		const auto trace = ModifierLog::GetSingleton().LiveTrace();
		using Section = ModifierTrace::Section;
		bool bAny = false;
		for (const auto section : { Section::kPlaying, Section::kActive }) {
			for (const auto& step : trace.steps) {
				if (step.section == section) {
					bAny = true;
					UI::UIModifierLog::DrawTraceStep(step);
				}
			}
		}
		if (!bAny) {
			UICommon::TextUnformattedDisabled("Nothing active on this actor.");
		}
	}

	void UIActor::DrawAvailableParts()
	{
		auto* refr = UIManager::GetSingleton().GetRefrToEvaluate();
		const auto handle = refr ? refr->GetHandle() : RE::ObjectRefHandle{};
		ActorState::Watch(handle);

		if (!Scan::Scanner::GetSingleton().IsFinished()) {
			UICommon::TextUnformattedDisabled("Waiting for the scan to finish.");
			return;
		}
		if (!handle) {
			return;
		}

		std::shared_ptr<const ActorState::Parts> parts;
		if (const auto state = ActorState::Get(handle)) {
			parts = state->parts.With([](const auto& a_parts) { return a_parts; });
		}

		if (!parts) {
			UICommon::TextUnformattedDisabled("Reading the actor's 3D...");
			return;
		}
		if (!parts->bLoaded) {
			UICommon::TextUnformattedDisabled("The actor's 3D is not loaded.");
			return;
		}
		if (parts->roots.empty()) {
			UICommon::TextUnformattedDisabled("No shape on this actor has a TRI.");
			return;
		}

		if (ImGui::BeginChild("Parts", ImVec2(0.f, Settings::fAnimationEventLogHeight), true)) {
			for (const auto& root : parts->roots) {
				DrawPart(root);
			}
		}
		ImGui::EndChild();
	}

	void UIActor::DrawPart(const ActorState::Part& a_part) const
	{
		const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();

		ImGui::PushID(&a_part);
		const bool bOpen = ImGui::TreeNodeEx(a_part.name.data(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
		if (a_part.bHidden) {
			ImGui::SameLine();
			ImGui::TextDisabled("hidden");
		}
		if (!a_part.collision.empty()) {
			ImGui::SameLine();
			UICommon::TextUnformattedColored(UICommon::WARNING_TEXT_COLOR, "SMP collision");
			UICommon::AddTooltip(std::format("A collision shape of {}", a_part.collision).data());
		}
		if (bOpen) {
			for (const auto& tri : a_part.tris) {
				std::size_t morphs = 0;
				bool bKnown = false;
				if (catalogue) {
					for (const auto& set : catalogue->faceMorphs) {
						if (set.tri == tri.path) {
							morphs = set.morphs.size();
							bKnown = true;
						}
					}
					for (const auto& set : catalogue->bodyMorphs) {
						if (set.tri == tri.path && set.bBuilt) {
							morphs = set.morphs.size();
							bKnown = true;
						}
					}
				}
				ImGui::TreeNodeEx(tri.path.data(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet);
				UICommon::AddTooltip(tri.via.data());
				ImGui::SameLine();
				if (bKnown) {
					ImGui::TextDisabled("%zu morphs", morphs);
				} else {
					UICommon::TextUnformattedColored(UICommon::WARNING_TEXT_COLOR, "not in the scan");
				}
			}
			for (const auto& child : a_part.children) {
				DrawPart(child);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	// Every channel the actor can take, under the modifiers' channel picker's categories, each drawn with the widget the
	// channel editor uses: a row's checkbox holds it and makes it editable; unchecked, it follows what the actor is sent
	void UIActor::DrawChannels()
	{
		auto* refr = UIManager::GetSingleton().GetRefrToEvaluate();
		const auto state = refr ? ActorState::Get(refr->GetHandle()) : nullptr;
		const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
		if (!state || !catalogue) {
			return;
		}
		const auto parts = state->parts.With([](const auto& a_parts) { return a_parts; });
		if (!parts || !parts->bLoaded) {
			UICommon::TextUnformattedDisabled("The actor's 3D is not loaded.");
			return;
		}

		struct Row
		{
			Apply::ChannelId id;
			Apply::Entry entry;
		};
		auto& entries = Apply::Entries::GetSingleton();

		// What the actor is sent this frame, and what the window holds
		const auto sampled = state->sampled.With([](const auto& a_sampled) { return a_sampled; });
		const auto manual = state->manual.With([](const ActorState::Manual& a_manual) { return a_manual; });

		// Rows checked with nothing to hold yet, held once their value is set; and a value that rebuilds the 3D, taken
		// once the mouse lets go; both forgotten with the actor
		static RE::FormID pendingActor = 0;
		static std::unordered_set<std::uint32_t> pending;
		static std::unordered_map<std::uint32_t, float> rebuildPending;
		if (pendingActor != refr->GetFormID()) {
			pendingActor = refr->GetFormID();
			pending.clear();
			rebuildPending.clear();
		}

		const auto liveNumber = [&](Apply::ChannelId a_id) -> std::optional<float> {
			if (!sampled) {
				return std::nullopt;
			}
			const auto& list = sampled->numbers[static_cast<std::size_t>(a_id.GetKind())];
			const auto it = std::ranges::find(list, a_id.GetIndex(), &Apply::NumberSample::index);
			return it != list.end() ? std::optional(it->value) : std::nullopt;
		};
		const auto liveColour = [&](Apply::ChannelId a_id) -> std::array<float, 3> {
			if (sampled) {
				const auto& list = sampled->colours[static_cast<std::size_t>(a_id.GetKind())];
				if (const auto it = std::ranges::find(list, a_id.GetIndex(), &Apply::ColourSample::index); it != list.end()) {
					return { it->red, it->green, it->blue };
				}
			}
			if (const auto own = ClipPlayer::OwnColour(refr, a_id)) {
				return { own->red, own->green, own->blue };
			}
			return { 1.f, 1.f, 1.f };
		};
		const auto liveReference = [&](Apply::ChannelId a_id) -> std::optional<std::uint64_t> {
			if (!sampled) {
				return std::nullopt;
			}
			const auto& list = sampled->references[static_cast<std::size_t>(a_id.GetKind())];
			const auto it = std::ranges::find(list, a_id.GetIndex(), &Apply::ReferenceSample::index);
			return it != list.end() ? std::optional(it->value) : std::nullopt;
		};
		const auto liveText = [&](Apply::ChannelId a_id) -> std::string {
			if (!sampled) {
				return {};
			}
			const auto& list = sampled->texts[static_cast<std::size_t>(a_id.GetKind())];
			const auto it = std::ranges::find(list, a_id.GetIndex(), &Apply::TextSample::index);
			return it != list.end() ? it->value : std::string{};
		};
		// Where a row rests with nothing driving it; a pose's neutral only marks it undriven
		const auto rest = [](const Row& a_row) {
			const auto kind = a_row.id.GetKind();
			const bool bPose = kind == Apply::Kind::kSpine || kind == Apply::Kind::kHead || kind == Apply::Kind::kGaze;
			return bPose || a_row.entry.neutral < a_row.entry.low ? a_row.entry.start : a_row.entry.neutral;
		};

		// The filter, how many rows are held, and Clear all
		static char filterBuf[64] = "";
		const auto heldCount = manual.values.size() + manual.colours.size() + manual.references.size() + manual.texts.size() + (manual.bodyPreset ? 1 : 0) + pending.size();
		{
			const auto status = heldCount == 0 ? std::string("nothing held") : std::format("{} held", heldCount);
			const auto& style = ImGui::GetStyle();
			const auto buttonWidth = [&](const char* a_label) { return ImGui::CalcTextSize(a_label).x + style.FramePadding.x * 2 + style.ItemSpacing.x; };
			const float right = ImGui::CalcTextSize(status.data()).x + style.ItemSpacing.x + buttonWidth("Copy held") + buttonWidth("Paste") + buttonWidth("Clear all");
			ImGui::SetNextItemWidth((std::max)(ImGui::GetContentRegionAvail().x - right, ImGui::GetFontSize() * 6));
			ImGui::InputTextWithHint("##channelFilter", "Filter channels...", filterBuf, IM_ARRAYSIZE(filterBuf));
			ImGui::SameLine();
			ImGui::TextDisabled("%s", status.data());
			ImGui::SameLine();
			ImGui::BeginDisabled(heldCount == 0);
			if (ImGui::Button("Copy held")) {
				UIMain::ChannelSetCopy() = HeldAsChannels(manual, *catalogue);
			}
			ImGui::EndDisabled();
			UICommon::AddTooltip("Copies every held row as a channel set, each at its value: a modifier's Channel set... > Paste channel set puts them down.");
			ImGui::SameLine();
			ImGui::BeginDisabled(!UIMain::ChannelSetCopy());
			if (ImGui::Button("Paste")) {
				state->manual.With([&](ActorState::Manual& a_manual) { HoldChannels(UIMain::ChannelSetCopy().get(), a_manual, refr, *catalogue); });
			}
			ImGui::EndDisabled();
			UICommon::AddTooltip("Holds every row of the copied channel set at its value: a feed is read once, a random draw is left out.");
			ImGui::SameLine();
			ImGui::BeginDisabled(heldCount == 0);
			if (ImGui::Button("Clear all")) {
				state->manual.With([](ActorState::Manual& a_manual) {
					a_manual.values.clear();
					a_manual.colours.clear();
					a_manual.references.clear();
					a_manual.texts.clear();
					a_manual.bodyPreset.reset();
				});
				pending.clear();
				rebuildPending.clear();
			}
			ImGui::EndDisabled();
		}
		const std::string_view filter = filterBuf;

		// The body presets by name, the window holding one by its place in the catalogue
		const auto presetIndex = [&](std::string_view a_name) -> std::optional<std::size_t> {
			for (std::size_t i = 0; i < catalogue->bodyPresets.size(); ++i) {
				if (catalogue->bodyPresets[i].name == a_name) {
					return i;
				}
			}
			return std::nullopt;
		};

		// One channel per row, made as the channel editor makes it, with nothing of a modifier's: the value alone,
		// static, never drawn at random nor from a feed
		static std::unordered_map<std::uint32_t, std::unique_ptr<Channels::ChannelBase>> rowChannels;
		const auto channelOf = [&](Apply::ChannelId a_id) -> Channels::Channel* {
			auto& slot = rowChannels[a_id.raw];
			if (!slot) {
				slot = Channels::CreateChannel(a_id);
				if (auto* number = dynamic_cast<Channels::NumberChannel*>(slot.get())) {
					number->value->value.getFeeds = nullptr;
					number->value->value.SetForcedType(Components::NumericValue::Type::kStaticValue);
				} else if (auto* reference = dynamic_cast<Channels::ReferenceChannel*>(slot.get())) {
					reference->value->random.reset();
					reference->value->bRandom = false;
				} else if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(slot.get())) {
					headPart->value->random.reset();
					headPart->value->bRandom = false;
					headPart->value->form.getFeeds = nullptr;
				}
			}
			return static_cast<Channels::Channel*>(slot.get());
		};

		// A morph read from the load order goes by its own name: the display names the files give are often wrong
		const auto labelOf = [](const Row& a_row) -> const std::string& {
			const auto kind = a_row.id.GetKind();
			const bool bScannedMorph = (kind == Apply::Kind::kBodyMorph || kind == Apply::Kind::kFaceMorph) && a_row.entry.bScanned;
			return bScannedMorph ? a_row.entry.name : a_row.entry.display;
		};
		const auto matches = [&](const Row& a_row) {
			return filter.empty() || Utils::ContainsStringIgnoreCase(labelOf(a_row), filter) || Utils::ContainsStringIgnoreCase(a_row.entry.name, filter) || Utils::ContainsStringIgnoreCase(a_row.entry.group, filter);
		};

		const auto drawRow = [&](const Row& a_row) {
			const auto key = a_row.id.raw;
			const auto kind = a_row.id.GetKind();
			const bool bScannedMorph = (kind == Apply::Kind::kBodyMorph || kind == Apply::Kind::kFaceMorph) && a_row.entry.bScanned;
			const auto& label = labelOf(a_row);
			auto* channel = channelOf(a_row.id);
			if (!channel) {
				return;
			}

			// Where the row's value lives in the window's holds, whether it is held, and the channel set to it: the
			// hold's when held, what the actor is sent otherwise
			bool bHeld = false;
			if (auto* number = dynamic_cast<Channels::NumberChannel*>(channel)) {
				const auto it = manual.values.find(key);
				bHeld = it != manual.values.end();
				if (!rebuildPending.contains(key)) {
					number->value->value.SetStaticValue(bHeld ? it->second : liveNumber(a_row.id).value_or(rest(a_row)));
				}
			} else if (auto* mode = dynamic_cast<Channels::ModeChannel*>(channel)) {
				const auto it = manual.values.find(key);
				bHeld = it != manual.values.end();
				const auto at = static_cast<std::size_t>((std::max)(bHeld ? it->second : liveNumber(a_row.id).value_or(0.f), 0.f));
				mode->value->value = at < a_row.entry.modes.size() ? a_row.entry.modes[at] : std::string{};
			} else if (auto* toggle = dynamic_cast<Channels::SwitchChannel*>(channel)) {
				const auto it = manual.values.find(key);
				bHeld = it != manual.values.end();
				toggle->value->bValue = (bHeld ? it->second : liveNumber(a_row.id).value_or(0.f)) > 0.5f;
			} else if (auto* colour = dynamic_cast<Channels::ColourChannel*>(channel)) {
				const auto it = manual.colours.find(key);
				bHeld = it != manual.colours.end();
				const auto rgb = bHeld ? it->second : liveColour(a_row.id);
				colour->value->value.SetValue({ rgb[0], rgb[1], rgb[2] });
			} else if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(channel)) {
				const auto it = manual.references.find(key);
				bHeld = it != manual.references.end() || pending.contains(key);
				auto live = liveReference(a_row.id);
				// With nothing sent, the actor's own part in the slot
				if (!live) {
					auto* actor = refr->As<RE::Actor>();
					auto* base = actor ? actor->GetActorBase() : nullptr;
					const auto& slots = Apply::HeadPart::Slots();
					if (base && a_row.id.GetIndex() < slots.size()) {
						if (auto* own = base->GetCurrentHeadPartByType(slots[a_row.id.GetIndex()].type)) {
							live = own->GetFormID();
						}
					}
				}
				const auto formID = it != manual.references.end() ? std::optional(it->second) : live;
				headPart->value->form.SetValue(formID ? RE::TESForm::LookupByID<RE::BGSHeadPart>(static_cast<RE::FormID>(*formID)) : nullptr);
			} else if (auto* form = dynamic_cast<Channels::FormChannel*>(channel)) {
				const auto it = manual.references.find(key);
				bHeld = it != manual.references.end() || pending.contains(key);
				const auto formID = it != manual.references.end() ? std::optional(it->second) : liveReference(a_row.id);
				if (!bHeld || it != manual.references.end()) {
					form->value->form.SetValue(formID ? RE::TESForm::LookupByID<RE::TESObjectREFR>(static_cast<RE::FormID>(*formID)) : nullptr);
				}
			} else if (auto* reference = dynamic_cast<Channels::ReferenceChannel*>(channel)) {
				if (a_row.entry.type == Apply::ValueType::kBlendedPick) {
					bHeld = manual.bodyPreset.has_value() || pending.contains(key);
					if (manual.bodyPreset && *manual.bodyPreset < catalogue->bodyPresets.size()) {
						reference->value->value = catalogue->bodyPresets[*manual.bodyPreset].name;
					} else if (!bHeld) {
						reference->value->value = sampled ? sampled->presetName : std::string{};
					}
				} else {
					const auto it = manual.texts.find(key);
					bHeld = it != manual.texts.end() || pending.contains(key);
					if (it != manual.texts.end()) {
						reference->value->value = it->second;
					} else if (!bHeld) {
						reference->value->value = liveText(a_row.id);
					}
				}
			}

			// The window's hold taken from the channel, as it stands now
			const auto take = [&] {
				state->manual.With([&](ActorState::Manual& a_manual) {
					if (auto* number = dynamic_cast<Channels::NumberChannel*>(channel)) {
						a_manual.values[key] = number->value->value.GetValue(nullptr);
					} else if (auto* mode = dynamic_cast<Channels::ModeChannel*>(channel)) {
						const auto at = std::ranges::find(a_row.entry.modes, mode->value->value);
						a_manual.values[key] = at != a_row.entry.modes.end() ? static_cast<float>(at - a_row.entry.modes.begin()) : 0.f;
					} else if (auto* toggle = dynamic_cast<Channels::SwitchChannel*>(channel)) {
						a_manual.values[key] = toggle->value->bValue ? 1.f : 0.f;
					} else if (auto* colour = dynamic_cast<Channels::ColourChannel*>(channel)) {
						const auto& rgb = colour->value->value.GetValue();
						a_manual.colours[key] = { rgb.red, rgb.green, rgb.blue };
					} else if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(channel)) {
						if (const auto* part = headPart->value->form.GetValue()) {
							a_manual.references[key] = part->GetFormID();
						} else {
							a_manual.references.erase(key);
						}
					} else if (auto* form = dynamic_cast<Channels::FormChannel*>(channel)) {
						if (const auto* refrForm = form->value->form.GetValue()) {
							a_manual.references[key] = refrForm->GetFormID();
						} else {
							a_manual.references.erase(key);
						}
					} else if (auto* reference = dynamic_cast<Channels::ReferenceChannel*>(channel)) {
						if (a_row.entry.type == Apply::ValueType::kBlendedPick) {
							a_manual.bodyPreset = presetIndex(reference->value->value);
						} else if (!reference->value->value.empty()) {
							a_manual.texts[key] = reference->value->value;
						} else {
							a_manual.texts.erase(key);
						}
					}
				});
			};
			// Whether the channel has something to hold: a reference, a form or a name may be empty
			const auto hasValue = [&] {
				if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(channel)) {
					return headPart->value->form.GetValue() != nullptr;
				}
				if (auto* form = dynamic_cast<Channels::FormChannel*>(channel)) {
					return form->value->form.GetValue() != nullptr;
				}
				if (auto* reference = dynamic_cast<Channels::ReferenceChannel*>(channel)) {
					return !reference->value->value.empty() && (a_row.entry.type != Apply::ValueType::kBlendedPick || presetIndex(reference->value->value).has_value());
				}
				return true;
			};
			const auto release = [&] {
				state->manual.With([&](ActorState::Manual& a_manual) {
					a_manual.values.erase(key);
					a_manual.colours.erase(key);
					a_manual.references.erase(key);
					a_manual.texts.erase(key);
					if (a_row.entry.type == Apply::ValueType::kBlendedPick) {
						a_manual.bodyPreset.reset();
					}
				});
				pending.erase(key);
				rebuildPending.erase(key);
			};

			ImGui::PushID(static_cast<int>(key));

			// The hold checkbox, then the value's widget; unchecked, the widget follows what the actor is sent
			bool bHold = bHeld;
			if (ImGui::Checkbox("##hold", &bHold)) {
				if (!bHold) {
					release();
				} else if (hasValue()) {
					take();
				} else {
					pending.insert(key);
				}
			}
			UICommon::AddTooltip(bHeld ? "Held: the value is the window's. Uncheck to hand the channel back to the mods." : "Check to hold the channel at its value and edit it.");

			// The value's widget: an on/off as legacy's toggle switch; anything else the channel editor's own widget over
			// most of the row, clipped there, so what it writes in its second column is left out; a group, so a widget of
			// more than one line keeps to its column. Editing a row not held holds it
			constexpr float kFirstColumn = 0.72f;
			ImGui::SameLine();
			bool bChanged = false;
			if (auto* toggle = dynamic_cast<Channels::SwitchChannel*>(channel)) {
				bChanged = ToggleSwitch("##value", &toggle->value->bValue);
			} else {
				const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x * kFirstColumn - ImGui::GetStyle().ItemSpacing.x;
				ImGui::BeginGroup();
				ImGui::PushClipRect(ImVec2(ImGui::GetWindowPos().x, -FLT_MAX), ImVec2(right, FLT_MAX), true);
				if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(channel)) {
					// The part's form row and its card picker, without the preview square the editor draws above it
					bChanged = headPart->value->form.DisplayInUI(true, kFirstColumn);
				} else {
					for (std::uint32_t i = 0; i < channel->GetNumComponents(); ++i) {
						auto* component = channel->GetComponent(i);
						if (component->GetName() == "Value"sv && component->IsShown()) {
							bChanged = component->DisplayInUI(true, kFirstColumn);
							break;
						}
					}
				}
				ImGui::PopClipRect();
				ImGui::EndGroup();
			}

			// The name just past the widget's column, in the key colour while held; what it is on hover: its id, and
			// what it does, or what kind of value it takes for one read from the load order
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x * kFirstColumn);
			if (bHeld) {
				UICommon::TextUnformattedColored(UICommon::KEY_TEXT_COLOR, label.data());
			} else {
				ImGui::TextUnformatted(label.data());
			}
			{
				std::string tip = std::format("{}\n{}", entries.Identifier(a_row.id), a_row.entry.description.empty() ? std::string(channel->GetDescription()) : a_row.entry.description);
				if (bScannedMorph && a_row.entry.display != a_row.entry.name) {
					tip += std::format("\nShown by BodySlide or RaceMenu as: {}", a_row.entry.display);
				}
				UICommon::AddTooltip(tip.data());
			}

			if (bChanged) {
				// A slider that rebuilds the 3D waits for the mouse to let go; a pick is one change, taken at once
				if (auto* number = dynamic_cast<Channels::NumberChannel*>(channel); number && a_row.entry.bRebuilds) {
					rebuildPending[key] = number->value->value.GetValue(nullptr);
				} else if (hasValue()) {
					take();
					pending.erase(key);
				}
			}
			if (const auto it = rebuildPending.find(key); it != rebuildPending.end() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				state->manual.With([&](ActorState::Manual& a_manual) { a_manual.values[key] = it->second; });
				rebuildPending.erase(it);
			}
			ImGui::PopID();
		};

		// The channels in eight families, each tinted in the condition presets' teal so it reads apart from the window's
		// own headers; in each, the modifiers' channel picker's categories, in its order, the rows in its order within
		constexpr std::array families{ "Engine MFG", "Face Morph", "Body Morph", "Actor & Body Preset", "Head Part", "Skin", "Overlay", "Pose" };
		const auto familyOf = [](Apply::ChannelId a_id, const Apply::Entry& a_entry) -> int {
			switch (a_id.GetKind()) {
			case Apply::Kind::kMfg:
				return 0;
			case Apply::Kind::kFaceMorph:
				return 1;
			case Apply::Kind::kBodyMorph:
				return a_entry.bScanned ? 2 : 3;  // the preset and its weight are the code's
			case Apply::Kind::kActorProperty:
				return 3;
			case Apply::Kind::kHeadPart:
				return 4;
			case Apply::Kind::kMaterial:
				return a_entry.name.starts_with("skin.") ? 5 : 4;  // a head part's tint and glow sit with the part
			case Apply::Kind::kSkin:
				return 5;
			case Apply::Kind::kOverlay:
				return 6;  // a category per slot
			case Apply::Kind::kSpine:
			case Apply::Kind::kHead:
			case Apply::Kind::kGaze:
				return 7;
			default:
				return -1;
			}
		};

		using Category = std::pair<std::string, std::vector<Row>>;
		std::array<std::vector<Category>, families.size()> byFamily;
		for (std::uint8_t k = 0; k < static_cast<std::uint8_t>(Apply::Kind::kTotal); ++k) {
			const auto kind = static_cast<Apply::Kind>(k);
			entries.ForEach(kind, [&](Apply::ChannelId a_id, const Apply::Entry& a_entry) {
				const auto family = familyOf(a_id, a_entry);
				if (family < 0) {
					return;
				}
				// A morph this actor's TRIs lack left out, and what the filter does not match
				const bool bMorph = (kind == Apply::Kind::kBodyMorph || kind == Apply::Kind::kFaceMorph) && a_entry.bScanned;
				if ((bMorph && !parts->morphs.contains(a_entry.name)) || !matches({ a_id, a_entry })) {
					return;
				}
				auto& categories = byFamily[family];
				const auto category = UIChannelComboFilter::CategoryName(a_entry);
				auto it = std::ranges::find(categories, category, &Category::first);
				if (it == categories.end()) {
					it = categories.insert(categories.end(), { category, {} });
				}
				it->second.push_back({ a_id, a_entry });
			});
		}

		static UIChannelComboFilter pickerOrder;
		const auto tint = [](float a_alpha) {
			auto colour = UICommon::CONDITION_PRESET_COLOR;
			colour.w = a_alpha;
			return colour;
		};
		for (std::size_t f = 0; f < families.size(); ++f) {
			auto& categories = byFamily[f];
			if (categories.empty()) {
				continue;
			}
			std::ranges::stable_sort(categories, [&](const Category& a_left, const Category& a_right) { return pickerOrder.CategoryRank(a_left.first) < pickerOrder.CategoryRank(a_right.first); });
			std::size_t count = 0;
			for (const auto& category : categories) {
				count += category.second.size();
			}
			ImGui::PushStyleColor(ImGuiCol_Header, tint(0.25f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tint(0.4f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, tint(0.55f));
			const bool bOpen = ImGui::CollapsingHeader(std::format("{} ({})###{}", families[f], count, families[f]).data());
			ImGui::PopStyleColor(3);
			if (!bOpen) {
				continue;
			}
			ImGui::Indent();
			bool bFirst = true;
			for (const auto& [category, rows] : categories) {
				if (!bFirst) {
					ImGui::Separator();
				}
				bFirst = false;
				ImGui::TextDisabled("%s", category.data());
				for (const auto& row : rows) {
					drawRow(row);
				}
			}
			ImGui::Unindent();
		}
	}
}

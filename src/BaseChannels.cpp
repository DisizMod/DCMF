#include "BaseChannels.h"

#include "BaseConditions.h"

#include "UI/MeshThumbs.h"
#include "UI/UIManager.h"

#include <array>
#include <format>
#include <numbers>

namespace Channels
{
	namespace
	{
		constexpr std::array kValueTypeNames{ "Static Value", "Random Value" };
	}

	HeadPartChannelComponent::HeadPartChannelComponent(std::string_view a_name, std::string_view a_description, RE::BGSHeadPart::HeadPartType a_type) :
		IChannelComponent(a_name, a_description), type(a_type)
	{
		// The form row's Pick opens the cards of this slot's type instead of the form list
		form.pickOverride = [this](RE::FormID a_current, std::uint64_t& a_outPicked) {
			RE::FormID picked = 0;
			auto* reference = UI::UIManager::GetSingleton().GetRefrToEvaluate();
			if (UI::MeshThumbs::PickerButton(type, a_current, picked, reference ? reference->As<RE::Actor>() : nullptr)) {
				a_outPicked = picked;
				return true;
			}
			return false;
		};
	}

	void HeadPartChannelComponent::Parse(const rapidjson::Value& a_value)
	{
		// A draw, {"random": ...}, or the form itself
		bRandom = random && a_value.IsObject() && a_value.HasMember("random");
		if (bRandom) {
			random->Parse(a_value["random"]);
		} else {
			form.Parse(const_cast<rapidjson::Value&>(a_value));
		}
	}

	rapidjson::Value HeadPartChannelComponent::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		if (random && bRandom) {
			rapidjson::Value object(rapidjson::kObjectType);
			object.AddMember("random", random->Serialize(a_allocator), a_allocator);
			return object;
		}
		return form.Serialize(a_allocator);
	}

	bool HeadPartChannelComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		// Static Value, Feed Value or Random Value on one slider, as a numeric value's type; only those the channel offers
		const bool bFeeds = form.getFeeds != nullptr;
		std::vector<const char*> types{ "Static Value" };
		if (bFeeds) {
			types.push_back("Feed Value");
		}
		if (random) {
			types.push_back("Random Value");
		}
		const int last = static_cast<int>(types.size()) - 1;
		int type = random && bRandom ? last : bFeeds && form.IsFeed() ? 1 : 0;
		if (types.size() > 1) {
			if (a_bEditable) {
				ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
				ImGui::PushID(&bRandom);
				if (ImGui::SliderInt("", &type, 0, last, types[type])) {
					bRandom = random && type == last;
					form.SetIsFeed(bFeeds && type == 1);
					bEdited = true;
				}
				ImGui::PopID();
			} else if (type > 0) {
				ImGui::TextUnformatted(types[type]);
			}
		}
		if (random && bRandom) {
			return random->DisplayInUI(a_bEditable, a_firstColumnWidthPercent) || bEdited;
		}
		if (form.IsFeed()) {
			return form.DisplayFeedRow(a_bEditable, a_firstColumnWidthPercent) || bEdited;
		}

		// The preview square above the name, always the same size, a placeholder when there is nothing to show; the
		// form row underneath, its boxes level with the name, without the feed slider of its own
		const float preview = ImGui::GetTextLineHeight() * 5.f;
		const auto* current = form.GetValue(UI::UIManager::GetSingleton().GetRefrToEvaluate());
		ImGui::Dummy(ImVec2(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent), preview));
		UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
		UI::MeshThumbs::DrawCard(current ? current->GetFormID() : 0, preview);
		auto feeds = std::exchange(form.getFeeds, nullptr);
		const bool bForm = form.DisplayInUI(a_bEditable, a_firstColumnWidthPercent);
		form.getFeeds = std::move(feeds);
		return bForm || bEdited;
	}

	std::string HeadPartChannelComponent::GetArgument() const
	{
		if (random && bRandom) {
			return std::format("random of {}", random->list.entries.size());
		}
		return form.IsValid() ? form.GetArgument() : std::string("(none)");
	}

	void ModeChannelComponent::Parse(const rapidjson::Value& a_value)
	{
		if (a_value.IsString()) {
			value = a_value.GetString();
		}
	}

	rapidjson::Value ModeChannelComponent::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		return rapidjson::Value(value.data(), static_cast<rapidjson::SizeType>(value.length()), a_allocator);
	}

	bool ModeChannelComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		if (!a_bEditable) {
			ImGui::TextUnformatted(value.data());
			return false;
		}

		ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
		ImGui::PushID(&value);
		if (ImGui::BeginCombo("##Mode", value.data())) {
			for (const auto& mode : modes) {
				const bool bIsCurrent = mode == value;
				if (ImGui::Selectable(mode.data(), bIsCurrent)) {
					if (!bIsCurrent) {
						value = mode;
						bEdited = true;
					}
				}
				if (bIsCurrent) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopID();

		UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
		ImGui::TextUnformatted(value.data());

		return bEdited;
	}

	namespace
	{
		// Activation, Channel and Actor our own; the clip or modifier and the mod as the conditions' state data scopes name them
		std::string_view ScopeName(RandomValue::Scope a_scope)
		{
			using Scope = RandomValue::Scope;
			using Conditions::ConditionStateComponent;
			switch (a_scope) {
			case Scope::kChannel:
				return "Channel"sv;
			case Scope::kSubMod:
				return ConditionStateComponent::GetStateDataScopeName(Conditions::StateDataScope::kSubMod);
			case Scope::kRegisteredMod:
				return ConditionStateComponent::GetStateDataScopeName(Conditions::StateDataScope::kRegisteredMod);
			case Scope::kActor:
				return "Actor"sv;
			case Scope::kActivation:
			default:
				return "Activation"sv;
			}
		}

		std::string_view ScopeTooltip(RandomValue::Scope a_scope)
		{
			using Scope = RandomValue::Scope;
			using Conditions::ConditionStateComponent;
			switch (a_scope) {
			case Scope::kChannel:
				return "The state data is unique for the combination of this actor and this channel of the clip or modifier, the same in every frame, and kept for good."sv;
			case Scope::kSubMod:
				return ConditionStateComponent::GetStateDataScopeTooltip(Conditions::StateDataScope::kSubMod);
			case Scope::kRegisteredMod:
				return ConditionStateComponent::GetStateDataScopeTooltip(Conditions::StateDataScope::kRegisteredMod);
			case Scope::kActor:
				return "The state data is unique for this actor, and is shared between every random channel of every clip, modifier and mod."sv;
			case Scope::kActivation:
			default:
				return "The state data is unique for the combination of this actor and this channel of the clip or modifier, the same in every frame, and is drawn again each time the clip or modifier activates."sv;
			}
		}
	}

	bool PickList::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent, const ValueFactory& a_factory, const PoolSource* a_pools)
	{
		bool bEdited = false;
		const float total = TotalWeight(a_pools);
		PickEntry* removed = nullptr;
		const auto poolNames = a_pools && a_pools->names ? a_pools->names() : std::vector<std::string>{};

		// The delete button on a line of its own, on the left
		const auto deleteRow = [&](PickEntry* a_entry) {
			if (!a_bEditable) {
				return;
			}
			UI::UICommon::ButtonWithConfirmationModal("Delete entry"sv, "Are you sure you want to remove the entry?\nThis operation cannot be undone!\n\n"sv, [&]() {
				removed = a_entry;
			});
		};

		for (auto& entry : entries) {
			PickList* pool = !entry->pool.empty() && a_pools && a_pools->entriesOf ? a_pools->entriesOf(entry->pool) : nullptr;
			const bool bPool = !entry->pool.empty();

			const std::string tableId = std::format("{}pickEntryTable", reinterpret_cast<std::uintptr_t>(entry.get()));
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			// A pool drawn as a preset is: its border, and what it holds inside
			if (bPool) {
				ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, UI::UICommon::CONDITION_PRESET_BORDER_COLOR);
			}
			const bool bTable = ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter);
			if (bPool) {
				ImGui::PopStyleColor();
			}
			if (bTable) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				bool bStyleVarPushed = false;
				if (entry->bDisabled) {
					auto& style = ImGui::GetStyle();
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
					bStyleVarPushed = true;
				}

				const bool bNodeOpen = ImGui::TreeNodeEx(entry.get(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth, "");

				// Entry context menu
				if (a_bEditable && ImGui::BeginPopupContextItem()) {
					auto& style = ImGui::GetStyle();
					const auto xButtonSize = ImGui::CalcTextSize("Delete entry").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
					UI::UICommon::ButtonWithConfirmationModal(
						"Delete entry"sv, "Are you sure you want to remove the entry?\nThis operation cannot be undone!\n\n"sv, [&]() {
							ImGui::ClosePopupsExceptModals();
							removed = entry.get();
						},
						ImVec2(xButtonSize, 0));
					ImGui::EndPopup();
				}

				// Disable checkbox
				ImGui::SameLine();
				if (a_bEditable) {
					ImGui::PushID(entry.get());
					bool bEnabled = !entry->bDisabled;
					if (ImGui::Checkbox("##toggleEntry", &bEnabled)) {
						entry->bDisabled = !bEnabled;
						bEdited = true;
					}
					UI::UICommon::AddTooltip("Toggles the entry on/off");
					ImGui::PopID();
				}

				// Entry name: the pool, in the preset colour, or what the value names
				ImGui::SameLine();
				if (bPool) {
					UI::UICommon::TextUnformattedColored(pool ? UI::UICommon::CONDITION_PRESET_COLOR : UI::UICommon::INVALID_CONDITION_COLOR, entry->pool.data());
				} else {
					const std::string name = entry->value ? entry->value->GetArgument() : std::string{};
					if (name.empty() || name == "(none)"sv) {
						UI::UICommon::TextUnformattedColored(UI::UICommon::INVALID_CONDITION_COLOR, "(nothing picked)");
					} else {
						ImGui::TextUnformatted(name.data());
					}
				}

				// Right column: a value's weight and chance; a pool's entry count and its share of the draw
				UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
				if (bPool) {
					const float share = !entry->bDisabled && pool && total > 0.f ? pool->TotalWeight() / total * 100.f : 0.f;
					const auto count = pool ? pool->entries.size() : 0;
					ImGui::Text("%zu %s  (%.0f%%)", count, count == 1 ? "entry" : "entries", share);
				} else {
					const float chance = !entry->bDisabled && total > 0.f ? entry->weight / total * 100.f : 0.f;
					ImGui::Text("%.2f  (%.0f%%)", entry->weight, chance);
				}

				if (bNodeOpen) {
					ImGui::Spacing();
					ImGui::PushID(entry.get());

					if (bPool) {
						// The pool picked from the mod's, as a trigger preset is; then what it holds, as it is set
						if (a_bEditable) {
							ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
							if (ImGui::BeginCombo("##Pool", entry->pool.data())) {
								for (const auto& name : poolNames) {
									const bool bSelected = entry->pool == name;
									if (ImGui::Selectable(name.data(), bSelected) && !bSelected) {
										entry->pool = name;
										bEdited = true;
									}
									if (bSelected) {
										ImGui::SetItemDefaultFocus();
									}
								}
								ImGui::EndCombo();
							}
						} else {
							ImGui::TextUnformatted(entry->pool.data());
						}
						UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
						ImGui::TextUnformatted("Pool");
						ImGui::Spacing();
						if (pool) {
							pool->DisplayInUI(false, a_firstColumnWidthPercent, nullptr, nullptr);
						} else {
							UI::UICommon::TextUnformattedColored(UI::UICommon::INVALID_CONDITION_COLOR, "The mod has no pool of this name for this channel.");
						}
					} else {
						// The channel's own value field, then the weight
						if (entry->value && entry->value->DisplayInUI(a_bEditable, a_firstColumnWidthPercent)) {
							bEdited = true;
						}
						if (a_bEditable) {
							ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
							if (ImGui::InputFloat("##Weight", &entry->weight, 0.01f, 1.0f, "%.3f")) {
								entry->weight = (std::max)(entry->weight, 0.f);
								bEdited = true;
							}
						} else {
							ImGui::Text("%.3f", entry->weight);
						}
						UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
						ImGui::TextUnformatted("Weight");
						ImGui::SameLine();
						UI::UICommon::HelpMarker("How often this entry comes up against the others: its weight over the sum of every enabled entry's, a pool's entries counted by their own.");
					}
					deleteRow(entry.get());

					ImGui::PopID();
					ImGui::Spacing();
					ImGui::TreePop();
				}

				if (bStyleVarPushed) {
					ImGui::PopStyleVar();
				}
				ImGui::EndTable();
			}
			ImGui::PopStyleVar();
		}
		if (removed) {
			std::erase_if(entries, [&](const auto& a_entry) { return a_entry.get() == removed; });
			bEdited = true;
		}

		// The blank row, as a slot with no overlay shows
		if (entries.empty()) {
			const std::string tableId = std::format("{}blankPickTable", reinterpret_cast<std::uintptr_t>(this));
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			if (ImGui::BeginTable(tableId.data(), 1, ImGuiTableFlags_BordersOuter)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				UI::UICommon::TreeNodeCollapsedLeaf(std::format("No entries##{}", reinterpret_cast<std::uintptr_t>(this)).data(), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
				ImGui::EndTable();
			}
			ImGui::PopStyleVar();
		}

		if (a_bEditable) {
			ImGui::PushID(this);
			if (ImGui::Button("Add new entry")) {
				auto entry = std::make_unique<PickEntry>();
				entry->value = a_factory ? a_factory() : nullptr;
				entries.push_back(std::move(entry));
				bEdited = true;
			}
			if (a_pools) {
				ImGui::SameLine();
				ImGui::BeginDisabled(poolNames.empty());
				if (ImGui::Button("Add pool")) {
					auto entry = std::make_unique<PickEntry>();
					entry->pool = poolNames.front();
					entries.push_back(std::move(entry));
					bEdited = true;
				}
				ImGui::EndDisabled();
				if (poolNames.empty()) {
					ImGui::SameLine();
					UI::UICommon::HelpMarker("The mod has no pool for this channel. Add one in its References Pool Presets.");
				}
			}
			ImGui::PopID();
		}
		return bEdited;
	}

	void PickList::Parse(const rapidjson::Value& a_value, const ValueFactory& a_factory)
	{
		entries.clear();
		if (!a_value.IsArray()) {
			return;
		}
		for (const auto& one : a_value.GetArray()) {
			if (!one.IsObject()) {
				continue;
			}
			auto entry = std::make_unique<PickEntry>();
			if (const auto it = one.FindMember("pool"); it != one.MemberEnd() && it->value.IsString()) {
				entry->pool = it->value.GetString();
			} else if (const auto value = one.FindMember("value"); value != one.MemberEnd() && a_factory) {
				entry->value = a_factory();
				if (entry->value) {
					entry->value->Parse(value->value);
				}
			}
			if (const auto it = one.FindMember("weight"); it != one.MemberEnd() && it->value.IsNumber()) {
				entry->weight = it->value.GetFloat();
			}
			if (const auto it = one.FindMember("disabled"); it != one.MemberEnd() && it->value.IsBool()) {
				entry->bDisabled = it->value.GetBool();
			}
			entries.push_back(std::move(entry));
		}
	}

	rapidjson::Value PickList::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		rapidjson::Value array(rapidjson::kArrayType);
		for (const auto& entry : entries) {
			rapidjson::Value one(rapidjson::kObjectType);
			if (!entry->pool.empty()) {
				one.AddMember("pool", rapidjson::Value(entry->pool.data(), static_cast<rapidjson::SizeType>(entry->pool.size()), a_allocator), a_allocator);
			} else if (entry->value) {
				one.AddMember("value", entry->value->Serialize(a_allocator), a_allocator);
			}
			one.AddMember("weight", entry->weight, a_allocator);
			if (entry->bDisabled) {
				one.AddMember("disabled", true, a_allocator);
			}
			array.PushBack(one, a_allocator);
		}
		return array;
	}

	float PickList::TotalWeight(const PoolSource* a_pools) const
	{
		float total = 0.f;
		for (const auto& entry : entries) {
			if (entry->bDisabled) {
				continue;
			}
			if (entry->pool.empty()) {
				total += entry->weight;
			} else if (const auto* pool = a_pools && a_pools->entriesOf ? a_pools->entriesOf(entry->pool) : nullptr) {
				total += pool->TotalWeight();
			}
		}
		return total;
	}

	void RandomValue::Parse(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return;
		}
		if (const auto it = a_value.FindMember("entries"); it != a_value.MemberEnd()) {
			list.Parse(it->value, factory);
		}
		if (const auto it = a_value.FindMember("scope"); it != a_value.MemberEnd() && it->value.IsString()) {
			// The actor-wide scope's older name, Reference, read as Actor
			const std::string_view name = it->value.GetString();
			scope = name == "Reference"sv ? Scope::kActor : scope;
			for (const auto one : { Scope::kActivation, Scope::kChannel, Scope::kSubMod, Scope::kRegisteredMod, Scope::kActor }) {
				if (ScopeName(one) == name) {
					scope = one;
				}
			}
		}
	}

	rapidjson::Value RandomValue::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		rapidjson::Value object(rapidjson::kObjectType);
		object.AddMember("entries", list.Serialize(a_allocator), a_allocator);
		return object;
	}

	bool RandomValue::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		ImGui::PushID(this);
		const bool bEdited = list.DisplayInUI(a_bEditable, a_firstColumnWidthPercent, factory, &pools);
		ImGui::PopID();
		return bEdited;
	}

	void RandomStateChannelComponent::Parse(const rapidjson::Value& a_value)
	{
		if (!random || !a_value.IsString()) {
			return;
		}
		// The actor-wide scope's older name, Reference, read as Actor
		const std::string_view name = a_value.GetString();
		random->scope = name == "Reference"sv ? RandomValue::Scope::kActor : random->scope;
		for (const auto one : { RandomValue::Scope::kActivation, RandomValue::Scope::kChannel, RandomValue::Scope::kSubMod, RandomValue::Scope::kRegisteredMod, RandomValue::Scope::kActor }) {
			if (ScopeName(one) == name) {
				random->scope = one;
			}
		}
	}

	rapidjson::Value RandomStateChannelComponent::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		const auto name = ScopeName(random ? random->scope : RandomValue::Scope::kChannel);
		return rapidjson::Value(name.data(), static_cast<rapidjson::SizeType>(name.size()), a_allocator);
	}

	bool RandomStateChannelComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		if (!random) {
			return false;
		}
		using Scope = RandomValue::Scope;
		bool bEdited = false;
		auto& scope = random->scope;
		// As a condition's state data scope is picked
		if (a_bEditable) {
			ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
			ImGui::PushID(&scope);
			if (ImGui::BeginCombo("State data scope##Enum value", ScopeName(scope).data())) {
				for (const auto one : { Scope::kActivation, Scope::kChannel, Scope::kSubMod, Scope::kRegisteredMod, Scope::kActor }) {
					const bool bIsCurrent = one == scope;
					if (ImGui::Selectable(ScopeName(one).data(), bIsCurrent) && !bIsCurrent) {
						scope = one;
						bEdited = true;
					}
					if (bIsCurrent) {
						ImGui::SetItemDefaultFocus();
					}
					UI::UICommon::AddTooltip(ScopeTooltip(one).data());
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
			UI::UICommon::AddTooltip(ScopeTooltip(scope).data());
		} else {
			ImGui::TextUnformatted(std::format("State data scope: {}", ScopeName(scope)).data());
			UI::UICommon::AddTooltip(ScopeTooltip(scope).data());
		}
		return bEdited;
	}

	std::string RandomStateChannelComponent::GetArgument() const
	{
		return {};
	}

	bool DisplayValueTypeSlider(bool& a_bRandom, float a_firstColumnWidthPercent)
	{
		int type = a_bRandom ? 1 : 0;
		ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
		ImGui::PushID(&a_bRandom);
		const bool bEdited = ImGui::SliderInt("", &type, 0, 1, kValueTypeNames[type]);
		ImGui::PopID();
		if (bEdited) {
			a_bRandom = type == 1;
		}
		return bEdited;
	}

	std::string ReferenceChannelComponent::GetArgument() const
	{
		return random && bRandom ? std::format("random of {}", random->list.entries.size()) : value;
	}

	void ReferenceChannelComponent::Parse(const rapidjson::Value& a_value)
	{
		// A draw, {"random": ...}, or the name itself
		bRandom = random && a_value.IsObject() && a_value.HasMember("random");
		if (bRandom) {
			random->Parse(a_value["random"]);
		} else if (a_value.IsString()) {
			value = a_value.GetString();
		}
	}

	rapidjson::Value ReferenceChannelComponent::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		if (random && bRandom) {
			rapidjson::Value object(rapidjson::kObjectType);
			object.AddMember("random", random->Serialize(a_allocator), a_allocator);
			return object;
		}
		return rapidjson::Value(value.data(), static_cast<rapidjson::SizeType>(value.length()), a_allocator);
	}

	bool ReferenceChannelComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		// Static Value or Random Value, as a numeric value's type
		if (random) {
			if (a_bEditable) {
				bEdited |= DisplayValueTypeSlider(bRandom, a_firstColumnWidthPercent);
			} else if (bRandom) {
				ImGui::TextUnformatted(kValueTypeNames[1]);
			}
			if (bRandom) {
				return random->DisplayInUI(a_bEditable, a_firstColumnWidthPercent) || bEdited;
			}
		}

		// A value the reference would not be drawn with, in the error colour, the reason on hover
		const auto warning = warningOf && !value.empty() ? warningOf(value) : std::string{};

		if (!a_bEditable) {
			if (warning.empty()) {
				ImGui::TextUnformatted(value.empty() ? "(none)" : value.data());
			} else {
				UI::UICommon::TextUnformattedColored(UI::UICommon::ERROR_TEXT_COLOR, value.data());
				UI::UICommon::AddTooltip(warning.data());
			}
			return bEdited;
		}

		const auto& style = ImGui::GetStyle();
		const float totalWidth = UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent);

		ImGui::PushID(&value);

		// The same shape as the actor value and form rows: the field, then Pick
		// in the space two step buttons would take.
		ImGui::SetNextItemWidth(items ? totalWidth - UI::UICommon::PickerButtonWidth() - style.ItemInnerSpacing.x : totalWidth);
		if (!warning.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, UI::UICommon::ERROR_TEXT_COLOR);
		}
		if (ImGui::InputTextWithHint("##value", "Name, path or form...", &value)) {
			bEdited = true;
		}
		if (!warning.empty()) {
			ImGui::PopStyleColor();
			UI::UICommon::AddTooltip(warning.data());
		}

		if (items) {
			ImGui::SameLine(0.f, style.ItemInnerSpacing.x);

			// The picker hands back an index into the list it was given; the
			// names are kept here to turn it back into one.
			static std::vector<std::string> names;
			const auto indexed = [this] {
				auto list = items();
				names.clear();
				for (std::size_t i = 0; i < list.size(); ++i) {
					names.push_back(list[i].label);
					list[i].value = i;
				}
				return list;
			};

			std::uint64_t current = UINT64_MAX;
			if (const auto it = std::ranges::find(names, value); it != names.end()) {
				current = static_cast<std::uint64_t>(std::distance(names.begin(), it));
			}

			std::uint64_t picked = 0;
			if (UI::UICommon::Picker("Reference", indexed, current, picked) && picked < names.size()) {
				value = names[picked];
				bEdited = true;
			}
		}

		ImGui::PopID();

		UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
		ImGui::TextUnformatted(value.data());

		return bEdited;
	}

	void SwitchChannelComponent::Parse(const rapidjson::Value& a_value)
	{
		if (a_value.IsBool()) {
			bValue = a_value.GetBool();
		}
	}

	rapidjson::Value SwitchChannelComponent::Serialize([[maybe_unused]] rapidjson::Document::AllocatorType& a_allocator) const
	{
		return rapidjson::Value(bValue);
	}

	bool SwitchChannelComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		ImGui::PushID(&bValue);
		ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
		ImGui::BeginDisabled(!a_bEditable);
		if (ImGui::Checkbox("##value", &bValue)) {
			bEdited = true;
		}
		ImGui::PopID();
		ImGui::EndDisabled();

		return bEdited;
	}

	namespace
	{
		constexpr std::array kBlendModes{ "claim"sv, "add"sv, "saturate"sv, "average"sv, "max"sv, "min"sv };
		constexpr std::array kBlendModeLabels{ "Claim"sv, "Add"sv, "Saturate"sv, "Average"sv, "Max"sv, "Min"sv };

		template <class Enum, std::size_t N>
		bool FromName(const std::array<std::string_view, N>& a_names, std::string_view a_name, Enum& a_out)
		{
			for (std::size_t i = 0; i < N; ++i) {
				if (a_names[i] == a_name) {
					a_out = static_cast<Enum>(i);
					return true;
				}
			}
			return false;
		}

		// One combo over a name list, the current one shown; true when another was picked
		template <class Enum, std::size_t N>
		bool Combo(const char* a_id, const std::array<std::string_view, N>& a_labels, Enum& a_value)
		{
			bool bEdited = false;
			if (ImGui::BeginCombo(a_id, a_labels[static_cast<std::size_t>(a_value)].data())) {
				for (std::size_t i = 0; i < N; ++i) {
					const bool bIsCurrent = static_cast<std::size_t>(a_value) == i;
					if (ImGui::Selectable(a_labels[i].data(), bIsCurrent) && !bIsCurrent) {
						a_value = static_cast<Enum>(i);
						bEdited = true;
					}
					if (bIsCurrent) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			return bEdited;
		}
	}

	void BlendChannelComponent::Parse(const rapidjson::Value& a_value)
	{
		if (a_value.IsString()) {
			FromName(kBlendModes, a_value.GetString(), mode);
		}
	}

	rapidjson::Value BlendChannelComponent::Serialize([[maybe_unused]] rapidjson::Document::AllocatorType& a_allocator) const
	{
		const auto name = kBlendModes[static_cast<std::size_t>(mode)];
		return rapidjson::Value(rapidjson::StringRef(name.data(), name.length()));
	}

	bool BlendChannelComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		if (!a_bEditable) {
			ImGui::TextUnformatted(kBlendModeLabels[static_cast<std::size_t>(mode)].data());
			return false;
		}

		bool bEdited = false;
		const float totalWidth = UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent);

		ImGui::PushID(this);
		// Claim, nothing below counting, or a way of mixing with what is below
		ImGui::SetNextItemWidth(totalWidth);
		bEdited |= Combo("##mode", kBlendModeLabels, mode);

		UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
		ImGui::TextUnformatted(GetArgument().data());
		ImGui::PopID();

		return bEdited;
	}

	std::string BlendChannelComponent::GetArgument() const
	{
		return std::string(kBlendModes[static_cast<std::size_t>(mode)]);
	}

	IChannelComponent* ChannelBase::GetComponent(uint32_t a_index) const
	{
		return a_index < _components.size() ? _components[a_index].get() : nullptr;
	}

	std::string ChannelBase::GetArgument() const
	{
		std::string argument;
		for (const auto& component : _components) {
			if (!component->IsShown()) {
				continue;
			}
			const auto componentArgument = component->GetArgument();
			if (componentArgument.empty()) {
				continue;
			}
			if (!argument.empty()) {
				argument += ", ";
			}
			argument += componentArgument;
		}
		return argument;
	}

	void ChannelBase::Parse(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return;
		}

		const auto object = a_value.GetObj();
		if (const auto it = object.FindMember("disabled"); it != object.MemberEnd() && it->value.IsBool()) {
			_bDisabled = it->value.GetBool();
		}
		for (const auto& component : _components) {
			const auto name = component->GetName();
			if (const auto it = object.FindMember(rapidjson::StringRef(name.data(), name.length())); it != object.MemberEnd()) {
				component->Parse(it->value);
			}
		}
	}

	void ChannelBase::Serialize(void* a_value, void* a_allocator) const
	{
		auto& object = *static_cast<rapidjson::Value*>(a_value);
		auto& allocator = *static_cast<rapidjson::Document::AllocatorType*>(a_allocator);

		object.AddMember("channel", rapidjson::Value(GetName().data(), allocator), allocator);
		if (_bDisabled) {
			object.AddMember("disabled", true, allocator);
		}
		for (const auto& component : _components) {
			if (!component->IsShown()) {
				continue;
			}
			const auto name = component->GetName();
			object.AddMember(rapidjson::StringRef(name.data(), name.length()), component->Serialize(allocator), allocator);
		}
	}

	namespace
	{
		float ToLinear(float a_c)
		{
			return a_c <= 0.04045f ? a_c / 12.92f : std::pow((a_c + 0.055f) / 1.055f, 2.4f);
		}

		float ToGamma(float a_c)
		{
			a_c = std::clamp(a_c, 0.f, 1.f);
			return a_c <= 0.0031308f ? a_c * 12.92f : 1.055f * std::pow(a_c, 1.f / 2.4f) - 0.055f;
		}

		std::array<float, 3> ToOklab(const RE::NiColor& a_colour)
		{
			const float r = ToLinear(a_colour.red), g = ToLinear(a_colour.green), b = ToLinear(a_colour.blue);
			const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
			const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
			const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
			return { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s, 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
		}

		RE::NiColor FromOklab(const std::array<float, 3>& a_lab)
		{
			const float l = std::pow(a_lab[0] + 0.3963377774f * a_lab[1] + 0.2158037573f * a_lab[2], 3.f);
			const float m = std::pow(a_lab[0] - 0.1055613458f * a_lab[1] - 0.0638541728f * a_lab[2], 3.f);
			const float s = std::pow(a_lab[0] - 0.0894841775f * a_lab[1] - 1.2914855480f * a_lab[2], 3.f);
			return { ToGamma(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s), ToGamma(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s), ToGamma(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s) };
		}
	}

	RE::NiColor MixColour(const RE::NiColor& a_from, const RE::NiColor& a_to, float a_t, std::string_view a_space)
	{
		const float t = std::clamp(a_t, 0.f, 1.f);
		if (a_space == "Oklab"sv || a_space == "Oklch"sv) {
			const auto a = ToOklab(a_from);
			const auto b = ToOklab(a_to);
			if (a_space == "Oklab"sv) {
				return FromOklab({ std::lerp(a[0], b[0], t), std::lerp(a[1], b[1], t), std::lerp(a[2], b[2], t) });
			}
			const float ca = std::hypot(a[1], a[2]);
			const float cb = std::hypot(b[1], b[2]);
			float ha = std::atan2(a[2], a[1]);
			float hb = std::atan2(b[2], b[1]);
			// A grey has no hue: it takes the other end's, so the turn is not made through a colour neither has
			if (ca < 1e-4f) {
				ha = hb;
			}
			if (cb < 1e-4f) {
				hb = ha;
			}
			float dh = hb - ha;
			const float pi = std::numbers::pi_v<float>;
			if (dh > pi) {
				dh -= 2.f * pi;
			} else if (dh < -pi) {
				dh += 2.f * pi;
			}
			const float c = std::lerp(ca, cb, t);
			const float h = ha + dh * t;
			return FromOklab({ std::lerp(a[0], b[0], t), c * std::cos(h), c * std::sin(h) });
		}
		if (a_space != "sRGB"sv) {
			if (static bool bSaid = false; !bSaid) {
				bSaid = true;
				logger::error("channels: colour space '{}' is not sRGB, Oklab or Oklch, though the loader checks them; it mixes in sRGB", a_space);
			}
		}
		return { std::lerp(a_from.red, a_to.red, t), std::lerp(a_from.green, a_to.green, t), std::lerp(a_from.blue, a_to.blue, t) };
	}

	bool ColourSpaceChannelComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		const bool bEdited = ModeChannelComponent::DisplayInUI(a_bEditable, a_firstColumnWidthPercent);

		// Under the choice, a strip the colour field's size: the way from one end to the other in slices
		const auto ends = colours ? colours() : std::array<RE::NiColor, 2>{};
		const std::string_view mixSpace = value;
		const float width = UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent);
		const float height = ImGui::GetFrameHeight();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		auto* draw = ImGui::GetWindowDrawList();
		constexpr int kSlices = 48;
		for (int i = 0; i < kSlices; ++i) {
			const auto colour = MixColour(ends[0], ends[1], (static_cast<float>(i) + 0.5f) / kSlices, mixSpace);
			const float x0 = origin.x + width * i / kSlices;
			const float x1 = origin.x + width * (i + 1) / kSlices;
			draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1 + 1.f, origin.y + height), ImGui::GetColorU32(ImVec4(colour.red, colour.green, colour.blue, 1.f)));
		}
		draw->AddRect(origin, ImVec2(origin.x + width, origin.y + height), ImGui::GetColorU32(ImGuiCol_Border));
		ImGui::Dummy(ImVec2(width, height));
		return bEdited;
	}
}

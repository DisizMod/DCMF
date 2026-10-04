#include "RegisteredMods.h"

#include "Conditions.h"
#include "DetectedProblems.h"
#include "Functions.h"
#include "ModRegistry.h"
#include "ClipPlayer.h"
#include "Resolve.h"
#include "Scheduler.h"
#include "UI/UIManager.h"
#include "apply/Entries.h"
#include "scan/Scan.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <ranges>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>


// ---------------------------------------------------------------- SubMod ---

Functions::FunctionSet* SubMod::GetFunctionSet(Functions::FunctionSetType a_setType) const
{
	switch (a_setType) {
	case Functions::FunctionSetType::kOnActivate:
		return _functionSetOnActivate.get();
	case Functions::FunctionSetType::kOnDeactivate:
		return _functionSetOnDeactivate.get();
	case Functions::FunctionSetType::kOnTrigger:
		return _functionSetOnTrigger.get();
	default:
		break;
	}

	return nullptr;
}

bool SubMod::HasFunctionSet(Functions::FunctionSetType a_setType) const
{
	switch (a_setType) {
	case Functions::FunctionSetType::kOnActivate:
		return _functionSetOnActivate != nullptr;
	case Functions::FunctionSetType::kOnDeactivate:
		return _functionSetOnDeactivate != nullptr;
	case Functions::FunctionSetType::kOnTrigger:
		return _functionSetOnTrigger != nullptr;
	default:
		break;
	}

	return false;
}

bool SubMod::HasValidFunctionSet(Functions::FunctionSetType a_setType) const
{
	switch (a_setType) {
	case Functions::FunctionSetType::kOnActivate:
		if (_functionSetOnActivate) {
			return !_functionSetOnActivate->IsEmpty() && _functionSetOnActivate->IsValid();
		}
		break;
	case Functions::FunctionSetType::kOnDeactivate:
		if (_functionSetOnDeactivate) {
			return !_functionSetOnDeactivate->IsEmpty() && _functionSetOnDeactivate->IsValid();
		}
		break;
	case Functions::FunctionSetType::kOnTrigger:
		if (_functionSetOnTrigger) {
			return !_functionSetOnTrigger->IsEmpty() && _functionSetOnTrigger->IsValid();
		}
		break;
	default:
		break;
	}

	return false;
}

bool SubMod::HasAnyFunctionSet() const
{
	return _functionSetOnActivate || _functionSetOnDeactivate || _functionSetOnTrigger;
}

Functions::FunctionSet* SubMod::CreateOrGetFunctionSet(Functions::FunctionSetType a_setType)
{
	switch (a_setType) {
	case Functions::FunctionSetType::kOnActivate:
		if (!_functionSetOnActivate) {
			_functionSetOnActivate = std::make_unique<Functions::FunctionSet>(this, a_setType);
		}
		return _functionSetOnActivate.get();
	case Functions::FunctionSetType::kOnDeactivate:
		if (!_functionSetOnDeactivate) {
			_functionSetOnDeactivate = std::make_unique<Functions::FunctionSet>(this, a_setType);
		}
		return _functionSetOnDeactivate.get();
	case Functions::FunctionSetType::kOnTrigger:
		if (!_functionSetOnTrigger) {
			_functionSetOnTrigger = std::make_unique<Functions::FunctionSet>(this, a_setType);
		}
		return _functionSetOnTrigger.get();
	default:
		break;
	}

	return nullptr;
}

bool SubMod::IsDirty() const
{
	if (_bDirty) {
		return true;
	}

	if (_conditionSet->IsDirtyRecursive()) {
		return true;
	}

	if (_channelSet->IsDirtyRecursive()) {
		return true;
	}

	if (_functionSetOnActivate && _functionSetOnActivate->IsDirtyRecursive()) {
		return true;
	}

	if (_functionSetOnDeactivate && _functionSetOnDeactivate->IsDirtyRecursive()) {
		return true;
	}

	if (_functionSetOnTrigger && _functionSetOnTrigger->IsDirtyRecursive()) {
		return true;
	}

	// A clip's triggers are its own too
	if (const auto* clip = dynamic_cast<const Clip*>(this); clip && clip->GetTriggerSet()->IsDirtyRecursive()) {
		return true;
	}

	return false;
}

// ----------------------------------------------------------------- Clip ---

namespace
{
	// The transitions a key's number offers, legacy's curves; a colour goes at once or along a gradient in a space
	const std::vector<std::string> kKeyTransitions{ "Ease in out", "Ease in", "Ease out", "Linear", "Instant", "Bounce", "Spring" };
	const std::vector<std::string> kColourTransitions{ "Gradient", "Instant" };
	const std::vector<std::string> kColourSpaces{ "sRGB", "Oklab", "Oklch" };

	// The claimed channels flattened, groups and all, in the claimed order; a disabled claim still has its row
	void CollectClaimed(Channels::ChannelSet* a_set, std::vector<Apply::ChannelId>& a_out)
	{
		a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
			if (a_entry->IsGroup()) {
				CollectClaimed(static_cast<Channels::GroupChannel*>(a_entry.get())->channels.get(), a_out);
			} else if (const auto id = static_cast<Channels::Channel*>(a_entry.get())->GetId(); id.IsValid() && std::ranges::find(a_out, id) == a_out.end()) {
				a_out.push_back(id);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	// A set row: on; a frame's row off is one the frame does not set
	bool IsSet(const Channels::Channel* a_row)
	{
		return a_row && !a_row->IsDisabled();
	}

	Channels::Channel* FindRow(Channels::ChannelSet* a_set, Apply::ChannelId a_id)
	{
		Channels::Channel* found = nullptr;
		a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
			if (!a_entry->IsGroup() && static_cast<Channels::Channel*>(a_entry.get())->GetId() == a_id) {
				found = static_cast<Channels::Channel*>(a_entry.get());
				return RE::BSVisit::BSVisitControl::kStop;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return found;
	}
}

rapidjson::Value Keyframe::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
{
	rapidjson::Value object(rapidjson::kObjectType);
	object.AddMember("time", time, a_allocator);
	if (bHold) {
		object.AddMember("hold", holdTime, a_allocator);
	}
	object.AddMember("channels", channels->Serialize(a_allocator), a_allocator);
	if (functions && !functions->IsEmpty()) {
		object.AddMember("functions", functions->Serialize(a_allocator), a_allocator);
	}
	return object;
}

Clip::Clip(RegisteredMod* a_parentMod) :
	SubMod(a_parentMod),
	_variants([] {
		std::vector<Variant> one;
		one.emplace_back(static_cast<uint16_t>(0), "Default"sv, 0);
		return one;
	}())
{
	_triggerSet = std::make_unique<Triggers::TriggerSet>(this);
	_variants.SetParentSubMod(this);
	_tracks.push_back({ "Default", {} });
	// A claim added or dropped reaches every frame
	GetChannelSet()->AddOnDirtyCallback([this] { RefitKeyframes(); });
}

ClipVariant& Clip::AddVariant(std::string_view a_name)
{
	_variants.AddVariant(a_name);
	return _tracks.emplace_back(ClipVariant{ std::string(a_name), {} });
}

void Clip::RemoveVariant(uint16_t a_index)
{
	if (_tracks.size() <= 1 || a_index >= _tracks.size()) {
		return;  // a clip always has one
	}
	_variants.RemoveVariant(a_index);
	_tracks.erase(_tracks.begin() + a_index);
}

Keyframe* Clip::AddKeyframe(ClipVariant& a_variant, float a_time)
{
	auto& keyframe = a_variant.keyframes.emplace_back(std::make_unique<Keyframe>());
	keyframe->time = a_time;
	keyframe->channels = std::make_unique<Channels::ChannelSet>(this);
	keyframe->functions = std::make_unique<Functions::FunctionSet>(this, Functions::FunctionSetType::kOnActivate);
	std::vector<Apply::ChannelId> claimed;
	CollectClaimed(GetChannelSet(), claimed);
	auto* raw = keyframe.get();
	FitKeyframe(a_variant, *raw, claimed);
	SortKeyframes(a_variant);
	return raw;
}

Keyframe* Clip::AddKeyframeFromJson(ClipVariant& a_variant, const rapidjson::Value& a_value)
{
	if (!a_value.IsObject()) {
		return nullptr;
	}
	float time = 0.f;
	if (const auto it = a_value.FindMember("time"); it != a_value.MemberEnd() && it->value.IsNumber()) {
		time = it->value.GetFloat();
	}
	auto* keyframe = AddKeyframe(a_variant, time);
	if (const auto it = a_value.FindMember("hold"); it != a_value.MemberEnd() && it->value.IsNumber()) {
		keyframe->bHold = true;
		keyframe->holdTime = it->value.GetFloat();
	}
	// The rows keep their claimed shape; a pasted row's value lands on the row of the same channel
	if (const auto it = a_value.FindMember("channels"); it != a_value.MemberEnd() && it->value.IsArray()) {
		for (const auto& value : it->value.GetArray()) {
			auto pasted = Channels::CreateChannelFromJson(value);
			if (!pasted || pasted->IsGroup()) {
				const auto named = value.IsObject() && value.HasMember("channel") && value["channel"].IsString() ? value["channel"].GetString() : "(no channel)";
				logger::warn("storage: clip '{}' at {}s: a key row no table names is dropped: {}", GetName(), time, named);
				continue;
			}
			if (const auto invalid = Channels::InvalidModeOf(*pasted); !invalid.empty()) {
				logger::error("storage: clip '{}' at {}s: key row {} is malformed and not read: {}", GetName(), time, pasted->GetName(), invalid);
				continue;
			}
			auto* row = FindRow(keyframe->channels.get(), static_cast<Channels::Channel*>(pasted.get())->GetId());
			if (!row) {
				logger::warn("storage: clip '{}' at {}s: key row {} is not among the clip's claims and is dropped", GetName(), time, pasted->GetName());
				continue;
			}
			row->Parse(value);
			row->SetDisabled(pasted->IsDisabled());
		}
	}
	if (const auto it = a_value.FindMember("functions"); it != a_value.MemberEnd() && it->value.IsArray()) {
		for (const auto& value : it->value.GetArray()) {
			if (auto function = Functions::CreateFunctionFromJson(const_cast<rapidjson::Value&>(value), keyframe->functions.get())) {
				keyframe->functions->Add(function);
			}
		}
	}
	SortKeyframes(a_variant);
	return keyframe;
}

void Clip::RemoveKeyframe(ClipVariant& a_variant, Keyframe* a_keyframe)
{
	std::erase_if(a_variant.keyframes, [&](const auto& a_entry) { return a_entry.get() == a_keyframe; });
}

void Clip::SortKeyframes(ClipVariant& a_variant)
{
	std::ranges::stable_sort(a_variant.keyframes, [](const auto& a, const auto& b) { return a->time < b->time; });
}

bool Clip::IsLastKeyframeFor(const ClipVariant& a_variant, const Keyframe& a_keyframe, Apply::ChannelId a_id) const
{
	for (auto it = a_variant.keyframes.rbegin(); it != a_variant.keyframes.rend(); ++it) {
		if (IsSet(FindRow((*it)->channels.get(), a_id))) {
			return it->get() == &a_keyframe;
		}
	}
	return false;
}

Clip::SetRow Clip::LastSetBefore(const ClipVariant& a_variant, const Keyframe& a_keyframe, Apply::ChannelId a_id) const
{
	SetRow last;
	for (const auto& keyframe : a_variant.keyframes) {
		if (keyframe.get() == &a_keyframe) {
			break;
		}
		if (auto* row = FindRow(keyframe->channels.get(), a_id); IsSet(row)) {
			last = { keyframe.get(), row };
		}
	}
	return last;
}

const ClipVariant* Clip::VariantOf(const Keyframe* a_keyframe) const
{
	for (const auto& variant : _tracks) {
		if (std::ranges::any_of(variant.keyframes, [&](const auto& a_entry) { return a_entry.get() == a_keyframe; })) {
			return &variant;
		}
	}
	return nullptr;
}

Clip::SetRow Clip::FirstSetAfter(const ClipVariant& a_variant, const Keyframe& a_keyframe, Apply::ChannelId a_id) const
{
	bool bPassed = false;
	for (const auto& keyframe : a_variant.keyframes) {
		if (keyframe.get() == &a_keyframe) {
			bPassed = true;
			continue;
		}
		if (bPassed) {
			if (auto* row = FindRow(keyframe->channels.get(), a_id); IsSet(row)) {
				return { keyframe.get(), row };
			}
		}
	}
	return {};
}

const Keyframe* Clip::HeldBy(const ClipVariant& a_variant, const Keyframe& a_keyframe, Apply::ChannelId a_id) const
{
	for (const auto& keyframe : a_variant.keyframes) {
		if (keyframe.get() == &a_keyframe) {
			break;
		}
		if (keyframe->bHold && keyframe->time <= a_keyframe.time && a_keyframe.time < keyframe->time + keyframe->holdTime && IsSet(FindRow(keyframe->channels.get(), a_id))) {
			return keyframe.get();
		}
	}
	return nullptr;
}

void Clip::FitKeyframe(ClipVariant& a_variant, Keyframe& a_keyframe, const std::vector<Apply::ChannelId>& a_claimed)
{
	auto old = std::move(a_keyframe.channels);
	auto fitted = std::make_unique<Channels::ChannelSet>(this);
	for (const auto id : a_claimed) {
		// A row already there keeps its value: found under the set's read lock, then taken out once the walk is over,
		// since taking it out wants the write lock
		std::unique_ptr<Channels::ChannelBase>* slot = nullptr;
		old->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
			if (!a_entry->IsGroup() && static_cast<Channels::Channel*>(a_entry.get())->GetId() == id) {
				slot = &a_entry;
				return RE::BSVisit::BSVisitControl::kStop;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		std::unique_ptr<Channels::ChannelBase> row = slot ? old->Extract(*slot) : nullptr;
		// A new claim gets a fresh row, not set until the author says so, its blend hidden since a key claims. A
		// number or a colour moves, so it gets its two transitions, the exit one shown on the channel's last key alone,
		// and a colour the space it is mixed in; a mode, a text, a form or a switch is set at once and gets none
		if (!row) {
			row = Channels::CreateChannel(id);
			if (!row) {
				continue;
			}
			row->SetDisabled(true);
			// A blended pick moves as a number does; a stepped number does not move
			const auto type = Apply::Entries::GetSingleton().Get(id).type;
			const bool bNumber = type == Apply::ValueType::kNumber || type == Apply::ValueType::kBlendedPick;
			const bool bColour = dynamic_cast<Channels::ColourChannel*>(row.get()) != nullptr;
			if (auto* number = dynamic_cast<Channels::NumberChannel*>(row.get())) {
				number->blend->shownWhen = [] { return false; };
			}
			const auto isLast = [this, keyframe = &a_keyframe, id] {
				const auto* variant = VariantOf(keyframe);
				return variant && IsLastKeyframeFor(*variant, *keyframe, id);
			};
			if (bNumber) {
				// Legacy's curves; a bounce or a spring with how far it falls back or overshoots, a fraction of the move
				auto* transition = row->AddComponent<Channels::ModeChannelComponent>("Transition"sv, "The way into this key from the channel's last value, over the time to this frame."sv, kKeyTransitions);
				auto* strength = row->AddComponent<Channels::NumberChannelComponent>("Strength"sv, "How far a bounce falls back or a spring overshoots, as a fraction of the move."sv, 0.f, 0.5f, 0.3f);
				strength->shownWhen = [transition] { return transition->value == "Bounce" || transition->value == "Spring"; };
				auto* exit = row->AddComponent<Channels::ModeChannelComponent>("Exit transition"sv, "The way back to rest once the clip ends, on the channel's last key."sv, kKeyTransitions);
				exit->shownWhen = isLast;
				auto* exitStrength = row->AddComponent<Channels::NumberChannelComponent>("Exit strength"sv, "How far the exit's bounce falls back or its spring overshoots."sv, 0.f, 0.5f, 0.3f);
				exitStrength->shownWhen = [exit, isLast] { return isLast() && (exit->value == "Bounce" || exit->value == "Spring"); };
			}
			if (bColour) {
				// At once, or along a gradient mixed in a space, previewed from the last value to this one
				auto* colourRow = static_cast<Channels::ColourChannel*>(row.get());
				auto* transition = row->AddComponent<Channels::ModeChannelComponent>("Transition"sv, "At once, or along a gradient from the channel's last colour, over the time to this frame."sv, kColourTransitions);
				auto* space = row->AddComponent<Channels::ColourSpaceChannelComponent>("Colour space"sv, "Where the gradient is mixed: component by component as written, or perceptually, by lightness and opponent axes, or by hue turned the short way. Drawn under it, from the channel's last colour to this one."sv, kColourSpaces, [this, keyframe = &a_keyframe, id, colourRow] {
					// Before any key, what the reference actor has of its own, else white
					RE::NiColor from = ClipPlayer::OwnColour(UI::UIManager::GetSingleton().GetRefrToEvaluate(), id).value_or(RE::NiColor{ 1.f, 1.f, 1.f });
					if (const auto* variant = VariantOf(keyframe)) {
						if (const auto before = LastSetBefore(*variant, *keyframe, id); before.row) {
							if (auto* last = dynamic_cast<const Channels::ColourChannel*>(before.row)) {
								from = last->value->value.GetValue();
							}
						}
					}
					return std::array{ from, colourRow->value->value.GetValue() };
				});
				space->shownWhen = [transition] { return transition->value == "Gradient"; };
				auto* exit = row->AddComponent<Channels::ModeChannelComponent>("Exit transition"sv, "The way back to rest once the clip ends, on the channel's last key."sv, kColourTransitions);
				exit->shownWhen = isLast;
				auto* exitSpace = row->AddComponent<Channels::ColourSpaceChannelComponent>("Exit colour space"sv, "Where the exit's gradient is mixed. Drawn under it, from this colour to the channel's rest."sv, kColourSpaces, [id, colourRow] {
					// Back to what the reference actor has of its own, else the channel's default
					RE::NiColor rest{ 1.f, 1.f, 1.f };
					if (const auto own = ClipPlayer::OwnColour(UI::UIManager::GetSingleton().GetRefrToEvaluate(), id)) {
						rest = *own;
					} else if (auto fresh = Channels::CreateChannel(id)) {
						if (auto* colour = dynamic_cast<Channels::ColourChannel*>(fresh.get())) {
							rest = colour->value->value.GetValue();
						}
					}
					return std::array{ colourRow->value->value.GetValue(), rest };
				});
				exitSpace->shownWhen = [exit, isLast] { return isLast() && exit->value == "Gradient"; };
			}
		}
		fitted->Add(row);
	}
	a_keyframe.channels = std::move(fitted);
}

void Clip::RefitKeyframes()
{
	std::vector<Apply::ChannelId> claimed;
	CollectClaimed(GetChannelSet(), claimed);
	for (auto& variant : _tracks) {
		for (auto& keyframe : variant.keyframes) {
			FitKeyframe(variant, *keyframe, claimed);
		}
	}
}

void Clip::ResetKeyframe(Keyframe& a_keyframe)
{
	a_keyframe.channels = std::make_unique<Channels::ChannelSet>(this);
	std::vector<Apply::ChannelId> claimed;
	CollectClaimed(GetChannelSet(), claimed);
	for (auto& variant : _tracks) {
		if (std::ranges::any_of(variant.keyframes, [&](const auto& a_entry) { return a_entry.get() == &a_keyframe; })) {
			FitKeyframe(variant, a_keyframe, claimed);
		}
	}
}

void SubMod::SetDirty(bool a_bDirty)
{
	_bDirty = a_bDirty;
	if (a_bDirty) {
		Scheduler::Recompile();
	}
}

void SubMod::SetDirtyRecursive(bool a_bDirty)
{
	_bDirty = a_bDirty;
	_conditionSet->SetDirtyRecursive(a_bDirty);
	_channelSet->SetDirtyRecursive(a_bDirty);
	if (_functionSetOnActivate) {
		_functionSetOnActivate->SetDirtyRecursive(a_bDirty);
	}
	if (_functionSetOnDeactivate) {
		_functionSetOnDeactivate->SetDirtyRecursive(a_bDirty);
	}
	if (_functionSetOnTrigger) {
		_functionSetOnTrigger->SetDirtyRecursive(a_bDirty);
	}
}

bool SubMod::HasInvalidConditions() const
{
	return !_conditionSet->IsValid();
}

bool SubMod::HasInvalidFunctions() const
{
	return (_functionSetOnActivate && !_functionSetOnActivate->IsValid()) ||
	       (_functionSetOnDeactivate && !_functionSetOnDeactivate->IsValid()) ||
	       (_functionSetOnTrigger && !_functionSetOnTrigger->IsValid());
}

// One clip or modifier is a part of its mod's file: read again from it, and saved with the rest of it
bool SubMod::ReloadConfig()
{
	return _parentMod ? _parentMod->ReloadSubMod(this) : false;
}

void SubMod::SaveConfig(EditMode a_editMode, bool)
{
	if (_parentMod) {
		_parentMod->SaveConfig(a_editMode);
	}
}

namespace
{
	// What a reload replaced, kept for the session: a running clip, a compiled rule, a preview or the timeline may still
	// point at it, from another thread
	void Retire(std::shared_ptr<void> a_object)
	{
		static std::mutex lock;
		static std::vector<std::shared_ptr<void>> retired;
		std::scoped_lock guard(lock);
		retired.push_back(std::move(a_object));
	}

	template <class T>
	void Retire(std::unique_ptr<T>&& a_object)
	{
		if (a_object) {
			Retire(std::shared_ptr<void>(std::shared_ptr<T>(std::move(a_object))));
		}
	}

	std::string_view StringOf(const rapidjson::Value& a_object, const char* a_key, std::string_view a_default = ""sv)
	{
		const auto it = a_object.FindMember(a_key);
		return it != a_object.MemberEnd() && it->value.IsString() ? std::string_view(it->value.GetString(), it->value.GetStringLength()) : a_default;
	}

	bool BoolOf(const rapidjson::Value& a_object, const char* a_key, bool a_default = false)
	{
		const auto it = a_object.FindMember(a_key);
		return it != a_object.MemberEnd() && it->value.IsBool() ? it->value.GetBool() : a_default;
	}

	float FloatOf(const rapidjson::Value& a_object, const char* a_key, float a_default)
	{
		const auto it = a_object.FindMember(a_key);
		return it != a_object.MemberEnd() && it->value.IsNumber() ? it->value.GetFloat() : a_default;
	}

	rapidjson::Value* ArrayOf(rapidjson::Value& a_object, const char* a_key)
	{
		const auto it = a_object.FindMember(a_key);
		return it != a_object.MemberEnd() && it->value.IsArray() ? &it->value : nullptr;
	}

	rapidjson::Value Text(std::string_view a_text, rapidjson::Document::AllocatorType& a_allocator)
	{
		return rapidjson::Value(a_text.data(), static_cast<rapidjson::SizeType>(a_text.size()), a_allocator);
	}

	constexpr std::array kFunctionSets{ std::pair{ Functions::FunctionSetType::kOnActivate, "onActivate" }, std::pair{ Functions::FunctionSetType::kOnDeactivate, "onDeactivate" },
		std::pair{ Functions::FunctionSetType::kOnTrigger, "onTrigger" } };

	void ParseConditions(Conditions::ConditionSet* a_set, rapidjson::Value* a_array)
	{
		a_set->Clear();
		if (!a_array) {
			return;
		}
		for (auto& value : a_array->GetArray()) {
			auto condition = Conditions::CreateConditionFromJson(value, a_set);
			a_set->Add(condition);
		}
	}

	void ParseChannels(Channels::ChannelSet* a_set, rapidjson::Value* a_array)
	{
		a_set->Clear();
		if (!a_array) {
			return;
		}
		for (const auto& value : a_array->GetArray()) {
			auto channel = Channels::CreateChannelFromJson(value);
			if (!channel) {
				logger::warn("storage: a channel no table names is dropped: {}", StringOf(value, "channel"));
				continue;
			}
			if (const auto invalid = Channels::InvalidModeOf(*channel); !invalid.empty()) {
				auto* owner = a_set->GetParentSubMod();
				logger::error("storage: '{}' row {} is malformed and not read: {}", owner ? owner->GetName() : std::string_view("(unknown)"), channel->GetName(), invalid);
				continue;
			}
			a_set->Add(channel);
		}
	}

	void ParseTriggers(Triggers::TriggerSet* a_set, rapidjson::Value* a_array, RegisteredMod* a_mod)
	{
		a_set->Clear();
		if (!a_array) {
			return;
		}
		for (const auto& value : a_array->GetArray()) {
			if (auto trigger = Triggers::CreateTriggerFromJson(value)) {
				if (auto* preset = dynamic_cast<Triggers::PresetTrigger*>(trigger.get())) {
					preset->preset->mod = a_mod;
				}
				a_set->Add(trigger);
			} else {
				logger::warn("storage: a trigger of no known type is dropped: {}", StringOf(value, "type"));
			}
		}
	}
}

namespace
{
	// Whether a mod other than this one defines a slot of the name: slots of one name are one slot, merged
	bool SlotDefinedElsewhere(const RegisteredMod* a_self, std::string_view a_name)
	{
		bool bFound = false;
		ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
			bFound = bFound || (a_mod != a_self && a_mod->HasOverlaySlot(a_name));
		});
		return bFound;
	}
}

rapidjson::Value SubMod::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
{
	rapidjson::Value object(rapidjson::kObjectType);
	object.AddMember("name", Text(_name, a_allocator), a_allocator);
	if (!_description.empty()) {
		object.AddMember("description", Text(_description, a_allocator), a_allocator);
	}
	object.AddMember("priority", _priority, a_allocator);
	if (_bDisabled) {
		object.AddMember("disabled", true, a_allocator);
	}
	if (_bInterruptible) {
		object.AddMember("interruptible", true, a_allocator);
	}
	if (_bReplaceOnEcho) {
		object.AddMember("replaceOnEcho", true, a_allocator);
	}
	object.AddMember("conditions", _conditionSet->Serialize(a_allocator), a_allocator);
	for (const auto& [type, key] : kFunctionSets) {
		if (auto* set = GetFunctionSet(type); set && !set->IsEmpty()) {
			object.AddMember(rapidjson::StringRef(key), set->Serialize(a_allocator), a_allocator);
		}
	}
	return object;
}

void SubMod::Parse(rapidjson::Value& a_value)
{
	_name = StringOf(a_value, "name", _name);
	_description = StringOf(a_value, "description");
	_priority = static_cast<int32_t>(FloatOf(a_value, "priority", 0.f));
	_bDisabled = BoolOf(a_value, "disabled");
	// A list an older file kept per clip or modifier joins the mod's, which saves it from then on
	if (auto* required = ArrayOf(a_value, "requiredMods"); required && _parentMod) {
		auto& mods = _parentMod->GetRequiredMods();
		for (const auto& mod : required->GetArray()) {
			if (mod.IsString() && std::ranges::find(mods, std::string_view(mod.GetString())) == mods.end()) {
				mods.emplace_back(mod.GetString());
			}
		}
	}
	_bInterruptible = BoolOf(a_value, "interruptible");
	_bReplaceOnEcho = BoolOf(a_value, "replaceOnEcho");
	ParseConditions(_conditionSet.get(), ArrayOf(a_value, "conditions"));
	for (const auto& [type, key] : kFunctionSets) {
		auto* array = ArrayOf(a_value, key);
		if (!array) {
			if (auto* set = GetFunctionSet(type)) {
				set->Clear();
			}
			continue;
		}
		auto* set = CreateOrGetFunctionSet(type);
		set->Clear();
		for (auto& value : array->GetArray()) {
			if (auto function = Functions::CreateFunctionFromJson(value, set)) {
				set->Add(function);
			}
		}
	}
}

rapidjson::Value AppearanceModifier::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
{
	auto object = SubMod::Serialize(a_allocator);
	object.AddMember("channels", GetChannelSet()->Serialize(a_allocator), a_allocator);
	const auto transition = [&](const char* a_key, bool a_bEnabled, bool a_bCustom, float a_time) {
		if (!a_bEnabled) {
			return;
		}
		rapidjson::Value one(rapidjson::kObjectType);
		if (a_bCustom) {
			one.AddMember("time", a_time, a_allocator);
		}
		object.AddMember(rapidjson::StringRef(a_key), one, a_allocator);
	};
	transition("startTransition", _bStartTransition, _bCustomStartTransitionTime, _startTransitionTime);
	transition("endTransition", _bEndTransition, _bCustomEndTransitionTime, _endTransitionTime);
	return object;
}

void AppearanceModifier::Parse(rapidjson::Value& a_value)
{
	SubMod::Parse(a_value);
	ParseChannels(GetChannelSet(), ArrayOf(a_value, "channels"));
	// A clip once named here as a transition is a trigger now, OnModifierActivated or OnModifierDeactivated: an old
	// file's clip transition is read as the smooth one
	const auto transition = [&](const char* a_key, bool& a_bEnabled, bool& a_bCustom, float& a_time) {
		const auto it = a_value.FindMember(a_key);
		a_bEnabled = it != a_value.MemberEnd() && it->value.IsObject();
		if (!a_bEnabled) {
			return;
		}
		a_bCustom = it->value.HasMember("time");
		a_time = FloatOf(it->value, "time", 0.3f);
	};
	transition("startTransition", _bStartTransition, _bCustomStartTransitionTime, _startTransitionTime);
	transition("endTransition", _bEndTransition, _bCustomEndTransitionTime, _endTransitionTime);
}

rapidjson::Value Clip::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
{
	auto object = SubMod::Serialize(a_allocator);
	object.AddMember("triggers", _triggerSet->Serialize(a_allocator), a_allocator);
	object.AddMember("claims", GetChannelSet()->Serialize(a_allocator), a_allocator);
	if (_bStopWhenConditionsFail) {
		object.AddMember("stopWhenConditionsFail", true, a_allocator);
	}
	if (_bApplyExitOnInterrupt) {
		object.AddMember("applyExitOnInterrupt", true, a_allocator);
	}
	if (_bCustomExitTime) {
		object.AddMember("customExitTime", _customExitTime, a_allocator);
	}
	auto& variants = const_cast<Variants&>(_variants);
	object.AddMember("variantMode", rapidjson::StringRef(variants.GetVariantMode() == VariantMode::kSequential ? "sequential" : "random"), a_allocator);
	object.AddMember("resetRandomOnLoopOrEcho", variants.ShouldResetRandomOnLoopOrEcho(), a_allocator);
	rapidjson::Value list(rapidjson::kArrayType);
	for (uint16_t i = 0; i < _tracks.size(); ++i) {
		rapidjson::Value one(rapidjson::kObjectType);
		one.AddMember("name", Text(_tracks[i].name, a_allocator), a_allocator);
		if (const auto* variant = variants.GetVariant(i)) {
			if (variant->IsDisabled()) {
				one.AddMember("disabled", true, a_allocator);
			}
			if (variant->ShouldPlayOnce()) {
				one.AddMember("playOnce", true, a_allocator);
			}
			one.AddMember("weight", variant->GetWeight(), a_allocator);
			one.AddMember("order", variant->GetOrder(), a_allocator);
		}
		rapidjson::Value keyframes(rapidjson::kArrayType);
		for (const auto& keyframe : _tracks[i].keyframes) {
			keyframes.PushBack(keyframe->Serialize(a_allocator), a_allocator);
		}
		one.AddMember("keyframes", keyframes, a_allocator);
		list.PushBack(one, a_allocator);
	}
	object.AddMember("variants", list, a_allocator);
	return object;
}

void Clip::Parse(rapidjson::Value& a_value)
{
	SubMod::Parse(a_value);
	ParseTriggers(_triggerSet.get(), ArrayOf(a_value, "triggers"), GetParentMod());
	_bStopWhenConditionsFail = BoolOf(a_value, "stopWhenConditionsFail");
	_bApplyExitOnInterrupt = BoolOf(a_value, "applyExitOnInterrupt");
	_bCustomExitTime = a_value.HasMember("customExitTime");
	_customExitTime = FloatOf(a_value, "customExitTime", 0.3f);

	// The claims first: every frame read after them is fitted to them
	ParseChannels(GetChannelSet(), ArrayOf(a_value, "claims"));

	// The variants back to the one every clip has, their frames retired, then the file's
	while (_tracks.size() > 1) {
		for (auto& keyframe : _tracks.back().keyframes) {
			Retire(std::move(keyframe));
		}
		RemoveVariant(static_cast<uint16_t>(_tracks.size() - 1));
	}
	for (auto& keyframe : _tracks.front().keyframes) {
		Retire(std::move(keyframe));
	}
	_tracks.front().keyframes.clear();

	_variants.SetVariantMode(StringOf(a_value, "variantMode") == "sequential"sv ? VariantMode::kSequential : VariantMode::kRandom);
	_variants.SetShouldResetRandomOnLoopOrEcho(BoolOf(a_value, "resetRandomOnLoopOrEcho", true));
	if (auto* list = ArrayOf(a_value, "variants")) {
		uint16_t index = 0;
		for (auto& one : list->GetArray()) {
			if (!one.IsObject()) {
				continue;
			}
			const auto name = StringOf(one, "name", "Default"sv);
			auto& track = index == 0 ? _tracks.front() : AddVariant(name);
			track.name = name;
			if (auto* variant = _variants.GetVariant(index)) {
				variant->SetDisabled(BoolOf(one, "disabled"));
				variant->SetPlayOnce(BoolOf(one, "playOnce"));
				variant->SetWeight(FloatOf(one, "weight", 1.f));
				variant->SetOrder(static_cast<int32_t>(FloatOf(one, "order", static_cast<float>(index))));
			}
			if (auto* keyframes = ArrayOf(one, "keyframes")) {
				for (const auto& keyframe : keyframes->GetArray()) {
					AddKeyframeFromJson(track, keyframe);
				}
			}
			++index;
		}
	}
	_variants.UpdateVariantCache();
}

RegisteredMod* SubMod::GetParentMod() const
{
	return _parentMod;
}

// ----------------------------------------------------------- RegisteredMod ---

void RegisteredMod::SetName(std::string_view a_name)
{
	_name = a_name;
}

bool RegisteredMod::IsDirty() const
{
	if (_bDirty) {
		return true;
	}
	using Result = RE::BSVisit::BSVisitControl;
	if (ForEachSubMod([](SubMod* a_subMod) { return a_subMod->IsDirty() ? Result::kStop : Result::kContinue; }) == Result::kStop) {
		return true;
	}
	if (ForEachConditionPreset([](Conditions::ConditionPreset* a_preset) { return a_preset->IsDirtyRecursive() ? Result::kStop : Result::kContinue; }) == Result::kStop) {
		return true;
	}
	return ForEachTriggerPreset([](Triggers::TriggerPreset* a_preset) { return a_preset->IsDirtyRecursive() ? Result::kStop : Result::kContinue; }) == Result::kStop;
}

namespace
{
	constexpr auto kModsFolder = "Data\\SKSE\\Plugins\\DCMF"sv;
	constexpr auto kAuthorFile = "config.json"sv;
	constexpr auto kUserFile = "user.json"sv;

	std::atomic<bool> g_bLoaded = false;

	std::optional<std::string> ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path, std::ios::binary);
		if (!file) {
			return std::nullopt;
		}
		return std::string(std::istreambuf_iterator<char>(file), {});
	}

	// The file read and parsed, or the reason it cannot be
	bool ReadDocument(const std::filesystem::path& a_path, rapidjson::Document& a_out)
	{
		const auto text = ReadText(a_path);
		if (!text) {
			logger::error("storage: cannot open {}", a_path.string());
			return false;
		}
		a_out.Parse(text->data());
		if (a_out.HasParseError() || !a_out.IsObject()) {
			logger::error("storage: {} is not a JSON object (error {} at offset {})", a_path.string(), static_cast<int>(a_out.GetParseError()), a_out.GetErrorOffset());
			return false;
		}
		return true;
	}

	// Every edit mark cleared: the mod, its clips and modifiers with their triggers, its presets
	void MarkClean(RegisteredMod* a_mod)
	{
		a_mod->ForEachSubMod([](SubMod* a_subMod) {
			a_subMod->SetDirtyRecursive(false);
			if (auto* clip = dynamic_cast<Clip*>(a_subMod)) {
				clip->GetTriggerSet()->SetDirtyRecursive(false);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		a_mod->ForEachConditionPreset([](Conditions::ConditionPreset* a_preset) {
			a_preset->SetDirtyRecursive(false);
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		a_mod->ForEachTriggerPreset([](Triggers::TriggerPreset* a_preset) {
			a_preset->SetDirtyRecursive(false);
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}
}

rapidjson::Value RegisteredMod::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
{
	using Result = RE::BSVisit::BSVisitControl;
	rapidjson::Value object(rapidjson::kObjectType);
	object.AddMember("name", Text(_name, a_allocator), a_allocator);
	object.AddMember("author", Text(_author, a_allocator), a_allocator);
	if (!_description.empty()) {
		object.AddMember("description", Text(_description, a_allocator), a_allocator);
	}
	if (!_requiredMods.empty()) {
		rapidjson::Value required(rapidjson::kArrayType);
		for (const auto& mod : _requiredMods) {
			required.PushBack(Text(mod, a_allocator), a_allocator);
		}
		object.AddMember("requiredMods", required, a_allocator);
	}

	rapidjson::Value conditionPresets(rapidjson::kArrayType);
	ForEachConditionPreset([&](Conditions::ConditionPreset* a_preset) {
		rapidjson::Value one(rapidjson::kObjectType);
		one.AddMember("name", Text(a_preset->GetName(), a_allocator), a_allocator);
		if (!a_preset->GetDescription().empty()) {
			one.AddMember("description", Text(a_preset->GetDescription(), a_allocator), a_allocator);
		}
		one.AddMember("conditions", a_preset->Serialize(a_allocator), a_allocator);
		conditionPresets.PushBack(one, a_allocator);
		return Result::kContinue;
	});
	object.AddMember("conditionPresets", conditionPresets, a_allocator);

	rapidjson::Value triggerPresets(rapidjson::kArrayType);
	ForEachTriggerPreset([&](Triggers::TriggerPreset* a_preset) {
		rapidjson::Value one(rapidjson::kObjectType);
		one.AddMember("name", Text(a_preset->GetName(), a_allocator), a_allocator);
		if (!a_preset->GetDescription().empty()) {
			one.AddMember("description", Text(a_preset->GetDescription(), a_allocator), a_allocator);
		}
		one.AddMember("triggers", a_preset->Serialize(a_allocator), a_allocator);
		triggerPresets.PushBack(one, a_allocator);
		return Result::kContinue;
	});
	object.AddMember("triggerPresets", triggerPresets, a_allocator);

	rapidjson::Value referencePools(rapidjson::kArrayType);
	ForEachReferencePool([&](ReferencePool* a_pool) {
		rapidjson::Value one(rapidjson::kObjectType);
		one.AddMember("name", Text(a_pool->GetName(), a_allocator), a_allocator);
		if (!a_pool->GetDescription().empty()) {
			one.AddMember("description", Text(a_pool->GetDescription(), a_allocator), a_allocator);
		}
		one.AddMember("channel", Text(a_pool->GetChannel(), a_allocator), a_allocator);
		one.AddMember("entries", a_pool->entries.Serialize(a_allocator), a_allocator);
		referencePools.PushBack(one, a_allocator);
		return Result::kContinue;
	});
	object.AddMember("referencePools", referencePools, a_allocator);

	rapidjson::Value slots(rapidjson::kArrayType);
	ForEachOverlaySlot([&](Overlays::Slot* a_slot) {
		slots.PushBack(a_slot->Serialize(a_allocator), a_allocator);
		return Result::kContinue;
	});
	object.AddMember("overlaySlots", slots, a_allocator);

	rapidjson::Value skins(rapidjson::kArrayType);
	ForEachSkinSet([&](Skins::Set* a_set) {
		skins.PushBack(a_set->Serialize(a_allocator), a_allocator);
		return Result::kContinue;
	});
	object.AddMember("skinSets", skins, a_allocator);

	rapidjson::Value modifiers(rapidjson::kArrayType);
	rapidjson::Value clips(rapidjson::kArrayType);
	ForEachSubMod([&](SubMod* a_subMod) {
		(a_subMod->GetKind() == SubModKind::kClip ? clips : modifiers).PushBack(a_subMod->Serialize(a_allocator), a_allocator);
		return Result::kContinue;
	});
	object.AddMember("modifiers", modifiers, a_allocator);
	object.AddMember("clips", clips, a_allocator);
	return object;
}

void RegisteredMod::Parse(rapidjson::Value& a_value)
{
	_name = StringOf(a_value, "name", _name);
	_author = StringOf(a_value, "author", _author);
	_description = StringOf(a_value, "description");
	_requiredMods.clear();
	if (auto* required = ArrayOf(a_value, "requiredMods")) {
		for (const auto& mod : required->GetArray()) {
			if (mod.IsString()) {
				_requiredMods.emplace_back(mod.GetString());
			}
		}
	}

	// What the clips and modifiers point at first: presets, slots and sets, the old ones retired
	{
		WriteLocker locker(_presetsLock);
		for (auto& preset : _conditionPresets) {
			Retire(std::move(preset));
		}
		_conditionPresets.clear();
		for (auto& preset : _triggerPresets) {
			Retire(std::move(preset));
		}
		_triggerPresets.clear();
		for (auto& pool : _referencePools) {
			Retire(std::move(pool));
		}
		_referencePools.clear();
		for (auto& slot : _overlaySlots) {
			if (!SlotDefinedElsewhere(this, slot->GetName())) {
				Apply::Entries::GetSingleton().RetireOverlaySlot(slot->GetName());
			}
			Retire(std::move(slot));
		}
		_overlaySlots.clear();
		for (auto& set : _skinSets) {
			Retire(std::move(set));
		}
		_skinSets.clear();
	}
	if (auto* presets = ArrayOf(a_value, "conditionPresets")) {
		for (auto& one : presets->GetArray()) {
			auto preset = std::make_unique<Conditions::ConditionPreset>(StringOf(one, "name"), StringOf(one, "description"));
			ParseConditions(preset.get(), ArrayOf(one, "conditions"));
			// A preset holds no other preset, at any depth: such a row never holds, and the mod is listed as having one
			const std::function<void(Conditions::ConditionSet*)> nested = [&](Conditions::ConditionSet* a_set) {
				a_set->ForEach([&](std::unique_ptr<Conditions::ICondition>& a_condition) {
					for (std::uint32_t i = 0; i < a_condition->GetNumComponents(); ++i) {
						auto* component = a_condition->GetComponent(i);
						if (component->GetType() == Conditions::ConditionComponentType::kPreset) {
							logger::error("registered mod '{}': condition preset '{}' holds a PRESET row ({}); a preset cannot hold another, so that row never holds",
								_name, preset->GetName(), a_condition->GetArgument().c_str());
						} else if (component->GetType() == Conditions::ConditionComponentType::kMulti) {
							nested(static_cast<Conditions::MultiConditionComponent*>(component)->GetConditions());
						}
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			};
			nested(preset.get());
			AddConditionPreset(preset);
		}
	}
	if (auto* presets = ArrayOf(a_value, "triggerPresets")) {
		for (auto& one : presets->GetArray()) {
			auto preset = std::make_unique<Triggers::TriggerPreset>(StringOf(one, "name"), StringOf(one, "description"));
			ParseTriggers(preset.get(), ArrayOf(one, "triggers"), this);
			AddTriggerPreset(preset);
		}
	}
	if (auto* pools = ArrayOf(a_value, "referencePools")) {
		for (auto& one : pools->GetArray()) {
			auto pool = std::make_unique<ReferencePool>(StringOf(one, "name"));
			pool->SetDescription(StringOf(one, "description"));
			pool->SetChannel(StringOf(one, "channel"));
			if (const auto it = one.FindMember("entries"); it != one.MemberEnd()) {
				pool->entries.Parse(it->value, Channels::ValueFactoryFor(Apply::Entries::GetSingleton().Find(pool->GetChannel())));
			}
			AddReferencePool(pool);
		}
	}
	if (ArrayOf(a_value, "feeds")) {
		logger::warn("registered mod '{}': its feeds are not read; a plugin registers a feed through DCMF's resources API now", _name);
	}
	if (auto* slots = ArrayOf(a_value, "overlaySlots")) {
		for (const auto& one : slots->GetArray()) {
			auto slot = std::make_unique<Overlays::Slot>(StringOf(one, "name"));
			slot->Parse(one);
			AddOverlaySlot(slot);
		}
	}
	if (auto* sets = ArrayOf(a_value, "skinSets")) {
		for (const auto& one : sets->GetArray()) {
			auto set = std::make_unique<Skins::Set>(StringOf(one, "name"));
			set->Parse(one);
			AddSkinSet(set);
		}
	}

	// Each clip and modifier into the one of its kind and name already here, so what points at it still does; one the
	// file no longer has is retired
	std::vector<SubMod*> kept;
	const auto parseKind = [&](const char* a_key, SubModKind a_kind) {
		auto* list = ArrayOf(a_value, a_key);
		if (!list) {
			return;
		}
		for (auto& one : list->GetArray()) {
			if (!one.IsObject()) {
				continue;
			}
			const auto name = StringOf(one, "name");
			SubMod* existing = nullptr;
			ForEachSubMod([&](SubMod* a_subMod) {
				if (a_subMod->GetKind() == a_kind && a_subMod->GetName() == name && std::ranges::find(kept, a_subMod) == kept.end()) {
					existing = a_subMod;
					return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (existing) {
				existing->Parse(one);
				existing->_configSource = _configSource;
				kept.push_back(existing);
				continue;
			}
			std::unique_ptr<SubMod> subMod = a_kind == SubModKind::kClip ? std::unique_ptr<SubMod>(std::make_unique<Clip>(this)) : std::unique_ptr<SubMod>(std::make_unique<AppearanceModifier>(this));
			subMod->_path = _path;
			subMod->_configSource = _configSource;
			subMod->Parse(one);
			kept.push_back(subMod.get());
			AddSubMod(subMod);
		}
	};
	parseKind("modifiers", SubModKind::kModifier);
	parseKind("clips", SubModKind::kClip);
	{
		WriteLocker locker(_dataLock);
		for (auto& subMod : _subMods) {
			if (std::ranges::find(kept, subMod.get()) == kept.end()) {
				Retire(std::move(subMod));
			}
		}
		std::erase(_subMods, nullptr);
	}
	SortSubMods();

	// Read again once every mod is: this one's PRESET rows and those of the mods requiring it find their presets anew
	if (IsLoaded()) {
		ResolveConditionPresets();
		ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
			if (a_mod != this && std::ranges::find(a_mod->GetRequiredMods(), _name) != a_mod->GetRequiredMods().end()) {
				a_mod->ResolveConditionPresets();
			}
		});
	}
	Scheduler::Recompile();
}

std::optional<std::filesystem::path> RegisteredMod::ConfigFile(Parsing::ConfigSource& a_outSource) const
{
	const std::filesystem::path folder(_path);
	std::error_code ec;
	if (std::filesystem::is_regular_file(folder / kUserFile, ec)) {
		a_outSource = Parsing::ConfigSource::kUser;
		return folder / kUserFile;
	}
	if (std::filesystem::is_regular_file(folder / kAuthorFile, ec)) {
		a_outSource = Parsing::ConfigSource::kAuthor;
		return folder / kAuthorFile;
	}
	return std::nullopt;
}

bool RegisteredMod::ReloadConfig()
{
	Parsing::ConfigSource source = Parsing::ConfigSource::kAuthor;
	const auto path = ConfigFile(source);
	if (!path) {
		// Nothing on disk yet: what is here stays, and is no longer an edit
		_bDirty = false;
		MarkClean(this);
		return false;
	}
	rapidjson::Document doc;
	if (!ReadDocument(*path, doc)) {
		return false;
	}
	_configSource = source;
	Parse(doc);
	_bDirty = false;
	MarkClean(this);
	logger::info("storage: '{}' read from {}", _name, path->string());
	return true;
}

bool RegisteredMod::ReloadSubMod(SubMod* a_subMod)
{
	Parsing::ConfigSource source = Parsing::ConfigSource::kAuthor;
	const auto path = ConfigFile(source);
	rapidjson::Document doc;
	if (!path || !ReadDocument(*path, doc)) {
		return false;
	}
	auto* list = ArrayOf(doc, a_subMod->GetKind() == SubModKind::kClip ? "clips" : "modifiers");
	if (!list) {
		return false;
	}
	for (auto& one : list->GetArray()) {
		if (one.IsObject() && StringOf(one, "name") == a_subMod->GetName()) {
			a_subMod->Parse(one);
			a_subMod->_configSource = source;
			a_subMod->SetDirtyRecursive(false);
			if (auto* clip = dynamic_cast<Clip*>(a_subMod)) {
				clip->GetTriggerSet()->SetDirtyRecursive(false);
			}
			SortSubMods();
			Scheduler::Recompile();
			return true;
		}
	}
	logger::warn("storage: '{}' is not in {}", a_subMod->GetName(), path->string());
	return false;
}

void RegisteredMod::SaveConfig(EditMode a_editMode)
{
	const bool bUser = a_editMode == EditMode::kUser;
	const std::filesystem::path folder(_path);
	const auto path = folder / (bUser ? kUserFile : kAuthorFile);
	std::error_code ec;
	std::filesystem::create_directories(folder, ec);

	rapidjson::Document doc(rapidjson::kObjectType);
	rapidjson::Value value = Serialize(doc.GetAllocator());
	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter writer(buffer);
	writer.SetIndent('\t', 1);
	value.Accept(writer);

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file || !file.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()))) {
		logger::error("storage: cannot write {}", path.string());
		return;
	}
	_configSource = bUser ? Parsing::ConfigSource::kUser : Parsing::ConfigSource::kAuthor;
	ForEachSubMod([&](SubMod* a_subMod) {
		a_subMod->_configSource = _configSource;
		return RE::BSVisit::BSVisitControl::kContinue;
	});
	_bDirty = false;
	MarkClean(this);
	logger::info("storage: '{}' saved to {}", _name, path.string());
}

void RegisteredMod::LoadAll()
{
	// The channels a file names are read against the tables the scan fills: not before it has
	if (g_bLoaded || !Scan::Scanner::GetSingleton().IsFinished()) {
		return;
	}
	g_bLoaded = true;
	Apply::Entries::GetSingleton().Sync();

	std::error_code ec;
	std::size_t loaded = 0;
	for (std::filesystem::directory_iterator it(std::filesystem::path(kModsFolder), ec), end; !ec && it != end; it.increment(ec)) {
		if (!it->is_directory(ec)) {
			continue;
		}
		const auto folder = it->path().filename().string();
		const auto path = std::format("{}\\{}", kModsFolder, folder);
		auto mod = std::make_unique<RegisteredMod>(path, folder, ""sv, ""sv);
		Parsing::ConfigSource source = Parsing::ConfigSource::kAuthor;
		if (!mod->ConfigFile(source)) {
			continue;
		}
		// A mod of that name already here, one built in, takes the file in its place
		if (auto* existing = ModRegistry::GetSingleton().GetRegisteredModByName(folder)) {
			existing->_path = path;
			existing->ReloadConfig();
			++loaded;
			continue;
		}
		if (mod->ReloadConfig()) {
			ModRegistry::GetSingleton().AddRegisteredMod(mod);
			++loaded;
		}
	}
	ModRegistry::GetSingleton().SortMods();
	// A PRESET naming a required mod's preset finds it once that mod is read too
	ModRegistry::GetSingleton().ForEachRegisteredMod([](RegisteredMod* a_mod) { a_mod->ResolveConditionPresets(); });
	Overlays::WarnOfPartMismatches();
	DetectedProblems::GetSingleton().CheckForProblems();
	logger::info("storage: {} mod{} read from {}", loaded, loaded == 1 ? "" : "s", kModsFolder);
}

bool RegisteredMod::IsLoaded()
{
	return g_bLoaded;
}

void RegisteredMod::SetDirty(bool a_bDirty)
{
	_bDirty = a_bDirty;
	if (a_bDirty) {
		Scheduler::Recompile();
	}
}

void RegisteredMod::AddSubMod(std::unique_ptr<SubMod>& a_subMod)
{
	WriteLocker locker(_dataLock);

	const auto insertPos = std::ranges::upper_bound(_subMods, a_subMod, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetPriority() > a_rhs->GetPriority();
	});

	_subMods.insert(insertPos, std::move(a_subMod));
}

void RegisteredMod::RemoveSubMod(SubMod* a_subMod)
{
	{
		WriteLocker locker(_dataLock);
		for (auto& subMod : _subMods) {
			if (subMod.get() == a_subMod) {
				Retire(std::move(subMod));
			}
		}
		std::erase(_subMods, nullptr);
	}
	Scheduler::Recompile();
}

bool RegisteredMod::HasSubMod(std::string_view a_path) const
{
	return GetSubMod(a_path) != nullptr;
}

SubMod* RegisteredMod::GetSubMod(std::string_view a_path) const
{
	ReadLocker locker(_dataLock);

	const auto it = std::ranges::find_if(_subMods, [&](const auto& a_subMod) {
		return a_subMod->_path == a_path;
	});

	if (it != _subMods.end()) {
		return it->get();
	}

	return nullptr;
}

RE::BSVisit::BSVisitControl RegisteredMod::ForEachSubMod(const std::function<RE::BSVisit::BSVisitControl(SubMod*)>& a_func) const
{
	using Result = RE::BSVisit::BSVisitControl;

	ReadLocker locker(_dataLock);

	for (auto& subMod : _subMods) {
		const auto result = a_func(subMod.get());
		if (result == Result::kStop) {
			return result;
		}
	}

	return Result::kContinue;
}

void RegisteredMod::SortSubMods()
{
	WriteLocker locker(_dataLock);

	std::ranges::sort(_subMods, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetPriority() > a_rhs->GetPriority();
	});
}

void RegisteredMod::AddConditionPreset(std::unique_ptr<Conditions::ConditionPreset>& a_conditionPreset)
{
	WriteLocker locker(_presetsLock);

	const auto it = std::ranges::lower_bound(_conditionPresets, a_conditionPreset, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});

	_conditionPresets.insert(it, std::move(a_conditionPreset));
}

void RegisteredMod::RemoveConditionPreset(std::string_view a_name)
{
	WriteLocker locker(_presetsLock);

	const auto search = std::ranges::find_if(_conditionPresets, [&](const auto& a_conditionPreset) {
		return a_conditionPreset->GetName() == a_name;
	});

	if (search == _conditionPresets.end()) {
		return;
	}

	_conditionPresets.erase(search);
}

bool RegisteredMod::HasConditionPresets() const
{
	ReadLocker locker(_presetsLock);

	return _conditionPresets.size() > 0;
}

bool RegisteredMod::HasConditionPreset(std::string_view a_name) const
{
	return GetConditionPreset(a_name) != nullptr;
}

Conditions::ConditionPreset* RegisteredMod::GetConditionPreset(std::string_view a_name) const
{
	ReadLocker locker(_presetsLock);

	const auto it = std::ranges::find_if(_conditionPresets, [&](const auto& a_conditionPreset) {
		return a_conditionPreset->GetName() == a_name;
	});

	if (it != _conditionPresets.end()) {
		return it->get();
	}

	return nullptr;
}

void RegisteredMod::AddOverlaySlot(std::unique_ptr<Overlays::Slot>& a_slot)
{
	WriteLocker locker(_presetsLock);

	const auto it = std::ranges::lower_bound(_overlaySlots, a_slot, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});

	Apply::Entries::GetSingleton().AddOverlaySlot(a_slot->GetName());
	_overlaySlots.insert(it, std::move(a_slot));
}

void RegisteredMod::RemoveOverlaySlot(std::string_view a_name)
{
	WriteLocker locker(_presetsLock);

	if (std::erase_if(_overlaySlots, [&](const auto& a_slot) { return a_slot->GetName() == a_name; }) > 0 && !SlotDefinedElsewhere(this, a_name)) {
		Apply::Entries::GetSingleton().RetireOverlaySlot(a_name);
	}
}

void RegisteredMod::RenameOverlaySlot(Overlays::Slot* a_slot, std::string_view a_name)
{
	WriteLocker locker(_presetsLock);

	if (!SlotDefinedElsewhere(this, a_slot->GetName())) {
		Apply::Entries::GetSingleton().RetireOverlaySlot(a_slot->GetName());
	}
	a_slot->SetName(a_name);
	Apply::Entries::GetSingleton().AddOverlaySlot(a_name);
}

bool RegisteredMod::HasOverlaySlots() const
{
	ReadLocker locker(_presetsLock);

	return !_overlaySlots.empty();
}

bool RegisteredMod::HasOverlaySlot(std::string_view a_name) const
{
	ReadLocker locker(_presetsLock);

	return std::ranges::any_of(_overlaySlots, [&](const auto& a_slot) { return a_slot->GetName() == a_name; });
}

RE::BSVisit::BSVisitControl RegisteredMod::ForEachOverlaySlot(const std::function<RE::BSVisit::BSVisitControl(Overlays::Slot*)>& a_func) const
{
	using Result = RE::BSVisit::BSVisitControl;

	ReadLocker locker(_presetsLock);

	for (auto& slot : _overlaySlots) {
		if (a_func(slot.get()) == Result::kStop) {
			return Result::kStop;
		}
	}

	return Result::kContinue;
}

void RegisteredMod::SortOverlaySlots()
{
	WriteLocker locker(_presetsLock);

	std::ranges::sort(_overlaySlots, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});
}

Overlays::Slot* RegisteredMod::GetOverlaySlot(std::string_view a_name) const
{
	ReadLocker locker(_presetsLock);

	const auto it = std::ranges::find_if(_overlaySlots, [&](const auto& a_slot) { return a_slot->GetName() == a_name; });
	return it != _overlaySlots.end() ? it->get() : nullptr;
}

void RegisteredMod::ForEachRequiredMod(const std::function<void(RegisteredMod*)>& a_func) const
{
	for (const auto& name : _requiredMods) {
		if (auto* mod = ModRegistry::GetSingleton().GetRegisteredModByName(name); mod && mod != this) {
			a_func(mod);
		}
	}
}

Conditions::ConditionPreset* RegisteredMod::FindConditionPreset(std::string_view a_name) const
{
	auto* found = GetConditionPreset(a_name);
	ForEachRequiredMod([&](RegisteredMod* a_mod) {
		if (!found) {
			found = a_mod->GetConditionPreset(a_name);
		}
	});
	return found;
}

Triggers::TriggerPreset* RegisteredMod::FindTriggerPreset(std::string_view a_name) const
{
	auto* found = GetTriggerPreset(a_name);
	ForEachRequiredMod([&](RegisteredMod* a_mod) {
		if (!found) {
			found = a_mod->GetTriggerPreset(a_name);
		}
	});
	return found;
}

ReferencePool* RegisteredMod::FindReferencePool(std::string_view a_name) const
{
	auto* found = GetReferencePool(a_name);
	ForEachRequiredMod([&](RegisteredMod* a_mod) {
		if (!found) {
			found = a_mod->GetReferencePool(a_name);
		}
	});
	return found;
}

void RegisteredMod::ResolveConditionPresets()
{
	const std::function<void(Conditions::ConditionSet*)> resolve = [&](Conditions::ConditionSet* a_set) {
		a_set->ForEach([&](std::unique_ptr<Conditions::ICondition>& a_condition) {
			for (std::uint32_t i = 0; i < a_condition->GetNumComponents(); ++i) {
				auto* component = a_condition->GetComponent(i);
				if (component->GetType() == Conditions::ConditionComponentType::kPreset) {
					static_cast<Conditions::ConditionPresetComponent*>(component)->TryFindPreset();
				} else if (component->GetType() == Conditions::ConditionComponentType::kMulti) {
					resolve(static_cast<Conditions::MultiConditionComponent*>(component)->GetConditions());
				}
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	};
	ForEachSubMod([&](SubMod* a_subMod) {
		resolve(a_subMod->GetConditionSet());
		return RE::BSVisit::BSVisitControl::kContinue;
	});
}

void RegisteredMod::AddSkinSet(std::unique_ptr<Skins::Set>& a_set)
{
	WriteLocker locker(_presetsLock);

	const auto it = std::ranges::lower_bound(_skinSets, a_set, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});

	_skinSets.insert(it, std::move(a_set));
}

void RegisteredMod::RemoveSkinSet(std::string_view a_name)
{
	WriteLocker locker(_presetsLock);

	std::erase_if(_skinSets, [&](const auto& a_set) { return a_set->GetName() == a_name; });
}

bool RegisteredMod::HasSkinSets() const
{
	ReadLocker locker(_presetsLock);

	return !_skinSets.empty();
}

bool RegisteredMod::HasSkinSet(std::string_view a_name) const
{
	ReadLocker locker(_presetsLock);

	return std::ranges::any_of(_skinSets, [&](const auto& a_set) { return a_set->GetName() == a_name; });
}

RE::BSVisit::BSVisitControl RegisteredMod::ForEachSkinSet(const std::function<RE::BSVisit::BSVisitControl(Skins::Set*)>& a_func) const
{
	using Result = RE::BSVisit::BSVisitControl;

	ReadLocker locker(_presetsLock);

	for (auto& set : _skinSets) {
		if (a_func(set.get()) == Result::kStop) {
			return Result::kStop;
		}
	}

	return Result::kContinue;
}

void RegisteredMod::SortSkinSets()
{
	WriteLocker locker(_presetsLock);

	std::ranges::sort(_skinSets, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});
}

RE::BSVisit::BSVisitControl RegisteredMod::ForEachConditionPreset(const std::function<RE::BSVisit::BSVisitControl(Conditions::ConditionPreset*)>& a_func) const
{
	using Result = RE::BSVisit::BSVisitControl;

	ReadLocker locker(_presetsLock);

	for (auto& preset : _conditionPresets) {
		const auto result = a_func(preset.get());
		if (result == Result::kStop) {
			return result;
		}
	}

	return Result::kContinue;
}

void RegisteredMod::AddReferencePool(std::unique_ptr<ReferencePool>& a_pool)
{
	WriteLocker locker(_presetsLock);

	const auto it = std::ranges::lower_bound(_referencePools, a_pool, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});
	_referencePools.insert(it, std::move(a_pool));
}

void RegisteredMod::RemoveReferencePool(std::string_view a_name)
{
	WriteLocker locker(_presetsLock);

	for (auto& pool : _referencePools) {
		if (pool->GetName() == a_name) {
			Retire(std::move(pool));
		}
	}
	std::erase(_referencePools, nullptr);
}

bool RegisteredMod::HasReferencePools() const
{
	ReadLocker locker(_presetsLock);

	return !_referencePools.empty();
}

ReferencePool* RegisteredMod::GetReferencePool(std::string_view a_name) const
{
	ReadLocker locker(_presetsLock);

	const auto it = std::ranges::find_if(_referencePools, [&](const auto& a_pool) { return a_pool->GetName() == a_name; });
	return it != _referencePools.end() ? it->get() : nullptr;
}

RE::BSVisit::BSVisitControl RegisteredMod::ForEachReferencePool(const std::function<RE::BSVisit::BSVisitControl(ReferencePool*)>& a_func) const
{
	using Result = RE::BSVisit::BSVisitControl;

	ReadLocker locker(_presetsLock);

	for (auto& pool : _referencePools) {
		if (a_func(pool.get()) == Result::kStop) {
			return Result::kStop;
		}
	}
	return Result::kContinue;
}

void RegisteredMod::AddTriggerPreset(std::unique_ptr<Triggers::TriggerPreset>& a_preset)
{
	WriteLocker locker(_presetsLock);

	const auto it = std::ranges::lower_bound(_triggerPresets, a_preset, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});

	_triggerPresets.insert(it, std::move(a_preset));
}

void RegisteredMod::RemoveTriggerPreset(std::string_view a_name)
{
	WriteLocker locker(_presetsLock);

	std::erase_if(_triggerPresets, [&](const auto& a_preset) { return a_preset->GetName() == a_name; });
}

bool RegisteredMod::HasTriggerPresets() const
{
	ReadLocker locker(_presetsLock);

	return !_triggerPresets.empty();
}

bool RegisteredMod::HasTriggerPreset(std::string_view a_name) const
{
	return GetTriggerPreset(a_name) != nullptr;
}

Triggers::TriggerPreset* RegisteredMod::GetTriggerPreset(std::string_view a_name) const
{
	ReadLocker locker(_presetsLock);

	const auto it = std::ranges::find_if(_triggerPresets, [&](const auto& a_preset) { return a_preset->GetName() == a_name; });
	return it != _triggerPresets.end() ? it->get() : nullptr;
}

RE::BSVisit::BSVisitControl RegisteredMod::ForEachTriggerPreset(const std::function<RE::BSVisit::BSVisitControl(Triggers::TriggerPreset*)>& a_func) const
{
	using Result = RE::BSVisit::BSVisitControl;

	ReadLocker locker(_presetsLock);

	for (auto& preset : _triggerPresets) {
		if (a_func(preset.get()) == Result::kStop) {
			return Result::kStop;
		}
	}

	return Result::kContinue;
}

void RegisteredMod::SortTriggerPresets()
{
	WriteLocker locker(_presetsLock);

	std::ranges::sort(_triggerPresets, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});
}

void RegisteredMod::SortConditionPresets()
{
	WriteLocker locker(_presetsLock);

	std::ranges::sort(_conditionPresets, [](const auto& a_lhs, const auto& a_rhs) {
		return a_lhs->GetName() < a_rhs->GetName();
	});
}

bool RegisteredMod::HasInvalidConditions(bool a_bCheckPresetsOnly) const
{
	using Result = RE::BSVisit::BSVisitControl;

	auto result = ForEachConditionPreset([&](const Conditions::ConditionPreset* a_preset) {
		if (a_preset->IsEmpty() || !a_preset->IsValid()) {
			return Result::kStop;
		}
		return Result::kContinue;
	});

	if (!a_bCheckPresetsOnly) {
		if (result == Result::kStop) {
			return true;
		}

		result = ForEachSubMod([&](const SubMod* a_subMod) {
			if (a_subMod->HasInvalidConditions()) {
				return Result::kStop;
			}
			return Result::kContinue;
		});
	}

	return result == Result::kStop;
}

bool RegisteredMod::HasInvalidFunctions() const
{
	using Result = RE::BSVisit::BSVisitControl;

	const auto result = ForEachSubMod([&](const SubMod* a_subMod) {
		if (a_subMod->HasInvalidFunctions()) {
			return Result::kStop;
		}
		return Result::kContinue;
	});

	return result == Result::kStop;
}

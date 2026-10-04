#include "Functions.h"

#include "ClipPlayer.h"
#include "Offsets.h"
#include "ModRegistry.h"
#include "RegisteredMods.h"
#include "Triggers.h"
#include "UI/UICommon.h"

#include <algorithm>
#include <format>
#include <random>

#include <imgui.h>
#include <rapidjson/document.h>

namespace Functions
{
	namespace
	{
		float RandomFloat(float a_min, float a_max)
		{
			static thread_local std::mt19937 generator{ std::random_device{}() };
			std::uniform_real_distribution<float> distribution(a_min, a_max);
			return distribution(generator);
		}
	}

	void RegisterFunctions()
	{
		auto& registry = ModRegistry::GetSingleton();

		registry.AddFunctionFactory("CONDITION"sv, []() -> IFunction* { return new CONDITIONFunction(); });
		registry.AddFunctionFactory("RANDOM"sv, []() -> IFunction* { return new RANDOMFunction(); });
		registry.AddFunctionFactory("ONE"sv, []() -> IFunction* { return new ONEFunction(); });
		registry.AddFunctionFactory("PlaySound"sv, []() -> IFunction* { return new PlaySoundFunction(); });
		registry.AddFunctionFactory("PlayDialogue"sv, []() -> IFunction* { return new PlayDialogueFunction(); });
	}

	std::unique_ptr<IFunction> CreateFunction(std::string_view a_functionName)
	{
		return ModRegistry::GetSingleton().CreateFunction(a_functionName);
	}

	// As CreateConditionFromJson, for the function sets.
	std::unique_ptr<IFunction> CreateFunctionFromJson(rapidjson::Value& a_value, FunctionSet* a_parentFunctionSet)
	{
		if (!a_value.IsObject()) {
			logger::error("storage: a function that is not an object is not read");
			return nullptr;
		}

		const auto object = a_value.GetObj();

		const auto functionNameIt = object.FindMember("function");
		if (functionNameIt == object.MemberEnd() || !functionNameIt->value.IsString()) {
			logger::error("storage: a function with no name is not read");
			return nullptr;
		}

		auto function = CreateFunction(functionNameIt->value.GetString());
		if (!function) {
			logger::error("storage: function '{}' is not one this plugin or another registered; it is not read", functionNameIt->value.GetString());
			return nullptr;
		}

		if (a_parentFunctionSet) {
			function->SetParentSet(a_parentFunctionSet);
		}

		if (const auto disabledIt = object.FindMember("disabled"); disabledIt != object.MemberEnd() && disabledIt->value.IsBool()) {
			function->SetDisabled(disabledIt->value.GetBool());
		}

		function->Initialize(&a_value);
		function->PostInitialize();

		return function;
	}

	std::unique_ptr<IFunction> DuplicateFunction(const std::unique_ptr<IFunction>& a_function)
	{
		if (!a_function) {
			return nullptr;
		}

		auto duplicate = CreateFunction(a_function->GetName().data());
		if (!duplicate) {
			return nullptr;
		}

		rapidjson::Document doc(rapidjson::kObjectType);
		rapidjson::Value object(rapidjson::kObjectType);
		a_function->Serialize(&object, &doc.GetAllocator());
		duplicate->Initialize(&object);

		duplicate->SetDisabled(a_function->IsDisabled());

		return duplicate;
	}

	std::unique_ptr<FunctionSet> DuplicateFunctionSet(FunctionSet* a_functionSetToDuplicate)
	{
		auto duplicate = std::make_unique<FunctionSet>(FunctionSetType::kNone);

		if (a_functionSetToDuplicate) {
			a_functionSetToDuplicate->ForEach([&](std::unique_ptr<IFunction>& a_function) {
				if (auto copy = DuplicateFunction(a_function)) {
					duplicate->Add(copy);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		return duplicate;
	}

	// ---- structure -------------------------------------------------------

	bool CONDITIONFunction::RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger) const
	{
		return conditionComponent->TryRun(a_refr, a_parentSubMod, a_trigger);
	}

	void RANDOMFunctionComponent::InitializeComponent(void* a_value)
	{
		MultiFunctionComponent::InitializeComponent(a_value);

		UpdateWeightCache();

		WriteLocker locker(_lock);

		auto& value = *static_cast<rapidjson::Value*>(a_value);
		const auto object = value.GetObj();
		if (const auto weightsIt = object.FindMember("weights"); weightsIt != object.MemberEnd() && weightsIt->value.IsArray()) {
			size_t i = 0;
			for (const auto weightsArray = weightsIt->value.GetArray(); auto& weightValue : weightsArray) {
				if (i < weights.size()) {
					weights[i] = weightValue.GetFloat();
				}
				++i;
			}
		}
	}

	void RANDOMFunctionComponent::SerializeComponent(void* a_value, void* a_allocator)
	{
		MultiFunctionComponent::SerializeComponent(a_value, a_allocator);

		auto& value = *static_cast<rapidjson::Value*>(a_value);
		auto& allocator = *static_cast<rapidjson::Document::AllocatorType*>(a_allocator);

		rapidjson::Value weightArrayValue(rapidjson::kArrayType);

		ReadLocker locker(_lock);
		for (auto& weight : weights) {
			weightArrayValue.PushBack(weight, allocator);
		}

		value.AddMember("weights", weightArrayValue, allocator);
	}

	bool RANDOMFunctionComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bSetDirty = false;

		{
			ReadLocker locker(_lock);

			size_t i = 0;
			functionSet->ForEach([&](const std::unique_ptr<IFunction>& a_function) {
				if (i >= weights.size()) {
					return RE::BSVisit::BSVisitControl::kStop;
				}

				ImGui::TextUnformatted(a_function->GetName().c_str());
				UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
				if (a_bEditable) {
					std::string label = std::format("Weight##{}", reinterpret_cast<uintptr_t>(a_function.get()));
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
					if (ImGui::InputFloat(label.c_str(), &weights[i], .01f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
						bSetDirty = true;
					}
				} else {
					std::string text = std::format("Weight: {}", weights[i]);
					ImGui::TextUnformatted(text.data());
				}

				ImGui::SameLine();
				UI::UICommon::HelpMarker("The weight of this function used for the weighted random selection (e.g. a function with a weight of 2 will be twice as likely to be picked than a function with a weight of 1)");

				++i;
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		return bSetDirty;
	}

	bool RANDOMFunctionComponent::Run(RE::TESObjectREFR* a_refr, void* a_parentSubMod, void* a_trigger) const
	{
		ReadLocker locker(_lock);

		if (_cumulativeWeights.empty()) {
			return false;
		}

		bool bRanFunction = false;

		const float random = RandomFloat(0.f, _cumulativeWeights.back());
		const size_t index = static_cast<size_t>(std::distance(_cumulativeWeights.begin(),
			std::lower_bound(_cumulativeWeights.begin(), _cumulativeWeights.end(), random)));

		size_t i = 0;
		functionSet->ForEach([&](const std::unique_ptr<IFunction>& a_function) {
			if (i == index) {
				if (a_function->Run(a_refr, a_parentSubMod, static_cast<Trigger*>(a_trigger))) {
					bRanFunction = true;
				}
				return RE::BSVisit::BSVisitControl::kStop;
			}
			++i;
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		return bRanFunction;
	}

	void RANDOMFunctionComponent::UpdateWeightCache()
	{
		WriteLocker locker(_lock);

		UpdateWeightCacheImpl();
	}

	void RANDOMFunctionComponent::UpdateWeightCacheImpl()
	{
		std::vector<IFunction*> newPointers;
		std::vector<float> newWeights;

		functionSet->ForEach([&](const std::unique_ptr<IFunction>& a_function) {
			IFunction* function = a_function.get();
			const auto it = std::find(_functions.begin(), _functions.end(), function);
			if (it != _functions.end()) {
				const size_t idx = static_cast<size_t>(std::distance(_functions.begin(), it));
				newPointers.push_back(function);
				newWeights.push_back(idx < weights.size() ? weights[idx] : 1.f);
			} else {
				newPointers.push_back(function);
				newWeights.push_back(1.f);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		_functions = std::move(newPointers);
		weights = std::move(newWeights);

		_cumulativeWeights.clear();
		float sum = 0.f;
		for (const float weight : weights) {
			sum += weight;
			_cumulativeWeights.push_back(sum);
		}
	}

	bool RANDOMFunction::RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger) const
	{
		return functionsComponent->Run(a_refr, a_parentSubMod, a_trigger);
	}

	bool ONEFunction::RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger) const
	{
		return functionsComponent->Run(a_refr, a_parentSubMod, a_trigger);
	}

	bool PlaySoundFunction::RunImpl(RE::TESObjectREFR* a_refr, void*, Trigger*) const
	{
		// The descriptor's sound, following the ref's 3D
		if (const auto form = formComponent->GetTESFormValue()) {
			if (const auto descriptor = form->As<RE::BGSSoundDescriptorForm>()) {
				RE::BSSoundHandle handle;
				RE::BSAudioManager::GetSingleton()->GetSoundHandle(handle, descriptor);
				if (a_refr) {
					handle.SetObjectToFollow(a_refr->Get3D());
				}
				handle.Play();
				return true;
			}
		}
		return false;
	}

	PlayDialogueFunction::PlayDialogueFunction()
	{
		topicComponent = AddComponent<FormFunctionComponent>("Topic", "The dialogue topic to say. Pick lists every topic with a line.", RE::FormType::Dialogue);
		topicComponent->form.pickOverride = [](RE::FormID a_current, std::uint64_t& a_picked) {
			return UI::UICommon::Picker("Topic", [] {
				// Plugin > topic, the plugins in load order
				std::vector<UI::UICommon::PickerItem> items;
				for (auto& group : Triggers::TopicGroups()) {
					items.push_back({ std::move(group.label), std::move(group.section), group.key });
				}
				return items;
			}, a_current, a_picked);
		};
		inHeadComponent = AddComponent<BoolFunctionComponent>("In head", "Spoken in the player's head, as a thought, rather than from the ref.");
	}

	bool PlayDialogueFunction::RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger*) const
	{
		auto* topic = topicComponent->GetTESFormValue() ? topicComponent->GetTESFormValue()->As<RE::TESTopic>() : nullptr;
		if (!a_refr || !topic) {
			return false;
		}
		if (auto* actor = a_refr->As<RE::Actor>(); actor && Actor_IsTalking(actor)) {
			return false;
		}
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return false;
		}
		// ObjectReference.Say on the ref's script object, bound as a Papyrus argument binds it
		RE::BSScript::Variable self;
		self.Pack(a_refr);
#pragma push_macro("GetObject")
#undef GetObject
		auto object = self.GetObject();
#pragma pop_macro("GetObject")
		if (!object) {
			return false;
		}
		// Its line does not fire the clip that said it
		if (auto* clip = dynamic_cast<const Clip*>(static_cast<SubMod*>(a_parentSubMod))) {
			ClipPlayer::NoteSaid(a_refr, topic->GetFormID(), clip);
		}
		auto* args = RE::MakeFunctionArguments(std::move(topic), static_cast<RE::Actor*>(nullptr), static_cast<bool>(inHeadComponent->bValue));
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		return vm->DispatchMethodCall1(object, "Say"sv, args, callback);
	}
}

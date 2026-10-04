#include "BaseTriggers.h"


namespace Triggers
{
	ITriggerComponent* TriggerBase::GetComponent(uint32_t a_index) const
	{
		if (a_index >= _components.size()) {
			return nullptr;
		}

		return _components[a_index].get();
	}

	std::string TriggerBase::GetArgument() const
	{
		std::string argument;

		for (const auto& component : _components) {
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

	bool TriggerBase::IsValid() const
	{
		for (const auto& component : _components) {
			if (!component->IsValid()) {
				return false;
			}
		}

		return true;
	}

	void TriggerBase::Parse(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return;
		}

		const auto object = a_value.GetObj();
		if (const auto disabled = object.FindMember("disabled"); disabled != object.MemberEnd() && disabled->value.IsBool()) {
			_bDisabled = disabled->value.GetBool();
		}
		for (const auto& component : _components) {
			const auto name = component->GetName();
			if (const auto it = object.FindMember(rapidjson::StringRef(name.data(), name.length())); it != object.MemberEnd()) {
				component->Parse(it->value);
			}
		}
	}

	rapidjson::Value TriggerBase::Serialize(rapidjson::Document::AllocatorType& a_allocator)
	{
		rapidjson::Value object(rapidjson::kObjectType);

		const auto typeName = GetTypeName();
		object.AddMember("type", rapidjson::StringRef(typeName.data(), typeName.length()), a_allocator);

		for (const auto& component : _components) {
			const auto name = component->GetName();
			object.AddMember(rapidjson::StringRef(name.data(), name.length()), component->Serialize(a_allocator), a_allocator);
		}
		if (_bDisabled) {
			object.AddMember("disabled", true, a_allocator);
		}

		return object;
	}

	void TriggerBase::Serialize(void* a_value, void* a_allocator)
	{
		auto& allocator = *static_cast<rapidjson::Document::AllocatorType*>(a_allocator);
		*static_cast<rapidjson::Value*>(a_value) = Serialize(allocator);
	}
}

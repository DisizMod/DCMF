#pragma once

#include "SharedTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Triggers
{
	enum class TriggerComponentType : uint8_t
	{
		kNumeric,
		kText,
		kForm,
		kAnimation
	};

	class ITriggerComponent
	{
	public:
		ITriggerComponent(std::string_view a_name, std::string_view a_description) :
			_name(a_name), _description(a_description) {}

		virtual ~ITriggerComponent() = default;

		[[nodiscard]] virtual TriggerComponentType GetType() const = 0;

		virtual void Parse(const rapidjson::Value& a_value) = 0;
		[[nodiscard]] virtual rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) = 0;

		virtual bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) = 0;

		[[nodiscard]] virtual std::string GetArgument() const = 0;
		[[nodiscard]] virtual bool IsValid() const { return true; }

		[[nodiscard]] std::string_view GetName() const { return _name; }
		[[nodiscard]] std::string_view GetDescription() const { return _description; }

	protected:
		std::string _name;
		std::string _description;
	};

	class NumericTriggerComponent : public ITriggerComponent
	{
	public:
		using ITriggerComponent::ITriggerComponent;

		[[nodiscard]] TriggerComponentType GetType() const override { return TriggerComponentType::kNumeric; }

		void Parse(const rapidjson::Value& a_value) override { value.Parse(const_cast<rapidjson::Value&>(a_value)); }
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) override { return value.Serialize(a_allocator); }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override { return value.DisplayInUI(a_bEditable, a_firstColumnWidthPercent); }

		[[nodiscard]] std::string GetArgument() const override { return value.GetArgument(); }

		Components::NumericValue value;
	};

	class TextTriggerComponent : public ITriggerComponent
	{
	public:
		using ITriggerComponent::ITriggerComponent;

		[[nodiscard]] TriggerComponentType GetType() const override { return TriggerComponentType::kText; }

		void Parse(const rapidjson::Value& a_value) override { value.Parse(a_value); }
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) override { return value.Serialize(a_allocator); }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override { return value.DisplayInUI(a_bEditable, a_firstColumnWidthPercent); }

		[[nodiscard]] std::string GetArgument() const override { return std::string(value.GetValue()); }

		Components::TextValue value;
	};

	class AnimationTriggerComponent : public ITriggerComponent
	{
	public:
		using ITriggerComponent::ITriggerComponent;

		[[nodiscard]] TriggerComponentType GetType() const override { return TriggerComponentType::kAnimation; }

		void Parse(const rapidjson::Value& a_value) override { value.Parse(a_value); }
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) override { return value.Serialize(a_allocator); }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override { return value.DisplayInUI(a_bEditable, a_firstColumnWidthPercent); }

		[[nodiscard]] std::string GetArgument() const override { return value.GetArgument(); }
		[[nodiscard]] bool IsValid() const override { return value.IsValid(); }

		Components::AnimationValue value;
	};

	class FormTriggerComponent : public ITriggerComponent
	{
	public:
		using ITriggerComponent::ITriggerComponent;

		[[nodiscard]] TriggerComponentType GetType() const override { return TriggerComponentType::kForm; }

		void Parse(const rapidjson::Value& a_value) override { value.Parse(const_cast<rapidjson::Value&>(a_value)); }
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) override { return value.Serialize(a_allocator); }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override { return value.DisplayInUI(a_bEditable, a_firstColumnWidthPercent); }

		[[nodiscard]] std::string GetArgument() const override { return value.GetArgument(); }
		[[nodiscard]] bool IsValid() const override { return value.IsValid(); }

		Components::TESFormValue<RE::TESForm> value;
	};

	class TriggerBase
	{
	public:
		virtual ~TriggerBase() = default;

		// What the factory registered it under, and what a file names.
		[[nodiscard]] virtual std::string_view GetTypeName() const = 0;
		[[nodiscard]] virtual std::string_view GetDescription() const = 0;

		[[nodiscard]] virtual std::string GetArgument() const;

		[[nodiscard]] uint32_t GetNumComponents() const { return static_cast<uint32_t>(_components.size()); }
		[[nodiscard]] ITriggerComponent* GetComponent(uint32_t a_index) const;

		[[nodiscard]] bool IsValid() const;

		[[nodiscard]] bool IsDisabled() const { return _bDisabled; }
		void SetDisabled(bool a_bDisabled) { _bDisabled = a_bDisabled; }

		void Parse(const rapidjson::Value& a_value);
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator);
		// The whole trigger as one object, the shape a set's Serialize expects
		void Serialize(void* a_value, void* a_allocator);

		template <typename T>
		T* AddComponent(std::string_view a_name, std::string_view a_description = ""sv)
		{
			auto& component = _components.emplace_back(std::make_unique<T>(a_name, a_description));
			return static_cast<T*>(component.get());
		}

	protected:
		TriggerBase() = default;

		std::vector<std::unique_ptr<ITriggerComponent>> _components;
		bool _bDisabled = false;
	};
}

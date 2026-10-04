#pragma once

#include "SharedTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Channels
{
	class ChannelSet;

	enum class ChannelComponentType : uint8_t
	{
		kNumber,
		kColour,
		kMode,
		kReference,
		kSwitch,
		kBlend,
		kBound,
		kForm,
		kHeadPart,
		kState
	};

	class IChannelComponent
	{
	public:
		IChannelComponent(std::string_view a_name, std::string_view a_description) :
			_name(a_name), _description(a_description) {}

		virtual ~IChannelComponent() = default;

		[[nodiscard]] virtual ChannelComponentType GetType() const = 0;

		virtual void Parse(const rapidjson::Value& a_value) = 0;
		[[nodiscard]] virtual rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const = 0;

		virtual bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) = 0;

		[[nodiscard]] virtual std::string GetArgument() const = 0;

		// A component that only means something beside another's state stays out of the node and the file otherwise
		[[nodiscard]] virtual bool IsShown() const { return !shownWhen || shownWhen(); }

		std::function<bool()> shownWhen;

		[[nodiscard]] std::string_view GetName() const { return _name; }
		[[nodiscard]] std::string_view GetDescription() const { return _description; }

	protected:
		std::string _name;
		std::string _description;
	};

	// A weight, an angle, a strength: a static value on the entry's range, a global variable or an actor value,
	// as a condition's number is
	class NumberChannelComponent : public IChannelComponent
	{
	public:
		NumberChannelComponent(std::string_view a_name, std::string_view a_description, float a_low = 0.f, float a_high = 1.f, float a_start = 0.f) :
			IChannelComponent(a_name, a_description)
		{
			value.SetStaticRange(a_low, a_high);
			value.SetForcedType(Components::NumericValue::Type::kNone);
			value.SetStaticValue(a_start);  // a static value, on the range, until the file or the author says otherwise
		}

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kNumber; }

		void Parse(const rapidjson::Value& a_value) override { value.Parse(const_cast<rapidjson::Value&>(a_value)); }
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override { return const_cast<Components::NumericValue&>(value).Serialize(a_allocator); }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override { return value.DisplayInUI(a_bEditable, a_firstColumnWidthPercent); }

		[[nodiscard]] std::string GetArgument() const override { return value.GetArgument(); }

		[[nodiscard]] bool IsStatic() const { return value.GetType() == Components::NumericValue::Type::kStaticValue; }

		Components::NumericValue value;
	};

	// One end of a mapping, a static number on its own row as a random condition's bounds are; shown only while
	// the value it belongs to is read from a global or an actor value, since a static value has nothing to map
	class BoundChannelComponent : public IChannelComponent
	{
	public:
		BoundChannelComponent(std::string_view a_name, std::string_view a_description, float a_bound, const NumberChannelComponent* a_value) :
			IChannelComponent(a_name, a_description), _value(a_value)
		{
			value.SetForcedType(Components::NumericValue::Type::kStaticValue);
			value.SetStaticValue(a_bound);
		}

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kBound; }

		void Parse(const rapidjson::Value& a_value) override { value.Parse(const_cast<rapidjson::Value&>(a_value)); }
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override { return const_cast<Components::NumericValue&>(value).Serialize(a_allocator); }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override { return value.DisplayInUI(a_bEditable, a_firstColumnWidthPercent); }

		[[nodiscard]] std::string GetArgument() const override { return value.GetArgument(); }
		[[nodiscard]] bool IsShown() const override { return !_value->IsStatic(); }

		Components::NumericValue value;

	private:
		const NumberChannelComponent* _value;
	};

	class ColourChannelComponent : public IChannelComponent
	{
	public:
		using IChannelComponent::IChannelComponent;

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kColour; }

		void Parse(const rapidjson::Value& a_value) override { value.Parse(const_cast<rapidjson::Value&>(a_value)); }
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override { return value.Serialize(a_allocator); }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override { return value.DisplayInUI(a_bEditable, a_firstColumnWidthPercent); }

		[[nodiscard]] std::string GetArgument() const override { return value.GetArgument(); }

		Components::ColourValue value;
	};

	// One of a short list of names, stored by name.
	class ModeChannelComponent : public IChannelComponent
	{
	public:
		ModeChannelComponent(std::string_view a_name, std::string_view a_description, std::vector<std::string> a_modes) :
			IChannelComponent(a_name, a_description), modes(std::move(a_modes))
		{
			if (!modes.empty()) {
				value = modes.front();
			}
		}

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kMode; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override { return value; }

		std::string value;
		std::vector<std::string> modes;
	};

	// A name, a path or a form, typed or picked. What the picker offers is the
	// channel's: an overlay slot's pool, the head parts, the skin sets.
	class RandomValue;

	class ReferenceChannelComponent : public IChannelComponent
	{
	public:
		using ItemsFn = std::function<std::vector<UI::UICommon::PickerItem>()>;

		ReferenceChannelComponent(std::string_view a_name, std::string_view a_description, ItemsFn a_items = nullptr) :
			IChannelComponent(a_name, a_description), items(std::move(a_items)) {}

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kReference; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override;

		std::string value;
		ItemsFn items;
		std::function<std::string(std::string_view)> warningOf;  // why a value would not be drawn, empty when it would
		std::unique_ptr<RandomValue> random;  // the channel's draw, when it offers one
		bool bRandom = false;
	};

	// A fresh value component of the kind a reference channel uses, for one entry of a pick or a pool
	using ValueFactory = std::function<std::unique_ptr<IChannelComponent>()>;

	class PickList;

	// The mod's pools of the channel: their names, and one's entries by its name
	struct PoolSource
	{
		std::function<std::vector<std::string>()> names;
		std::function<PickList*(std::string_view)> entriesOf;
	};

	// One entry of a random pick or a pool: the channel's own value with its weight, or one of the mod's pools, whose
	// entries carry their own
	struct PickEntry
	{
		std::unique_ptr<IChannelComponent> value;  // a name or a head part, as the channel's static value is
		std::string pool;                          // a pool by name in its place; only in a channel's pick
		float weight = 1.f;                        // a value's; a pool has none of its own
		bool bDisabled = false;
	};

	// Entries drawn as an overlay slot's items are: a node each, toggled, named, its weight and chance on the right;
	// open, the channel's own value field or the pool, and the weight with the delete button
	class PickList
	{
	public:
		// The mod's pools can be added beside the values; none for a pool's own list
		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent, const ValueFactory& a_factory, const PoolSource* a_pools);

		void Parse(const rapidjson::Value& a_value, const ValueFactory& a_factory);
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;

		// Every enabled value's weight, and each enabled pool's entries' weights
		[[nodiscard]] float TotalWeight(const PoolSource* a_pools = nullptr) const;

		std::vector<std::unique_ptr<PickEntry>> entries;
	};

	// Static, the row's own value, or Random: a weighted draw among values and the mod's pools, held for a scope
	// A channel's value drawn at random instead of set: a weighted draw among values and the mod's pools, held for a
	// scope. Only a channel's own value has one; an entry of a draw is always a set value
	class RandomValue
	{
	public:
		// Activation, Channel and Actor our own, the clip or modifier and the mod the conditions' state data scopes
		enum class Scope : std::uint8_t
		{
			kActivation,     // this channel and actor, drawn again each time the clip or modifier activates
			kChannel,        // this channel and actor, for good
			kSubMod,         // every random channel of the clip or modifier, for the actor
			kRegisteredMod,  // every random channel of the mod, for the actor
			kActor           // every random channel, for the actor
		};

		RandomValue(ValueFactory a_factory, PoolSource a_pools) :
			factory(std::move(a_factory)), pools(std::move(a_pools)) {}

		// The entries; the Static Value / Random Value slider above them is the value's own, the scope the State block's
		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent);

		void Parse(const rapidjson::Value& a_value);
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;

		PickList list;
		Scope scope = Scope::kChannel;
		ValueFactory factory;
		PoolSource pools;  // the mod's pools of the channel
	};

	// The type slider a channel's value starts with, as a numeric value's does: Static Value or Random Value
	bool DisplayValueTypeSlider(bool& a_bRandom, float a_firstColumnWidthPercent);

	// The State block of a Random Value: the scope its draw is held for, as the Random condition's state data scope
	class RandomStateChannelComponent : public IChannelComponent
	{
	public:
		RandomStateChannelComponent(std::string_view a_name, std::string_view a_description, RandomValue* a_random) :
			IChannelComponent(a_name, a_description), random(a_random) {}

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kState; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override;

		RandomValue* random;  // the value's own, whose scope this is
	};

	class SwitchChannelComponent : public IChannelComponent
	{
	public:
		using IChannelComponent::IChannelComponent;

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kSwitch; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override { return bValue ? "On" : "Off"; }

		bool bValue = false;
	};

	// A head part for one slot: the form, shown as its picture on the head it was made for, picked from a grid of them
	class HeadPartChannelComponent : public IChannelComponent
	{
	public:
		HeadPartChannelComponent(std::string_view a_name, std::string_view a_description, RE::BGSHeadPart::HeadPartType a_type);

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kHeadPart; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override;

		Components::TESFormValue<RE::BGSHeadPart> form;
		RE::BGSHeadPart::HeadPartType type;
		std::unique_ptr<RandomValue> random;  // the channel's draw, when it offers one
		bool bRandom = false;
	};

	// A reference by form: a plugin name and an id, as a condition's form row
	class FormChannelComponent : public IChannelComponent
	{
	public:
		using IChannelComponent::IChannelComponent;

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kForm; }

		void Parse(const rapidjson::Value& a_value) override { form.Parse(const_cast<rapidjson::Value&>(a_value)); }
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override { return form.Serialize(a_allocator); }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override { return form.DisplayInUI(a_bEditable, a_firstColumnWidthPercent); }

		[[nodiscard]] std::string GetArgument() const override { return form.GetValue() ? form.GetArgument() : std::string("(none)"); }

		Components::TESFormValue<RE::TESObjectREFR> form;
	};

	// The space a colour's gradient is mixed in, a mode, with the gradient drawn under it from one end to the other,
	// the strip the colour field's size
	class ColourSpaceChannelComponent : public ModeChannelComponent
	{
	public:
		using ColoursFn = std::function<std::array<RE::NiColor, 2>()>;

		ColourSpaceChannelComponent(std::string_view a_name, std::string_view a_description, std::vector<std::string> a_modes, ColoursFn a_colours) :
			ModeChannelComponent(a_name, a_description, std::move(a_modes)), colours(std::move(a_colours)) {}

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		ColoursFn colours;
	};

	// A colour mixed a fraction of the way from one to another in a space: sRGB component by component, Oklab in a
	// straight line through lightness and the opponent axes, Oklch with the hue turned the short way
	[[nodiscard]] RE::NiColor MixColour(const RE::NiColor& a_from, const RE::NiColor& a_to, float a_t, std::string_view a_space);

	// Whether the value replaces what lower priority modifiers set, or is blended with it
	class BlendChannelComponent : public IChannelComponent
	{
	public:
		enum class Mode : uint8_t
		{
			kClaim,     // this value, and nothing below it counts
			kAdd,       // running + value
			kSaturate,  // 1 - (1 - running)(1 - value): approaches 1 without clipping
			kAverage,   // the mean of everything averaging this channel
			kMax,
			kMin
		};

		using IChannelComponent::IChannelComponent;

		[[nodiscard]] ChannelComponentType GetType() const override { return ChannelComponentType::kBlend; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override;

		Mode mode = Mode::kClaim;
	};

	class ChannelBase
	{
	public:
		virtual ~ChannelBase() = default;

		[[nodiscard]] virtual std::string_view GetTypeName() const = 0;
		[[nodiscard]] virtual std::string_view GetDescription() const = 0;

		// What the type combo shows: "body:BreastsSH", or "Group"
		[[nodiscard]] virtual std::string GetName() const = 0;
		[[nodiscard]] virtual bool IsValid() const = 0;
		[[nodiscard]] virtual bool IsGroup() const { return false; }

		[[nodiscard]] virtual std::string GetArgument() const;

		[[nodiscard]] uint32_t GetNumComponents() const { return static_cast<uint32_t>(_components.size()); }
		[[nodiscard]] IChannelComponent* GetComponent(uint32_t a_index) const;

		[[nodiscard]] bool IsDisabled() const { return _bDisabled; }
		void SetDisabled(bool a_bDisabled) { _bDisabled = a_bDisabled; }

		[[nodiscard]] ChannelSet* GetParentSet() const { return _parentSet; }
		void SetParentSet(ChannelSet* a_set) { _parentSet = a_set; }

		// The whole entry as one object, the shape a set's Serialize expects; the components sit under their names
		virtual void Parse(const rapidjson::Value& a_value);
		virtual void Serialize(void* a_value, void* a_allocator) const;

		template <typename T, typename... Args>
		T* AddComponent(std::string_view a_name, std::string_view a_description = ""sv, Args&&... a_args)
		{
			auto& component = _components.emplace_back(std::make_unique<T>(a_name, a_description, std::forward<Args>(a_args)...));
			return static_cast<T*>(component.get());
		}

	protected:
		ChannelBase() = default;

		std::vector<std::unique_ptr<IChannelComponent>> _components;
		ChannelSet* _parentSet = nullptr;
		bool _bDisabled = false;
	};
}

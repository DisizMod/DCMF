#pragma once

#include "BaseChannels.h"
#include "Containers.h"
#include "apply/Kind.h"

#include <memory>
#include <string_view>

namespace Channels
{
	class GroupChannel;

	// One channel a modifier drives: an entry of a kind's table, by name, with its value, and for a number or a
	// colour how it gets there and whether it blends with what is below
	class Channel : public ChannelBase
	{
	public:
		Apply::Kind kind = Apply::Kind::kBodyMorph;
		std::string identifier;

		// Looked up when asked: the table may not have held the name when this was loaded
		[[nodiscard]] Apply::ChannelId GetId() const;

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] bool IsValid() const override { return GetId().IsValid(); }
		[[nodiscard]] std::string GetArgument() const override;

	protected:
		Channel() = default;
	};

	class NumberChannel : public Channel
	{
	public:
		NumberChannel(float a_low = 0.f, float a_high = 1.f, float a_start = 0.f);

		[[nodiscard]] std::string_view GetTypeName() const override { return "number"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "A number on the channel's range."sv; }

		NumberChannelComponent* value;
		BoundChannelComponent* readLow;  // what a global or an actor value runs over
		BoundChannelComponent* readHigh;
		BoundChannelComponent* channelLow;  // what that becomes on the channel
		BoundChannelComponent* channelHigh;
		BlendChannelComponent* blend;
	};

	class ColourChannel : public Channel
	{
	public:
		ColourChannel();

		[[nodiscard]] std::string_view GetTypeName() const override { return "colour"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "A colour."sv; }

		ColourChannelComponent* value;
	};

	class ModeChannel : public Channel
	{
	public:
		explicit ModeChannel(std::vector<std::string> a_modes = {});

		[[nodiscard]] std::string_view GetTypeName() const override { return "mode"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "One of a short list of names."sv; }

		ModeChannelComponent* value;
	};

	class ReferenceChannel : public Channel
	{
	public:
		explicit ReferenceChannel(ReferenceChannelComponent::ItemsFn a_items = nullptr);
		[[nodiscard]] std::string_view GetTypeName() const override { return "reference"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "A name picked from the channel's list."sv; }

		ReferenceChannelComponent* value;
	};

	class HeadPartChannel : public Channel
	{
	public:
		explicit HeadPartChannel(RE::BGSHeadPart::HeadPartType a_type);
		[[nodiscard]] std::string_view GetTypeName() const override { return "headpart"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "A head part for its slot."sv; }

		HeadPartChannelComponent* value;
	};

	// A reference by form: a plugin name and an id, as a condition's form row
	class FormChannel : public Channel
	{
	public:
		FormChannel();

		[[nodiscard]] std::string_view GetTypeName() const override { return "form"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "A reference by plugin and form ID."sv; }

		FormChannelComponent* value;
	};

	class SwitchChannel : public Channel
	{
	public:
		SwitchChannel();

		[[nodiscard]] std::string_view GetTypeName() const override { return "switch"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "On or off."sv; }

		SwitchChannelComponent* value;
	};

	// The channels a modifier drives, and the groups of them; one per modifier, and one inside each group
	class ChannelSet : public Set<ChannelBase, ChannelSet>
	{
	public:
		using Set::Set;

		explicit ChannelSet(GroupChannel* a_parentGroup) :
			_parentGroup(a_parentGroup) {}

		void SetAsParentImpl(std::unique_ptr<ChannelBase>& a_channel);
		[[nodiscard]] bool IsChildOfImpl(ChannelBase* a_channel);
		[[nodiscard]] SubMod* GetParentSubModImpl() const;
		[[nodiscard]] std::string NumTextImpl() const;
		[[nodiscard]] bool IsDirtyRecursiveImpl() const;
		void SetDirtyRecursiveImpl(bool a_bDirty);

		[[nodiscard]] GroupChannel* GetParentGroup() const { return _parentGroup; }

		// Whether this channel is already driven here or in any group below, by an entry other than the one asking
		[[nodiscard]] bool Contains(Apply::ChannelId a_id, const ChannelBase* a_except = nullptr);

	private:
		GroupChannel* _parentGroup = nullptr;
	};

	// Channels claimed together: when a higher priority modifier holds any one of them, the whole group is dropped
	class GroupChannel : public ChannelBase
	{
	public:
		GroupChannel();

		[[nodiscard]] std::string_view GetTypeName() const override { return "group"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Channels claimed together: if a higher priority modifier holds any one of them, none of the group is applied."sv; }
		[[nodiscard]] std::string GetName() const override { return "Group"; }
		[[nodiscard]] bool IsValid() const override { return !channels->IsEmpty() && channels->IsValid(); }
		[[nodiscard]] bool IsGroup() const override { return true; }
		[[nodiscard]] std::string GetArgument() const override { return channels->NumText(); }

		void Parse(const rapidjson::Value& a_value) override;
		void Serialize(void* a_value, void* a_allocator) const override;

		std::unique_ptr<ChannelSet> channels;
	};

	// A channel of the entry's type, by id or by "kind:name"; "group" makes a group; null for a name nothing has
	[[nodiscard]] std::unique_ptr<ChannelBase> CreateChannel(Apply::ChannelId a_id);

	// A fresh value component of the kind the reference channel uses, its picker the same, for an entry of a pick or a pool
	[[nodiscard]] ValueFactory ValueFactoryFor(Apply::ChannelId a_id);
	[[nodiscard]] std::unique_ptr<ChannelBase> CreateChannel(std::string_view a_name);
	[[nodiscard]] std::unique_ptr<ChannelBase> CreateChannelFromJson(const rapidjson::Value& a_value);

	// A row's mode value its list does not have, a transition, a colour space, a pose mode or target, said as what is
	// wrong; empty when every one is listed. A file holding one is malformed, and the row is not read
	[[nodiscard]] std::string InvalidModeOf(ChannelBase& a_channel);

	// A copy, by way of JSON, as a condition set is copied
	[[nodiscard]] std::unique_ptr<ChannelSet> DuplicateChannelSet(ChannelSet* a_channelSet);

	constexpr auto kGroupName = "group"sv;
}

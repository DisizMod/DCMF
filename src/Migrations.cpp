#include "Migrations.h"

#include <array>
#include <functional>

namespace Migrations
{
	namespace
	{
		// Where an object sits: the mod, and the clip, modifier or preset it is in
		struct Where
		{
			std::string_view mod;
			std::string_view owner;
		};

		std::string_view StringOf(const rapidjson::Value& a_object, const char* a_key)
		{
			const auto it = a_object.IsObject() ? a_object.FindMember(a_key) : a_object.MemberEnd();
			return it != a_object.MemberEnd() && it->value.IsString() ? std::string_view(it->value.GetString(), it->value.GetStringLength()) : std::string_view{};
		}

		// Every object under the value, depth first, each before what it holds
		void Walk(rapidjson::Value& a_value, const std::function<void(rapidjson::Value&)>& a_visit)
		{
			if (a_value.IsObject()) {
				a_visit(a_value);
				for (auto& member : a_value.GetObj()) {
					Walk(member.value, a_visit);
				}
			} else if (a_value.IsArray()) {
				for (auto& item : a_value.GetArray()) {
					Walk(item, a_visit);
				}
			}
		}

		// Every object of each clip, modifier and preset, with the item it is in: a condition has "condition", a channel row
		// "channel", wherever they nest
		void ForEachObject(rapidjson::Document& a_doc, std::string_view a_mod, const std::function<void(rapidjson::Value&, const Where&)>& a_visit)
		{
			for (const auto* collection : { "modifiers", "clips", "conditionPresets", "triggerPresets" }) {
				const auto it = a_doc.FindMember(collection);
				if (it == a_doc.MemberEnd() || !it->value.IsArray()) {
					continue;
				}
				for (auto& item : it->value.GetArray()) {
					const Where where{ a_mod, StringOf(item, "name") };
					Walk(item, [&](rapidjson::Value& a_object) { a_visit(a_object, where); });
				}
			}
		}

		std::uint32_t Fnv32(std::string_view a_text)
		{
			std::uint32_t hash = 0x811C9DC5u;
			for (const char c : a_text) {
				hash ^= static_cast<std::uint8_t>(c);
				hash *= 0x01000193u;
			}
			return hash;
		}

		rapidjson::Value SeedOf(std::uint32_t a_seed, bool a_bPerActivation, rapidjson::Document::AllocatorType& a_allocator)
		{
			rapidjson::Value seed(rapidjson::kObjectType);
			seed.AddMember("seed", a_seed, a_allocator);
			seed.AddMember("perActivation", a_bPerActivation, a_allocator);
			return seed;
		}

		// 1.1.0: a random value's scope becomes a seed, rows that shared a scope sharing one; the Random condition's state
		// a seed of its own, its reset on loop or echo the seed's each activation
		std::size_t RandomScopeToSeed(rapidjson::Document& a_doc, std::string_view a_mod)
		{
			auto& allocator = a_doc.GetAllocator();
			std::size_t changed = 0;
			std::size_t randoms = 0;
			ForEachObject(a_doc, a_mod, [&](rapidjson::Value& a_object, const Where& a_where) {
				if (const auto channel = StringOf(a_object, "channel"); !channel.empty()) {
					// The scope on the row's State, or on the draw itself as older files had it
					auto* random = [&]() -> rapidjson::Value* {
						const auto value = a_object.FindMember("Value");
						if (value == a_object.MemberEnd() || !value->value.IsObject()) {
							return nullptr;
						}
						const auto it = value->value.FindMember("random");
						return it != value->value.MemberEnd() && it->value.IsObject() ? &it->value : nullptr;
					}();
					std::string scope(StringOf(a_object, "State"));
					if (random && scope.empty()) {
						scope = StringOf(*random, "scope");
					}
					if (!random || a_object.HasMember("Seed")) {
						return;
					}
					a_object.RemoveMember("State");
					random->RemoveMember("scope");
					std::uint32_t seed = Fnv32(std::format("{}|{}|{}", a_where.mod, a_where.owner, channel));
					if (scope == "Clip or modifier") {
						seed = Fnv32(std::format("{}|{}", a_where.mod, a_where.owner));
					} else if (scope == "Registered mod") {
						seed = Fnv32(a_where.mod);
					} else if (scope == "Actor" || scope == "Reference") {
						seed = 0;
					}
					a_object.AddMember("Seed", SeedOf(seed, scope == "Activation", allocator), allocator);
					++changed;
					return;
				}
				if (StringOf(a_object, "condition") == "Random") {
					const auto state = a_object.FindMember("State");
					if (state == a_object.MemberEnd() || !state->value.IsObject()) {
						return;
					}
					const auto reset = state->value.FindMember("shouldResetOnLoopOrEcho");
					const bool bPerActivation = reset == state->value.MemberEnd() || !reset->value.IsBool() || reset->value.GetBool();
					a_object.RemoveMember("State");
					a_object.AddMember("Seed", SeedOf(Fnv32(std::format("{}|{}|Random|{}", a_where.mod, a_where.owner, randoms++)), bPerActivation, allocator), allocator);
					++changed;
				}
			});
			return changed;
		}

		struct Step
		{
			REL::Version to;
			std::string_view what;
			std::size_t (*apply)(rapidjson::Document&, std::string_view);
		};

		// In order; a step only ever adds to this list
		const std::array kSteps{
			Step{ { 1, 1, 0 }, "random scopes to seeds"sv, &RandomScopeToSeed },
		};
	}

	REL::Version VersionOf(const rapidjson::Value& a_doc)
	{
		const auto text = StringOf(a_doc, "dcmfVersion");
		return text.empty() ? REL::Version{ 1, 0, 0 } : REL::Version(text);
	}

	std::string CurrentVersionText()
	{
		const auto version = SKSE::PluginDeclaration::GetSingleton()->GetVersion();
		return std::format("{}.{}.{}", version.major(), version.minor(), version.patch());
	}

	void Apply(rapidjson::Document& a_doc, REL::Version a_from, std::string_view a_mod)
	{
		if (!a_doc.IsObject()) {
			return;
		}
		for (const auto& step : kSteps) {
			if (step.to > a_from) {
				if (const auto changed = step.apply(a_doc, a_mod)) {
					logger::info("storage: {}: migrated to {}.{}.{}, {}: {} item(s)", a_mod, step.to.major(), step.to.minor(), step.to.patch(), step.what, changed);
				}
			}
		}
		const auto current = CurrentVersionText();
		a_doc.RemoveMember("dcmfVersion");
		a_doc.AddMember("dcmfVersion", rapidjson::Value(current.data(), static_cast<rapidjson::SizeType>(current.size()), a_doc.GetAllocator()), a_doc.GetAllocator());
	}
}

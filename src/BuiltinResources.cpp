#include "BuiltinResources.h"

#include "Conditions.h"
#include "Resources.h"
#include "Utils.h"

#include <cmath>
#include <deque>
#include <limits>
#include <bit>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace BuiltinResources
{
	namespace
	{
		constexpr auto kPlugin = "DCMF"sv;
		constexpr float kNoAnswer = std::numeric_limits<float>::quiet_NaN();

		// A resource of this plugin's: the registry refusing one is a bug, said with its key
		Resources::Result RegisterBuiltin(Resources::Info a_info, Resources::Gatherer* a_gatherer)
		{
			const auto key = a_info.key;
			const auto result = Resources::Register(std::move(a_info), a_gatherer);
			if (result != Resources::Result::OK) {
				logger::error("resources: the built-in '{}' was refused ({}); every row reading it never holds", key, static_cast<int>(result));
			}
			return result;
		}

		// A number as a component reads it: an actor value off the subject, a global off the world
		class NumberGatherer : public Resources::Gatherer
		{
		public:
			explicit NumberGatherer(const Components::NumericValue& a_value) :
				_value(a_value) {}

			float GatherNumber(RE::TESObjectREFR* a_subject) noexcept override
			{
				if (_value.GetType() == Components::NumericValue::Type::kActorValue && !a_subject) {
					return kNoAnswer;
				}
				return _value.GetValue(a_subject);
			}

		private:
			Components::NumericValue _value;
		};

		// An actor's target of one kind
		class TargetGatherer : public Resources::Gatherer
		{
		public:
			explicit TargetGatherer(Utils::TargetType a_type) :
				_type(a_type) {}

			RE::TESForm* GatherForm(RE::TESObjectREFR* a_subject) noexcept override
			{
				auto* actor = a_subject ? a_subject->As<RE::Actor>() : nullptr;
				RE::TESObjectREFRPtr target = nullptr;
				return actor && Utils::GetCurrentTarget(actor, _type, target) ? target.get() : nullptr;
			}

		private:
			Utils::TargetType _type;
		};

		// What an actor wears, from the worn entries of its inventory: the forms, their keywords, or their names as the
		// inventory shows them and as the forms have them
		class WornGatherer : public Resources::Gatherer
		{
		public:
			enum class What : std::uint8_t
			{
				kForms,
				kKeywords,
				kNames,
				kPlugins
			};

			explicit WornGatherer(What a_what) :
				_what(a_what) {}

			void GatherSet(RE::TESObjectREFR* a_subject, DCMF_API::Resources::ISetSink& a_out) noexcept override
			{
				auto* actor = a_subject ? a_subject->As<RE::Actor>() : nullptr;
				if (!actor) {
					return;
				}
				for (const auto& [object, data] : actor->GetInventory()) {
					const auto& [count, entry] = data;
					if (!object || count <= 0 || !entry || !entry->IsWorn()) {
						continue;
					}
					switch (_what) {
					case What::kForms:
						a_out.AddForm(object->GetFormID());
						break;
					case What::kKeywords:
						if (const auto* keywords = object->As<RE::BGSKeywordForm>()) {
							for (std::uint32_t i = 0; i < keywords->numKeywords; ++i) {
								if (keywords->keywords[i]) {
									a_out.AddForm(keywords->keywords[i]->GetFormID());
								}
							}
						}
						break;
					case What::kNames:
						if (const char* shown = entry->GetDisplayName()) {
							a_out.AddText(shown);
						}
						a_out.AddText(object->GetName());
						break;
					case What::kPlugins:
						if (const auto* file = object->GetFile(0)) {
							a_out.AddText(file->GetFilename().data());
						}
						break;
					}
				}
			}

		private:
			What _what;
		};

		// Anything else, read by a function
		class FunctionGatherer : public Resources::Gatherer
		{
		public:
			std::function<float(RE::TESObjectREFR*)> number;
			std::function<RE::TESForm*(RE::TESObjectREFR*)> form;
			std::function<void(RE::TESObjectREFR*, std::vector<RE::FormID>&)> set;
			std::function<void(RE::TESObjectREFR*, std::vector<std::string>&)> texts;

			float GatherNumber(RE::TESObjectREFR* a_subject) noexcept override
			{
				return number ? number(a_subject) : kNoAnswer;
			}

			RE::TESForm* GatherForm(RE::TESObjectREFR* a_subject) noexcept override
			{
				return form ? form(a_subject) : nullptr;
			}

			void GatherSet(RE::TESObjectREFR* a_subject, DCMF_API::Resources::ISetSink& a_out) noexcept override
			{
				if (texts) {
					std::vector<std::string> out;
					texts(a_subject, out);
					for (const auto& text : out) {
						a_out.AddText(text.c_str());
					}
					return;
				}
				if (!set) {
					return;
				}
				std::vector<RE::FormID> forms;
				set(a_subject, forms);
				for (const auto id : forms) {
					a_out.AddForm(id);
				}
			}
		};

		// What an actor is: its race, or its base
		class IdentityGatherer : public Resources::Gatherer
		{
		public:
			explicit IdentityGatherer(bool a_bRace) :
				_bRace(a_bRace) {}

			RE::TESForm* GatherForm(RE::TESObjectREFR* a_subject) noexcept override
			{
				auto* actor = a_subject ? a_subject->As<RE::Actor>() : nullptr;
				if (!actor) {
					return nullptr;
				}
				return _bRace ? static_cast<RE::TESForm*>(actor->GetRace()) : static_cast<RE::TESForm*>(Utils::GetActorBase(actor));
			}

		private:
			bool _bRace;
		};

		// An equip or unequip: what the actor wears is read again when next asked
		class EquipSink : public RE::BSTEventSink<RE::TESEquipEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				if (a_event && a_event->actor) {
					auto* actor = a_event->actor.get();
					(void)Resources::Invalidate(kWorn, actor);
					(void)Resources::Invalidate(kWornKeywords, actor);
					(void)Resources::Invalidate(kWornNames, actor);
					(void)Resources::Invalidate(kWornPlugins, actor);
					(void)Resources::Invalidate(kWornSlots, actor);
					for (std::uint32_t slot = 0; slot < 32; ++slot) {
						(void)Resources::Invalidate(WornSlotKeywordsKey(slot), actor);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// Kept for the game's life: the registry holds their addresses
		std::mutex g_lock;
		std::deque<NumberGatherer> g_numbers;
		std::deque<TargetGatherer> g_targets;
		std::deque<FunctionGatherer> g_functions;
		WornGatherer g_worn{ WornGatherer::What::kForms };
		WornGatherer g_wornKeywords{ WornGatherer::What::kKeywords };
		WornGatherer g_wornNames{ WornGatherer::What::kNames };
		WornGatherer g_wornPlugins{ WornGatherer::What::kPlugins };
		IdentityGatherer g_race{ true };
		IdentityGatherer g_actorBase{ false };
		EquipSink g_equipSink;

		// Registered the first time the key is asked for; the registry refuses the ones after
		std::string Ensure(std::string a_key, Resources::Scope a_scope, const Components::NumericValue& a_value, std::string_view a_description)
		{
			std::scoped_lock guard(g_lock);
			if (Resources::Exists(a_key)) {
				return a_key;
			}
			auto& gatherer = g_numbers.emplace_back(a_value);
			RegisterBuiltin({ .key = a_key, .description = std::string(a_description), .plugin = std::string(kPlugin), .type = Resources::ValueType::Number, .scope = a_scope,
									.refresh = Resources::Refresh::EveryTick, .defaultNumber = kNoAnswer },
				&gatherer);
			return a_key;
		}

		std::string_view TargetName(Utils::TargetType a_type)
		{
			switch (a_type) {
			case Utils::TargetType::kCombatTarget:
				return "combat"sv;
			case Utils::TargetType::kDialogueTarget:
				return "dialogue"sv;
			case Utils::TargetType::kFollowTarget:
				return "follow"sv;
			case Utils::TargetType::kHeadtrackTarget:
				return "headtrack"sv;
			case Utils::TargetType::kPackageTarget:
				return "package"sv;
			case Utils::TargetType::kAnyTarget:
				return "any"sv;
			case Utils::TargetType::kTarget:
			default:
				return "target"sv;
			}
		}
	}

	void Register()
	{
		std::scoped_lock guard(g_lock);
		for (auto type = 0; type <= static_cast<int>(Utils::TargetType::kAnyTarget); ++type) {
			auto& gatherer = g_targets.emplace_back(static_cast<Utils::TargetType>(type));
			RegisterBuiltin({ .key = TargetKey(type), .description = std::format("The actor's {} target.", TargetName(static_cast<Utils::TargetType>(type))), .plugin = std::string(kPlugin),
									.type = Resources::ValueType::Form, .scope = Resources::Scope::Actor, .refresh = Resources::Refresh::EveryTick },
				&gatherer);
		}

		// What an actor wears is read again on an equip; every few seconds as well, for whatever changes it without one
		const auto worn = [](std::string_view a_key, Resources::ValueType a_type, std::string_view a_description, Resources::Gatherer* a_gatherer) {
			RegisterBuiltin({ .key = std::string(a_key), .description = std::string(a_description), .plugin = std::string(kPlugin), .type = a_type, .scope = Resources::Scope::Actor,
									.refresh = Resources::Refresh::Interval, .intervalSeconds = 5.f },
				a_gatherer);
		};
		worn(kWorn, Resources::ValueType::FormSet, "What the actor wears.", &g_worn);
		worn(kWornKeywords, Resources::ValueType::FormSet, "The keywords of what the actor wears.", &g_wornKeywords);
		worn(kWornNames, Resources::ValueType::TextSet, "The names of what the actor wears, as its inventory shows them and as their forms have them.", &g_wornNames);
		worn(kWornPlugins, Resources::ValueType::TextSet, "The plugins that add what the actor wears.", &g_wornPlugins);
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESEquipEvent>(&g_equipSink);
		} else {
			logger::error("resources: no script event source; what is worn is read again only every few seconds");
		}
		RegisterBuiltin({ .key = std::string(kRace), .description = "The actor's race.", .plugin = std::string(kPlugin), .type = Resources::ValueType::Form, .scope = Resources::Scope::Actor,
								.refresh = Resources::Refresh::EveryTick },
			&g_race);
		RegisterBuiltin({ .key = std::string(kActorBase), .description = "The actor's base.", .plugin = std::string(kPlugin), .type = Resources::ValueType::Form, .scope = Resources::Scope::Actor,
								.refresh = Resources::Refresh::EveryTick },
			&g_actorBase);
	}

	namespace
	{
		// Registered once: the first asking gives the function, the later ones find the key taken
		std::string EnsureFunction(std::string a_key, std::string_view a_description, Resources::ValueType a_type, Resources::Scope a_scope, Resources::Refresh a_refresh, float a_interval,
			const std::function<void(FunctionGatherer&)>& a_fill)
		{
			std::scoped_lock guard(g_lock);
			if (Resources::Exists(a_key)) {
				return a_key;
			}
			auto& gatherer = g_functions.emplace_back();
			a_fill(gatherer);
			RegisterBuiltin({ .key = a_key, .description = std::string(a_description), .plugin = std::string(kPlugin), .type = a_type, .scope = a_scope, .refresh = a_refresh,
									.intervalSeconds = a_interval, .defaultNumber = kNoAnswer },
				&gatherer);
			return a_key;
		}
	}

	std::string Number(std::string a_key, std::string_view a_description, std::function<float(RE::TESObjectREFR*)> a_read, Resources::Scope a_scope)
	{
		return EnsureFunction(std::move(a_key), a_description, Resources::ValueType::Number, a_scope, Resources::Refresh::EveryTick, 0.f, [&](FunctionGatherer& a_gatherer) { a_gatherer.number = std::move(a_read); });
	}

	std::string Form(std::string a_key, std::string_view a_description, std::function<RE::TESForm*(RE::TESObjectREFR*)> a_read, Resources::Scope a_scope)
	{
		return EnsureFunction(std::move(a_key), a_description, Resources::ValueType::Form, a_scope, Resources::Refresh::EveryTick, 0.f, [&](FunctionGatherer& a_gatherer) { a_gatherer.form = std::move(a_read); });
	}

	std::string FormSet(std::string a_key, std::string_view a_description, std::function<void(RE::TESObjectREFR*, std::vector<RE::FormID>&)> a_read, float a_intervalSeconds)
	{
		const auto refresh = a_intervalSeconds > 0.f ? Resources::Refresh::Interval : Resources::Refresh::EveryTick;
		return EnsureFunction(std::move(a_key), a_description, Resources::ValueType::FormSet, Resources::Scope::Actor, refresh, a_intervalSeconds, [&](FunctionGatherer& a_gatherer) { a_gatherer.set = std::move(a_read); });
	}

	std::string TextSet(std::string a_key, std::string_view a_description, std::function<void(RE::TESObjectREFR*, std::vector<std::string>&)> a_read)
	{
		return EnsureFunction(std::move(a_key), a_description, Resources::ValueType::TextSet, Resources::Scope::Actor, Resources::Refresh::EveryTick, 0.f, [&](FunctionGatherer& a_gatherer) { a_gatherer.texts = std::move(a_read); });
	}

	void Drawn(const std::string& a_key, float a_low, float a_high)
	{
		(void)EnsureFunction(a_key, "A number drawn for each actor, held until the clip loops or echoes when the row asks.", Resources::ValueType::Number, Resources::Scope::Actor, Resources::Refresh::Invalidated, 0.f,
			[&](FunctionGatherer& a_gatherer) {
				a_gatherer.number = [a_low, a_high](RE::TESObjectREFR* a_refr) { return a_refr ? Utils::GetRandomFloat(a_low, a_high) : kNoAnswer; };
			});
	}

	namespace
	{
		// Seconds since each reference's last read, for state that moves with time
		float SinceLast(std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point>& a_last, RE::FormID a_id)
		{
			const auto now = std::chrono::steady_clock::now();
			const auto it = a_last.find(a_id);
			const float seconds = it == a_last.end() ? 0.f : std::chrono::duration<float>(now - it->second).count();
			a_last[a_id] = now;
			return seconds;
		}
	}

	std::string IdleTime()
	{
		return EnsureFunction("dcmf.idleTime", "How long the actor has stood idle, in seconds.", Resources::ValueType::Number, Resources::Scope::Actor, Resources::Refresh::EveryTick, 0.f, [](FunctionGatherer& a_gatherer) {
			struct State
			{
				std::unordered_map<RE::FormID, std::unique_ptr<Conditions::IdleTimeCondition::IdleTimeConditionStateData>> idle;
				std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> last;
			};
			a_gatherer.number = [state = std::make_shared<State>()](RE::TESObjectREFR* a_refr) {
				if (!a_refr || !a_refr->As<RE::Actor>()) {
					return kNoAnswer;
				}
				auto& data = state->idle[a_refr->GetFormID()];
				if (!data) {
					data = std::make_unique<Conditions::IdleTimeCondition::IdleTimeConditionStateData>();
					data->Initialize(a_refr);
				}
				data->Update(SinceLast(state->last, a_refr->GetFormID()));
				return data->GetIdleTime();
			};
		});
	}

	std::string SurfaceAngle(float a_smoothing, bool a_bNavmesh, bool a_bDegrees)
	{
		const auto key = std::format("dcmf.surfaceAngle.{:X}.{}.{}", std::bit_cast<std::uint32_t>(a_smoothing), a_bNavmesh ? "navmesh" : "collision", a_bDegrees ? "degrees" : "radians");
		return EnsureFunction(key, "The angle of the ground under the actor to its facing.", Resources::ValueType::Number, Resources::Scope::Actor, Resources::Refresh::EveryTick, 0.f, [&](FunctionGatherer& a_gatherer) {
			struct State
			{
				std::unordered_map<RE::FormID, std::unique_ptr<Conditions::MovementSurfaceAngleCondition::MovementSurfaceAngleConditionStateData>> smoothed;
				std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> last;
			};
			a_gatherer.number = [state = std::make_shared<State>(), a_smoothing, a_bNavmesh, a_bDegrees](RE::TESObjectREFR* a_refr) {
				auto* actor = a_refr ? a_refr->As<RE::Actor>() : nullptr;
				auto* controller = actor ? actor->GetCharController() : nullptr;
				if (!controller) {
					return kNoAnswer;
				}
				RE::hkVector4 normal;
				bool bHave = false;
				if (a_smoothing < 1.f) {
					auto& data = state->smoothed[a_refr->GetFormID()];
					if (!data) {
						data = std::make_unique<Conditions::MovementSurfaceAngleCondition::MovementSurfaceAngleConditionStateData>();
						data->Initialize(a_refr, a_smoothing, a_bNavmesh);
					}
					data->Update(SinceLast(state->last, a_refr->GetFormID()));
					bHave = data->GetSmoothedSurfaceNormal(normal);
				}
				if (!bHave && !Utils::GetSurfaceNormal(a_refr, normal, a_bNavmesh)) {
					return kNoAnswer;
				}
				const auto& forward = controller->forwardVec;
				const float dot = normal.quad.m128_f32[0] * forward.quad.m128_f32[0] + normal.quad.m128_f32[1] * forward.quad.m128_f32[1] + normal.quad.m128_f32[2] * forward.quad.m128_f32[2];
				const float angle = RE::NI_HALF_PI - std::acos(dot);
				return a_bDegrees ? RE::rad_to_deg(angle) : angle;
			};
		});
	}

	namespace
	{
		std::mutex g_resetLock;
		std::unordered_map<const SubMod*, std::vector<std::string>> g_resets;
	}

	void ResetWith(const SubMod* a_clip, std::string a_key)
	{
		std::scoped_lock guard(g_resetLock);
		auto& keys = g_resets[a_clip];
		if (std::ranges::find(keys, a_key) == keys.end()) {
			keys.push_back(std::move(a_key));
		}
	}

	void OnLoopOrEcho(const SubMod* a_clip, RE::TESObjectREFR* a_refr)
	{
		std::vector<std::string> keys;
		{
			std::scoped_lock guard(g_resetLock);
			if (const auto it = g_resets.find(a_clip); it != g_resets.end()) {
				keys = it->second;
			}
		}
		for (const auto& key : keys) {
			(void)Resources::Invalidate(key, a_refr);
		}
	}

	std::string WornSlotKeywordsKey(std::uint32_t a_slot)
	{
		return std::format("dcmf.wornSlotKeywords.{}", a_slot);
	}

	std::string ActorValueKey(const Components::NumericValue& a_value)
	{
		constexpr std::array kTypes{ "value"sv, "base"sv, "max"sv, "percentage"sv };
		const auto index = static_cast<std::size_t>(a_value.GetActorValueType());
		if (index >= kTypes.size()) {
			logger::error("resources: actor value type {} is not one of the {}; read as its value", index, kTypes.size());
		}
		const auto type = kTypes[index < kTypes.size() ? index : 0];
		return Ensure(std::format("dcmf.av.{}.{}", static_cast<int>(a_value.GetActorValue()), type), Resources::Scope::Actor, a_value, "An actor value off the actor.");
	}

	std::string GlobalKey(const Components::NumericValue& a_value)
	{
		const auto* global = a_value.GetGlobalVariable();
		return Ensure(std::format("dcmf.global.{:08X}", global ? global->GetFormID() : 0), Resources::Scope::World, a_value, "A global variable's value.");
	}

	std::string TargetKey(std::int32_t a_targetType)
	{
		return std::format("dcmf.target.{}", TargetName(static_cast<Utils::TargetType>(a_targetType)));
	}
}

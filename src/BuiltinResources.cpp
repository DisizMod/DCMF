#include "BuiltinResources.h"

#include "Conditions.h"
#include "Resources.h"
#include "Resolve.h"
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

		// The engine's events, each making the resources that name it read again for the actor it is about
		class EventSinks :
			public RE::BSTEventSink<RE::TESEquipEvent>,
			public RE::BSTEventSink<RE::TESContainerChangedEvent>,
			public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>,
			public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>,
			public RE::BSTEventSink<RE::TESCombatEvent>,
			public RE::BSTEventSink<RE::TESDeathEvent>,
			public RE::BSTEventSink<RE::TESEnterBleedoutEvent>,
			public RE::BSTEventSink<RE::TESFurnitureEvent>,
			public RE::BSTEventSink<RE::TESSwitchRaceCompleteEvent>,
			public RE::BSTEventSink<RE::TESCellAttachDetachEvent>,
			public RE::BSTEventSink<RE::TESMoveAttachDetachEvent>,
			public RE::BSTEventSink<RE::TESActorLocationChangeEvent>,
			public RE::BSTEventSink<RE::TESQuestStageEvent>,
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			using Control = RE::BSEventNotifyControl;

			Control ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override { return On(Event::kEquip, a_event ? a_event->actor.get() : nullptr); }

			Control ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (a_event) {
					for (const auto id : { a_event->oldContainer, a_event->newContainer }) {
						if (auto* container = id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(id) : nullptr) {
							(void)On(Event::kContainer, container);
						}
					}
				}
				return Control::kContinue;
			}

			Control ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* a_event, RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*) override { return On(Event::kMagicEffect, a_event ? a_event->target.get() : nullptr); }
			Control ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override { return On(Event::kMagicEffect, a_event ? a_event->target.get() : nullptr); }

			Control ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*) override
			{
				if (a_event) {
					(void)On(Event::kCombat, a_event->actor.get());
					(void)On(Event::kCombat, a_event->targetActor.get());
				}
				return Control::kContinue;
			}

			Control ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override { return On(Event::kDeath, a_event ? a_event->actorDying.get() : nullptr); }
			Control ProcessEvent(const RE::TESEnterBleedoutEvent* a_event, RE::BSTEventSource<RE::TESEnterBleedoutEvent>*) override { return On(Event::kDeath, a_event ? a_event->actor.get() : nullptr); }
			Control ProcessEvent(const RE::TESFurnitureEvent* a_event, RE::BSTEventSource<RE::TESFurnitureEvent>*) override { return On(Event::kFurniture, a_event ? a_event->actor.get() : nullptr); }
			Control ProcessEvent(const RE::TESSwitchRaceCompleteEvent* a_event, RE::BSTEventSource<RE::TESSwitchRaceCompleteEvent>*) override { return On(Event::kRace, a_event ? a_event->subject.get() : nullptr); }
			Control ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override { return On(Event::kCell, a_event ? a_event->reference.get() : nullptr); }
			Control ProcessEvent(const RE::TESMoveAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESMoveAttachDetachEvent>*) override { return On(Event::kCell, a_event ? a_event->movedRef.get() : nullptr); }
			Control ProcessEvent(const RE::TESActorLocationChangeEvent* a_event, RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override { return On(Event::kLocation, a_event ? a_event->actor.get() : nullptr); }

			Control ProcessEvent(const RE::TESQuestStageEvent*, RE::BSTEventSource<RE::TESQuestStageEvent>*) override
			{
				Resources::InvalidateEvent(Event::kQuestStage, nullptr);
				return Control::kContinue;
			}

			Control ProcessEvent(const RE::MenuOpenCloseEvent*, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				Resources::InvalidateEvent(Event::kMenu, nullptr);
				return Control::kContinue;
			}

		private:
			// An event about no one is dropped: with no subject the resources would be read again for everyone
			static Control On(std::uint32_t a_event, RE::TESObjectREFR* a_subject)
			{
				if (a_subject) {
					Resources::InvalidateEvent(a_event, a_subject);
				}
				return Control::kContinue;
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
		EventSinks g_eventSinks;

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
									.refresh = Resources::Refresh::Invalidated, .events = Event::kEquip, .safetySeconds = 5.f },
				a_gatherer);
		};
		worn(kWorn, Resources::ValueType::FormSet, "What the actor wears.", &g_worn);
		worn(kWornKeywords, Resources::ValueType::FormSet, "The keywords of what the actor wears.", &g_wornKeywords);
		worn(kWornNames, Resources::ValueType::TextSet, "The names of what the actor wears, as its inventory shows them and as their forms have them.", &g_wornNames);
		worn(kWornPlugins, Resources::ValueType::TextSet, "The plugins that add what the actor wears.", &g_wornPlugins);
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESEquipEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESContainerChangedEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESMagicEffectApplyEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESCombatEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESDeathEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESEnterBleedoutEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESFurnitureEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESSwitchRaceCompleteEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESCellAttachDetachEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESMoveAttachDetachEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESActorLocationChangeEvent>(&g_eventSinks);
			holder->AddEventSink<RE::TESQuestStageEvent>(&g_eventSinks);
		} else {
			logger::error("resources: no script event source; what reads on events is read again only by its safety interval");
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_eventSinks);
		}
		// A race changes by its event, a base never; both read again every few seconds for whatever changes them without one
		RegisterBuiltin({ .key = std::string(kRace), .description = "The actor's race.", .plugin = std::string(kPlugin), .type = Resources::ValueType::Form, .scope = Resources::Scope::Actor,
								.refresh = Resources::Refresh::Invalidated, .events = Event::kRace, .safetySeconds = 5.f },
			&g_race);
		RegisterBuiltin({ .key = std::string(kActorBase), .description = "The actor's base.", .plugin = std::string(kPlugin), .type = Resources::ValueType::Form, .scope = Resources::Scope::Actor,
								.refresh = Resources::Refresh::Invalidated, .safetySeconds = 5.f },
			&g_actorBase);
	}

	namespace
	{
		// Registered once: the first asking gives the function, the later ones find the key taken
		std::string EnsureFunction(std::string a_key, std::string_view a_description, Resources::ValueType a_type, Resources::Scope a_scope, Resources::Refresh a_refresh, float a_interval,
			const std::function<void(FunctionGatherer&)>& a_fill, Updates a_updates = {})
		{
			std::scoped_lock guard(g_lock);
			if (Resources::Exists(a_key)) {
				return a_key;
			}
			auto& gatherer = g_functions.emplace_back();
			a_fill(gatherer);
			// Declared updates take the place of the polling asked for
			if (a_updates.events != 0 || a_updates.bOnce) {
				a_refresh = Resources::Refresh::Invalidated;
			}
			RegisterBuiltin({ .key = a_key, .description = std::string(a_description), .plugin = std::string(kPlugin), .type = a_type, .scope = a_scope, .refresh = a_refresh,
									.intervalSeconds = a_interval, .defaultNumber = kNoAnswer, .events = a_updates.events, .safetySeconds = a_updates.bOnce ? 0.f : a_updates.safetySeconds },
				&gatherer);
			return a_key;
		}
	}

	std::string Number(std::string a_key, std::string_view a_description, std::function<float(RE::TESObjectREFR*)> a_read, Resources::Scope a_scope, Updates a_updates)
	{
		return EnsureFunction(std::move(a_key), a_description, Resources::ValueType::Number, a_scope, Resources::Refresh::EveryTick, 0.f, [&](FunctionGatherer& a_gatherer) { a_gatherer.number = std::move(a_read); }, a_updates);
	}

	std::string Form(std::string a_key, std::string_view a_description, std::function<RE::TESForm*(RE::TESObjectREFR*)> a_read, Resources::Scope a_scope, Updates a_updates)
	{
		return EnsureFunction(std::move(a_key), a_description, Resources::ValueType::Form, a_scope, Resources::Refresh::EveryTick, 0.f, [&](FunctionGatherer& a_gatherer) { a_gatherer.form = std::move(a_read); }, a_updates);
	}

	std::string FormSet(std::string a_key, std::string_view a_description, std::function<void(RE::TESObjectREFR*, std::vector<RE::FormID>&)> a_read, float a_intervalSeconds, Updates a_updates)
	{
		const auto refresh = a_intervalSeconds > 0.f ? Resources::Refresh::Interval : Resources::Refresh::EveryTick;
		return EnsureFunction(std::move(a_key), a_description, Resources::ValueType::FormSet, Resources::Scope::Actor, refresh, a_intervalSeconds, [&](FunctionGatherer& a_gatherer) { a_gatherer.set = std::move(a_read); }, a_updates);
	}

	std::string TextSet(std::string a_key, std::string_view a_description, std::function<void(RE::TESObjectREFR*, std::vector<std::string>&)> a_read, float a_intervalSeconds, Updates a_updates)
	{
		// A negative interval: read once per actor and held
		const auto refresh = a_intervalSeconds < 0.f ? Resources::Refresh::Invalidated : a_intervalSeconds > 0.f ? Resources::Refresh::Interval : Resources::Refresh::EveryTick;
		return EnsureFunction(std::move(a_key), a_description, Resources::ValueType::TextSet, Resources::Scope::Actor, refresh, a_intervalSeconds, [&](FunctionGatherer& a_gatherer) { a_gatherer.texts = std::move(a_read); }, a_updates);
	}

	namespace
	{
		// How many times each key's clip has looped or echoed on each actor, for a draw that changes with it
		std::mutex g_activationsLock;
		std::unordered_map<std::string, std::unordered_map<RE::FormID, std::uint32_t>> g_activations;

		std::uint32_t Activations(const std::string& a_key, RE::TESObjectREFR* a_refr)
		{
			std::scoped_lock guard(g_activationsLock);
			const auto it = g_activations.find(a_key);
			if (it == g_activations.end()) {
				return 0;
			}
			const auto count = it->second.find(a_refr->GetFormID());
			return count != it->second.end() ? count->second : 0;
		}
	}

	void Seeded(const std::string& a_key, float a_low, float a_high, const Components::RandomSeed& a_seed)
	{
		(void)EnsureFunction(a_key, "A number drawn for each actor from its seed, again on each loop or echo of the clip when the seed says.", Resources::ValueType::Number, Resources::Scope::Actor, Resources::Refresh::Invalidated, 0.f,
			[&](FunctionGatherer& a_gatherer) {
				a_gatherer.number = [a_low, a_high, a_seed, a_key](RE::TESObjectREFR* a_refr) {
					return a_refr ? std::lerp(a_low, a_high, static_cast<float>(Resolve::SeededUnit(a_refr, a_seed, Activations(a_key, a_refr)))) : kNoAnswer;
				};
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
			if (a_refr) {
				std::scoped_lock guard(g_activationsLock);
				++g_activations[key][a_refr->GetFormID()];
			}
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

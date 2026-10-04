#include "Resources.h"

#include "Conditions.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace Resources
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// One reference's value, or the world's under form id 0
		struct Held
		{
			float number = std::numeric_limits<float>::quiet_NaN();
			RE::FormID form = 0;
			std::vector<std::uint64_t> set;  // sorted; a form set's ids, a text set's keys
			std::uint64_t tick = 0;
			Clock::time_point at{};
			bool bFresh = false;  // set or gathered, and not invalidated since
		};

		struct Resource
		{
			Info info;
			Gatherer* gatherer = nullptr;
			std::unordered_map<RE::FormID, Held> held;  // the actors DCMF tracks, and the world's under 0
		};

		std::shared_mutex g_lock;
		std::map<std::string, Resource, std::less<>> g_resources;
		std::atomic<std::uint64_t> g_tick{ 1 };
		std::atomic<std::thread::id> g_mainThread;

		std::mutex g_listenerLock;
		std::vector<DCMF_API::Resources::IActorListener*> g_listeners;

		std::vector<DCMF_API::Resources::IActorListener*> Listeners()
		{
			std::scoped_lock lock(g_listenerLock);
			return g_listeners;
		}

		bool ValidKey(std::string_view a_key)
		{
			return !a_key.empty() && std::ranges::all_of(a_key, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-'; });
		}

		// Whose value a read or a write is: the world's for a resource for every actor, else the reference's; none for a
		// per-actor resource asked without one
		std::optional<RE::FormID> SubjectOf(Scope a_scope, RE::TESObjectREFR* a_refr)
		{
			if (a_scope == Scope::World) {
				return RE::FormID{ 0 };
			}
			return a_refr ? std::optional<RE::FormID>(a_refr->GetFormID()) : std::nullopt;
		}

		bool Due(const Info& a_info, const Held* a_held)
		{
			if (!a_held || !a_held->bFresh) {
				return true;
			}
			switch (a_info.refresh) {
			case Refresh::EveryTick:
				return a_held->tick != g_tick.load();
			case Refresh::Interval:
				return Clock::now() - a_held->at >= std::chrono::duration<float>(a_info.intervalSeconds);
			default:
				return false;
			}
		}

		Result Set(std::string_view a_key, RE::TESObjectREFR* a_subject, ValueType a_type, const Held& a_value)
		{
			std::unique_lock lock(g_lock);
			const auto it = g_resources.find(a_key);
			if (it == g_resources.end()) {
				return Result::NotFound;
			}
			auto& resource = it->second;
			if (resource.info.type != a_type) {
				return Result::WrongType;
			}
			if (resource.info.refresh != Refresh::Stored) {
				return Result::WrongRefresh;
			}
			if ((resource.info.scope == Scope::Actor) != (a_subject != nullptr)) {
				return Result::WrongScope;
			}
			auto& held = resource.held[a_subject ? a_subject->GetFormID() : 0];
			held = a_value;
			held.bFresh = true;
			return Result::OK;
		}

		// A set's members as DCMF keeps them: form ids as they are, texts by their key
		class SetSink : public DCMF_API::Resources::ISetSink
		{
		public:
			explicit SetSink(std::vector<std::uint64_t>& a_out) :
				_out(a_out) {}

			void AddForm(RE::FormID a_form) noexcept override { _out.push_back(a_form); }
			void AddText(const char* a_text) noexcept override
			{
				if (a_text) {
					_out.push_back(TextKey(a_text));
				}
			}

		private:
			std::vector<std::uint64_t>& _out;
		};

		// What a resource holds for a reference, gathered first when due and this is the main thread; none when no
		// resource of an accepted type has the key
		template <class Accept>
		std::optional<Held> Read(std::string_view a_key, Accept a_accept, RE::TESObjectREFR* a_refr, Info& a_outInfo)
		{
			Gatherer* gatherer = nullptr;
			std::optional<Held> held;
			std::optional<RE::FormID> subject;
			{
				std::shared_lock lock(g_lock);
				const auto it = g_resources.find(a_key);
				if (it == g_resources.end() || !a_accept(it->second.info.type)) {
					return std::nullopt;
				}
				const auto& resource = it->second;
				a_outInfo.type = resource.info.type;
				a_outInfo.scope = resource.info.scope;
				a_outInfo.refresh = resource.info.refresh;
				a_outInfo.intervalSeconds = resource.info.intervalSeconds;
				a_outInfo.defaultNumber = resource.info.defaultNumber;
				a_outInfo.defaultForm = resource.info.defaultForm;
				gatherer = resource.gatherer;
				subject = SubjectOf(resource.info.scope, a_refr);
				if (!subject) {
					return Held{};
				}
				if (const auto found = resource.held.find(*subject); found != resource.held.end()) {
					held = found->second;
				}
			}
			if (gatherer && a_outInfo.refresh != Refresh::Stored && std::this_thread::get_id() == g_mainThread.load() && Due(a_outInfo, held ? &*held : nullptr)) {
				// Read outside the lock: it is another plugin's code
				Held fresh{ .tick = g_tick.load(), .at = Clock::now(), .bFresh = true };
				auto* target = a_outInfo.scope == Scope::World ? nullptr : a_refr;
				switch (a_outInfo.type) {
				case ValueType::Number:
					fresh.number = gatherer->GatherNumber(target);
					break;
				case ValueType::Form:
					{
						auto* form = gatherer->GatherForm(target);
						fresh.form = form ? form->GetFormID() : 0;
						break;
					}
				default:
					{
						SetSink sink(fresh.set);
						gatherer->GatherSet(target, sink);
						std::ranges::sort(fresh.set);
						fresh.set.erase(std::ranges::unique(fresh.set).begin(), fresh.set.end());
						break;
					}
				}
				std::unique_lock lock(g_lock);
				if (const auto it = g_resources.find(a_key); it != g_resources.end()) {
					it->second.held[*subject] = fresh;
				}
				held = fresh;
			}
			return held.value_or(Held{});
		}
	}

	std::string_view TypeName(ValueType a_type)
	{
		switch (a_type) {
		case ValueType::Form:
			return "Form"sv;
		case ValueType::FormSet:
			return "Form set"sv;
		case ValueType::TextSet:
			return "Text set"sv;
		case ValueType::Number:
		default:
			return "Number"sv;
		}
	}

	std::uint64_t TextKey(std::string_view a_text)
	{
		// FNV-1a over the lowercased text
		std::uint64_t hash = 14695981039346656037ull;
		for (const char c : a_text) {
			hash ^= static_cast<std::uint8_t>(std::tolower(static_cast<unsigned char>(c)));
			hash *= 1099511628211ull;
		}
		return hash;
	}

	std::string_view ScopeName(Scope a_scope)
	{
		return a_scope == Scope::World ? "Every actor"sv : "Per actor"sv;
	}

	std::string RefreshName(const Info& a_info)
	{
		switch (a_info.refresh) {
		case Refresh::EveryTick:
			return "Every tick";
		case Refresh::Interval:
			return std::format("Every {:g} s", a_info.intervalSeconds);
		case Refresh::Invalidated:
			return "When invalidated";
		case Refresh::Stored:
		default:
			return "Stored";
		}
	}

	Result Register(Info a_info, Gatherer* a_gatherer)
	{
		if (!ValidKey(a_info.key)) {
			return Result::InvalidKey;
		}
		if (a_info.refresh != Refresh::Stored && !a_gatherer) {
			return Result::NoGatherer;
		}
		std::unique_lock lock(g_lock);
		if (g_resources.contains(a_info.key)) {
			return Result::InvalidKey;
		}
		auto key = a_info.key;
		g_resources.emplace(std::move(key), Resource{ std::move(a_info), a_gatherer, {} });
		return Result::OK;
	}

	bool Exists(std::string_view a_key)
	{
		std::shared_lock lock(g_lock);
		return g_resources.contains(a_key);
	}

	std::vector<Info> List()
	{
		std::shared_lock lock(g_lock);
		std::vector<Info> out;
		out.reserve(g_resources.size());
		for (const auto& [key, resource] : g_resources) {
			out.push_back(resource.info);
		}
		return out;
	}

	std::vector<std::string> FeedKeys(ValueType a_type)
	{
		std::shared_lock lock(g_lock);
		std::vector<std::string> out;
		for (const auto& [key, resource] : g_resources) {
			if (resource.info.bFeed && resource.info.type == a_type) {
				out.push_back(key);
			}
		}
		return out;
	}

	Result SetNumber(std::string_view a_key, RE::TESObjectREFR* a_subject, float a_value)
	{
		return Set(a_key, a_subject, ValueType::Number, Held{ .number = a_value });
	}

	Result SetForm(std::string_view a_key, RE::TESObjectREFR* a_subject, RE::TESForm* a_form)
	{
		return Set(a_key, a_subject, ValueType::Form, Held{ .form = a_form ? a_form->GetFormID() : 0 });
	}

	Result Invalidate(std::string_view a_key, RE::TESObjectREFR* a_subject)
	{
		std::unique_lock lock(g_lock);
		const auto it = g_resources.find(a_key);
		if (it == g_resources.end()) {
			return Result::NotFound;
		}
		auto& resource = it->second;
		if (resource.info.refresh == Refresh::Stored) {
			return Result::WrongRefresh;
		}
		if (!a_subject || resource.info.scope == Scope::World) {
			for (auto& [id, held] : resource.held) {
				held.bFresh = false;
			}
		} else if (const auto found = resource.held.find(a_subject->GetFormID()); found != resource.held.end()) {
			found->second.bFresh = false;
		}
		return Result::OK;
	}

	std::optional<float> GetNumber(std::string_view a_key, RE::TESObjectREFR* a_refr)
	{
		Info info;
		const auto held = Read(a_key, [](ValueType a_type) { return a_type == ValueType::Number; }, a_refr, info);
		if (!held) {
			return std::nullopt;
		}
		return held->bFresh && !std::isnan(held->number) ? held->number : info.defaultNumber;
	}

	RE::TESForm* GetForm(std::string_view a_key, RE::TESObjectREFR* a_refr)
	{
		Info info;
		const auto held = Read(a_key, [](ValueType a_type) { return a_type == ValueType::Form; }, a_refr, info);
		if (!held) {
			return nullptr;
		}
		const auto id = held->bFresh ? held->form : info.defaultForm;
		return id ? RE::TESForm::LookupByID(id) : nullptr;
	}

	bool GetSet(std::string_view a_key, RE::TESObjectREFR* a_refr, std::vector<std::uint64_t>& a_out)
	{
		a_out.clear();
		Info info;
		const auto held = Read(a_key, [](ValueType a_type) { return a_type != ValueType::Number; }, a_refr, info);
		if (!held) {
			return false;
		}
		if (info.type == ValueType::Form) {
			const auto id = held->bFresh ? held->form : info.defaultForm;
			if (id) {
				a_out.push_back(id);
			}
		} else if (held->bFresh) {
			a_out = held->set;
		}
		return true;
	}

	void InvalidateAll()
	{
		std::unique_lock lock(g_lock);
		for (auto& [key, resource] : g_resources) {
			if (resource.info.refresh != Refresh::Stored) {
				resource.held.clear();
			}
		}
	}

	void AddActorListener(DCMF_API::Resources::IActorListener* a_listener)
	{
		std::scoped_lock lock(g_listenerLock);
		g_listeners.push_back(a_listener);
	}

	void OnTracked(RE::TESObjectREFR* a_actor)
	{
		for (auto* listener : Listeners()) {
			listener->OnActorTracked(a_actor);
		}
	}

	void OnUntracked(RE::FormID a_actor)
	{
		{
			std::unique_lock lock(g_lock);
			for (auto& [key, resource] : g_resources) {
				resource.held.erase(a_actor);
			}
		}
		for (auto* listener : Listeners()) {
			listener->OnActorUntracked(a_actor);
		}
	}

	void SetMainThread()
	{
		g_mainThread = std::this_thread::get_id();
	}

	void BeginTick()
	{
		++g_tick;
	}
}

namespace
{
	// One per plugin, so a resource knows who registered it
	class ResourcesInterface : public DCMF_API::Resources::IResourcesInterface
	{
	public:
		explicit ResourcesInterface(std::string a_plugin) :
			_plugin(std::move(a_plugin)) {}

		DCMF_API::Resources::APIResult RegisterResource(const DCMF_API::Resources::ResourceInfo& a_info, DCMF_API::Resources::IGatherer* a_gatherer) noexcept override
		{
			const auto text = [](const char* a_text) { return a_text ? std::string(a_text) : std::string{}; };
			Resources::Info info{ .key = text(a_info.key), .displayName = text(a_info.displayName), .description = text(a_info.description), .plugin = _plugin, .type = a_info.type, .scope = a_info.scope,
				.refresh = a_info.refresh, .intervalSeconds = (std::max)(a_info.intervalSeconds, 0.f), .bFeed = a_info.bFeed, .defaultNumber = a_info.defaultNumber, .defaultForm = a_info.defaultForm };
			const auto key = info.key;
			const auto result = Resources::Register(std::move(info), a_gatherer);
			if (result == DCMF_API::Resources::APIResult::OK) {
				logger::info("resources: {} registered '{}'", _plugin, key);
			} else {
				logger::warn("resources: {} could not register '{}' ({})", _plugin, key, static_cast<std::uint8_t>(result));
			}
			return result;
		}

		DCMF_API::Resources::APIResult SetNumber(const char* a_key, RE::TESObjectREFR* a_subject, float a_value) noexcept override
		{
			return a_key ? Resources::SetNumber(a_key, a_subject, a_value) : DCMF_API::Resources::APIResult::NotFound;
		}

		DCMF_API::Resources::APIResult SetForm(const char* a_key, RE::TESObjectREFR* a_subject, RE::TESForm* a_form) noexcept override
		{
			return a_key ? Resources::SetForm(a_key, a_subject, a_form) : DCMF_API::Resources::APIResult::NotFound;
		}

		DCMF_API::Resources::APIResult Invalidate(const char* a_key, RE::TESObjectREFR* a_subject) noexcept override
		{
			return a_key ? Resources::Invalidate(a_key, a_subject) : DCMF_API::Resources::APIResult::NotFound;
		}

		DCMF_API::Resources::APIResult RegisterCondition(const DCMF_API::Resources::ConditionInfo& a_info) noexcept override
		{
			const auto text = [](const char* a_text) { return a_text ? std::string(a_text) : std::string{}; };
			if (!Resources::Exists(text(a_info.resource))) {
				logger::warn("resources: {} declared condition '{}' on '{}', which is not registered", _plugin, text(a_info.name), text(a_info.resource));
				return DCMF_API::Resources::APIResult::NotFound;
			}
			Conditions::ResourceCondition::Definition definition{ .name = text(a_info.name), .description = text(a_info.description), .resource = text(a_info.resource),
				.operandName = text(a_info.operandName), .plugin = _plugin, .test = a_info.test, .formType = a_info.formType };
			const auto name = definition.name;
			if (!Conditions::AddResourceCondition(std::move(definition))) {
				logger::warn("resources: {} could not add condition '{}': no name, or a condition has it", _plugin, name);
				return DCMF_API::Resources::APIResult::InvalidName;
			}
			logger::info("resources: {} added condition '{}'", _plugin, name);
			return DCMF_API::Resources::APIResult::OK;
		}

		DCMF_API::Resources::APIResult AddActorListener(DCMF_API::Resources::IActorListener* a_listener) noexcept override
		{
			if (a_listener) {
				Resources::AddActorListener(a_listener);
				logger::info("resources: {} listens for tracked actors", _plugin);
			}
			return DCMF_API::Resources::APIResult::OK;
		}

	private:
		std::string _plugin;
	};
}

extern "C" __declspec(dllexport) DCMF_API::Resources::IResourcesInterface* RequestPluginAPI_Resources(const DCMF_API::Resources::InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion)
{
	if (!a_pluginName) {
		logger::warn("resources: RequestPluginAPI_Resources called with no plugin name");
		return nullptr;
	}
	logger::info("resources: RequestPluginAPI_Resources called by {} {}, interface version {}", a_pluginName, a_pluginVersion, static_cast<std::uint8_t>(a_interfaceVersion) + 1);
	if (a_interfaceVersion != DCMF_API::Resources::InterfaceVersion::V1) {
		logger::warn("resources: RequestPluginAPI_Resources asked for an interface version this build does not have");
		return nullptr;
	}
	static std::mutex lock;
	static std::map<std::string, std::unique_ptr<ResourcesInterface>, std::less<>> interfaces;
	std::scoped_lock guard(lock);
	auto& one = interfaces[a_pluginName];
	if (!one) {
		one = std::make_unique<ResourcesInterface>(a_pluginName);
	}
	return one.get();
}

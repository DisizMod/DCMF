#include "API/DCMF-QueryAPI.h"
#include "ActorState.h"
#include "apply/Entries.h"
#include "RegisteredMods.h"

#include <filesystem>

namespace
{
	class QueryInterface : public DCMF_API::Query::IQueryInterface
	{
	public:
		static QueryInterface* GetSingleton() noexcept
		{
			static QueryInterface singleton;
			return std::addressof(singleton);
		}

		bool IsActorTracked(RE::Actor* a_actor) noexcept override
		{
			return StateOf(a_actor) != nullptr;
		}

		bool IsModifierActive(RE::Actor* a_actor, const char* a_modifier) noexcept override
		{
			bool bFound = false;
			ForEachModifier(a_actor, [&](const std::string& a_id) { bFound |= a_modifier && a_id == a_modifier; });
			return bFound;
		}

		bool IsClipPlaying(RE::Actor* a_actor, const char* a_clip) noexcept override
		{
			bool bFound = false;
			ForEachClip(a_actor, [&](const std::string& a_id) { bFound |= a_clip && a_id == a_clip; });
			return bFound;
		}

		void ForEachActiveModifier(RE::Actor* a_actor, DCMF_API::Query::NameCallback a_callback, void* a_userData) noexcept override
		{
			if (a_callback) {
				ForEachModifier(a_actor, [&](const std::string& a_id) { a_callback(a_id.data(), a_userData); });
			}
		}

		void ForEachPlayingClip(RE::Actor* a_actor, DCMF_API::Query::NameCallback a_callback, void* a_userData) noexcept override
		{
			if (a_callback) {
				ForEachClip(a_actor, [&](const std::string& a_id) { a_callback(a_id.data(), a_userData); });
			}
		}

		bool GetNumberChannel(RE::Actor* a_actor, const char* a_channel, float& a_outValue) noexcept override
		{
			return Sample(a_actor, a_channel, [&](const ActorState::Sampled& a_sampled, Apply::ChannelId a_id) {
				const auto& list = a_sampled.numbers[static_cast<std::size_t>(a_id.GetKind())];
				const auto it = std::ranges::find(list, a_id.GetIndex(), &Apply::NumberSample::index);
				if (it == list.end()) {
					return false;
				}
				a_outValue = it->value;
				return true;
			});
		}

		bool GetColourChannel(RE::Actor* a_actor, const char* a_channel, float* a_outRgb) noexcept override
		{
			return a_outRgb && Sample(a_actor, a_channel, [&](const ActorState::Sampled& a_sampled, Apply::ChannelId a_id) {
				const auto& list = a_sampled.colours[static_cast<std::size_t>(a_id.GetKind())];
				const auto it = std::ranges::find(list, a_id.GetIndex(), &Apply::ColourSample::index);
				if (it == list.end()) {
					return false;
				}
				a_outRgb[0] = it->red;
				a_outRgb[1] = it->green;
				a_outRgb[2] = it->blue;
				return true;
			});
		}

		bool GetFormChannel(RE::Actor* a_actor, const char* a_channel, RE::TESForm*& a_outForm) noexcept override
		{
			return Sample(a_actor, a_channel, [&](const ActorState::Sampled& a_sampled, Apply::ChannelId a_id) {
				const auto& list = a_sampled.references[static_cast<std::size_t>(a_id.GetKind())];
				const auto it = std::ranges::find(list, a_id.GetIndex(), &Apply::ReferenceSample::index);
				if (it == list.end()) {
					return false;
				}
				a_outForm = RE::TESForm::LookupByID(static_cast<RE::FormID>(it->value));
				return a_outForm != nullptr;
			});
		}

		void ForEachBodyMorph(RE::Actor* a_actor, DCMF_API::Query::NameCallback a_callback, void* a_userData) noexcept override
		{
			if (const auto parts = PartsOf(a_actor); parts && a_callback) {
				for (const auto& name : parts->bodyMorphs) {
					a_callback(name.data(), a_userData);
				}
			}
		}

		void ForEachFaceMorph(RE::Actor* a_actor, DCMF_API::Query::NameCallback a_callback, void* a_userData) noexcept override
		{
			if (const auto parts = PartsOf(a_actor); parts && a_callback) {
				for (const auto& name : parts->faceMorphs) {
					a_callback(name.data(), a_userData);
				}
			}
		}

		bool HasMorph(RE::Actor* a_actor, const char* a_morph) noexcept override
		{
			const auto parts = PartsOf(a_actor);
			return parts && a_morph && parts->morphs.contains(a_morph);
		}

		void ForEachShape(RE::Actor* a_actor, DCMF_API::Query::ShapeCallback a_callback, void* a_userData) noexcept override
		{
			const auto parts = PartsOf(a_actor);
			if (!parts || !a_callback) {
				return;
			}
			const std::function<void(const ActorState::Part&)> walk = [&](const ActorState::Part& a_part) {
				if (a_part.bGeometry) {
					a_callback(a_part.name.data(), a_part.bHidden, a_part.collision.data(), a_userData);
				}
				for (const auto& child : a_part.children) {
					walk(child);
				}
			};
			for (const auto& root : parts->roots) {
				walk(root);
			}
		}

	private:
		// The actor's state, when DCMF keeps it and has not let it go
		static std::shared_ptr<ActorState::State> StateOf(RE::Actor* a_actor)
		{
			auto state = a_actor ? ActorState::Get(a_actor->GetHandle()) : nullptr;
			return state && !state->bRetired.load() ? state : nullptr;
		}

		static std::shared_ptr<const ActorState::Parts> PartsOf(RE::Actor* a_actor)
		{
			const auto state = StateOf(a_actor);
			return state ? state->parts.With([](const auto& a_parts) { return a_parts; }) : nullptr;
		}

		// "mod:name", the mod's folder name and the clip or modifier's
		static std::string IdOf(const SubMod* a_subMod)
		{
			const auto* mod = a_subMod ? a_subMod->GetParentMod() : nullptr;
			return mod ? std::format("{}:{}", std::filesystem::path(mod->GetPath()).filename().string(), a_subMod->GetName()) : std::string{};
		}

		// Highest priority first, as the resolver keeps them
		template <class F>
		static void ForEachModifier(RE::Actor* a_actor, F&& a_function)
		{
			const auto state = StateOf(a_actor);
			const auto resolved = state ? state->resolved.With([](const auto& a_resolved) { return a_resolved; }) : nullptr;
			if (resolved) {
				for (const auto& active : resolved->active) {
					a_function(IdOf(active.subMod));
				}
			}
		}

		// Highest priority first; the clip player keeps them lowest first
		template <class F>
		static void ForEachClip(RE::Actor* a_actor, F&& a_function)
		{
			const auto state = StateOf(a_actor);
			if (!state) {
				return;
			}
			std::vector<std::string> ids;
			state->clips.With([&](const ActorState::ClipState& a_clips) {
				for (auto it = a_clips.running.rbegin(); it != a_clips.running.rend(); ++it) {
					ids.push_back(IdOf(it->clip));
				}
			});
			for (const auto& id : ids) {
				a_function(id);
			}
		}

		// This frame's sample of the channel on the actor, read by a_read
		template <class F>
		static bool Sample(RE::Actor* a_actor, const char* a_channel, F&& a_read)
		{
			if (!a_channel) {
				return false;
			}
			const auto id = Apply::Entries::GetSingleton().Find(std::string_view(a_channel));
			const auto state = StateOf(a_actor);
			if (!id.IsValid() || !state) {
				return false;
			}
			const auto sampled = state->sampled.With([](const auto& a_sampled) { return a_sampled; });
			return sampled && a_read(*sampled, id);
		}
	};
}

extern "C" __declspec(dllexport) DCMF_API::Query::IQueryInterface* RequestPluginAPI_Query(const DCMF_API::Query::InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion)
{
	if (!a_pluginName) {
		logger::warn("query: RequestPluginAPI_Query called with no plugin name");
		return nullptr;
	}
	logger::info("query: RequestPluginAPI_Query called by {} {}, interface version {}", a_pluginName, a_pluginVersion, static_cast<std::uint8_t>(a_interfaceVersion) + 1);
	if (a_interfaceVersion == DCMF_API::Query::InterfaceVersion::V1) {
		return QueryInterface::GetSingleton();
	}
	logger::warn("query: RequestPluginAPI_Query asked for an interface version this build does not have");
	return nullptr;
}

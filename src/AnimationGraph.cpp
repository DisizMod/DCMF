#include "AnimationGraph.h"

#include "BuiltinResources.h"
#include "Resources.h"

#include "ClipPlayer.h"
#include "ModRegistry.h"
#include "RegisteredMods.h"
#include "SharedTypes.h"
#include "Triggers.h"
#include "UI/UICommon.h"
#include "UI/UIManager.h"
#include "Utils.h"

#include <rapidjson/document.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// OAR's animations interface, as its API header declares it
namespace OAR_API::Animations
{
	enum class InterfaceVersion : uint8_t
	{
		V1
	};

	struct ReplacementAnimationInfo
	{
		RE::BSString animationPath{};
		RE::BSString projectName{};
		RE::BSString variantFilename{};
		RE::BSString subModName{};
		RE::BSString modName{};
	};

	class IAnimationsInterface1
	{
	public:
		[[nodiscard]] virtual ReplacementAnimationInfo GetCurrentReplacementAnimationInfo(RE::hkbClipGenerator* a_clipGenerator) noexcept = 0;
		virtual void ClearConditionStateData(RE::hkbClipGenerator* a_clipGenerator) noexcept = 0;
		virtual void ClearConditionStateData(RE::TESObjectREFR* a_refr) noexcept = 0;
	};

	using _RequestPluginAPI_Animations = IAnimationsInterface1* (*)(InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion);
}

namespace AnimationGraph
{
	namespace
	{
		constexpr std::size_t kActivateIndex = 0x4;
		constexpr std::size_t kDeactivateIndex = 0x7;

		using Activate_t = void(RE::hkbClipGenerator*, const RE::hkbContext&);
		Activate_t* g_activate = nullptr;
		Activate_t* g_deactivate = nullptr;

		OAR_API::Animations::IAnimationsInterface1* g_oar = nullptr;

		// A clip playing on an actor: which graph it is in, so a graph since torn down is told apart
		struct Active
		{
			const RE::hkbClipGenerator* clip = nullptr;
			const RE::BShkbAnimationGraph* graph = nullptr;
			std::string file;
			std::string replacer;
		};

		// A clip deactivated, held back a couple of frames: OAR swapping a replacer mid-clip, for a variant on a loop or
		// on an echo, deactivates and activates the same clip at once, which is neither an end nor a start
		struct Ending
		{
			Active active;
			RE::ActorHandle actor;
			std::uint32_t frame = 0;
		};

		std::mutex g_lock;
		std::unordered_map<RE::FormID, std::vector<Active>> g_active;  // by the actor's form id
		std::unordered_map<RE::FormID, std::vector<std::string>> g_recent;  // the last files each actor started, newest first
		constexpr std::size_t kRecent = 5;
		std::vector<Ending> g_ending;
		std::atomic<std::uint32_t> g_frame = 0;

		RE::BShkbAnimationGraph* GraphOf(const RE::hkbContext& a_context)
		{
			return a_context.character ? SKSE::stl::adjust_pointer<RE::BShkbAnimationGraph>(a_context.character, -0xC0) : nullptr;
		}

		// Under g_lock: a file the actor started, on top of its last ones
		void NoteRecent(RE::FormID a_actor, const std::string& a_file)
		{
			auto& recent = g_recent[a_actor];
			std::erase(recent, a_file);
			recent.insert(recent.begin(), a_file);
			if (recent.size() > kRecent) {
				recent.pop_back();
			}
		}

		// "Mod / Submod" when OAR plays a replacer for the clip, and " / variant.hkx" after it when that is one of a
		// variants folder's; nothing when it plays the file itself
		std::string ReplacerOf(RE::hkbClipGenerator* a_clip)
		{
			if (!g_oar) {
				return {};
			}
			const auto info = g_oar->GetCurrentReplacementAnimationInfo(a_clip);
			if (info.subModName.empty()) {
				return {};
			}
			auto out = std::format("{} / {}", info.modName.c_str(), info.subModName.c_str());
			if (!info.variantFilename.empty()) {
				out += " / " + std::filesystem::path(info.variantFilename.c_str()).filename().string();
			}
			return out;
		}

		// The file a clip plays: its binding's entry in the actor's own character data, so a female actor's clip is its
		// female file; the behavior's clip name, shared by every project, only when the binding is not there
		std::string FileOf(RE::hkbClipGenerator* a_clip, RE::BShkbAnimationGraph* a_graph, std::uint16_t a_binding)
		{
			auto* setup = a_graph->characterInstance.setup.get();
			auto* data = setup ? setup->data.get() : nullptr;
			const auto* strings = data ? data->stringData.get() : nullptr;
			if (strings && a_binding < strings->animationNames.size() && strings->animationNames[a_binding].c_str()) {
				return Components::AnimationValue::Normalize(strings->animationNames[a_binding].c_str());
			}
			return a_clip->animationName.c_str() ? Components::AnimationValue::Normalize(a_clip->animationName.c_str()) : std::string{};
		}

		// Behavior threads: the binding read before OAR swaps it for its replacer's, the replacer once it has
		void ActivateHook(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context)
		{
			const auto binding = a_this->animationBindingIndex;
			g_activate(a_this, a_context);
			auto* graph = GraphOf(a_context);
			auto* actor = graph ? graph->holder : nullptr;
			if (!actor) {
				return;
			}
			Active active{ a_this, graph, {}, ReplacerOf(a_this) };
			bool bSwapped = false;
			{
				std::scoped_lock guard(g_lock);
				// Deactivated a moment ago: OAR swapping its replacer, the binding already the new one's, so the file
				// is the one it had; the run goes on, nothing starts
				if (const auto it = std::ranges::find(g_ending, static_cast<const RE::hkbClipGenerator*>(a_this), [](const Ending& a_ending) { return a_ending.active.clip; }); it != g_ending.end()) {
					active.file = it->active.file;
					g_ending.erase(it);
					bSwapped = true;
				} else {
					active.file = FileOf(a_this, graph, binding);
				}
				if (active.file.empty()) {
					return;
				}
				auto& list = g_active[actor->GetFormID()];
				std::erase_if(list, [&](const Active& a_entry) { return a_entry.clip == a_this; });
				list.push_back(active);
				if (!bSwapped) {
					NoteRecent(actor->GetFormID(), active.file);
				}
			}
			if (!bSwapped) {
				ClipPlayer::QueueAnimation(actor, ClipPlayer::Event::kAnimationStart, active.file, active.replacer);
			}
		}

		// Behavior threads: a clip gone from the graph, held back rather than queued: Flush sends it once no activation of
		// the same clip took it
		void Deactivated(const RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context)
		{
			auto* graph = GraphOf(a_context);
			auto* actor = graph ? graph->holder : nullptr;
			if (!actor) {
				return;
			}
			std::scoped_lock guard(g_lock);
			if (const auto it = g_active.find(actor->GetFormID()); it != g_active.end()) {
				if (const auto found = std::ranges::find(it->second, a_this, &Active::clip); found != it->second.end()) {
					g_ending.push_back({ std::move(*found), actor->GetHandle(), g_frame.load() });
					it->second.erase(found);
				}
			}
		}

		void DeactivateHook(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context)
		{
			g_deactivate(a_this, a_context);
			Deactivated(a_this, a_context);
		}

		// The event names some clip's OnAnimationEvent listens for, lowercase; read on the behavior threads, rebuilt on the main
		std::atomic<std::shared_ptr<const std::unordered_set<std::string>>> g_listened;

		std::unordered_set<std::string> Listened()
		{
			std::unordered_set<std::string> out;
			const auto collect = [&](std::unique_ptr<Triggers::TriggerBase>& a_trigger) {
				if (auto* graphEvent = dynamic_cast<Triggers::OnAnimationEventTrigger*>(a_trigger.get()); graphEvent && !graphEvent->IsDisabled()) {
					auto name = std::string(graphEvent->event->value.GetValue());
					std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
					if (!name.empty()) {
						out.insert(std::move(name));
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			};
			ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
				a_mod->ForEachSubMod([&](SubMod* a_subMod) {
					if (a_subMod->GetKind() == SubModKind::kClip) {
						static_cast<Clip*>(a_subMod)->GetTriggerSet()->ForEach(collect);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				a_mod->ForEachTriggerPreset([&](Triggers::TriggerPreset* a_preset) {
					a_preset->ForEach(collect);
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			});
			return out;
		}

		// The actors' sink for their graphs' events: one listened for queued for the actor, if tracked
		using ProcessEvent_t = RE::BSEventNotifyControl(RE::BSTEventSink<RE::BSAnimationGraphEvent>*, const RE::BSAnimationGraphEvent*, RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
		ProcessEvent_t* g_characterEvent = nullptr;
		ProcessEvent_t* g_playerEvent = nullptr;

		void Heard(const RE::BSAnimationGraphEvent* a_event)
		{
			if (!a_event || !a_event->holder || a_event->tag.empty()) {
				return;
			}
			// What the actor's animation decides is read again: attacking, blocking, sneaking, in the air and the like
			Resources::InvalidateEvent(BuiltinResources::Event::kAnimation, const_cast<RE::TESObjectREFR*>(a_event->holder));
			const auto listened = g_listened.load();
			if (!listened || listened->empty()) {
				return;
			}
			std::string name(a_event->tag.c_str());
			std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			if (listened->contains(name)) {
				ClipPlayer::QueueAnimation(a_event->holder, ClipPlayer::Event::kAnimationEvent, std::move(name), {});
			}
		}

		RE::BSEventNotifyControl CharacterEventHook(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this, const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
		{
			Heard(a_event);
			return g_characterEvent(a_this, a_event, a_source);
		}

		RE::BSEventNotifyControl PlayerEventHook(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this, const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
		{
			Heard(a_event);
			return g_playerEvent(a_this, a_event, a_source);
		}

		// The graph the actor animates with now
		RE::BShkbAnimationGraph* CurrentGraph(RE::TESObjectREFR* a_refr)
		{
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
			if (!a_refr || !a_refr->GetAnimationGraphManager(manager) || !manager || manager->graphs.size() == 0) {
				return nullptr;
			}
			const auto index = manager->GetRuntimeData().activeGraph;
			return index < manager->graphs.size() ? manager->graphs[index].get() : nullptr;
		}

		// The ref's clips still in one of its graphs; a clip of a graph torn down without deactivating is dropped
		std::vector<Active> ActiveOn(RE::TESObjectREFR* a_refr)
		{
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
			if (!a_refr || !a_refr->GetAnimationGraphManager(manager) || !manager) {
				return {};
			}
			std::scoped_lock guard(g_lock);
			const auto it = g_active.find(a_refr->GetFormID());
			if (it == g_active.end()) {
				return {};
			}
			std::erase_if(it->second, [&](const Active& a_entry) {
				return std::ranges::none_of(manager->graphs, [&](const auto& a_graph) { return a_graph.get() == a_entry.graph; });
			});
			return it->second;
		}

		// The animation files a graph can play, as its character data lists them
		const RE::hkbCharacterStringData* StringsOf(RE::BShkbAnimationGraph* a_graph)
		{
			auto* setup = a_graph ? a_graph->characterInstance.setup.get() : nullptr;
			auto* data = setup ? setup->data.get() : nullptr;
			return data ? data->stringData.get() : nullptr;
		}

		std::vector<std::string> ReferenceFiles()
		{
			std::vector<std::string> out;
			const auto* strings = StringsOf(CurrentGraph(UI::UIManager::GetSingleton().GetRefrToEvaluate()));
			if (!strings) {
				return out;
			}
			out.reserve(strings->animationNames.size());
			// OAR appends its replacers' files to the list; those are picked as a replacer, not as a file
			for (const auto& name : strings->animationNames) {
				if (!name.c_str()) {
					continue;
				}
				auto file = Components::AnimationValue::Normalize(name.c_str());
				if (file.find("openanimationreplacer\\") == std::string::npos && file.find("dynamicanimationreplacer\\") == std::string::npos) {
					out.push_back(std::move(file));
				}
			}
			return out;
		}

		// The picker draws a group once, so each group's items together; an item's value is its place, which the text value reads back
		void SortByGroup(std::vector<UI::UICommon::PickerItem>& a_items)
		{
			std::ranges::sort(a_items, [](const auto& a, const auto& b) { return a.group != b.group ? a.group < b.group : a.label < b.label; });
			for (std::size_t i = 0; i < a_items.size(); ++i) {
				a_items[i].value = i;
			}
		}

		// A config.json's name, or the folder's own when it has none
		std::string ConfigName(const std::filesystem::path& a_folder)
		{
			std::ifstream stream(a_folder / "config.json");
			if (stream) {
				std::stringstream buffer;
				buffer << stream.rdbuf();
				rapidjson::Document document;
				document.Parse(buffer.str().c_str());
				if (!document.HasParseError() && document.IsObject()) {
					if (const auto it = document.FindMember("name"); it != document.MemberEnd() && it->value.IsString()) {
						return it->value.GetString();
					}
				}
			}
			return a_folder.filename().string();
		}

		// The OAR folder of the actor's project: its behavior file's folder, the replacers under its animations
		std::filesystem::path ReplacerRoot(RE::Actor* a_actor)
		{
			auto* race = a_actor ? a_actor->GetRace() : nullptr;
			auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
			if (!race || !base) {
				return {};
			}
			const auto sex = base->GetSex() == RE::SEX::kFemale ? RE::SEXES::kFemale : RE::SEXES::kMale;
			const char* model = race->behaviorGraphs[sex].GetModel();
			if (!model || !*model) {
				return {};
			}
			return std::filesystem::path("Data\\meshes") / std::filesystem::path(model).parent_path() / "animations" / "OpenAnimationReplacer";
		}

		// Whether two files hold the same bytes
		bool SameBytes(const std::filesystem::path& a_one, const std::filesystem::path& a_other)
		{
			std::error_code error;
			const auto size = std::filesystem::file_size(a_one, error);
			if (error || std::filesystem::file_size(a_other, error) != size || error) {
				return false;
			}
			std::ifstream one(a_one, std::ios::binary);
			std::ifstream other(a_other, std::ios::binary);
			if (!one || !other) {
				return false;
			}
			std::vector<char> left(64 * 1024);
			std::vector<char> right(64 * 1024);
			while (one && other) {
				one.read(left.data(), static_cast<std::streamsize>(left.size()));
				other.read(right.data(), static_cast<std::streamsize>(right.size()));
				if (one.gcount() != other.gcount() || !std::equal(left.begin(), left.begin() + one.gcount(), right.begin())) {
					return false;
				}
			}
			return true;
		}

		// The file an OAR replacer, "Mod / Submod" or one of its variants, plays for the animation, as the graph's list
		// names it from the animations folder; empty when it has none
		std::string ReplacerFile(RE::Actor* a_actor, std::string_view a_file, std::string_view a_replacer)
		{
			const auto root = ReplacerRoot(a_actor);
			if (root.empty() || a_file.empty()) {
				return {};
			}
			const std::filesystem::path relative(a_file);
			const auto variants = relative.parent_path() / ("_variants_" + relative.stem().string());
			std::error_code error;
			for (const auto& mod : std::filesystem::directory_iterator(root, error)) {
				if (!mod.is_directory()) {
					continue;
				}
				const auto modName = ConfigName(mod.path());
				for (const auto& subMod : std::filesystem::directory_iterator(mod.path(), error)) {
					if (!subMod.is_directory()) {
						continue;
					}
					const auto name = std::format("{} / {}", modName, ConfigName(subMod.path()));
					std::filesystem::path found;
					if (Utils::CompareStringsIgnoreCase(a_replacer, std::string_view(name))) {
						// The submod: its own file, or the first of its variants
						if (std::filesystem::exists(subMod.path() / relative, error)) {
							found = subMod.path() / relative;
						} else {
							for (const auto& variant : std::filesystem::directory_iterator(subMod.path() / variants, error)) {
								if (variant.is_regular_file() && Utils::CompareStringsIgnoreCase(variant.path().extension().string(), ".hkx"sv)) {
									found = variant.path();
									break;
								}
							}
						}
					} else if (a_replacer.size() > name.size() + 3 && Utils::CompareStringsIgnoreCase(a_replacer.substr(0, name.size()), std::string_view(name)) && a_replacer.substr(name.size(), 3) == " / "sv) {
						found = subMod.path() / variants / std::string(a_replacer.substr(name.size() + 3));
						if (!std::filesystem::exists(found, error)) {
							found.clear();
						}
					}
					if (!found.empty()) {
						return Components::AnimationValue::Normalize(found.lexically_relative(root.parent_path()).string());
					}
				}
			}
			return {};
		}

		// Every OAR submod of the reference's project with a file or variants for this animation, "Mod / Submod"
		std::vector<UI::UICommon::PickerItem> ReplacerItems(std::string_view a_file)
		{
			std::vector<UI::UICommon::PickerItem> out;
			auto* actor = UI::UIManager::GetSingleton().GetRefrToEvaluate() ? UI::UIManager::GetSingleton().GetRefrToEvaluate()->As<RE::Actor>() : nullptr;
			const auto file = Components::AnimationValue::Normalize(a_file);
			const auto root = ReplacerRoot(actor);
			if (root.empty() || file.empty()) {
				return out;
			}
			const std::filesystem::path relative(file);
			const auto variants = relative.parent_path() / ("_variants_" + relative.stem().string());
			std::error_code error;
			for (const auto& mod : std::filesystem::directory_iterator(root, error)) {
				if (!mod.is_directory()) {
					continue;
				}
				const auto modName = ConfigName(mod.path());
				for (const auto& subMod : std::filesystem::directory_iterator(mod.path(), error)) {
					if (!subMod.is_directory()) {
						continue;
					}
					const auto name = std::format("{} / {}", modName, ConfigName(subMod.path()));
					if (std::filesystem::exists(subMod.path() / relative, error)) {
						out.push_back({ name, modName });
					} else if (std::filesystem::is_directory(subMod.path() / variants, error)) {
						// A variants folder: the submod whichever plays, then each of its files, under a group of their own
						out.push_back({ name, name });
						for (const auto& variant : std::filesystem::directory_iterator(subMod.path() / variants, error)) {
							if (variant.is_regular_file() && Utils::CompareStringsIgnoreCase(variant.path().extension().string(), ".hkx"sv)) {
								out.push_back({ std::format("{} / {}", name, variant.path().filename().string()), name });
							}
						}
					}
				}
			}
			SortByGroup(out);
			return out;
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_hkbClipGenerator[0] };
		g_activate = reinterpret_cast<Activate_t*>(vtable.write_vfunc(kActivateIndex, ActivateHook));
		g_deactivate = reinterpret_cast<Activate_t*>(vtable.write_vfunc(kDeactivateIndex, DeactivateHook));
		logger::info("animations: hooked hkbClipGenerator::Activate and Deactivate");

		// Every actor's graph events, at the sink the engine hands them to
		REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[2] };
		g_characterEvent = reinterpret_cast<ProcessEvent_t*>(character.write_vfunc(0x1, CharacterEventHook));
		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[2] };
		g_playerEvent = reinterpret_cast<ProcessEvent_t*>(player.write_vfunc(0x1, PlayerEventHook));
		logger::info("animations: hooked the actors' animation graph event sink");

		// Which replacer plays a clip, when OAR is loaded
		if (const auto module = GetModuleHandleA("OpenAnimationReplacer.dll")) {
			if (const auto request = reinterpret_cast<OAR_API::Animations::_RequestPluginAPI_Animations>(GetProcAddress(module, "RequestPluginAPI_Animations"))) {
				const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
				g_oar = request(OAR_API::Animations::InterfaceVersion::V1, plugin->GetName().data(), plugin->GetVersion());
			}
		}
		logger::info("animations: OAR's animations interface {}", g_oar ? "found" : "not found; replacers are not told apart");
	}

	void Flush()
	{
		// Two frames old at least, so a swap still between its deactivation and its activation on another thread is not
		// taken for an end
		const auto frame = ++g_frame;

		// The event names listened for, again about once a second, so an edit or a mod reloaded is heard
		if (frame % 60 == 1) {
			g_listened.store(std::make_shared<const std::unordered_set<std::string>>(Listened()));
		}
		std::vector<Ending> ended;
		{
			std::scoped_lock guard(g_lock);
			const auto split = std::stable_partition(g_ending.begin(), g_ending.end(), [&](const Ending& a_ending) { return frame - a_ending.frame < 2; });
			std::move(split, g_ending.end(), std::back_inserter(ended));
			g_ending.erase(split, g_ending.end());
		}
		for (auto& ending : ended) {
			if (const auto actor = ending.actor.get()) {
				ClipPlayer::QueueAnimation(actor.get(), ClipPlayer::Event::kAnimationEnd, std::move(ending.active.file), std::move(ending.active.replacer));
			}
		}
	}

	bool IsPlaying(RE::TESObjectREFR* a_refr, const Components::AnimationValue& a_value)
	{
		return std::ranges::any_of(ActiveOn(a_refr), [&](const Active& a_active) { return a_value.Matches(a_active.file, a_active.replacer); });
	}

	std::vector<std::string> PlayingOn(RE::TESObjectREFR* a_refr)
	{
		std::vector<std::string> out;
		for (const auto& active : ActiveOn(a_refr)) {
			out.push_back(active.replacer.empty() ? active.file : std::format("{} as {}", active.file, active.replacer));
		}
		return out;
	}

	std::vector<std::string> RecentOn(RE::TESObjectREFR* a_refr)
	{
		if (!a_refr) {
			return {};
		}
		std::scoped_lock guard(g_lock);
		const auto it = g_recent.find(a_refr->GetFormID());
		return it != g_recent.end() ? it->second : std::vector<std::string>{};
	}

	void SetPickers(Components::AnimationValue& a_value)
	{
		// The reference's files, grouped by their folder, the ones it started last on top; said to be missing from it,
		// or that there is no reference
		a_value.file.SetPickerItems([] {
			std::vector<UI::UICommon::PickerItem> out;
			for (auto& file : ReferenceFiles()) {
				const auto slash = file.rfind('\\');
				std::string group = slash == std::string::npos ? std::string("animations") : file.substr(0, slash);
				out.push_back({ std::move(file), std::move(group) });
			}
			SortByGroup(out);
			std::vector<UI::UICommon::PickerItem> recent;
			if (!UI::UIManager::GetSingleton().GetRefrToEvaluate()) {
				recent.push_back({ "(no reference selected)", "Recently played", 0, {}, {}, true });
			}
			for (auto& file : RecentOn(UI::UIManager::GetSingleton().GetRefrToEvaluate())) {
				recent.push_back({ std::move(file), "Recently played" });
			}
			if (recent.empty()) {
				recent.push_back({ "(nothing)", "Recently played", 0, {}, {}, true });
			}
			out.insert(out.begin(), std::make_move_iterator(recent.begin()), std::make_move_iterator(recent.end()));
			// Each item's value its place again, the recent ones counted
			for (std::size_t i = 0; i < out.size(); ++i) {
				out[i].value = i;
			}
			return out;
		},
			[](std::string_view a_file) -> std::string {
				auto* graph = CurrentGraph(UI::UIManager::GetSingleton().GetRefrToEvaluate());
				const auto* strings = StringsOf(graph);
				if (!strings) {
					return "(No reference)";
				}
				// Drawn every frame: compared as the names are, without a list built
				const auto wanted = Components::AnimationValue::Normalize(a_file);
				for (const auto& name : strings->animationNames) {
					if (name.c_str() && Components::AnimationValue::Normalize(name.c_str()) == wanted) {
						return graph->projectName.c_str();
					}
				}
				return {};
			});
		a_value.replacer.SetPickerItems([value = &a_value] { return ReplacerItems(value->file.GetValue()); },
			[](std::string_view a_replacer) -> std::string { return a_replacer.empty() ? std::string{} : std::string("Replacer"); });
	}

	RE::BShkbAnimationGraph* GraphOn(RE::TESObjectREFR* a_refr)
	{
		return CurrentGraph(a_refr);
	}

	std::optional<Binding> BindingOf(RE::TESObjectREFR* a_refr, const Components::AnimationValue& a_value, std::string* a_outWhy)
	{
		const auto fail = [&](std::string_view a_why) -> std::optional<Binding> {
			if (a_outWhy) {
				*a_outWhy = a_why;
			}
			return std::nullopt;
		};
		auto* graph = CurrentGraph(a_refr);
		const auto* strings = StringsOf(graph);
		if (!strings) {
			return fail("The actor has no behavior graph loaded.");
		}
		const auto file = Components::AnimationValue::Normalize(a_value.file.GetValue());
		if (file.empty()) {
			return fail("No animation picked.");
		}
		// The file itself, or the replacer's own, which the list names from the game's folder: matched by its end
		std::string wanted = file;
		const bool bReplacer = a_value.bReplacer && !a_value.replacer.GetValue().empty();
		if (bReplacer) {
			wanted = ReplacerFile(a_refr ? a_refr->As<RE::Actor>() : nullptr, file, a_value.replacer.GetValue());
			if (wanted.empty()) {
				return fail("The replacer has no file for this animation in the actor's project.");
			}
		}
		for (std::int32_t i = 0; i < static_cast<std::int32_t>(strings->animationNames.size()); ++i) {
			const char* name = strings->animationNames[i].c_str();
			if (!name) {
				continue;
			}
			const auto entry = Components::AnimationValue::Normalize(name);
			const bool bMatch = bReplacer ? entry.ends_with(wanted) && (entry.size() == wanted.size() || entry[entry.size() - wanted.size() - 1] == '\\') : entry == wanted;
			if (bMatch) {
				return Binding{ graph, static_cast<std::uint16_t>(i) };
			}
		}
		// OAR keeps one of identical files, the first submod's: the replacer's file is that entry when their bytes are equal
		if (bReplacer) {
			const auto own = ReplacerRoot(a_refr ? a_refr->As<RE::Actor>() : nullptr).parent_path() / wanted;
			const auto name = std::filesystem::path(wanted).filename().string();
			for (std::int32_t i = 0; i < static_cast<std::int32_t>(strings->animationNames.size()); ++i) {
				const char* entry = strings->animationNames[i].c_str();
				if (!entry) {
					continue;
				}
				const auto normalized = Components::AnimationValue::Normalize(entry);
				if (normalized.find("openanimationreplacer\\") != std::string::npos && normalized.ends_with("\\" + name) && SameBytes(own, std::filesystem::path(entry))) {
					return Binding{ graph, static_cast<std::uint16_t>(i) };
				}
			}
		}
		return fail(bReplacer ? "The actor's graph does not list the replacer's file." : "The actor's graph does not list this file.");
	}

	void Started(const RE::hkbContext& a_context, const void* a_clip, std::string a_file, std::string a_replacer)
	{
		auto* graph = GraphOf(a_context);
		auto* actor = graph ? graph->holder : nullptr;
		if (!actor || a_file.empty()) {
			return;
		}
		Active active{ static_cast<const RE::hkbClipGenerator*>(a_clip), graph, std::move(a_file), std::move(a_replacer) };
		{
			std::scoped_lock guard(g_lock);
			auto& list = g_active[actor->GetFormID()];
			std::erase_if(list, [&](const Active& a_entry) { return a_entry.clip == active.clip; });
			list.push_back(active);
			NoteRecent(actor->GetFormID(), active.file);
		}
		ClipPlayer::QueueAnimation(actor, ClipPlayer::Event::kAnimationStart, active.file, active.replacer);
	}

	void Ended(const RE::hkbContext& a_context, const void* a_clip)
	{
		Deactivated(static_cast<const RE::hkbClipGenerator*>(a_clip), a_context);
	}

	void SetEventPicker(Components::TextValue& a_value)
	{
		// The reference's graph's event names, grouped by what comes before a dot, the rest under Events
		const auto eventNames = [](RE::BShkbAnimationGraph* a_graph) -> const RE::hkArray<RE::hkStringPtr>* {
			auto* behavior = a_graph ? a_graph->behaviorGraph : nullptr;
			auto* data = behavior ? behavior->data.get() : nullptr;
			auto* strings = data ? data->stringData.get() : nullptr;
			return strings ? &strings->eventNames : nullptr;
		};
		a_value.SetPickerItems([eventNames] {
			std::vector<UI::UICommon::PickerItem> out;
			if (const auto* names = eventNames(CurrentGraph(UI::UIManager::GetSingleton().GetRefrToEvaluate()))) {
				for (const auto& name : *names) {
					if (!name.c_str() || !*name.c_str()) {
						continue;
					}
					std::string label(name.c_str());
					const auto dot = label.find('.');
					out.push_back({ label, dot == std::string::npos ? std::string("Events") : label.substr(0, dot) });
				}
			}
			SortByGroup(out);
			return out;
		},
			[eventNames](std::string_view a_name) -> std::string {
				auto* graph = CurrentGraph(UI::UIManager::GetSingleton().GetRefrToEvaluate());
				const auto* names = eventNames(graph);
				if (!names) {
					return "(No reference)";
				}
				for (const auto& name : *names) {
					if (name.c_str() && Utils::CompareStringsIgnoreCase(std::string_view(name.c_str()), a_name)) {
						return graph->projectName.c_str();
					}
				}
				return {};
			});
	}
}

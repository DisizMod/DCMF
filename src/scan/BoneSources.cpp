#include "Scan.h"

#include "Nif.h"

#include <bit>
#include <set>

namespace Scan
{
	namespace
	{
		std::string MeshPath(const char* a_model)
		{
			if (!a_model || !*a_model) {
				return {};
			}
			auto path = Files::Normalize(a_model);
			return path.starts_with("meshes/") ? path : "meshes/" + path;
		}

		// The folder a skeleton model sits in, which names it: "character assets", "argonian"
		std::string FolderOf(std::string_view a_model)
		{
			const auto slash = a_model.rfind('/');
			if (slash == std::string_view::npos) {
				return std::string(a_model);
			}
			const auto folder = a_model.substr(0, slash);
			const auto start = folder.rfind('/');
			return std::string(start == std::string_view::npos ? folder : folder.substr(start + 1));
		}

		std::string_view HeadPartKind(RE::BGSHeadPart::HeadPartType a_type)
		{
			using Type = RE::BGSHeadPart::HeadPartType;
			switch (a_type) {
			case Type::kFace:
				return "Face"sv;
			case Type::kEyes:
				return "Eyes"sv;
			case Type::kHair:
				return "Hair"sv;
			case Type::kFacialHair:
				return "Beard"sv;
			case Type::kScar:
				return "Scar"sv;
			case Type::kEyebrows:
				return "Brows"sv;
			default:
				return "Head part"sv;
			}
		}

		// An armour addon's first biped slot, by the engine's name where it has one
		std::string SlotOf(const RE::TESObjectARMA* a_addon)
		{
			const auto mask = static_cast<std::uint32_t>(a_addon->GetSlotMask().underlying());
			if (mask == 0) {
				return {};
			}
			const auto bit = static_cast<std::size_t>(std::countr_zero(mask));
			constexpr const char* kNames[]{ "Head", "Hair", "Body", "Hands", "Forearms", "Amulet", "Ring", "Feet", "Calves", "Shield", "Tail", "Long hair", "Circlet", "Ears" };
			return bit < std::size(kNames) ? std::format("{} ({})", kNames[bit], bit + 30) : std::format("Slot {}", bit + 30);
		}

		std::string LabelOf(const RE::TESForm* a_form, const char* a_name)
		{
			if (a_name && *a_name) {
				return a_name;
			}
			const char* editorID = a_form->GetFormEditorID();
			return editorID && *editorID ? editorID : std::format("{:08X}", a_form->GetFormID());
		}

		// The meshes that bring bones: every race's skeleton, every head part's model, every armour addon's models
		class BoneRecords : public ISource
		{
		public:
			std::string_view GetName() const override { return "Bone records"sv; }
			Target GetTarget() const override { return Target::kBones; }
			Stage GetStage() const override { return Stage::kRecords; }

			void Run(Context& a_context) override
			{
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					a_context.Report(""sv, "no data handler");
					return;
				}

				const auto& races = dataHandler->GetFormArray<RE::TESRace>();
				const auto& parts = dataHandler->GetFormArray<RE::BGSHeadPart>();
				const auto& addons = dataHandler->GetFormArray<RE::TESObjectARMA>();
				a_context.progress.total = races.size() + parts.size() + addons.size();

				std::set<std::tuple<std::string, std::string, std::string, std::string>> seen;
				const auto add = [&](std::string a_model, std::string_view a_kind, std::string a_record, std::string a_slot) {
					if (a_model.empty() || !seen.emplace(a_model, std::string(a_kind), a_record, a_slot).second) {
						return;
					}
					a_context.out.boneSources.push_back({ std::move(a_model), std::string(a_kind), std::move(a_record), std::move(a_slot) });
				};

				for (auto* race : races) {
					++a_context.progress.done;
					if (!race) {
						continue;
					}
					for (int sex = 0; sex < 2; ++sex) {
						const auto model = MeshPath(race->skeletonModels[sex].GetModel());
						add(model, "Skeleton"sv, FolderOf(model), {});
					}
				}

				for (auto* part : parts) {
					++a_context.progress.done;
					if (part) {
						add(MeshPath(part->GetModel()), HeadPartKind(part->type.get()), LabelOf(part, nullptr), {});
					}
				}

				// An addon is named by the first armour that uses it
				std::unordered_map<const RE::TESObjectARMA*, std::string> armourOf;
				for (auto* armour : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
					if (!armour) {
						continue;
					}
					const auto label = LabelOf(armour, armour->GetFullName());
					for (const auto* addon : armour->armorAddons) {
						if (addon) {
							armourOf.emplace(addon, label);
						}
					}
				}
				for (auto* addon : addons) {
					++a_context.progress.done;
					if (!addon) {
						continue;
					}
					const auto it = armourOf.find(addon);
					const auto record = it != armourOf.end() ? it->second : LabelOf(addon, nullptr);
					const auto slot = SlotOf(addon);
					for (int sex = 0; sex < 2; ++sex) {
						add(MeshPath(addon->bipedModels[sex].GetModel()), "Armour"sv, record, slot);
						add(MeshPath(addon->bipedModel1stPersons[sex].GetModel()), "Armour"sv, record, slot);
					}
				}
			}
		};

		// A bone's category: its skeleton's; else the one record that brings it; else the kind and slot they share; else Shared
		std::string CategoryOf(const std::vector<BoneSource>& a_sources, const std::vector<std::size_t>& a_from)
		{
			for (const auto i : a_from) {
				if (a_sources[i].kind == "Skeleton"sv) {
					return std::format("Skeleton · {}", a_sources[i].record);
				}
			}
			const auto& first = a_sources[a_from.front()];
			const auto all = [&](auto a_same) { return std::ranges::all_of(a_from, [&](std::size_t i) { return a_same(a_sources[i]); }); };
			if (all([&](const BoneSource& a_source) { return a_source.kind == first.kind && a_source.record == first.record; })) {
				return std::format("{} · {}", first.kind, first.record);
			}
			if (all([&](const BoneSource& a_source) { return a_source.kind == first.kind && a_source.slot == first.slot; })) {
				return first.slot.empty() ? first.kind : std::format("{} · {}", first.kind, first.slot);
			}
			return "Shared";
		}

		// The node names in each of those meshes, loose or archived, the root left out: it is the file's own node, not a bone
		class BoneNames : public ISource
		{
		public:
			std::string_view GetName() const override { return "Bone meshes"sv; }
			Target GetTarget() const override { return Target::kBones; }

			void Run(Context& a_context) override
			{
				const auto& sources = a_context.records.boneSources;
				a_context.progress.total = static_cast<uint32_t>(sources.size());

				std::unordered_map<std::string, std::vector<std::string>> byModel;
				std::unordered_map<std::string, std::vector<std::size_t>> byBone;
				std::vector<std::string> order;
				for (std::size_t i = 0; i < sources.size(); ++i) {
					++a_context.progress.done;
					const auto& source = sources[i];
					auto cached = byModel.find(source.model);
					if (cached == byModel.end()) {
						std::vector<std::string> names;
						const auto bytes = Files::Read(source.model);
						std::string error;
						if (bytes.empty()) {
							a_context.Report(source.model, "not found, loose or in an archive");
						} else if (!Nif::ReadNodeNames(bytes, names, error)) {
							a_context.Report(source.model, error);
							names.clear();
						} else if (!names.empty()) {
							names.erase(names.begin());
						}
						cached = byModel.emplace(source.model, std::move(names)).first;
					}
					for (const auto& name : cached->second) {
						auto [at, bNew] = byBone.try_emplace(name);
						if (bNew) {
							order.push_back(name);
						}
						if (at->second.empty() || at->second.back() != i) {
							at->second.push_back(i);
						}
					}
				}

				for (const auto& name : order) {
					a_context.out.bones.push_back({ name, CategoryOf(sources, byBone[name]) });
				}
				std::ranges::sort(a_context.out.bones, [](const Bone& a, const Bone& b) { return std::tie(a.category, a.name) < std::tie(b.category, b.name); });
			}
		};
	}

	void AddBoneSources(Scanner& a_scanner)
	{
		a_scanner.AddSource(std::make_unique<BoneRecords>());
		a_scanner.AddSource(std::make_unique<BoneNames>());
	}
}

#include "Scan.h"

#include "Nif.h"

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

		std::string RaceName(const RE::TESRace* a_race)
		{
			const char* editorID = a_race->GetFormEditorID();
			return editorID && *editorID ? editorID : std::format("{:08X}", a_race->GetFormID());
		}

		// What every race wears naked: its face head part and its skin's body, hands and feet, per sex
		class SkinModelRecords : public ISource
		{
		public:
			std::string_view GetName() const override { return "Race records"sv; }
			Target GetTarget() const override { return Target::kSkinModels; }
			Stage GetStage() const override { return Stage::kRecords; }

			void Run(Context& a_context) override
			{
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					a_context.Report(""sv, "no data handler");
					return;
				}

				const auto& races = dataHandler->GetFormArray<RE::TESRace>();
				a_context.progress.total = races.size();

				std::unordered_map<std::string, std::size_t> byModel;
				const auto add = [&](const std::string& a_model, SkinPart a_part, const RE::TESRace* a_race, int a_sex) {
					if (a_model.empty()) {
						return;
					}
					auto it = byModel.find(a_model);
					if (it == byModel.end()) {
						SkinModel out;
						out.model = a_model;
						out.part = a_part;
						out.foundBy.push_back({ std::string(GetName()), a_model });
						it = byModel.emplace(a_model, a_context.out.skinModels.size()).first;
						a_context.out.skinModels.push_back(std::move(out));
					}
					auto& model = a_context.out.skinModels[it->second];
					(a_sex == 0 ? model.bMale : model.bFemale) = true;
					if (const auto name = RaceName(a_race); std::ranges::find(model.races, name) == model.races.end()) {
						model.races.push_back(name);
					}
				};

				using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
				for (auto* race : races) {
					++a_context.progress.done;
					if (!race) {
						continue;
					}
					// A race with no face head parts is a creature, and its skin is not a skin overlays are painted for
					const auto hasFace = [&](int a_sex) {
						const auto* face = race->faceRelatedData[a_sex];
						return face && face->headParts && !face->headParts->empty();
					};
					if (!hasFace(0) && !hasFace(1)) {
						continue;
					}
					for (int sex = 0; sex < 2; ++sex) {
						if (const auto* face = race->faceRelatedData[sex]; face && face->headParts) {
							for (const auto* part : *face->headParts) {
								if (part && part->type.get() == RE::BGSHeadPart::HeadPartType::kFace) {
									add(MeshPath(part->GetModel()), SkinPart::kHead, race, sex);
								}
							}
						}
						if (!race->skin) {
							continue;
						}
						for (const auto* addon : race->skin->armorAddons) {
							if (!addon || !addon->IsValidRace(race)) {
								continue;
							}
							const auto model = MeshPath(addon->bipedModels[sex].GetModel());
							for (const auto [slot, part] : { std::pair{ Slot::kBody, SkinPart::kBody }, std::pair{ Slot::kHands, SkinPart::kHands }, std::pair{ Slot::kFeet, SkinPart::kFeet } }) {
								if (addon->HasPartOf(slot)) {
									add(model, part, race, sex);
								}
							}
						}
					}
				}
			}
		};

		// The shape names inside each of those meshes, loose or archived
		class SkinModelShapes : public ISource
		{
		public:
			std::string_view GetName() const override { return "Skin meshes"sv; }
			Target GetTarget() const override { return Target::kSkinModels; }

			void Run(Context& a_context) override
			{
				a_context.progress.total = static_cast<uint32_t>(a_context.records.skinModels.size());
				for (const auto& record : a_context.records.skinModels) {
					++a_context.progress.done;
					const auto bytes = Files::Read(record.model);
					if (bytes.empty()) {
						a_context.Report(record.model, "not found, loose or in an archive");
						continue;
					}
					SkinModel out;
					out.model = record.model;
					out.part = record.part;
					std::string error;
					if (!Nif::ReadShapeNames(bytes, out.shapes, error)) {
						a_context.Report(record.model, error);
						continue;
					}
					out.foundBy.push_back({ std::string(GetName()), record.model });
					a_context.out.skinModels.push_back(std::move(out));
				}
			}
		};
	}

	std::string_view SkinPartName(SkinPart a_part)
	{
		switch (a_part) {
		case SkinPart::kHead:
			return "Head"sv;
		case SkinPart::kBody:
			return "Body"sv;
		case SkinPart::kHands:
			return "Hands"sv;
		case SkinPart::kFeet:
			return "Feet"sv;
		default:
			return "?"sv;
		}
	}

	void AddSkinModelSources(Scanner& a_scanner)
	{
		a_scanner.AddSource(std::make_unique<SkinModelRecords>());
		a_scanner.AddSource(std::make_unique<SkinModelShapes>());
	}
}

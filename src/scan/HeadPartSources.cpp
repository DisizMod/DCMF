#include "Scan.h"

namespace Scan
{
	namespace
	{
		std::string PluginOf(const RE::TESForm* a_form)
		{
			const auto* file = a_form->GetFile(0);
			return file ? std::string(file->GetFilename()) : std::string{};
		}

		std::string MeshPath(const char* a_model)
		{
			if (!a_model || !*a_model) {
				return {};
			}
			auto path = Files::Normalize(a_model);
			return path.starts_with("meshes/") ? path : "meshes/" + path;
		}

		// Every BGSHeadPart in the load order
		class HeadPartRecords : public ISource
		{
		public:
			std::string_view GetName() const override { return "Head part records"sv; }
			Target GetTarget() const override { return Target::kHeadParts; }
			Stage GetStage() const override { return Stage::kRecords; }

			void Run(Context& a_context) override
			{
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					a_context.Report(""sv, "no data handler");
					return;
				}

				const auto& parts = dataHandler->GetFormArray<RE::BGSHeadPart>();
				a_context.progress.total = parts.size();

				for (const auto* part : parts) {
					++a_context.progress.done;
					if (!part) {
						continue;
					}

					HeadPart out;
					out.formID = part->GetFormID();
					out.plugin = PluginOf(part);
					out.editorID = part->formEditorID.c_str();
					out.name = part->GetFullName();
					out.type = part->type.get();
					out.bMale = part->flags.any(RE::BGSHeadPart::Flag::kMale);
					out.bFemale = part->flags.any(RE::BGSHeadPart::Flag::kFemale);
					out.bPlayable = part->flags.any(RE::BGSHeadPart::Flag::kPlayable);
					out.bExtraPart = part->flags.any(RE::BGSHeadPart::Flag::kIsExtraPart);
					out.model = MeshPath(part->GetModel());
					for (uint32_t i = 0; i < RE::BGSHeadPart::MorphIndices::kTotal; ++i) {
						out.tris[i] = MeshPath(part->morphs[i].GetModel());
					}
					if (part->validRaces) {
						for (const auto* form : part->validRaces->forms) {
							if (form) {
								const char* editorID = form->GetFormEditorID();
								out.races.emplace_back(editorID && *editorID ? editorID : std::format("{:08X}", form->GetFormID()));
							}
						}
					}
					for (const auto* extra : part->extraParts) {
						if (extra) {
							out.extraParts.push_back(extra->GetFormID());
						}
					}
					out.foundBy.push_back({ std::string(GetName()), out.plugin });

					a_context.out.headParts.push_back(std::move(out));
				}
			}
		};
	}

	void AddHeadPartSources(Scanner& a_scanner)
	{
		a_scanner.AddSource(std::make_unique<HeadPartRecords>());
	}
}

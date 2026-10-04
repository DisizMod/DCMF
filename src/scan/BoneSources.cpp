#include "Scan.h"

#include "Nif.h"

#include <rapidjson/document.h>

#include <fstream>


namespace Scan
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/DCMF.bones.json"sv;

		// The meshes DCMF.bones.json lists; a bone's category is the file it was first found in
		class BoneRecords : public ISource
		{
		public:
			std::string_view GetName() const override { return "Bone files"sv; }
			Target GetTarget() const override { return Target::kBones; }
			Stage GetStage() const override { return Stage::kRecords; }

			void Run(Context& a_context) override
			{
				std::ifstream file{ std::string(kPath) };
				if (!file.is_open()) {
					a_context.Report(kPath, "not found; no bone is scanned");
					return;
				}
				const std::string text{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
				rapidjson::Document document;
				const bool bValid = !document.Parse(text.data()).HasParseError() && document.IsObject();
				const auto files = bValid ? document.FindMember("files") : document.MemberEnd();
				if (!bValid || files == document.MemberEnd() || !files->value.IsArray()) {
					a_context.Report(kPath, "not valid JSON with a files list; no bone is scanned");
					return;
				}
				for (const auto& path : files->value.GetArray()) {
					if (path.IsString()) {
						auto model = Files::Normalize(path.GetString());
						a_context.out.boneSources.push_back({ model, {}, model, {} });
					}
				}
				a_context.progress.total = static_cast<uint32_t>(a_context.out.boneSources.size());
				a_context.progress.done = a_context.progress.total.load();
			}
		};

		// A bone's category: the first file that has it
		std::string CategoryOf(const std::vector<BoneSource>& a_sources, const std::vector<std::size_t>& a_from)
		{
			return a_sources[a_from.front()].record;
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
							logger::info("scan: {} is not in the load order; its bones are skipped", source.model);
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

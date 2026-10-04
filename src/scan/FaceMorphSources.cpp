#include "Scan.h"
#include "Tri.h"

#include <charconv>

namespace Scan
{
	namespace
	{
		constexpr auto kFaceGenMorphs = "meshes/actors/character/facegenmorphs"sv;

		std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			return a_text.substr(first, a_text.find_last_not_of(" \t\r\n") - first + 1);
		}

		bool EqualNoCase(std::string_view a_lhs, std::string_view a_rhs)
		{
			return a_lhs.size() == a_rhs.size() && std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](char l, char r) {
				return std::tolower(static_cast<unsigned char>(l)) == std::tolower(static_cast<unsigned char>(r));
			});
		}

		std::vector<std::string_view> Lines(std::string_view a_text)
		{
			std::vector<std::string_view> lines;
			while (!a_text.empty()) {
				const auto newline = a_text.find('\n');
				const auto line = Trim(a_text.substr(0, newline));
				if (!line.empty() && line.front() != '#' && line.front() != ';') {
					lines.push_back(line);
				}
				if (newline == std::string_view::npos) {
					break;
				}
				a_text.remove_prefix(newline + 1);
			}
			return lines;
		}

		std::vector<std::string_view> Split(std::string_view a_text, char a_separator)
		{
			std::vector<std::string_view> parts;
			while (true) {
				const auto at = a_text.find(a_separator);
				parts.push_back(Trim(a_text.substr(0, at)));
				if (at == std::string_view::npos) {
					return parts;
				}
				a_text.remove_prefix(at + 1);
			}
		}

		std::string DataRelative(const std::filesystem::path& a_path)
		{
			return Files::Normalize(a_path.generic_string());
		}

		// Reads one face TRI into a set; false, with the reason reported, when it cannot
		bool ReadSet(const Context& a_context, const std::string& a_tri, FaceMorphSet& a_out, std::string a_missing = "not found, loose or in an archive")
		{
			const auto bytes = Files::Read(a_tri);
			if (bytes.empty()) {
				a_context.Report(a_tri, std::move(a_missing));
				return false;
			}
			Tri::FaceTri tri;
			std::string error;
			if (!Tri::ReadFaceTri(bytes, tri, error)) {
				a_context.Report(a_tri, error);
				return false;
			}
			a_out.tri = a_tri;
			a_out.vertexCount = tri.vertexCount;
			a_out.sample = std::move(tri.sample);
			const Provenance provenance{ std::string(a_context.source), a_tri };
			for (auto& name : tri.morphs) {
				a_out.morphs.push_back({ .name = std::move(name), .foundBy = { provenance } });
			}
			a_out.foundBy.push_back(provenance);
			return true;
		}

		// The TRIs each head part record names
		class HeadPartTris : public ISource
		{
		public:
			std::string_view GetName() const override { return "Head part TRIs"sv; }
			Target GetTarget() const override { return Target::kFaceMorphs; }

			void Run(Context& a_context) override
			{
				std::map<std::string, std::vector<RE::FormID>> tris;
				for (const auto& part : a_context.records.headParts) {
					for (const auto& tri : part.tris) {
						if (!tri.empty()) {
							tris[tri].push_back(part.formID);
						}
					}
				}
				a_context.progress.total = static_cast<uint32_t>(tris.size());

				for (auto& [tri, parts] : tris) {
					FaceMorphSet set;
					if (ReadSet(a_context, tri, set)) {
						set.headParts = std::move(parts);
						a_context.out.faceMorphs.push_back(std::move(set));
					}
					++a_context.progress.done;
				}
			}
		};

		// RaceMenu's facegenmorphs/<plugin>/morphs.ini: extra TRIs added onto a head part's chargen TRI
		class RaceMenuExtensions : public ISource
		{
		public:
			std::string_view GetName() const override { return "RaceMenu morphs.ini"sv; }
			Target GetTarget() const override { return Target::kFaceMorphs; }

			void Run(Context& a_context) override
			{
				struct Extension
				{
					std::string ini;
					std::string source;
					std::string tri;
				};
				std::vector<Extension> extensions;

				for (const auto& file : Files::List(kFaceGenMorphs, ".ini"sv, true)) {
					if (!EqualNoCase(file.filename().string(), "morphs.ini")) {
						continue;
					}
					// The lines point into the text, which has to outlive the loop
					const auto text = Files::ReadText(file);
					for (const auto line : Lines(text)) {
						const auto equals = line.find('=');
						if (equals == std::string_view::npos) {
							continue;
						}
						const auto key = Trim(line.substr(0, equals));
						if (!EqualNoCase(key, "extension") && !EqualNoCase(key, "extensionfile")) {
							continue;
						}
						// The source relative to meshes, the extensions relative to facegenmorphs/morphs
						const auto paths = Split(line.substr(equals + 1), ',');
						if (paths.empty() || paths[0].empty()) {
							continue;
						}
						const auto source = "meshes/" + Files::Normalize(paths[0]);
						for (std::size_t i = 1; i < paths.size(); ++i) {
							if (!paths[i].empty()) {
								extensions.push_back({ DataRelative(file), source, std::string(kFaceGenMorphs) + "/morphs/" + Files::Normalize(paths[i]) });
							}
						}
					}
				}

				a_context.progress.total = static_cast<uint32_t>(extensions.size());
				for (const auto& extension : extensions) {
					FaceMorphSet set;
					const auto missing = std::format("listed in {}, but the file is missing", extension.ini);
					if (ReadSet(a_context, extension.tri, set, missing)) {
						set.extends = extension.source;
						a_context.out.faceMorphs.push_back(std::move(set));
					}
					++a_context.progress.done;
				}
			}
		};

		// RaceMenu's facegenmorphs/<plugin>/sliders/*.ini: what each morph is for, and which two make one slider
		class RaceMenuSliders : public ISource
		{
		public:
			std::string_view GetName() const override { return "RaceMenu sliders"sv; }
			Target GetTarget() const override { return Target::kFaceMorphs; }

			void Run(Context& a_context) override
			{
				std::vector<std::filesystem::path> files;
				for (const auto& file : Files::List(kFaceGenMorphs, ".ini"sv, true)) {
					if (EqualNoCase(file.parent_path().filename().string(), "sliders")) {
						files.push_back(file);
					}
				}
				a_context.progress.total = static_cast<uint32_t>(files.size());

				for (const auto& file : files) {
					const auto origin = DataRelative(file);
					const auto text = Files::ReadText(file);
					for (const auto line : Lines(text)) {
						const auto equals = line.find('=');
						if (equals == std::string_view::npos) {
							continue;
						}
						const auto fields = Split(line.substr(equals + 1), ',');
						int flag = 0;
						if (fields.size() < 2 || std::from_chars(fields[0].data(), fields[0].data() + fields[0].size(), flag).ec != std::errc{}) {
							continue;
						}
						// A head part slider's data is an index, not a morph
						if (EqualNoCase(fields[1], "HeadPart")) {
							continue;
						}

						const auto slider = std::string(Trim(line.substr(0, equals)));
						// A slider runs negative into its first morph and positive into its second
						const auto present = [&](std::size_t a_index) { return fields.size() > a_index && !fields[a_index].empty() && !EqualNoCase(fields[a_index], "None"); };
						const bool bSlider = EqualNoCase(fields[1], "Slider");
						const float min = bSlider && present(2) ? -1.f : 0.f;
						const float max = bSlider && !present(3) && present(2) ? 0.f : 1.f;
						for (std::size_t i = 2; i < fields.size(); ++i) {
							if (fields[i].empty() || EqualNoCase(fields[i], "None")) {
								continue;
							}
							Morph morph;
							morph.name = fields[i];
							morph.display = slider;
							morph.category = CategoryName(flag);
							if (bSlider && i <= 3) {
								morph.slider = slider;
								morph.sign = i == 2 ? -1 : 1;
								morph.min = min;
								morph.max = max;
							}
							morph.foundBy.push_back({ std::string(GetName()), origin });
							a_context.out.faceSliders.push_back(std::move(morph));
						}
					}
					++a_context.progress.done;
				}
			}

		private:
			static std::string CategoryName(int a_flag)
			{
				switch (a_flag) {
				case -1:
					return "Extra";
				case 4:
					return "Body";
				case 8:
					return "Head";
				case 16:
					return "Face";
				case 32:
					return "Eyes";
				case 64:
					return "Brow";
				case 128:
					return "Mouth";
				case 256:
					return "Hair";
				case 1024:
					return "Expression";
				default:
					return std::format("Flag {}", a_flag);
				}
			}
		};
	}

	void AddFaceMorphSources(Scanner& a_scanner)
	{
		a_scanner.AddSource(std::make_unique<HeadPartTris>());
		a_scanner.AddSource(std::make_unique<RaceMenuExtensions>());
		a_scanner.AddSource(std::make_unique<RaceMenuSliders>());
	}
}

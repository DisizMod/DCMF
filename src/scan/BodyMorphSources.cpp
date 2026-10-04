#include "Pex.h"
#include "Scan.h"
#include "Tri.h"
#include "Xml.h"

namespace Scan
{
	namespace
	{
		std::string PluginOf(const RE::TESForm* a_form)
		{
			const auto* file = a_form->GetFile(0);
			return file ? std::string(file->GetFilename()) : std::string{};
		}

		std::string DataRelative(const std::filesystem::path& a_path)
		{
			return Files::Normalize(a_path.generic_string());
		}

		// Every armor addon's mesh, to be matched against what BodySlide builds
		class ArmorAddonRecords : public ISource
		{
		public:
			std::string_view GetName() const override { return "Armor addon records"sv; }
			Target GetTarget() const override { return Target::kBodyMorphs; }
			Stage GetStage() const override { return Stage::kRecords; }

			void Run(Context& a_context) override
			{
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					a_context.Report(""sv, "no data handler");
					return;
				}

				const auto& addons = dataHandler->GetFormArray<RE::TESObjectARMA>();
				a_context.progress.total = addons.size();

				for (const auto* addon : addons) {
					++a_context.progress.done;
					if (!addon) {
						continue;
					}
					ArmorAddon out;
					out.formID = addon->GetFormID();
					out.plugin = PluginOf(addon);
					for (int sex = 0; sex < 2; ++sex) {
						if (const char* model = addon->bipedModels[sex].GetModel(); model && *model) {
							out.models[sex] = Files::Normalize(model);
						}
					}
					a_context.out.armorAddons.push_back(std::move(out));
				}
			}
		};

		// BodySlide's CalienteTools/BodySlide/SliderSets/*.osp: what each project builds, and its sliders
		class BodySlideProjects : public ISource
		{
		public:
			std::string_view GetName() const override { return "BodySlide projects"sv; }
			Target GetTarget() const override { return Target::kBodyMorphs; }

			void Run(Context& a_context) override
			{
				const auto files = Files::List("CalienteTools/BodySlide/SliderSets"sv, ".osp"sv, true);
				a_context.progress.total = static_cast<uint32_t>(files.size());

				for (const auto& file : files) {
					const auto origin = DataRelative(file);
					Xml::Node root;
					std::string error;
					if (!Xml::Parse(Files::ReadText(file), root, error)) {
						a_context.Report(origin, error);
						++a_context.progress.done;
						continue;
					}

					for (const auto& project : root.children) {
						if (project.name != "SliderSet") {
							continue;
						}
						const auto* outputPath = project.Child("OutputPath");
						const auto* outputFile = project.Child("OutputFile");
						if (!outputPath || !outputFile || outputFile->text.empty()) {
							continue;
						}

						BodyMorphSet set;
						set.key = Files::Normalize(outputPath->text + "/" + outputFile->text);
						if (!set.key.starts_with("meshes/")) {
							set.key.insert(0, "meshes/");
						}
						set.project = project.Attribute("name");
						set.tri = set.key + ".tri";
						BodySlideProject own{ .name = set.project, .key = set.key, .origin = origin };
						for (const auto& child : project.children) {
							if (child.name == "Shape") {
								set.shapes.push_back(child.text.empty() ? std::string(child.Attribute("target")) : child.text);
								own.shapes.push_back(set.shapes.back());
							} else if (child.name == "Slider") {
								own.sliders.emplace_back(child.Attribute("name"));
								if (child.Attribute("zap") == "true") {
									set.zaps.emplace_back(child.Attribute("name"));
								} else {
									Morph morph{ .name = std::string(child.Attribute("name")) };
									for (const auto& data : child.children) {
										if (const auto target = data.Attribute("target"); data.name == "Data" && !target.empty()) {
											morph.shapes.emplace_back(target);
										}
									}
									set.morphs.push_back(std::move(morph));
								}
							}
						}
						set.foundBy.push_back({ std::string(GetName()), origin });
						a_context.out.bodyMorphs.push_back(std::move(set));
						a_context.out.projects.push_back(std::move(own));
					}

					++a_context.progress.done;
				}
			}
		};

		// The .tri BodySlide writes beside the mesh it builds: the project was built, and these are its morphs
		class BodySlideOutputTris : public ISource
		{
		public:
			std::string_view GetName() const override { return "BodySlide output TRIs"sv; }
			Target GetTarget() const override { return Target::kBodyMorphs; }
			Stage GetStage() const override { return Stage::kFollowUp; }

			void Run(Context& a_context) override
			{
				std::vector<std::pair<std::string, std::string>> tris;
				for (const auto& set : a_context.records.bodyMorphs) {
					if (!set.tri.empty()) {
						tris.emplace_back(set.key, set.tri);
					}
				}
				a_context.progress.total = static_cast<uint32_t>(tris.size());

				for (const auto& [key, path] : tris) {
					++a_context.progress.done;

					// BodySlide writes loose files; an archive would only be probed for nothing
					const auto bytes = Files::Read(path, false);
					if (bytes.empty()) {
						continue;
					}

					std::vector<Tri::BodyTriShape> shapes;
					std::string error;
					if (!Tri::ReadBodyTri(bytes, shapes, error)) {
						a_context.Report(path, error);
						continue;
					}

					BodyMorphSet set;
					set.key = key;
					set.tri = path;
					set.bBuilt = true;
					const Provenance provenance{ std::string(GetName()), path };
					BuiltBodyTri built{ .key = key, .tri = path };
					std::unordered_map<std::string_view, std::size_t> index;
					for (const auto& shape : shapes) {
						built.shapes.push_back(shape.name);
						for (const auto& name : shape.morphs) {
							if (std::ranges::find(built.morphs, name) == built.morphs.end()) {
								built.morphs.push_back(name);
							}
							if (const auto it = index.find(name); it != index.end()) {
								set.morphs[it->second].shapes.push_back(shape.name);
							} else {
								index.emplace(name, set.morphs.size());
								set.morphs.push_back({ .name = name, .shapes = { shape.name }, .foundBy = { provenance } });
							}
						}
					}
					set.foundBy.push_back(provenance);
					a_context.out.bodyMorphs.push_back(std::move(set));
					a_context.out.builtTris.push_back(std::move(built));
				}
			}
		};

		// RaceMenu morph scripts (UBE, CBBE 3BA, BHUNP, HIMBO): the morph names they fill their slider arrays with
		class RaceMenuMorphScripts : public ISource
		{
		public:
			std::string_view GetName() const override { return "RaceMenu morph scripts"sv; }
			Target GetTarget() const override { return Target::kBodyMorphs; }

			void Run(Context& a_context) override
			{
				constexpr auto marker = "SetBodyMorph"sv;

				const auto files = Files::List("Scripts"sv, ".pex"sv, false);
				a_context.progress.total = static_cast<uint32_t>(files.size());

				for (const auto& file : files) {
					++a_context.progress.done;

					const auto origin = DataRelative(file);
					const auto bytes = Files::Read(origin, false);
					const auto* begin = reinterpret_cast<const char*>(bytes.data());
					if (std::search(begin, begin + bytes.size(), marker.begin(), marker.end()) == begin + bytes.size()) {
						continue;
					}

					Pex::Script script;
					std::string error;
					if (!Pex::Read(bytes, script, error)) {
						a_context.Report(origin, error);
						continue;
					}

					ReadScript(a_context, script, origin);
				}
			}

		private:
			struct Array
			{
				std::string_view name;
				std::vector<std::string_view> elements;
			};

			void ReadScript(Context& a_context, const Pex::Script& a_script, const std::string& a_origin) const
			{
				// Arrays filled one element at a time with string literals, directly or through a temporary
				std::vector<Array> arrays;
				for (const auto& function : a_script.functions) {
					std::unordered_map<std::string_view, std::string_view> literals;
					for (const auto& instruction : function.code) {
						const auto& args = instruction.arguments;
						if (instruction.opcode == Pex::Instruction::kAssign && args.size() == 2 && args[0].type == Pex::Value::Type::kIdentifier) {
							if (args[1].type == Pex::Value::Type::kString) {
								literals[args[0].text] = args[1].text;
							} else {
								literals.erase(args[0].text);
							}
						} else if (instruction.opcode == Pex::Instruction::kArraySetElement && args.size() == 3 && args[1].type == Pex::Value::Type::kInt && args[1].integer >= 0) {
							std::string_view value;
							if (args[2].type == Pex::Value::Type::kString) {
								value = args[2].text;
							} else if (const auto it = literals.find(args[2].text); args[2].type == Pex::Value::Type::kIdentifier && it != literals.end()) {
								value = it->second;
							} else {
								continue;
							}
							auto array = std::ranges::find(arrays, args[0].text, &Array::name);
							if (array == arrays.end()) {
								array = arrays.insert(arrays.end(), { args[0].text });
							}
							const auto index = static_cast<std::size_t>(args[1].integer);
							if (array->elements.size() <= index) {
								array->elements.resize(index + 1);
							}
							array->elements[index] = value;
						}
					}
				}

				const auto isDisplay = [](std::string_view a_name) {
					std::string lower(a_name);
					std::ranges::transform(lower, lower.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
					return lower.find("display") != std::string::npos;
				};

				BodyMorphSet set;
				set.key = "script:" + std::string(a_script.name);
				const Provenance provenance{ std::string(GetName()), a_origin };

				std::vector<const Array*> displays;
				for (const auto& array : arrays) {
					if (isDisplay(array.name)) {
						displays.push_back(&array);
					}
				}

				for (const auto& array : arrays) {
					if (isDisplay(array.name) || array.elements.size() < 3) {
						continue;
					}
					// Its display names: the first unclaimed display array of the same length
					const Array* display = nullptr;
					for (auto& candidate : displays) {
						if (candidate && candidate->elements.size() == array.elements.size()) {
							display = candidate;
							candidate = nullptr;
							break;
						}
					}
					for (std::size_t i = 0; i < array.elements.size(); ++i) {
						if (array.elements[i].empty()) {
							continue;
						}
						Morph morph;
						morph.name = array.elements[i];
						morph.category = array.name;
						if (display && !display->elements[i].empty()) {
							morph.display = display->elements[i];
						}
						morph.foundBy.push_back(provenance);
						set.morphs.push_back(std::move(morph));
					}
				}

				if (set.morphs.empty()) {
					return;
				}
				set.foundBy.push_back(provenance);
				a_context.out.bodyMorphs.push_back(std::move(set));
			}
		};
	}

	void AddBodyMorphSources(Scanner& a_scanner)
	{
		a_scanner.AddSource(std::make_unique<ArmorAddonRecords>());
		a_scanner.AddSource(std::make_unique<BodySlideProjects>());
		a_scanner.AddSource(std::make_unique<BodySlideOutputTris>());
		a_scanner.AddSource(std::make_unique<RaceMenuMorphScripts>());
	}
}

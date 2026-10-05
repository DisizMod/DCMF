#include "Pex.h"
#include "Scan.h"
#include "Tri.h"
#include "Xml.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <filesystem>
#include <fstream>

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

		// What each output TRI read to, kept beside the log between sessions: a file whose size and time have not changed
		// is not opened again
		struct TriCacheEntry
		{
			std::uintmax_t size = 0;
			std::int64_t time = 0;
			std::vector<Tri::BodyTriShape> shapes;
		};
		using TriCache = std::unordered_map<std::string, TriCacheEntry>;

		constexpr int kTriCacheVersion = 1;

		std::optional<std::filesystem::path> TriCachePath()
		{
			auto directory = logger::log_directory();
			return directory ? std::optional(*directory / "DCMF.scancache.json") : std::nullopt;
		}

		// Nothing when there is none yet; a cache that cannot be read is said, and every TRI read again
		TriCache LoadTriCache()
		{
			TriCache cache;
			const auto path = TriCachePath();
			if (!path || !std::filesystem::exists(*path)) {
				return cache;
			}
			const auto text = Files::ReadText(*path);
			rapidjson::Document doc;
			if (doc.Parse(text.data()).HasParseError() || !doc.IsObject()) {
				logger::warn("scan: the TRI cache {} is not valid JSON; every output TRI is read again", path->string());
				return cache;
			}
			const auto version = doc.FindMember("version");
			const auto tris = doc.FindMember("tris");
			if (version == doc.MemberEnd() || !version->value.IsInt() || version->value.GetInt() != kTriCacheVersion || tris == doc.MemberEnd() || !tris->value.IsArray()) {
				logger::info("scan: the TRI cache is from another version; every output TRI is read again");
				return cache;
			}
			for (const auto& tri : tris->value.GetArray()) {
				const auto file = tri.FindMember("path");
				const auto size = tri.FindMember("size");
				const auto time = tri.FindMember("time");
				const auto shapes = tri.FindMember("shapes");
				if (file == tri.MemberEnd() || !file->value.IsString() || size == tri.MemberEnd() || !size->value.IsUint64() || time == tri.MemberEnd() || !time->value.IsInt64() ||
					shapes == tri.MemberEnd() || !shapes->value.IsArray()) {
					logger::warn("scan: a TRI cache row is malformed and is read again");
					continue;
				}
				TriCacheEntry entry{ .size = size->value.GetUint64(), .time = time->value.GetInt64() };
				for (const auto& shape : shapes->value.GetArray()) {
					auto& out = entry.shapes.emplace_back();
					if (const auto name = shape.FindMember("name"); name != shape.MemberEnd() && name->value.IsString()) {
						out.name = name->value.GetString();
					}
					if (const auto morphs = shape.FindMember("morphs"); morphs != shape.MemberEnd() && morphs->value.IsArray()) {
						for (const auto& morph : morphs->value.GetArray()) {
							if (morph.IsString()) {
								out.morphs.emplace_back(morph.GetString());
							}
						}
					}
				}
				cache.emplace(file->value.GetString(), std::move(entry));
			}
			return cache;
		}

		void SaveTriCache(const TriCache& a_cache)
		{
			const auto path = TriCachePath();
			if (!path) {
				logger::warn("scan: no log folder to keep the TRI cache in");
				return;
			}
			rapidjson::StringBuffer buffer;
			rapidjson::Writer writer(buffer);
			writer.StartObject();
			writer.Key("version");
			writer.Int(kTriCacheVersion);
			writer.Key("tris");
			writer.StartArray();
			for (const auto& [file, entry] : a_cache) {
				writer.StartObject();
				writer.Key("path");
				writer.String(file.data(), static_cast<rapidjson::SizeType>(file.size()));
				writer.Key("size");
				writer.Uint64(entry.size);
				writer.Key("time");
				writer.Int64(entry.time);
				writer.Key("shapes");
				writer.StartArray();
				for (const auto& shape : entry.shapes) {
					writer.StartObject();
					writer.Key("name");
					writer.String(shape.name.data(), static_cast<rapidjson::SizeType>(shape.name.size()));
					writer.Key("morphs");
					writer.StartArray();
					for (const auto& morph : shape.morphs) {
						writer.String(morph.data(), static_cast<rapidjson::SizeType>(morph.size()));
					}
					writer.EndArray();
					writer.EndObject();
				}
				writer.EndArray();
				writer.EndObject();
			}
			writer.EndArray();
			writer.EndObject();
			std::ofstream out(*path, std::ios::binary | std::ios::trunc);
			if (!out || !out.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()))) {
				logger::warn("scan: the TRI cache {} could not be written", path->string());
			}
		}

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

				const auto cache = LoadTriCache();
				TriCache next;
				std::uint32_t reused = 0;
				std::uint32_t read = 0;
				for (const auto& [key, path] : tris) {
					++a_context.progress.done;

					// BodySlide writes loose files; an archive would only be probed for nothing. A project not built has none
					const auto file = std::filesystem::path("Data") / Files::Normalize(path);
					std::error_code ec;
					const auto size = std::filesystem::file_size(file, ec);
					const auto time = ec ? 0 : static_cast<std::int64_t>(std::filesystem::last_write_time(file, ec).time_since_epoch().count());
					if (ec) {
						continue;
					}

					std::vector<Tri::BodyTriShape> shapes;
					if (const auto cached = cache.find(path); cached != cache.end() && cached->second.size == size && cached->second.time == time) {
						shapes = cached->second.shapes;
						++reused;
					} else {
						const auto bytes = Files::Read(path, false);
						if (bytes.empty()) {
							continue;
						}
						std::string error;
						if (!Tri::ReadBodyTri(bytes, shapes, error)) {
							a_context.Report(path, error);
							continue;
						}
						++read;
					}
					next.insert_or_assign(path, TriCacheEntry{ size, time, shapes });

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
				SaveTriCache(next);
				logger::info("scan: output TRIs: {} from the cache, {} read from disk", reused, read);
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

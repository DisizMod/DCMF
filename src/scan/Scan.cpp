#include "Scan.h"

#include "Tri.h"

#include <chrono>
#include <fstream>

namespace Scan
{
	std::string_view TargetName(Target a_target)
	{
		switch (a_target) {
		case Target::kHeadParts:
			return "Head parts"sv;
		case Target::kFaceMorphs:
			return "Face morphs"sv;
		case Target::kBodyMorphs:
			return "Body morphs"sv;
		case Target::kBodySlideCategories:
			return "BodySlide categories"sv;
		case Target::kBodyPresets:
			return "BodySlide presets"sv;
		case Target::kSkinModels:
			return "Skin models"sv;
		default:
			return "?"sv;
		}
	}

	void Context::Report(std::string_view a_origin, std::string a_message) const
	{
		out.problems.push_back({ std::string(source), std::string(a_origin), std::move(a_message) });
	}

	namespace Files
	{
		std::string Normalize(std::string_view a_path)
		{
			std::string out(a_path);
			std::ranges::replace(out, '\\', '/');
			std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			while (!out.empty() && out.front() == '/') {
				out.erase(out.begin());
			}
			if (out.starts_with("data/")) {
				out.erase(0, 5);
			}
			return out;
		}

		std::vector<std::byte> Read(std::string_view a_dataRelative, bool a_bArchives)
		{
			const auto relative = Normalize(a_dataRelative);

			if (std::ifstream in(std::filesystem::path("Data") / relative, std::ios::binary | std::ios::ate); in) {
				const auto size = static_cast<std::size_t>(in.tellg());
				std::vector<std::byte> bytes(size);
				in.seekg(0);
				if (size > 0 && in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
					return bytes;
				}
				return {};
			}

			if (!a_bArchives) {
				return {};
			}

			// The engine's resource stream reaches into the archives; one at a time from here
			static std::mutex archiveLock;
			std::scoped_lock lock(archiveLock);

			auto backslashed = relative;
			std::ranges::replace(backslashed, '/', '\\');
			RE::BSResourceNiBinaryStream stream(backslashed);
			if (!stream.good() || !stream.stream) {
				return {};
			}
			const auto size = stream.stream->totalSize;
			if (size == 0) {
				return {};
			}
			std::vector<std::byte> bytes(size);
			if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
				return {};
			}
			return bytes;
		}

		std::string ReadText(const std::filesystem::path& a_path)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				return {};
			}
			std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
			if (text.starts_with("\xEF\xBB\xBF")) {
				text.erase(0, 3);
			}
			return text;
		}

		std::vector<std::filesystem::path> List(std::string_view a_dataRelativeFolder, std::string_view a_extension, bool a_bRecursive)
		{
			std::vector<std::filesystem::path> out;

			const auto root = std::filesystem::path("Data") / a_dataRelativeFolder;
			std::error_code ec;
			if (!std::filesystem::is_directory(root, ec)) {
				return out;
			}

			const auto matches = [&](const std::filesystem::directory_entry& a_entry) {
				if (!a_entry.is_regular_file(ec)) {
					return false;
				}
				const auto extension = a_entry.path().extension().string();
				return extension.size() == a_extension.size() && std::equal(extension.begin(), extension.end(), a_extension.begin(), [](char a, char b) {
					return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
				});
			};

			if (a_bRecursive) {
				for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
					if (matches(*it)) {
						out.push_back(it->path());
					}
				}
			} else {
				for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
					if (matches(*it)) {
						out.push_back(it->path());
					}
				}
			}

			return out;
		}
	}

	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return out;
		}

		template <class T>
		void Append(std::vector<T>& a_to, const std::vector<T>& a_from)
		{
			a_to.insert(a_to.end(), a_from.begin(), a_from.end());
		}

		template <class T>
		void AppendUnique(std::vector<T>& a_to, const std::vector<T>& a_from)
		{
			for (const auto& item : a_from) {
				if (std::ranges::find(a_to, item) == a_to.end()) {
					a_to.push_back(item);
				}
			}
		}

		void AddProvenance(std::vector<Provenance>& a_to, const Provenance& a_provenance)
		{
			const auto same = [&](const Provenance& p) { return p.source == a_provenance.source && p.origin == a_provenance.origin; };
			if (std::ranges::find_if(a_to, same) == a_to.end()) {
				a_to.push_back(a_provenance);
			}
		}

		// Fills what the morph does not say yet from another finding of the same name
		void Enrich(Morph& a_morph, const Morph& a_from)
		{
			if (a_morph.display.empty()) {
				a_morph.display = a_from.display;
			}
			if (a_morph.category.empty()) {
				a_morph.category = a_from.category;
			}
			if (a_morph.slider.empty() && !a_from.slider.empty()) {
				a_morph.slider = a_from.slider;
				a_morph.sign = a_from.sign;
				a_morph.min = a_from.min;
				a_morph.max = a_from.max;
			}
			for (const auto& shape : a_from.shapes) {
				if (std::ranges::find(a_morph.shapes, shape) == a_morph.shapes.end()) {
					a_morph.shapes.push_back(shape);
				}
			}
			for (const auto& provenance : a_from.foundBy) {
				AddProvenance(a_morph.foundBy, provenance);
			}
		}

		// The engine's own face morphs: its expressions, modifiers and phonemes
		std::string_view EngineCategory(std::string_view a_name)
		{
			static const std::unordered_map<std::string, std::string_view> table = [] {
				std::unordered_map<std::string, std::string_view> map;
				for (const auto* name : { "DialogueAnger", "DialogueFear", "DialogueHappy", "DialogueSad", "DialogueSurprise", "DialoguePuzzled", "DialogueDisgusted",
						 "MoodNeutral", "MoodAnger", "MoodFear", "MoodHappy", "MoodSad", "MoodSurprise", "MoodPuzzled", "MoodDisgusted", "CombatAnger", "CombatShout" }) {
					map.emplace(Lower(name), "Engine expression"sv);
				}
				for (const auto* name : { "BlinkLeft", "BlinkRight", "BrowDownLeft", "BrowDownRight", "BrowInLeft", "BrowInRight", "BrowUpLeft", "BrowUpRight",
						 "LookDown", "LookLeft", "LookRight", "LookUp", "SquintLeft", "SquintRight", "HeadPitch", "HeadRoll", "HeadYaw" }) {
					map.emplace(Lower(name), "Engine modifier"sv);
				}
				for (const auto* name : { "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W" }) {
					map.emplace(Lower(name), "Engine phoneme"sv);
				}
				map.emplace(Lower("SkinnyMorph"), "Engine custom"sv);
				return map;
			}();
			const auto it = table.find(Lower(a_name));
			return it != table.end() ? it->second : std::string_view{};
		}

		// Every morph name once, with the sets that define it
		template <class Set>
		std::vector<MorphSummary> Summarize(const std::vector<Set>& a_sets)
		{
			std::vector<MorphSummary> out;
			std::unordered_map<std::string, std::size_t> index;
			for (std::size_t i = 0; i < a_sets.size(); ++i) {
				for (const auto& morph : a_sets[i].morphs) {
					const auto key = Lower(morph.name);
					auto it = index.find(key);
					if (it == index.end()) {
						it = index.emplace(key, out.size()).first;
						out.push_back({ .name = morph.name, .min = morph.min, .max = morph.max });
					}
					auto& summary = out[it->second];
					summary.min = std::min(summary.min, morph.min);
					summary.max = std::max(summary.max, morph.max);
					if (summary.display.empty()) {
						summary.display = morph.display;
					}
					if (summary.category.empty()) {
						summary.category = morph.category;
					}
					if (summary.sets.empty() || summary.sets.back() != i) {
						summary.sets.push_back(i);
					}
					for (const auto& provenance : a_sets[i].foundBy) {
						AddProvenance(summary.foundBy, provenance);
					}
					for (const auto& provenance : morph.foundBy) {
						AddProvenance(summary.foundBy, provenance);
					}
				}
			}
			for (auto& summary : out) {
				for (const auto& provenance : summary.foundBy) {
					if (summary.sources.find(provenance.source) == std::string::npos) {
						summary.sources += summary.sources.empty() ? provenance.source : " + " + provenance.source;
					}
				}
			}
			std::ranges::sort(out, [](const MorphSummary& a, const MorphSummary& b) { return Lower(a.name) < Lower(b.name); });
			return out;
		}

		void MergeMorphs(std::vector<Morph>& a_to, const std::vector<Morph>& a_from)
		{
			std::unordered_map<std::string, std::size_t> index;
			for (std::size_t i = 0; i < a_to.size(); ++i) {
				index.emplace(Lower(a_to[i].name), i);
			}
			for (const auto& morph : a_from) {
				if (const auto it = index.find(Lower(morph.name)); it != index.end()) {
					Enrich(a_to[it->second], morph);
				} else {
					index.emplace(Lower(morph.name), a_to.size());
					a_to.push_back(morph);
				}
			}
		}

		// Folds one source's findings into the whole
		void Merge(Catalogue& a_to, const Catalogue& a_from)
		{
			Append(a_to.headParts, a_from.headParts);
			Append(a_to.armorAddons, a_from.armorAddons);
			Append(a_to.bodyPresets, a_from.bodyPresets);
			Append(a_to.faceSliders, a_from.faceSliders);
			Append(a_to.projects, a_from.projects);
			Append(a_to.builtTris, a_from.builtTris);
			Append(a_to.problems, a_from.problems);
			Append(a_to.timings, a_from.timings);

			for (const auto& set : a_from.faceMorphs) {
				auto it = std::ranges::find_if(a_to.faceMorphs, [&](const FaceMorphSet& s) { return s.tri == set.tri; });
				if (it == a_to.faceMorphs.end()) {
					a_to.faceMorphs.push_back(set);
					continue;
				}
				if (it->extends.empty()) {
					it->extends = set.extends;
				}
				if (it->vertexCount == 0) {
					it->vertexCount = set.vertexCount;
					it->sample = set.sample;
				}
				MergeMorphs(it->morphs, set.morphs);
				AppendUnique(it->headParts, set.headParts);
				for (const auto& provenance : set.foundBy) {
					AddProvenance(it->foundBy, provenance);
				}
			}

			for (const auto& set : a_from.bodyMorphs) {
				auto it = std::ranges::find_if(a_to.bodyMorphs, [&](const BodyMorphSet& s) { return s.key == set.key; });
				if (it == a_to.bodyMorphs.end()) {
					a_to.bodyMorphs.push_back(set);
					continue;
				}
				if (it->project.empty()) {
					it->project = set.project;
				}
				if (it->tri.empty()) {
					it->tri = set.tri;
				}
				it->bBuilt = it->bBuilt || set.bBuilt;
				for (const auto& shape : set.shapes) {
					if (std::ranges::find(it->shapes, shape) == it->shapes.end()) {
						it->shapes.push_back(shape);
					}
				}
				for (const auto& zap : set.zaps) {
					if (std::ranges::find(it->zaps, zap) == it->zaps.end()) {
						it->zaps.push_back(zap);
					}
				}
				MergeMorphs(it->morphs, set.morphs);
				AppendUnique(it->armorAddons, set.armorAddons);
				for (const auto& provenance : set.foundBy) {
					AddProvenance(it->foundBy, provenance);
				}
			}

			for (const auto& model : a_from.skinModels) {
				auto it = std::ranges::find_if(a_to.skinModels, [&](const SkinModel& m) { return m.model == model.model; });
				if (it == a_to.skinModels.end()) {
					a_to.skinModels.push_back(model);
					continue;
				}
				it->bMale = it->bMale || model.bMale;
				it->bFemale = it->bFemale || model.bFemale;
				AppendUnique(it->races, model.races);
				AppendUnique(it->shapes, model.shapes);
				for (const auto& provenance : model.foundBy) {
					AddProvenance(it->foundBy, provenance);
				}
			}

			for (const auto& category : a_from.categories) {
				auto it = std::ranges::find_if(a_to.categories, [&](const BodySlideCategory& c) { return c.name == category.name; });
				if (it == a_to.categories.end()) {
					a_to.categories.push_back(category);
					continue;
				}
				MergeMorphs(it->sliders, category.sliders);
				for (const auto& provenance : category.foundBy) {
					AddProvenance(it->foundBy, provenance);
				}
			}
		}

		// Ties what the sources found separately to each other
		void Link(Catalogue& a_catalogue)
		{
			// Face: a RaceMenu extension applies to every head part naming the TRI it extends
			for (auto& set : a_catalogue.faceMorphs) {
				if (set.extends.empty()) {
					continue;
				}
				for (const auto& part : a_catalogue.headParts) {
					if (std::ranges::find(part.tris, set.extends) != std::end(part.tris)) {
						if (std::ranges::find(set.headParts, part.formID) == set.headParts.end()) {
							set.headParts.push_back(part.formID);
						}
					}
				}
			}

			// Face: a TRI nothing names applies to the head parts of any TRI of the same mesh
			constexpr float kSameShape = 0.05f;
			for (auto& set : a_catalogue.faceMorphs) {
				if (!set.headParts.empty() || set.vertexCount == 0) {
					continue;
				}
				for (const auto& other : a_catalogue.faceMorphs) {
					if (&other == &set || other.headParts.empty() || other.vertexCount != set.vertexCount) {
						continue;
					}
					if (Tri::ShapeDistance(other.sample, set.sample) <= kSameShape) {
						AppendUnique(set.headParts, other.headParts);
						AddProvenance(set.foundBy, { "Topology match"s, other.tri });
					}
				}
			}

			// Face: the engine's own morphs are known by name
			for (auto& set : a_catalogue.faceMorphs) {
				for (auto& morph : set.morphs) {
					if (const auto category = EngineCategory(morph.name); !category.empty()) {
						morph.category = category;
					}
				}
			}

			// Face: RaceMenu's slider files name and pair the morphs
			std::unordered_map<std::string, const Morph*> sliders;
			for (const auto& slider : a_catalogue.faceSliders) {
				sliders.emplace(Lower(slider.name), &slider);
			}
			for (auto& set : a_catalogue.faceMorphs) {
				for (auto& morph : set.morphs) {
					if (const auto it = sliders.find(Lower(morph.name)); it != sliders.end()) {
						Enrich(morph, *it->second);
					}
				}
			}

			// Body: an armor addon wears the mesh a BodySlide project builds
			const auto meshKey = [](std::string_view a_model) {
				auto key = Files::Normalize(a_model);
				if (!key.starts_with("meshes/")) {
					key.insert(0, "meshes/");
				}
				if (key.ends_with(".nif")) {
					key.resize(key.size() - 4);
				}
				if (key.ends_with("_0") || key.ends_with("_1")) {
					key.resize(key.size() - 2);
				}
				return key;
			};
			for (const auto& addon : a_catalogue.armorAddons) {
				for (const auto& model : addon.models) {
					if (model.empty()) {
						continue;
					}
					const auto key = meshKey(model);
					for (auto& set : a_catalogue.bodyMorphs) {
						if (set.key == key && std::ranges::find(set.armorAddons, addon.formID) == set.armorAddons.end()) {
							set.armorAddons.push_back(addon.formID);
						}
					}
				}
			}

			// Body: BodySlide's categories file every slider they know, over whatever a script grouped it under
			std::unordered_map<std::string, Morph> categorized;
			for (const auto& category : a_catalogue.categories) {
				for (auto slider : category.sliders) {
					slider.category = category.name;
					categorized.emplace(Lower(slider.name), std::move(slider));
				}
			}
			for (auto& set : a_catalogue.bodyMorphs) {
				for (auto& morph : set.morphs) {
					if (const auto it = categorized.find(Lower(morph.name)); it != categorized.end()) {
						morph.category = it->second.category;
						Enrich(morph, it->second);
					}
				}
			}

			// Body: RaceMenu's scripts name what the projects found
			std::unordered_multimap<std::string, Morph> scripted;
			for (const auto& set : a_catalogue.bodyMorphs) {
				if (set.project.empty()) {
					for (const auto& morph : set.morphs) {
						scripted.emplace(Lower(morph.name), morph);
					}
				}
			}
			for (auto& set : a_catalogue.bodyMorphs) {
				if (set.project.empty()) {
					continue;
				}
				for (auto& morph : set.morphs) {
					const auto [first, last] = scripted.equal_range(Lower(morph.name));
					for (auto it = first; it != last; ++it) {
						Enrich(morph, it->second);
					}
				}
			}

			std::ranges::sort(a_catalogue.headParts, [](const HeadPart& a, const HeadPart& b) {
				return a.type != b.type ? a.type < b.type : a.editorID < b.editorID;
			});
			std::ranges::sort(a_catalogue.faceMorphs, {}, &FaceMorphSet::tri);
			std::ranges::sort(a_catalogue.bodyMorphs, {}, &BodyMorphSet::key);
			std::ranges::sort(a_catalogue.categories, {}, &BodySlideCategory::name);
			std::ranges::sort(a_catalogue.bodyPresets, [](const BodyPreset& a, const BodyPreset& b) { return Lower(a.name) < Lower(b.name); });

			a_catalogue.faceMorphNames = Summarize(a_catalogue.faceMorphs);
			a_catalogue.bodyMorphNames = Summarize(a_catalogue.bodyMorphs);
		}
	}

	void Scanner::AddSource(std::unique_ptr<ISource> a_source)
	{
		_sources.push_back({ std::move(a_source) });
	}

	void Scanner::RegisterSources()
	{
		if (!_sources.empty()) {
			return;
		}

		AddHeadPartSources(*this);
		AddFaceMorphSources(*this);
		AddBodyMorphSources(*this);
		AddCategorySources(*this);
		AddPresetSources(*this);
		AddSkinModelSources(*this);
	}

	std::vector<Scanner::SourceProgress> Scanner::GetProgress() const
	{
		std::vector<SourceProgress> out;
		out.reserve(_sources.size());
		for (const auto& entry : _sources) {
			out.push_back({ entry.source->GetName(), entry.source->GetTarget(), entry.progress->done.load(), entry.progress->total.load(), entry.progress->bFinished.load() });
		}
		return out;
	}

	void Scanner::Start()
	{
		if (_bRunning.exchange(true)) {
			return;
		}

		if (_thread.joinable()) {
			_thread.join();
		}

		RegisterSources();

		for (auto& entry : _sources) {
			entry.progress->done = 0;
			entry.progress->total = 0;
			entry.progress->bFinished = false;
		}

		logger::info("scan: started");

		// The records are read here, on the main thread, and copied out as plain data
		auto records = std::make_shared<Catalogue>();
		for (auto& entry : _sources) {
			if (entry.source->GetStage() != ISource::Stage::kRecords) {
				continue;
			}
			const auto start = std::chrono::steady_clock::now();
			Context context{ *records, *records, *entry.progress, entry.source->GetName() };
			entry.source->Run(context);
			entry.progress->bFinished = true;
			const std::chrono::duration<float, std::milli> took = std::chrono::steady_clock::now() - start;
			records->timings.push_back({ std::string(entry.source->GetName()), entry.source->GetTarget(), took.count(), entry.progress->total.load() });
		}

		_thread = std::jthread([this, records] { RunWorkers(records); });
	}

	void Scanner::RunWorkers(std::shared_ptr<Catalogue> a_records)
	{
		const auto start = std::chrono::steady_clock::now();

		auto catalogue = std::make_shared<Catalogue>(*a_records);

		const auto runStage = [&](ISource::Stage a_stage) {
			std::vector<Entry*> stage;
			for (auto& entry : _sources) {
				if (entry.source->GetStage() == a_stage) {
					stage.push_back(&entry);
				}
			}

			// Each source writes into a catalogue of its own, folded in once all are done
			std::vector<Catalogue> outputs(stage.size());
			std::atomic<std::size_t> next = 0;
			const auto work = [&] {
				for (auto i = next++; i < stage.size(); i = next++) {
					auto& entry = *stage[i];
					const auto sourceStart = std::chrono::steady_clock::now();
					Context context{ *catalogue, outputs[i], *entry.progress, entry.source->GetName() };
					try {
						entry.source->Run(context);
					} catch (const std::exception& e) {
						context.Report(""sv, std::format("stopped: {}", e.what()));
					}
					entry.progress->bFinished = true;
					const std::chrono::duration<float, std::milli> took = std::chrono::steady_clock::now() - sourceStart;
					outputs[i].timings.push_back({ std::string(entry.source->GetName()), entry.source->GetTarget(), took.count(), entry.progress->total.load() });
				}
			};

			// Two workers: nothing waits on the scan, so it keeps out of the loading screen's way
			{
				std::jthread a(work);
				std::jthread b(work);
			}

			for (const auto& output : outputs) {
				Merge(*catalogue, output);
			}
		};

		runStage(ISource::Stage::kFiles);
		runStage(ISource::Stage::kFollowUp);

		Link(*catalogue);

		const std::chrono::duration<float, std::milli> took = std::chrono::steady_clock::now() - start;
		catalogue->totalMilliseconds = took.count();
		for (const auto& timing : a_records->timings) {
			catalogue->totalMilliseconds += timing.milliseconds;
		}

		logger::info("scan: {} head parts, {} face morph sets, {} body morph sets, {} categories, {} problems in {:.0f}ms",
			catalogue->headParts.size(), catalogue->faceMorphs.size(), catalogue->bodyMorphs.size(), catalogue->categories.size(), catalogue->problems.size(), catalogue->totalMilliseconds);
		for (const auto& timing : catalogue->timings) {
			logger::info("  {}: {:.0f}ms, {} files", timing.source, timing.milliseconds, timing.files);
		}

		std::atomic_store(&_catalogue, std::shared_ptr<const Catalogue>(std::move(catalogue)));
		_bRunning = false;
	}
}

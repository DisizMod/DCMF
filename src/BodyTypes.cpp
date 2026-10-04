#include "BodyTypes.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <fstream>
#include <mutex>

namespace BodyTypes
{
	namespace
	{
		std::mutex g_lock;
		Config g_config;
		std::vector<Resolution> g_resolutions;
		const Scan::Catalogue* g_resolvedFrom = nullptr;

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		bool SameName(std::string_view a, std::string_view b)
		{
			return Lower(a) == Lower(b);
		}

		std::string MeshPath(const char* a_model)
		{
			if (!a_model || !*a_model) {
				return {};
			}
			auto path = Scan::Files::Normalize(a_model);
			return path.starts_with("meshes/") ? path : "meshes/" + path;
		}

		void ReadRows(const rapidjson::Value& a_value, std::vector<Row>& a_out)
		{
			if (!a_value.IsArray()) {
				return;
			}
			for (const auto& item : a_value.GetArray()) {
				if (!item.IsObject()) {
					continue;
				}
				Row row;
				if (const auto name = item.FindMember("name"); name != item.MemberEnd() && name->value.IsString()) {
					row.name = name->value.GetString();
				}
				if (const auto uv = item.FindMember("uv"); uv != item.MemberEnd() && uv->value.IsString()) {
					row.uv = uv->value.GetString();
				}
				if (const auto matches = item.FindMember("matches"); matches != item.MemberEnd() && matches->value.IsArray()) {
					for (const auto& match : matches->value.GetArray()) {
						if (match.IsString()) {
							row.matches.emplace_back(match.GetString());
						}
					}
				}
				if (!row.name.empty()) {
					a_out.push_back(std::move(row));
				}
			}
		}

		rapidjson::Value WriteRows(const std::vector<Row>& a_rows, rapidjson::Document::AllocatorType& a_allocator)
		{
			rapidjson::Value out(rapidjson::kArrayType);
			for (const auto& row : a_rows) {
				rapidjson::Value item(rapidjson::kObjectType);
				item.AddMember("name", rapidjson::Value(row.name.data(), a_allocator), a_allocator);
				item.AddMember("uv", rapidjson::Value(row.uv.data(), a_allocator), a_allocator);
				rapidjson::Value matches(rapidjson::kArrayType);
				for (const auto& match : row.matches) {
					matches.PushBack(rapidjson::Value(match.data(), a_allocator), a_allocator);
				}
				item.AddMember("matches", matches, a_allocator);
				out.PushBack(item, a_allocator);
			}
			return out;
		}

		// The output mesh without its _0/_1.nif, as a project's key has it
		std::string KeyOf(std::string_view a_model)
		{
			std::string key(a_model);
			for (const auto suffix : { "_0.nif"sv, "_1.nif"sv, ".nif"sv }) {
				if (key.ends_with(suffix)) {
					key.erase(key.size() - suffix.size());
					break;
				}
			}
			return key;
		}

		const Row* HeadRow(const Config& a_config, std::string_view a_path)
		{
			const auto path = Lower(a_path);
			for (const auto& row : a_config.heads) {
				for (const auto& match : row.matches) {
					if (path.find(Scan::Files::Normalize(match)) != std::string::npos) {
						return &row;
					}
				}
			}
			return nullptr;
		}

		std::string HeadType(const Config& a_config, std::string_view a_path)
		{
			const auto* row = HeadRow(a_config, a_path);
			return row ? row->name : std::string{};
		}

		// What vanilla's textures are painted for: the head's one layout, the body's by sex
		std::string VanillaUv(Scan::SkinPart a_part, bool a_bFemale)
		{
			return a_part == Scan::SkinPart::kHead ? "Vanilla" : a_bFemale ? "Vanilla Female" : "Vanilla Male";
		}

		// A body: the TRI beside the mesh names the project that built it, and the project names the row
		Resolution ResolveBody(const Config& a_config, const Scan::Catalogue& a_catalogue, const Scan::SkinModel& a_model)
		{
			Resolution out{ .part = Scan::SkinPart::kBody, .path = a_model.model, .type = std::string(kVanilla) };
			const auto key = KeyOf(a_model.model);
			const auto tri = std::ranges::find(a_catalogue.builtTris, key, &Scan::BuiltBodyTri::key);
			if (tri == a_catalogue.builtTris.end()) {
				out.reason = "no TRI beside the mesh: not built by BodySlide, or built without morphs";
				return out;
			}
			out.tri = tri->tri;

			// Same shapes, and every morph of the TRI among the project's sliders; the closest project when several
			auto triShapes = tri->shapes;
			std::ranges::sort(triShapes);
			const Scan::BodySlideProject* best = nullptr;
			std::size_t bestExtra = 0;
			for (const auto& project : a_catalogue.projects) {
				if (project.key != key) {
					continue;
				}
				auto shapes = project.shapes;
				std::ranges::sort(shapes);
				if (shapes != triShapes) {
					continue;
				}
				const bool bCovers = std::ranges::all_of(tri->morphs, [&](const std::string& a_morph) {
					return std::ranges::any_of(project.sliders, [&](const std::string& a_slider) { return SameName(a_slider, a_morph); });
				});
				if (!bCovers) {
					continue;
				}
				const auto extra = project.sliders.size() - tri->morphs.size();
				if (!best || extra < bestExtra) {
					best = &project;
					bestExtra = extra;
				}
			}
			if (!best) {
				out.reason = "the TRI's shapes and morphs match no project that builds this file";
				return out;
			}
			out.project = best->name;
			for (const auto& row : a_config.bodies) {
				if (std::ranges::any_of(row.matches, [&](const std::string& a_match) { return SameName(a_match, best->name); })) {
					out.type = row.name;
					out.uv = row.uv;
					return out;
				}
			}
			out.reason = std::format("project '{}' is in no body row", best->name);
			return out;
		}

		// Under the lock
		void ResolveLocked()
		{
			const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
			g_resolutions.clear();
			g_resolvedFrom = catalogue.get();
			if (!catalogue) {
				return;
			}
			for (const auto& model : catalogue->skinModels) {
				if (model.part == Scan::SkinPart::kBody) {
					auto out = ResolveBody(g_config, *catalogue, model);
					// A body BodySlide built that no row names counts as vanilla: said, so a row can be added for it
					if (!out.tri.empty() && out.type == kVanilla) {
						logger::warn("bodytypes: '{}' was built by BodySlide but is no body type: {}; it counts as {}", out.path, out.reason, VanillaUv(Scan::SkinPart::kBody, model.bFemale));
					}
					if (out.uv.empty()) {
						out.uv = VanillaUv(Scan::SkinPart::kBody, model.bFemale);
					}
					g_resolutions.push_back(std::move(out));
				} else if (model.part == Scan::SkinPart::kHead) {
					Resolution out{ .part = Scan::SkinPart::kHead, .path = model.model };
					if (const auto* row = HeadRow(g_config, model.model)) {
						out.type = row->name;
						out.uv = row->uv;
					} else {
						out.type = kVanilla;
						out.uv = VanillaUv(Scan::SkinPart::kHead, model.bFemale);
						out.reason = "no head row names this model";
					}
					g_resolutions.push_back(std::move(out));
				}
			}
		}

		void EnsureResolvedLocked()
		{
			if (g_resolvedFrom != Scan::Scanner::GetSingleton().GetCatalogue().get()) {
				ResolveLocked();
			}
		}
	}

	void Load()
	{
		std::ifstream file{ std::string(kPath) };
		Config config;
		if (!file.is_open()) {
			logger::warn("bodytypes: no {}; every actor reads as vanilla until one is saved", kPath);
		} else {
			const std::string text{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
			rapidjson::Document document;
			if (document.Parse(text.data()).HasParseError() || !document.IsObject()) {
				logger::warn("bodytypes: {} is not valid JSON; every actor reads as vanilla", kPath);
			} else {
				if (const auto bodies = document.FindMember("bodies"); bodies != document.MemberEnd()) {
					ReadRows(bodies->value, config.bodies);
				}
				if (const auto heads = document.FindMember("heads"); heads != document.MemberEnd()) {
					ReadRows(heads->value, config.heads);
				}
				logger::info("bodytypes: {} body row(s), {} head row(s) from {}", config.bodies.size(), config.heads.size(), kPath);
			}
		}
		std::scoped_lock lock(g_lock);
		g_config = std::move(config);
		g_resolvedFrom = nullptr;
	}

	void Save()
	{
		const auto config = GetConfig();
		rapidjson::Document document(rapidjson::kObjectType);
		auto& allocator = document.GetAllocator();
		document.AddMember("bodies", WriteRows(config.bodies, allocator), allocator);
		document.AddMember("heads", WriteRows(config.heads, allocator), allocator);
		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter writer(buffer);
		document.Accept(writer);
		std::ofstream file{ std::string(kPath) };
		if (!file.is_open()) {
			logger::warn("bodytypes: could not write {}", kPath);
			return;
		}
		file << buffer.GetString();
	}

	Config GetConfig()
	{
		std::scoped_lock lock(g_lock);
		return g_config;
	}

	void SetConfig(Config a_config)
	{
		std::scoped_lock lock(g_lock);
		g_config = std::move(a_config);
		ResolveLocked();
	}

	std::vector<std::string> Names(Scan::SkinPart a_part)
	{
		std::scoped_lock lock(g_lock);
		std::vector<std::string> out{ std::string(kVanilla) };
		for (const auto& row : a_part == Scan::SkinPart::kHead ? g_config.heads : g_config.bodies) {
			out.push_back(row.name);
		}
		return out;
	}

	std::vector<std::string> Layouts(Scan::SkinPart a_part)
	{
		std::scoped_lock lock(g_lock);
		std::vector<std::string> out;
		if (a_part == Scan::SkinPart::kHead) {
			out.push_back(VanillaUv(a_part, true));
		} else {
			out.push_back(VanillaUv(a_part, true));
			out.push_back(VanillaUv(a_part, false));
		}
		for (const auto& row : a_part == Scan::SkinPart::kHead ? g_config.heads : g_config.bodies) {
			if (!row.uv.empty() && std::ranges::none_of(out, [&](const std::string& a_uv) { return SameName(a_uv, row.uv); })) {
				out.push_back(row.uv);
			}
		}
		return out;
	}

	std::vector<Resolution> GetResolutions()
	{
		std::scoped_lock lock(g_lock);
		EnsureResolvedLocked();
		return g_resolutions;
	}

	std::string BodyPathOf(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		auto* race = a_actor ? a_actor->GetRace() : nullptr;
		auto* skin = a_actor ? a_actor->GetSkin() : nullptr;
		if (!base || !race || !skin) {
			return {};
		}
		const auto sex = base->GetSex() == RE::SEX::kFemale ? 1 : 0;
		for (const auto* addon : skin->armorAddons) {
			if (addon && addon->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody) && addon->IsValidRace(race)) {
				if (auto model = MeshPath(addon->bipedModels[sex].GetModel()); !model.empty()) {
					return model;
				}
			}
		}
		return {};
	}

	std::string HeadPathOf(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!base) {
			return {};
		}
		for (std::int8_t i = 0; i < base->numHeadParts; ++i) {
			const auto* part = base->headParts ? base->headParts[i] : nullptr;
			if (part && part->type.get() == RE::BGSHeadPart::HeadPartType::kFace) {
				return MeshPath(part->GetModel());
			}
		}
		auto* race = a_actor->GetRace();
		const auto sex = base->GetSex() == RE::SEX::kFemale ? 1 : 0;
		if (const auto* face = race ? race->faceRelatedData[sex] : nullptr; face && face->headParts) {
			for (const auto* part : *face->headParts) {
				if (part && part->type.get() == RE::BGSHeadPart::HeadPartType::kFace) {
					return MeshPath(part->GetModel());
				}
			}
		}
		return {};
	}

	std::string TypeOf(Scan::SkinPart a_part, std::string_view a_path)
	{
		std::scoped_lock lock(g_lock);
		if (a_part == Scan::SkinPart::kHead) {
			// Straight from the rows: an NPC's own face part need not be one the scan listed
			const auto type = HeadType(g_config, a_path);
			return type.empty() ? std::string(kVanilla) : type;
		}
		EnsureResolvedLocked();
		const auto it = std::ranges::find_if(g_resolutions, [&](const Resolution& a_resolution) { return a_resolution.part == a_part && a_resolution.path == a_path; });
		return it != g_resolutions.end() ? it->type : std::string(kVanilla);
	}

	std::string ModelFor(Scan::SkinPart a_part, std::string_view a_uv, std::string* a_why)
	{
		const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
		if (!catalogue) {
			if (a_why) {
				*a_why = "the scan has not finished";
			}
			return {};
		}
		std::scoped_lock lock(g_lock);
		EnsureResolvedLocked();

		// A body stands for its layout only when a row resolved it: one that fell to vanilla is not known to be vanilla
		const auto resolvedTo = [&](Scan::SkinPart a_resolvedPart) -> std::string {
			for (const auto& resolution : g_resolutions) {
				if (resolution.part == a_resolvedPart && SameName(resolution.uv, a_uv) && (a_resolvedPart != Scan::SkinPart::kBody || resolution.reason.empty())) {
					return resolution.path;
				}
			}
			return {};
		};

		if (a_part == Scan::SkinPart::kHead || a_part == Scan::SkinPart::kBody) {
			if (auto model = resolvedTo(a_part); !model.empty()) {
				return model;
			}
			if (a_why) {
				*a_why = std::format("no scanned {} resolves to the {} layout", Scan::SkinPartName(a_part), a_uv);
			}
			return {};
		}

		// Hands and feet follow the body: the ones worn by a race whose body has this layout, for the layout's sex
		const auto body = resolvedTo(Scan::SkinPart::kBody);
		const auto bodyModel = std::ranges::find(catalogue->skinModels, body, &Scan::SkinModel::model);
		if (body.empty() || bodyModel == catalogue->skinModels.end()) {
			if (a_why) {
				*a_why = std::format("no scanned body resolves to the {} layout", a_uv);
			}
			return {};
		}
		const bool bMale = SameName(a_uv, "Vanilla Male");
		for (const auto& model : catalogue->skinModels) {
			if (model.part != a_part || (bMale ? !model.bMale : !model.bFemale)) {
				continue;
			}
			if (std::ranges::any_of(model.races, [&](const std::string& a_race) { return std::ranges::find(bodyModel->races, a_race) != bodyModel->races.end(); })) {
				return model.model;
			}
		}
		if (a_why) {
			*a_why = std::format("no scanned {} for a race whose body is {}", Scan::SkinPartName(a_part), a_uv);
		}
		return {};
	}

	std::string UvOf(Scan::SkinPart a_part, std::string_view a_path, bool a_bFemale)
	{
		std::scoped_lock lock(g_lock);
		if (a_part == Scan::SkinPart::kHead) {
			const auto* row = HeadRow(g_config, a_path);
			return row && !row->uv.empty() ? row->uv : VanillaUv(a_part, a_bFemale);
		}
		EnsureResolvedLocked();
		const auto it = std::ranges::find_if(g_resolutions, [&](const Resolution& a_resolution) { return a_resolution.part == a_part && a_resolution.path == a_path; });
		return it != g_resolutions.end() && !it->uv.empty() ? it->uv : VanillaUv(a_part, a_bFemale);
	}
}

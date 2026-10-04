#pragma once

#include "scan/Scan.h"

#include <string>
#include <string_view>
#include <vector>

// Which body and which head an actor has, by name: the rows of DCMF.bodytypes.json, each naming the BodySlide projects
// or the head model paths that are that type, resolved against what the scan found
namespace BodyTypes
{
	constexpr auto kVanilla = "vanilla"sv;
	constexpr auto kPath = "Data/SKSE/Plugins/DCMF.bodytypes.json"sv;

	struct Row
	{
		std::string name;
		std::string uv;                    // the UV layout its textures are painted for: cbbe, unp, ube, vanillamale; vanilla, cotr, khajiit...
		std::vector<std::string> matches;  // body: project names; head: what the face model's path contains
	};

	struct Config
	{
		std::vector<Row> bodies;
		std::vector<Row> heads;
	};

	// One scanned naked model and what it came to
	struct Resolution
	{
		Scan::SkinPart part = Scan::SkinPart::kBody;
		std::string path;
		std::string type;     // a row's name, or vanilla
		std::string uv;       // the row's layout; vanilla's is by sex for a body
		std::string tri;      // body: the TRI beside it
		std::string project;  // body: the project whose sliders it carries
		std::string reason;   // why it fell to vanilla, when it did
	};

	// Main thread, once the game's data is loaded: the file shipped beside the plugin, as the user may have edited it
	void Load();
	void Save();

	// Copies; setting re-resolves
	[[nodiscard]] Config GetConfig();
	void SetConfig(Config a_config);

	// The names a condition can pick, vanilla first
	[[nodiscard]] std::vector<std::string> Names(Scan::SkinPart a_part);

	// The UV layouts a condition can pick: vanilla's, then every row's, once each
	[[nodiscard]] std::vector<std::string> Layouts(Scan::SkinPart a_part);

	// Every scanned body and head, resolved against the newest catalogue and config
	[[nodiscard]] std::vector<Resolution> GetResolutions();

	// The actor's naked body and its face, from its records; empty when it has none
	[[nodiscard]] std::string BodyPathOf(RE::Actor* a_actor);
	[[nodiscard]] std::string HeadPathOf(RE::Actor* a_actor);

	// The type of a path, vanilla when nothing names it
	[[nodiscard]] std::string TypeOf(Scan::SkinPart a_part, std::string_view a_path);

	// The UV layout of a path: its row's, else vanilla's for the part and sex
	[[nodiscard]] std::string UvOf(Scan::SkinPart a_part, std::string_view a_path, bool a_bFemale);

	// A scanned naked model of the part whose layout this is: a head or body that resolved to it, or for hands and
	// feet the ones a race with such a body wears; empty, with why, when none
	[[nodiscard]] std::string ModelFor(Scan::SkinPart a_part, std::string_view a_uv, std::string* a_why = nullptr);
}

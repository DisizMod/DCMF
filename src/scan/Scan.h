#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Scan
{
	enum class Target : uint8_t
	{
		kHeadParts,
		kFaceMorphs,
		kBodyMorphs,
		kBodySlideCategories,
		kBodyPresets,
		kSkinModels,

		kTotal
	};

	[[nodiscard]] std::string_view TargetName(Target a_target);

	// Where a skin mesh goes on an actor
	enum class SkinPart : uint8_t
	{
		kHead,
		kBody,
		kHands,
		kFeet,

		kTotal
	};

	[[nodiscard]] std::string_view SkinPartName(SkinPart a_part);

	// Who found an entry, and where
	struct Provenance
	{
		std::string source;
		std::string origin;  // a file, or a plugin
	};

	struct Vec3
	{
		float x = 0.f;
		float y = 0.f;
		float z = 0.f;
	};

	struct HeadPart
	{
		RE::FormID formID = 0;
		std::string plugin;
		std::string editorID;
		std::string name;
		RE::BGSHeadPart::HeadPartType type = RE::BGSHeadPart::HeadPartType::kMisc;
		bool bMale = false;
		bool bFemale = false;
		bool bPlayable = false;
		bool bExtraPart = false;
		std::string model;
		std::string tris[RE::BGSHeadPart::MorphIndices::kTotal];  // race, expression, chargen; Data-relative, lowercased
		std::vector<std::string> races;
		std::vector<RE::FormID> extraParts;
		std::vector<Provenance> foundBy;
	};

	struct ArmorAddon
	{
		RE::FormID formID = 0;
		std::string plugin;
		std::string models[2];  // male, female; Data-relative, lowercased
	};

	struct Morph
	{
		std::string name;
		std::string display;
		std::string category;
		std::string slider;  // the two-ended slider this morph is one half of
		int sign = 0;        // -1 the low half, +1 the high half
		float min = 0.f;     // the range its slider runs over
		float max = 1.f;
		std::vector<std::string> shapes;  // the shapes of its set it moves, where the source says
		std::vector<Provenance> foundBy;
	};

	// One morph name across every set that defines it
	struct MorphSummary
	{
		std::string name;
		std::string display;
		std::string category;
		float min = 0.f;
		float max = 1.f;
		std::vector<std::size_t> sets;  // into faceMorphs or bodyMorphs
		std::vector<Provenance> foundBy;
		std::string sources;  // the names in foundBy, once each
	};

	struct FaceMorphSet
	{
		std::string tri;       // Data-relative, lowercased
		std::string extends;   // the chargen TRI a RaceMenu extension adds to
		uint32_t vertexCount = 0;
		std::vector<Vec3> sample;
		std::vector<Morph> morphs;
		std::vector<RE::FormID> headParts;
		std::vector<Provenance> foundBy;
	};

	struct BodyMorphSet
	{
		std::string key;  // the output mesh without its _0/_1.nif, or a script's name
		std::string project;
		std::string tri;
		bool bBuilt = false;  // the output TRI exists
		std::vector<std::string> shapes;
		std::vector<Morph> morphs;
		std::vector<std::string> zaps;
		std::vector<RE::FormID> armorAddons;
		std::vector<Provenance> foundBy;
	};

	struct BodySlideCategory
	{
		std::string name;
		std::vector<Morph> sliders;
		std::vector<Provenance> foundBy;
	};

	// A BodySlide preset: a percentage per slider at each weight
	struct BodyPreset
	{
		struct Slider
		{
			std::string name;
			float atZero = 0.f;     // at weight 0
			float atHundred = 0.f;  // at weight 100
		};

		std::string name;
		std::string set;
		std::string origin;
		std::vector<std::string> groups;
		std::vector<Slider> sliders;
		std::vector<Provenance> foundBy;
	};

	// A mesh some race wears naked: a face, or a skin's body, hands or feet; what a texture is painted for
	struct SkinModel
	{
		std::string model;  // Data-relative, lowercased
		SkinPart part = SkinPart::kBody;
		bool bMale = false;
		bool bFemale = false;
		std::vector<std::string> races;   // editor IDs
		std::vector<std::string> shapes;  // the geometry names inside, read from the file
		std::vector<Provenance> foundBy;
	};

	// One BodySlide project as its file has it, kept apart from the merged sets: what it builds and every slider it carries
	struct BodySlideProject
	{
		std::string name;
		std::string key;  // the output mesh without its _0/_1.nif
		std::string origin;
		std::vector<std::string> shapes;
		std::vector<std::string> sliders;  // zaps included
	};

	// A .tri BodySlide wrote beside a mesh it built: the shapes and morph names of whatever project built it
	struct BuiltBodyTri
	{
		std::string key;
		std::string tri;
		std::vector<std::string> shapes;
		std::vector<std::string> morphs;
	};

	struct Problem
	{
		std::string source;
		std::string origin;
		std::string message;
	};

	struct SourceTiming
	{
		std::string source;
		Target target = Target::kHeadParts;
		float milliseconds = 0.f;
		uint32_t files = 0;
	};

	struct Catalogue
	{
		std::vector<HeadPart> headParts;
		std::vector<ArmorAddon> armorAddons;
		std::vector<FaceMorphSet> faceMorphs;
		std::vector<Morph> faceSliders;  // RaceMenu's slider files, folded into faceMorphs when linked
		std::vector<BodyMorphSet> bodyMorphs;
		std::vector<BodySlideCategory> categories;
		std::vector<BodyPreset> bodyPresets;
		std::vector<SkinModel> skinModels;
		std::vector<BodySlideProject> projects;
		std::vector<BuiltBodyTri> builtTris;
		std::vector<MorphSummary> faceMorphNames;  // built when linked
		std::vector<MorphSummary> bodyMorphNames;
		std::vector<Problem> problems;
		std::vector<SourceTiming> timings;
		float totalMilliseconds = 0.f;
	};

	struct Progress
	{
		std::atomic<uint32_t> done = 0;
		std::atomic<uint32_t> total = 0;
		std::atomic<bool> bFinished = false;
	};

	// What a source is handed while it runs
	struct Context
	{
		const Catalogue& records;  // what the record sources found, on the main thread, before any file source ran
		Catalogue& out;
		Progress& progress;
		std::string_view source;

		void Report(std::string_view a_origin, std::string a_message) const;
	};

	class ISource
	{
	public:
		enum class Stage : uint8_t
		{
			kRecords,  // main thread, first
			kFiles,    // worker threads, after the records
			kFollowUp  // worker threads, after every kFiles source, reading their findings
		};

		virtual ~ISource() = default;

		[[nodiscard]] virtual std::string_view GetName() const = 0;
		[[nodiscard]] virtual Target GetTarget() const = 0;
		[[nodiscard]] virtual Stage GetStage() const { return Stage::kFiles; }

		virtual void Run(Context& a_context) = 0;
	};

	class Scanner
	{
	public:
		static Scanner& GetSingleton()
		{
			static Scanner singleton;
			return singleton;
		}

		void AddSource(std::unique_ptr<ISource> a_source);

		// Main thread: runs the record sources, then hands the rest to a thread of its own
		void Start();

		[[nodiscard]] bool IsRunning() const { return _bRunning.load(); }
		[[nodiscard]] bool IsFinished() const { return static_cast<bool>(std::atomic_load(&_catalogue)); }

		// The last finished scan; null until one finishes
		[[nodiscard]] std::shared_ptr<const Catalogue> GetCatalogue() const { return std::atomic_load(&_catalogue); }

		struct SourceProgress
		{
			std::string_view name;
			Target target;
			uint32_t done;
			uint32_t total;
			bool bFinished;
		};
		[[nodiscard]] std::vector<SourceProgress> GetProgress() const;

	private:
		Scanner() = default;

		struct Entry
		{
			std::unique_ptr<ISource> source;
			std::unique_ptr<Progress> progress = std::make_unique<Progress>();
		};

		void RegisterSources();
		void RunWorkers(std::shared_ptr<Catalogue> a_records);

		std::vector<Entry> _sources;
		std::atomic<bool> _bRunning = false;
		std::shared_ptr<const Catalogue> _catalogue;
		std::jthread _thread;
	};

	// Files, as the game sees them through the virtual file system
	namespace Files
	{
		// Data-relative, forward slashes, lowercased
		[[nodiscard]] std::string Normalize(std::string_view a_path);

		// Loose first, then the archives unless told not to; empty when neither has it
		[[nodiscard]] std::vector<std::byte> Read(std::string_view a_dataRelative, bool a_bArchives = true);

		[[nodiscard]] std::string ReadText(const std::filesystem::path& a_path);

		// Loose files only: listing the archives is not possible here
		[[nodiscard]] std::vector<std::filesystem::path> List(std::string_view a_dataRelativeFolder, std::string_view a_extension, bool a_bRecursive);
	}

	// One helper per source family, each adding its own sources
	void AddHeadPartSources(Scanner& a_scanner);
	void AddFaceMorphSources(Scanner& a_scanner);
	void AddBodyMorphSources(Scanner& a_scanner);
	void AddCategorySources(Scanner& a_scanner);
	void AddPresetSources(Scanner& a_scanner);
	void AddSkinModelSources(Scanner& a_scanner);
}

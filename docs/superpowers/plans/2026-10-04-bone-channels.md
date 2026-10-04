# Bone Channels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every bone that a race skeleton, head part or armour addon in the load order brings gets six `bone:` channels: mode (Offset, Fixed or Released), scale, x, y, z and length. The spine and head pose segments gain Released.

**Architecture:** A new scan source reads node names from every skeleton, head part and armour addon NIF and categorises each bone. `Entries::Populate` turns each bone into six entries of the existing `bone` kind. The existing `Bone` applier reads the new mode and length entries; its take-back now covers what a length moves. `Pose::Expand` turns Released into a per-bone Released mode, and turns a bone on Fixed into a rest aim.

**Tech Stack:** C++23, CommonLibSSE-NG, MSVC, CMake; Dear ImGui for the Actor window.

**Spec:** `docs/superpowers/specs/2026-10-04-bone-channels-design.md`

## Global Constraints

- The working agreement in `CLAUDE.md` applies: single-line comments in OAR's style, no file headers, no new widgets.
- Line endings are mixed per file in `src`. Detect each file's ending before editing (`file <path>`), and keep it.
- Shell heredocs mangle backslashes. Use the Edit tool for source edits.
- Verify a build by checking the exit code and grepping for `error C`, `error LNK` and `Build FAILED`. A bare `error` also matches `UIErrorBanner.cpp`.
- Build: `cmake --build build --config Release`. Deploy check: `cmp -s build/src/Release/DCMF.dll "<SE mod folder>/SKSE/Plugins/DCMF.dll"`.
- Commits use the repo's configured identity (DisizMod, the noreply email) and UTC dates:
  `D=$(date -u +%Y-%m-%dT%H:%M:%S+0000); GIT_AUTHOR_DATE=$D GIT_COMMITTER_DATE=$D git commit -m "..."`. Never push.
- Channel names: `bone:<node>.<property>`. Properties, in stride order: `mode`, `scale`, `x`, `y`, `z`, `length`.
- Mode names, in this order: `Offset`, `Fixed`, `Released`.
- Ranges: scale 0.25 to 4 (neutral 1); x, y and z −1.5 to 1.5 radians (neutral 0); length −10 to 10 game units (neutral 0).
- Spine1, Spine2, Neck and Head show `scale` only; their other five entries are hidden.
- This project has no unit test harness. Each task is verified by a clean build. Task 6 is the in-game check.

## Review Focus

1. **A bone in hundreds of armour NIFs** (`NPC Spine2 [Spn2]` is in every body mesh) must be categorised by its skeleton, not as Shared. Task 1's category rule checks for a skeleton source first.
2. **Released handing a bone back mid-animation.** The pass-start take-back restores a bone only while its value still equals what DCMF wrote. A bone that animation or another mod has already rewritten keeps that other value. Task 6 checks both cases.
3. **A length on an animated bone's children.** Animation rewrites a child's translate every frame. The take-back must restore a child's translate only when it still equals what was written, or the edit compounds. Task 3 mirrors the rotation take-back for this.
4. **A mod file naming a bone the load order no longer has.** Such a bone is not in the table, so the loader reports the channel as unknown, the way it does for a missing morph. Task 6 checks that it's reported rather than silently dropped.
5. **The picker with thousands of new entries.** The channel combo and the Actor window must stay usable. Task 6 checks that opening them doesn't stall the frame.

---

### Task 1: Scan every bone the load order brings

**Files:**
- Create: `src/scan/BoneSources.cpp`
- Modify: `src/scan/Scan.h` (Target enum, two new structs, Catalogue, the source-family declaration)
- Modify: `src/scan/Scan.cpp` (TargetName, Merge, registration, the summary log line)
- Modify: `src/CMakeLists.txt:76` (add the new source after `scan/SkinModelSources.cpp`)

**Interfaces:**
- Produces: `Scan::Bone { std::string name; std::string category; }`, `Scan::Catalogue::bones` (sorted by category, then name), `Scan::Catalogue::boneSources`, `Scan::Target::kBones`, `Scan::AddBoneSources(Scanner&)`.

- [ ] **Step 1: Add the types to `src/scan/Scan.h`**

In `enum class Target`, add `kBones,` after `kSkinModels,`.

After `struct SkinModel { ... };`, add:

```cpp
	// A mesh some record brings bones in with: a race's skeleton, a head part's model, an armour addon's
	struct BoneSource
	{
		std::string model;   // Data-relative, lowercased
		std::string kind;    // "Skeleton", "Hair", "Armour"
		std::string record;  // the skeleton's folder, the head part's editor ID, the armour's name
		std::string slot;    // an armour addon's first biped slot, "Body (32)"; empty otherwise
	};

	// A bone some mesh of the load order has, and what it is listed under
	struct Bone
	{
		std::string name;
		std::string category;
	};
```

In `struct Catalogue`, after `std::vector<SkinModel> skinModels;`, add:

```cpp
		std::vector<BoneSource> boneSources;
		std::vector<Bone> bones;
```

After `void AddSkinModelSources(Scanner& a_scanner);`, add:

```cpp
	void AddBoneSources(Scanner& a_scanner);
```

- [ ] **Step 2: Wire it into `src/scan/Scan.cpp`**

In `TargetName`, before `default:`:

```cpp
		case Target::kBones:
			return "Bones"sv;
```

In `Merge`, after `Append(a_to.timings, a_from.timings);`:

```cpp
			Append(a_to.boneSources, a_from.boneSources);
			Append(a_to.bones, a_from.bones);
```

After `AddSkinModelSources(*this);` (line 524):

```cpp
		AddBoneSources(*this);
```

In `RunWorkers`, replace the summary log call with:

```cpp
		logger::info("scan: {} head parts, {} face morph sets, {} body morph sets, {} categories, {} bones, {} problems in {:.0f}ms",
			catalogue->headParts.size(), catalogue->faceMorphs.size(), catalogue->bodyMorphs.size(), catalogue->categories.size(), catalogue->bones.size(), catalogue->problems.size(), catalogue->totalMilliseconds);
```

- [ ] **Step 3: Write `src/scan/BoneSources.cpp`**

Match the line ending of `src/scan/SkinModelSources.cpp` (check it with `file`).

```cpp
#include "Scan.h"

#include "Nif.h"

#include <bit>
#include <set>

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

		// The folder a skeleton model sits in, which names it: "character assets", "argonian"
		std::string FolderOf(std::string_view a_model)
		{
			const auto slash = a_model.rfind('/');
			if (slash == std::string_view::npos) {
				return std::string(a_model);
			}
			const auto folder = a_model.substr(0, slash);
			const auto start = folder.rfind('/');
			return std::string(start == std::string_view::npos ? folder : folder.substr(start + 1));
		}

		std::string_view HeadPartKind(RE::BGSHeadPart::HeadPartType a_type)
		{
			using Type = RE::BGSHeadPart::HeadPartType;
			switch (a_type) {
			case Type::kFace:
				return "Face"sv;
			case Type::kEyes:
				return "Eyes"sv;
			case Type::kHair:
				return "Hair"sv;
			case Type::kFacialHair:
				return "Beard"sv;
			case Type::kScar:
				return "Scar"sv;
			case Type::kEyebrows:
				return "Brows"sv;
			default:
				return "Head part"sv;
			}
		}

		// An armour addon's first biped slot, by the engine's name where it has one
		std::string SlotOf(const RE::TESObjectARMA* a_addon)
		{
			const auto mask = static_cast<std::uint32_t>(a_addon->GetSlotMask().underlying());
			if (mask == 0) {
				return {};
			}
			const auto bit = static_cast<std::size_t>(std::countr_zero(mask));
			constexpr const char* kNames[]{ "Head", "Hair", "Body", "Hands", "Forearms", "Amulet", "Ring", "Feet", "Calves", "Shield", "Tail", "Long hair", "Circlet", "Ears" };
			return bit < std::size(kNames) ? std::format("{} ({})", kNames[bit], bit + 30) : std::format("Slot {}", bit + 30);
		}

		std::string LabelOf(const RE::TESForm* a_form, const char* a_name)
		{
			if (a_name && *a_name) {
				return a_name;
			}
			const char* editorID = a_form->GetFormEditorID();
			return editorID && *editorID ? editorID : std::format("{:08X}", a_form->GetFormID());
		}

		// The meshes that bring bones: every race's skeleton, every head part's model, every armour addon's models
		class BoneRecords : public ISource
		{
		public:
			std::string_view GetName() const override { return "Bone records"sv; }
			Target GetTarget() const override { return Target::kBones; }
			Stage GetStage() const override { return Stage::kRecords; }

			void Run(Context& a_context) override
			{
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					a_context.Report(""sv, "no data handler");
					return;
				}

				const auto& races = dataHandler->GetFormArray<RE::TESRace>();
				const auto& parts = dataHandler->GetFormArray<RE::BGSHeadPart>();
				const auto& addons = dataHandler->GetFormArray<RE::TESObjectARMA>();
				a_context.progress.total = races.size() + parts.size() + addons.size();

				std::set<std::tuple<std::string, std::string, std::string, std::string>> seen;
				const auto add = [&](std::string a_model, std::string_view a_kind, std::string a_record, std::string a_slot) {
					if (a_model.empty() || !seen.emplace(a_model, std::string(a_kind), a_record, a_slot).second) {
						return;
					}
					a_context.out.boneSources.push_back({ std::move(a_model), std::string(a_kind), std::move(a_record), std::move(a_slot) });
				};

				for (auto* race : races) {
					++a_context.progress.done;
					if (!race) {
						continue;
					}
					for (int sex = 0; sex < 2; ++sex) {
						const auto model = MeshPath(race->skeletonModels[sex].GetModel());
						add(model, "Skeleton"sv, FolderOf(model), {});
					}
				}

				for (auto* part : parts) {
					++a_context.progress.done;
					if (part) {
						add(MeshPath(part->GetModel()), HeadPartKind(part->type.get()), LabelOf(part, nullptr), {});
					}
				}

				// An addon is named by the first armour that uses it
				std::unordered_map<const RE::TESObjectARMA*, std::string> armourOf;
				for (auto* armour : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
					if (!armour) {
						continue;
					}
					const auto label = LabelOf(armour, armour->GetFullName());
					for (const auto* addon : armour->armorAddons) {
						if (addon) {
							armourOf.emplace(addon, label);
						}
					}
				}
				for (auto* addon : addons) {
					++a_context.progress.done;
					if (!addon) {
						continue;
					}
					const auto it = armourOf.find(addon);
					const auto record = it != armourOf.end() ? it->second : LabelOf(addon, nullptr);
					const auto slot = SlotOf(addon);
					for (int sex = 0; sex < 2; ++sex) {
						add(MeshPath(addon->bipedModels[sex].GetModel()), "Armour"sv, record, slot);
						add(MeshPath(addon->bipedModel1stPersons[sex].GetModel()), "Armour"sv, record, slot);
					}
				}
			}
		};

		// A bone's category: its skeleton's; else the one record that brings it; else the kind and slot they share; else Shared
		std::string CategoryOf(const std::vector<BoneSource>& a_sources, const std::vector<std::size_t>& a_from)
		{
			for (const auto i : a_from) {
				if (a_sources[i].kind == "Skeleton"sv) {
					return std::format("Skeleton · {}", a_sources[i].record);
				}
			}
			const auto& first = a_sources[a_from.front()];
			const auto all = [&](auto a_same) { return std::ranges::all_of(a_from, [&](std::size_t i) { return a_same(a_sources[i]); }); };
			if (all([&](const BoneSource& a_source) { return a_source.kind == first.kind && a_source.record == first.record; })) {
				return std::format("{} · {}", first.kind, first.record);
			}
			if (all([&](const BoneSource& a_source) { return a_source.kind == first.kind && a_source.slot == first.slot; })) {
				return first.slot.empty() ? first.kind : std::format("{} · {}", first.kind, first.slot);
			}
			return "Shared";
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
							a_context.Report(source.model, "not found, loose or in an archive");
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
```

- [ ] **Step 4: Add the file to `src/CMakeLists.txt`**

After the line `    scan/SkinModelSources.cpp`, add `    scan/BoneSources.cpp`.

- [ ] **Step 5: Build**

Run: `cmake --build build --config Release > "$TEMP/b.txt" 2>&1; echo exit=$?; grep -E "error C|error LNK|Build FAILED" "$TEMP/b.txt"`
Expected: `exit=0`, no matches.

- [ ] **Step 6: Commit**

```bash
git add src/scan/BoneSources.cpp src/scan/Scan.h src/scan/Scan.cpp src/CMakeLists.txt
D=$(date -u +%Y-%m-%dT%H:%M:%S+0000); GIT_AUTHOR_DATE=$D GIT_COMMITTER_DATE=$D git commit -m "The scan reads every bone the load order's skeletons, head parts and armour addons bring, each under its skeleton, the one record that brings it, the kind and slot its records share, or Shared"
```

### Task 2: Six entries per bone

**Files:**
- Modify: `src/apply/Bone.h` (the Property enum; a new Mode enum)
- Modify: `src/apply/Entries.h` (declare `AddBone`)
- Modify: `src/apply/Entries.cpp:77-96` (the fixed four bones), `Populate` (the scanned bones)
- Modify: `src/apply/Bone.cpp:388-391` (bone name from the entry's name, not its group)

**Interfaces:**
- Consumes: `Scan::Catalogue::bones` (Task 1).
- Produces: `Apply::Bone::Property { kMode, kScale, kX, kY, kZ, kLength, kTotal }`, `Apply::Bone::Mode { kOffset, kFixed, kReleased }`, entries `bone:<node>.mode|scale|x|y|z|length`.

- [ ] **Step 1: `src/apply/Bone.h`**

Replace the Property enum and its comment with:

```cpp
		// Six entries per bone, in this order
		enum class Property : std::uint32_t
		{
			kMode,
			kScale,
			kX,
			kY,
			kZ,
			kLength,
			kTotal
		};

		// A bone's mode entry, by its place in the entry's list
		enum class Mode : std::uint32_t
		{
			kOffset,
			kFixed,
			kReleased
		};
```

- [ ] **Step 2: `src/apply/Entries.h`**

After `void Populate(const Scan::Catalogue& a_catalogue);`, add:

```cpp
		void AddBone(std::string_view a_bone, std::string_view a_group, bool a_bPose);
```

- [ ] **Step 3: `src/apply/Entries.cpp`, the helper and the fixed four**

Replace the block from `// Bones: the spine and the head, four entries each` through the end of its `for` loop with:

```cpp
		// The bones the spine and head poses turn, before the scan: their scale alone in the pickers
		for (const auto* bone : { "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Neck [Neck]", "NPC Head [Head]" }) {
			AddBone(bone, "Skeleton · character assets", true);
		}
```

Add this member function after `AddFixed`:

```cpp
	// A bone's six entries, in the order Bone::Property counts; a pose bone shows its scale alone
	void Entries::AddBone(std::string_view a_bone, std::string_view a_group, bool a_bPose)
	{
		const auto add = [&](std::string_view a_property, std::string a_description, ValueType a_type, float a_low, float a_high, float a_neutral, std::vector<std::string> a_modes = {}) {
			Entry entry;
			entry.name = std::format("{}.{}", a_bone, a_property);
			entry.display = std::format("{} {}", a_bone, a_property);
			entry.group = a_group;
			entry.description = std::move(a_description);
			entry.type = a_type;
			entry.low = a_low;
			entry.high = a_high;
			entry.neutral = a_neutral;
			entry.modes = std::move(a_modes);
			entry.bHidden = a_bPose && a_property != "scale"sv;
			Add(Kind::kBone, std::move(entry));
		};
		// The modes in the order Bone::Mode counts
		add("mode", "Offset adds the angles to the animation, Fixed puts them on the bone's rest, Released leaves the bone to the animation and other mods.", ValueType::kMode, 0.f, 1.f, -1.f, { "Offset", "Fixed", "Released" });
		add("scale", "Scales the bone and everything below it.", ValueType::kNumber, 0.25f, 4.f, 1.f);
		add("x", "Turns the bone about its x axis, in radians.", ValueType::kNumber, -1.5f, 1.5f, 0.f);
		add("y", "Turns the bone about its y axis, in radians.", ValueType::kNumber, -1.5f, 1.5f, 0.f);
		add("z", "Turns the bone about its z axis, in radians.", ValueType::kNumber, -1.5f, 1.5f, 0.f);
		add("length", "Moves the bone's children away from it, in game units, as XPMSE's length sliders do.", ValueType::kNumber, -10.f, 10.f, 0.f);
	}
```

- [ ] **Step 4: `src/apply/Entries.cpp`, the scanned bones**

At the end of `Populate`, add:

```cpp
		// Bones: every one the load order's meshes bring, under what brings it
		for (const auto& bone : a_catalogue.bones) {
			if (!Find(Kind::kBone, std::format("{}.mode", bone.name)).IsValid()) {
				AddBone(bone.name, bone.category, false);
			}
		}
```

- [ ] **Step 5: `src/apply/Bone.cpp`, the bone's node by its name**

Replace:

```cpp
					// The node is the entry's group: the bone name the skeleton scan found
					BoneWrite write;
					const auto name = entries.Get({ Kind::kBone, slot * perBone }).group;
```

with:

```cpp
					// The node is the entry's name without its property: the bone name the scan found
					BoneWrite write;
					const auto entryName = entries.Get({ Kind::kBone, slot * perBone }).name;
					const auto name = entryName.substr(0, entryName.rfind('.'));
```

In the same per-bone loop's `switch`, the existing cases keep working. Add `case Property::kMode:` and `case Property::kLength:` to the `default: break;` group for now; Task 3 gives them meaning.

- [ ] **Step 6: Build**

Run: `cmake --build build --config Release > "$TEMP/b.txt" 2>&1; echo exit=$?; grep -E "error C|error LNK|Build FAILED" "$TEMP/b.txt"`
Expected: `exit=0`, no matches.

- [ ] **Step 7: Commit**

```bash
git add src/apply/Bone.h src/apply/Bone.cpp src/apply/Entries.h src/apply/Entries.cpp
D=$(date -u +%Y-%m-%dT%H:%M:%S+0000); GIT_AUTHOR_DATE=$D GIT_COMMITTER_DATE=$D git commit -m "Every scanned bone has six channels, mode, scale, three turns and length, under its category; the spine and head bones show their scale alone"
```

### Task 3: Mode and length in the bone pass

**Files:**
- Modify: `src/apply/Data.h:140-149` (`BoneWrite`; a new `ChildWrite`)
- Modify: `src/apply/Bone.cpp` (`Apply`: the take-back, Released, `writeTurn`, the per-bone loop)

**Interfaces:**
- Consumes: `Bone::Property`, `Bone::Mode` (Task 2).
- Produces: `BoneWrite::first`, `BoneWrite::bRested`, `BoneWrite::children`. `writeTurn` skips Released bones; `bRested` is set when a skeleton rest replaces a bone's rotation.

- [ ] **Step 1: `src/apply/Data.h`**

Before `struct BoneWrite`, add:

```cpp
	// A child a length moved: its offset from the bone as found, and as left
	struct ChildWrite
	{
		RE::NiPointer<RE::NiAVObject> node;
		RE::NiPoint3 animated;
		RE::NiPoint3 written;
	};
```

In `struct BoneWrite`, after `bool bWritten = false;`, add:

```cpp
		RE::NiMatrix3 first;                // the rotation it had when first seen, before any write of ours: a Fixed bone's rest when the skeleton has none
		bool bRested = false;               // this pass: a skeleton rest replaced its rotation
		std::vector<ChildWrite> children;   // what a length moved, taken back with the rest
```

- [ ] **Step 2: `src/apply/Bone.cpp`, the take-back covers children**

Add to the anonymous namespace, after `SameRotation`:

```cpp
		bool SamePoint(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
		{
			return a_a.x == a_b.x && a_a.y == a_b.y && a_a.z == a_b.z;
		}
```

In `takeBack`, before `a_bone.bWritten = false;`, add:

```cpp
				for (auto& child : a_bone.children) {
					if (child.node && SamePoint(child.node->local.translate, child.written)) {
						child.node->local.translate = child.animated;
					}
				}
				a_bone.children.clear();
				a_bone.bRested = false;
```

- [ ] **Step 3: `src/apply/Bone.cpp`, the Released bones**

After `constexpr auto perBone = static_cast<std::uint32_t>(Property::kTotal);`, add:

```cpp
			// A bone on Released is not ours: nothing below writes it, the take-back above having put it back
			std::vector<std::uint32_t> released;
			for (const auto& sample : samples) {
				if (sample.index % perBone == static_cast<std::uint32_t>(Property::kMode) && static_cast<Mode>(static_cast<std::uint32_t>(sample.value)) == Mode::kReleased) {
					released.push_back(sample.index / perBone);
				}
			}
			const auto isReleased = [&](std::uint32_t a_slot) { return std::ranges::find(released, a_slot) != released.end(); };
```

In `writeTurn`, right after `const auto slot = id.GetIndex() / perBone;`, add:

```cpp
					if (isReleased(slot)) {
						return;
					}
```

In both `insert` calls in `writeTurn`, record the first rotation: `BoneWrite{ .node = RE::NiPointer(a_node), .first = a_node->local.rotate }`.

In `writeTurn`'s `if (a_bReplace) {` branch, after `write.base = Partway(write.base, a_by, a_weight);`, add `write.bRested = true;`.

- [ ] **Step 4: `src/apply/Bone.cpp`, the per-bone loop**

Replace the per-bone loop (from `// Per bone: scale and three angles` to the end of its `for`) with:

```cpp
			// Per bone: its mode, scale, three angles and length, gathered from the sorted samples, on top of the aims:
			// an angle the author sets is an offset on whatever the tracking or the rest did
			for (std::size_t i = 0; i < samples.size();) {
				const auto slot = samples[i].index / perBone;
				auto mode = Mode::kOffset;
				float scale = 1.f;
				float angles[3]{};
				float length = 0.f;
				for (; i < samples.size() && samples[i].index / perBone == slot; ++i) {
					switch (static_cast<Property>(samples[i].index % perBone)) {
					case Property::kMode:
						mode = static_cast<Mode>(static_cast<std::uint32_t>(samples[i].value));
						break;
					case Property::kScale:
						scale = samples[i].value;
						break;
					case Property::kX:
						angles[0] = samples[i].value;
						break;
					case Property::kY:
						angles[1] = samples[i].value;
						break;
					case Property::kZ:
						angles[2] = samples[i].value;
						break;
					case Property::kLength:
						length = samples[i].value;
						break;
					default:
						break;
					}
				}
				if (mode == Mode::kReleased) {
					continue;
				}
				auto bone = std::ranges::find(a_data.bones, slot, [](const auto& a_entry) { return a_entry.first; });
				if (bone == a_data.bones.end()) {
					// The node is the entry's name without its property: the bone name the scan found
					BoneWrite write;
					const auto entryName = entries.Get({ Kind::kBone, slot * perBone }).name;
					const auto name = entryName.substr(0, entryName.rfind('.'));
					if (!name.empty()) {
						write.node = RE::NiPointer(root->GetObjectByName(RE::BSFixedString(name)));
					}
					if (write.node) {
						write.first = write.node->local.rotate;
					} else {
						logger::info("bone: '{}' is not on this actor's skeleton", name);
					}
					bone = a_data.bones.insert(a_data.bones.end(), { slot, std::move(write) });
				}
				auto& write = bone->second;
				if (!write.node) {
					continue;
				}

				if (!write.bWritten) {
					write.animated = write.node->local;
				}
				// Fixed with no skeleton rest put on above: the rotation it had when first seen
				if (mode == Mode::kFixed && !write.bRested) {
					write.node->local.rotate = write.first;
				}
				RE::NiMatrix3 turn;
				turn.SetEulerAnglesXYZ(angles[0], angles[1], angles[2]);
				write.node->local.rotate = write.node->local.rotate * turn;
				write.node->local.scale = write.animated.scale * scale;
				// Length: each child moved away from the bone along the line to it, its offset as found kept for the take-back
				if (length != 0.f) {
					if (auto* node = write.node->AsNode()) {
						for (auto& child : node->GetChildren()) {
							if (!child) {
								continue;
							}
							const auto offset = child->local.translate;
							const float distance = offset.Length();
							if (distance < 1e-3f) {
								continue;
							}
							child->local.translate = offset + offset * (length / distance);
							write.children.push_back({ RE::NiPointer(child.get()), offset, child->local.translate });
						}
					}
				}
				write.written = write.node->local;
				write.bWritten = true;
			}
```

- [ ] **Step 5: Build**

Run: `cmake --build build --config Release > "$TEMP/b.txt" 2>&1; echo exit=$?; grep -E "error C|error LNK|Build FAILED" "$TEMP/b.txt"`
Expected: `exit=0`, no matches.

- [ ] **Step 6: Commit**

```bash
git add src/apply/Data.h src/apply/Bone.cpp
D=$(date -u +%Y-%m-%dT%H:%M:%S+0000); GIT_AUTHOR_DATE=$D GIT_COMMITTER_DATE=$D git commit -m "The bone pass reads a bone's mode and length: Released writes nothing, Fixed puts the bone on its rest or on what it had when first seen, and a length moves its children out along the line to each, taken back with the rest"
```

### Task 4: Released on the spine and head; Fixed bones' rests

**Files:**
- Modify: `src/apply/Data.h` (`PoseSample::Mode` gains `kReleased`)
- Modify: `src/Pose.cpp` (`ModesFor`, `ModeName`, `ModeOf`, a `SetNumber` helper, `Expand`)
- Modify: `src/apply/Entries.cpp` (the spine and head mode lists and description)

**Interfaces:**
- Consumes: `Apply::Bone::Property`, `Apply::Bone::Mode` (Task 2); `writeTurn`'s Released check and `bRested` (Task 3).
- Produces: pose mode `Released` for the spine and head segments.

- [ ] **Step 1: `src/apply/Data.h`**

In `PoseSample::Mode`, after `kEngine   // the gaze only: the engine's own eye tracking`, add:

```cpp
			kReleased  // the spine and head only: nothing written, the bones handed back
```

(Add a comma after `kEngine`.)

- [ ] **Step 2: `src/Pose.cpp`, names**

Add `#include "apply/Bone.h"` after `#include "apply/Entries.h"`.

In `ModesFor`, the non-gaze return becomes:

```cpp
		return { std::string(ModeName(Apply::PoseSample::Mode::kOffset)), std::string(ModeName(Apply::PoseSample::Mode::kFixed)), std::string(ModeName(Apply::PoseSample::Mode::kTrack)), std::string(ModeName(Apply::PoseSample::Mode::kReleased)) };
```

In `ModeName`, before `case Apply::PoseSample::Mode::kOffset:`:

```cpp
		case Apply::PoseSample::Mode::kReleased:
			return "Released"sv;
```

In `ModeOf`, add `Apply::PoseSample::Mode::kReleased` to the list the loop walks.

- [ ] **Step 3: `src/apply/Entries.cpp`, the pose mode lists**

Replace the non-gaze list `std::vector<std::string>{ "Offset", "Fixed", "Track" }` with `std::vector<std::string>{ "Offset", "Fixed", "Track", "Released" }`, and its description `"Offset adds the angles to the animation, Fixed holds them, Track turns toward the target."` with `"Offset adds the angles to the animation, Fixed holds them, Track turns toward the target, Released leaves the bones to the animation and other mods."`.

- [ ] **Step 4: `src/Pose.cpp`, a setter beside `AddNumber`**

After `AddNumber`, add:

```cpp
		// A value put in place of whatever the sample holds, not added: a mode
		void SetNumber(ActorState::Sampled& a_out, Apply::Kind a_kind, std::string_view a_name, float a_value)
		{
			const auto id = Apply::Entries::GetSingleton().Require(a_kind, a_name);
			if (!id.IsValid()) {
				return;
			}
			auto& numbers = a_out.numbers[static_cast<std::size_t>(a_kind)];
			if (auto it = std::ranges::find(numbers, id.GetIndex(), &Apply::NumberSample::index); it != numbers.end()) {
				it->value = a_value;
			} else {
				numbers.push_back({ id.GetIndex(), a_value });
			}
		}
```

- [ ] **Step 5: `src/Pose.cpp`, Released in `Expand`**

In the loop `for (const auto& [segment, sample] : poses) {`, right after the `engine >= 1.f` check's closing brace, add:

```cpp
				// Released: the segment's bones are handed back at once, their own channels with them
				if (segment < 2 && a_memory.mode[segment] == Apply::PoseSample::Mode::kReleased) {
					constexpr auto kReleased = static_cast<float>(Apply::Bone::Mode::kReleased);
					if (segment == 0) {
						for (const auto& bone : SpineChain()) {
							SetNumber(a_out, Apply::Kind::kBone, std::format("{}.mode", bone.name), kReleased);
						}
					} else {
						SetNumber(a_out, Apply::Kind::kBone, std::format("{}.mode", HeadChain()[0].name), kReleased);
					}
					continue;
				}
```

- [ ] **Step 6: `src/Pose.cpp`, rests for bones on Fixed**

After the A pose block (`if (a_out.bAPose) { ... }`), add:

```cpp
		// A bone on Fixed sits on the skeleton's rest, its own angles on top; one the skeleton has no rest for is left to
		// the bone pass, which puts it on what it had when first seen
		{
			constexpr auto perBone = static_cast<std::uint32_t>(Apply::Bone::Property::kTotal);
			const auto& bones = a_out.numbers[static_cast<std::size_t>(Apply::Kind::kBone)];
			std::optional<Rest> rest;
			Apply::AimSample aim{ .segment = 4, .goal = {}, .weight = 1.f, .bRest = true };
			for (const auto& sample : bones) {
				if (sample.index % perBone != static_cast<std::uint32_t>(Apply::Bone::Property::kMode) || static_cast<Apply::Bone::Mode>(static_cast<std::uint32_t>(sample.value)) != Apply::Bone::Mode::kFixed) {
					continue;
				}
				if (!rest && !(rest = RestFor(a_actor))) {
					break;
				}
				const auto entryName = entries.Get({ Apply::Kind::kBone, sample.index }).name;
				const auto name = entryName.substr(0, entryName.rfind('.'));
				if (const auto it = rest->bones.find(name); it != rest->bones.end()) {
					aim.rests.emplace_back(name, it->second);
				}
			}
			if (!aim.rests.empty()) {
				a_out.aims.push_back(std::move(aim));
			}
		}
```

- [ ] **Step 7: Build**

Run: `cmake --build build --config Release > "$TEMP/b.txt" 2>&1; echo exit=$?; grep -E "error C|error LNK|Build FAILED" "$TEMP/b.txt"`
Expected: `exit=0`, no matches.

- [ ] **Step 8: Commit**

```bash
git add src/apply/Data.h src/Pose.cpp src/apply/Entries.cpp
D=$(date -u +%Y-%m-%dT%H:%M:%S+0000); GIT_AUTHOR_DATE=$D GIT_COMMITTER_DATE=$D git commit -m "The spine and head poses have Released, which hands their bones back with their own channels; a bone on Fixed sits on the skeleton's rest"
```

### Task 5: Bones in the Actor window

**Files:**
- Modify: `src/UI/UIActor.cpp:721-747` (the families list and `familyOf`)

**Interfaces:**
- Consumes: the `bone` entries (Task 2).

- [ ] **Step 1: Add the family**

Change `families` to:

```cpp
		constexpr std::array families{ "Engine MFG", "Face Morph", "Body Morph", "Actor & Body Preset", "Head Part", "Skin", "Overlay", "Pose", "Bone" };
```

Update the comment above it from "eight families" to "nine families".

In `familyOf`, before `default:`:

```cpp
			case Apply::Kind::kBone:
				return 8;
```

- [ ] **Step 2: Build**

Run: `cmake --build build --config Release > "$TEMP/b.txt" 2>&1; echo exit=$?; grep -E "error C|error LNK|Build FAILED" "$TEMP/b.txt"`
Expected: `exit=0`, no matches.

- [ ] **Step 3: Commit**

```bash
git add src/UI/UIActor.cpp
D=$(date -u +%Y-%m-%dT%H:%M:%S+0000); GIT_AUTHOR_DATE=$D GIT_COMMITTER_DATE=$D git commit -m "The Actor window lists the bone channels, under their categories"
```

### Task 6: In-game verification

**Files:** none.

- [ ] **Step 1: Deploy check**

Run: `cmp -s build/src/Release/DCMF.dll "<SE mod folder>/SKSE/Plugins/DCMF.dll"; echo $?`
Expected: `0`.

- [ ] **Step 2: The scan (user, in game, then the log)**

Load a save. In `Documents/My Games/Skyrim Special Edition/SKSE/DCMF.log`, expect the line `scan: … {N} bones …` with N in the hundreds or more, and timings for `Bone records` and `Bone meshes`.

- [ ] **Step 3: The checks the spec names (user, in game)**

In the Actor window's Bone family, holding rows on a tracked actor:
- `NPC L Thigh [LThg] length` at 5: the thigh lengthens and the calf stays its size. Release it, and the leg goes back exactly.
- A breast bone's `scale` at 1.5 with physics running: it scales and still moves with the physics.
- A skirt bone on `Fixed`, with a skirt worn: it holds still against the animation.
- `Released` on a bone that RaceMenu's XPMSE sliders also scale: the XPMSE scale shows, and DCMF's doesn't.
- The spine on `Released` in the Pose family while a bone row holds Spine2's scale: Spine2 is untouched.
- Opening the channel picker and the Bone family: no visible stall.
- A mod file naming `bone:NoSuchBone.scale`: the load reports it.

What to check throughout: nothing drifts over time, and each Released or let-go hands the bone back exactly.

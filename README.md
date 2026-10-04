# DCMF -- Dynamic Character Modifier Framework

Facial animation authority for **Skyrim SE 1.5.97 and AE 1.6.317+**, per
`FACIAL-ANIMATION-SYSTEM.md`.

**Runtimes.** SE 1.5.97 is where every hook was found and run. AE (1.6.317 up
to the current 1.6.1179, GOG included -- one Address Library id space) places
the same hooks by their published ids: Open Animation Replacer's for the four
overlay call sites, Mfg Fix NG's for the keyframe update, CommonLibSSE-NG's own
for the three headtracking functions; the two vtable hooks resolve through the
library. VR is refused at load (no keyframe or headtrack ids, and a face-node
vtable that diverges). Both need the Address Library for SKSE Plugins for their
runtime.

Licensed **GPL-3.0-or-later**, because it links CommonLibSSE-NG, builds on Open Animation Replacer and ports fixes
from Mfg Fix NG. See `LICENSE` and `THIRD-PARTY.md`.

This is **milestone 1 — prove the render path**. The design document's own build
order gates everything else behind it: parse a TRI, apply one named morph per
frame to one actor, watch it move, and do not proceed until that is boring.

## Status

**Milestone 1 is done.** A named morph holds on an actor's face, and the engine's
own blinking and dialogue lip sync composite on top of it.

| | |
|---|---|
| TRI parser, morph registry, config, blink timing | done, **108 tests / 291 assertions** |
| Per-actor TRI discovery from head parts | done -- male, High Poly Head, UBE all resolve themselves |
| `morphs.ini` / slider discovery, categorised | done -- hundreds of morphs, grouped as RaceMenu groups them |
| Keyframe hook (`KeyframesUpdate`, id 25983) | done -- we own blinking and the console/script merge |
| Morphs with an engine channel | **working** -- driven through the keyframe layer |
| Morphs without one (`EXPR2_*`, `LIP_*` ...) | **working** -- driven through the base morph data |
| In-game UI (ImGui overlay, Shift+L) | done -- target picker, categorised sliders, live engine state, and the game keeps running while it is open |
| Condition-driven entries (JSON groups, priority, blend modes) | done -- loads, matches, resolves, drives, and authors in the panel |
| Conditions | done -- **124**, in Open Animation Replacer's file format, nestable |
| Sequences (interval trigger, timed steps, conditions) | done -- plays over the entries and releases when it ends |
| Head and gaze driven from entries and steps | done -- four angles that fold, ease and save like any slider |
| Morph catalogue for authoring | done for loose files -- names read without their deltas, so 832 head meshes cost a scan rather than gigabytes |
| Transitions (six curves, rate, separate exit, game time) | done -- per slider, shared by entries and sequences; a colour mixes in sRGB, Oklab or Oklch (`"space"` on its transition), and leaves over the gap it arrived across |
| Suppressing the engine's own animation | done -- blink, lip sync, dialogue emotion, eye tracking and head tracking each stop on a checkbox, at the dialogue tier as well as the final one |

## Appearance authoring (2026-09-13)

The face system became an appearance system. Everything the probes proved --
overlays of our own (no RaceMenu), body morphs by direct buffer writes, hair
and skin tint, gloss, eye glow, eye texture, hairstyle, brows, beard and scar
swaps (the player's only: an NPC's head is a FaceGen bake), the actor's weight, look-at-camera for head and eyes -- is authored through one
model:

| | |
|---|---|
| **channel** | one typed value on an actor: a morph, a colour, a texture, a switch, an aim mode. Everything is resolved on channels |
| **channel group** | the channels a modifier or clip sets together, with the ones it cannot do without marked *required*: written on the modifier, or a reference to one of the mod's channel group presets. Private to the mod, nothing registered, no built-ins; the list is picked from every channel there is, in the Actor window's sections (`docs/channel-groups.md`) |
| **slot** | a named overlay layer on one part, with a pool of overlays any mod may add to; textures keyed by the UV layout the actor's head or body has -- heads `vanilla` (HPH shares it) or `ube` (COtR is close enough to use it); bodies `vanillafemale`, `vanillamale` (HIMBO and TNG too), `cbbe` (3BA too), `unp` (BHUNP too), `ube`. An actor's layouts are decided from exact facts -- the model paths it wears and the plugins loaded, per `DCMF.families.json` -- or set by hand in Settings; never read off a name |
| **modifier** | held while its conditions hold: priority, channel groups with per-channel *required* flags, values (static, computed off the actor, or drawn once per actor), one transition (`instant`, `smooth`, or a clip), and the mod's appearance presets it uses by reference |
| **clip** | a triggered timeline of keyframes over channels -- triggers: an interval, conditions starting or ending to hold, a dialogue line, an animation event, **an animation starting or ending** (the file the behaviour graph plays, an Open Animation Replacer replacement's own path included), a hit, death, a modifier switching on or off; `stopWhenConditionsFail` lets a running clip go the moment its conditions fail, so with `IsPlayingAnimation` it lasts exactly as long as the animation; also a modifier's way in or out. Its frames are edited as a list, or on the **Timeline** window -- a ruler with a handle per frame, dragged to retime it, and a row per keyed channel drawing what the run has it at (a number's curve, a colour's gradient, a stepped channel's blocks); the ruler scrubs the pose onto the actor, and a frame clicked opens in a window with the same frame editor. An animation named in a trigger or an `IsPlayingAnimation` condition has a **Play** button: it reads the actor's behaviour graph through Havok's own reflection for the state whose clip plays the file and the event whose transition enters it, and sends that event, so the graph plays it as the game would -- the clip's own triggers run, the animation object it holds included -- and the start and end triggers fire; a replacement's path (Open Animation Replacer's or Dynamic Animation Replacer's folders) enters the state of the animation it stands in for, and OAR binds whichever replacement its conditions pick; a file no state plays is played directly over the graph instead, the way Open Animation Replacer previews one, and its start and end triggers fire all the same -- either way the button tests what is attached to the animation |
| **presets** | three kinds, all private to the mod: **conditions** (used by the `PRESET` condition), **appearance** (groups and values a modifier uses by reference -- every channel the preset touches is locked in the modifier, editable in the preset or by making the reference a copy), **channel groups** (a named channel list groups refer to) |
| **source** | another mod's state, registered by name for the load order and read as a value: a mod event it sends (its string, its number, or that it fired within n seconds), a value it keeps in StorageUtil or JContainers, a Papyrus function it exposes, a faction rank, a global, an actor or graph variable. Typed number, string or bool, with an actor rule -- the actor being evaluated, the player, nobody, or where a mod event names its actor. A modifier or a clip key takes a string source as its reference or mode (`$hair`, an overlay of a pool) and a bool source as a switch; a number source drives a number through a driver; a `Source` condition compares one. The other mod never knows DCMF exists, and priority stays in the DCMF file |

Cross-mod, then, is one-directional on purpose: other mods **say** what state
they are in, in whatever way they already do, and DCMF files **decide** what a
face makes of it. A wig mod that sends `SendModEvent("WigMod_Hair",
"KS_Hair12")` from the actor's script has picked a hairstyle for every DCMF file
that registers that event as a string source and points `$hair` at it; an
arousal framework that stores a float in StorageUtil has become a blush driver.
Settings lists the mod events heard, so an author can find an event's name by
watching. No script of DCMF's is involved and nothing is saved: a source with no
value yet is absent, and reads as rest.

Several mods drive one actor at once: the highest priority wins each channel,
blend modes fold above an overwrite, and a modifier that loses a channel it
marked required withdraws that whole channel group -- unless the winner asked
for the same value. The editor has Open Animation
Replacer's three modes: inspect, user (saves `<mod>.user.json` beside the
mod's own) and author.

## Conditions

An entry or a sequence holds a set of conditions, and a set is implicitly AND --
every one of them must hold. There are **134** to choose from, grouped as the
picker groups them: structure, identity, state, magic, equipment and inventory,
world, target and mount, and values and packages. Twelve are this mod's own,
each standing for a question authors kept re-deriving: `HeadLayout` and
`BodyLayout` hold when the actor's head or body is of a UV layout -- vanilla or
UBE for a head; vanilla female, vanilla male, CBBE, UNP or UBE for a body -- as
the families table decides it from the part the actor actually wears; `Body`
names the body mod itself (CBBE, 3BA, UNP, BHUNP, UBE, HIMBO) off the geometry
names BodySlide writes into the actor's own skin mesh, so "is this 3BA" is one
condition rather than a race list; `RaceFamily` is human, elf or beast, a
custom race asked through the race it borrows its armour from; `IsNaked`,
`HasHeadCover` and `HasHelmet` read the body and head slots; `TimeOfDay` is
dawn, day, dusk or night by the climate's own sunrise and sunset; `IsIndoors`
and `IsInWater` are the cell and the engine's water flag. Plus `IsPlayer` and
`IsDead`.

Six of them hold a set of their own rather than asking an actor anything:

| Condition | Holds when |
|---|---|
| `AND` | every one of its children holds |
| `OR` | any one of them holds |
| `XOR` | exactly one of them holds |
| `TARGET` | its children hold for whoever this actor is targeting |
| `PLAYER` | its children hold for the player |
| `MOUNT` | its children hold for what this actor is riding |

The last three are why nesting is worth having: *the person I am looking at is
my friend* is a better facial condition than anything about the actor alone.
`PRESET` is the seventh, and stands for a named set on the group -- fix it there
and it is fixed everywhere it is used. A preset may not reference a preset,
which is refused at load rather than detected as a cycle.

An argument is a typed component -- a form, a keyword, a comparison, a number
that can read a global, an actor value or a graph variable -- so "wince below
thirty percent health" is a `CompareValues` with an actor value on one side and
`0.3` on the other, rather than a string somebody has to parse.

**The file format is Open Animation Replacer's**, so a condition set can be
copied between the two mods. A file written in the older flat format this mod
used to write still loads, and is upgraded when it is next saved. A condition
nothing here knows never matches, warns at load, and reads *unknown -- never
matches* in red in the editor, rather than quietly passing.

**Incompatible with Mfg Fix and Mfg Fix NG -- remove `mfgfix.dll`.** This mod
hooks the same function they do, by choice: their fixes are here (the re-apply
flag, the console and script channels merged into the finals with NG's
Look-group rule, `docs/mfgfix-interop.md`), and so are the seven Papyrus
natives their scripts declare -- `MfgConsoleFunc.SetPhonemeModifier` /
`GetPhonemeModifier` and `MfgConsoleFuncExt.ApplyExpressionPreset` /
`ResetMFGSmooth` / `SetPhonemeModifierSmooth` / `GetPlayerSpeechTarget` /
`IsInDialogue` -- so the expression and animation mods that call them keep
working. Keep the two `.pex` scripts (they are the names the VM resolves; the
natives behind them are now this plugin's) and delete only the DLL. With the
DLL still loaded the two blink and ease the face at once; the plugin says so in
its log and in the panel's problems bar, and leaves Mfg Fix's own natives
standing rather than registering a second set.

## Layout

```
src/plugin/    the SKSE DLL
docs/          hook-analysis.md — how the hook point was found, and open questions
```

## Building

Requires Visual Studio 2022, CMake, and vcpkg.

```powershell
cmake -S . -B build -DOER_BUILD_PLUGIN=ON `
    -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

The plugin build deploys `DCMF.dll`, its PDB and `DCMF.ini` into
`%LOCALAPPDATA%/ModOrganizer/Skyrim Special Edition/mods/DCMF/SKSE/Plugins`,
so it appears as an ordinary MO2 mod — toggleable, and removable without
residue. Override with `-DOER_DEPLOY_DIR=...`.

## Testing in game

**Remove `mfgfix.dll` first** (Mfg Fix or Mfg Fix NG), keeping their scripts.
The two hook the same function this does; running both is not a supported
configuration, and the panel's problems bar says so in red while it is the case.

1. Enable the `DCMF` mod in MO2.
2. Launch through SKSE, load a save.
3. Press **Shift+L** (changeable under Settings -> Menu key). The panel opens over the running game — nothing pauses, so a
   morph can be dragged and watched at the same time. **Escape** or the window's
   close button puts it away. At the top, **Drive** says whose faces the mods
   drive: Disabled, Player only, or All; it is kept in the settings file.
4. Pick a target (nearest NPC, the console selection, or the player) and open
   the **Actor** window: every channel on the actor at once, as a grid -- the
   name on the left, the control on the right -- grouped as **Engine** (head
   controls, eyes control, lip sync, expressions, modifiers), **Face morphs**
   by RaceMenu's categories, **Body morphs** by BodySlide's own
   `SliderCategories` files, **Overlays** (every slot a mod registers, with
   its pool), **Skin**, **Eyes** and **Hair**. A row the actor cannot use --
   a morph no TRI on this head names, a body slider its BodySlide .tri lacks
   -- is greyed with the reason on hover, and still editable. What you set
   is a pose held on the actor above whatever the mods drive, by the same
   route a modifier's rows take; **x** on a row hands that channel back, and
   closing the window lets everything go. Hovering a face morph's name says
   how it is driven: through the base morph the engine composes from (see
   `docs/morph-routing.md`), how far it reaches when the head's other
   shapes have no TRI naming it, and whether it is named with every delta
   zero -- Skyrim's own heads ship `LookLeft` and `LookRight` that way, which
   is why horizontal eye movement is the gaze's job and not a morph's. The
   probes that are not channels -- the paint tests, RaceMenu's node
   rotation, the engine's keyframe readout, the base morph, the body .tri
   files -- are under **Diagnostics** at the bottom.
5. Under **Engine**, switch on `Disable blinking`, `Disable lip sync`,
   `Disable dialogue emotions` or `Eyes control` to hold that part of
   vanilla's animation flat. Each is
   zeroed every frame *before* your own sliders are applied, so a morph you
   drive on the same channel still wins -- suppression removes the engine's
   animation, not your authority over it. Untick to hand the channel straight
   back; nothing needs restoring, because the engine recomputes all three every
   frame. The `dialogue` and `final` readouts are the check: both matching
   lines go to all zero while the box is ticked.

   With `Eyes control` on, **Gaze mode**, **Gaze heading** and **Gaze
   pitch** are the only way to aim the eyes: they are rotated by
   `eyesHeading`/`eyesPitch`, not by any morph, so a `Look` slider cannot move
   them and the look-at system overwrites anything written while it is still
   running. `Head control` with **Head mode**, **Head yaw** and **Head pitch**
   works differently: the head is not reachable by writing anything. The
   modifier channel names indices 14-16 `HeadPitch`/`HeadRoll`/`HeadYaw` and
   writing them moves nothing, and the head geometry is skinned to the skeleton,
   so rotating the node holding it moves nothing either. Instead the engine's own
   look-at is fed a world point through `AIProcess::SetHeadtrackTarget`, which
   supplies vanilla's blending, angle clamps and neck contribution -- so pitch and
   yaw are radians offset from the actor's facing, and there is no roll, because a
   look-at point cannot express one.

   Head aim has two modes. **takeover** offsets from the actor's own facing and
   stops it following anyone. **offset from vanilla's target** keeps whoever the
   actor was looking at and bends the aim off them, which is what a nod during
   dialogue needs -- the gaze has to stay on its subject while the head moves.
   The engine's own `headTrackTargetOffset` field turned out not to do this, so
   the second mode works by recording what vanilla wanted in the same detour that
   stops it taking effect, and re-expressing that intent with the offset applied.

   Nothing is saved and put back on release: vanilla headtracking re-evaluates on
   a timer, so we stop feeding it and zero `headTrackTargetTimer` to make it
   re-acquire at once instead of staring past its target for a second.

   The engine stages the face in three tiers -- console/script input,
   `phoneme1`/`modifier1` holding the **dialogue** values, and `phoneme3`/
   `modifier3` holding the final values that reach the mesh. Suppressing only
   the final tier overrides the output while dialogue keeps producing, and the
   engine re-derives the final values from it -- the mouth then moves on any
   frame our write is not the last to land. Suppression reaches the source tier
   for that reason. The gaze is not in any channel at all: it rotates the eye
   bones through `eyesHeading`/`eyesPitch`, which is why a `Look` morph on the
   slider list cannot override it and why it needs its own checkbox.
6. Read `Documents/My Games/Skyrim Special Edition/SKSE/DCMF.log`.

The panel is the only thing that applies anything: there are no hotkeys beyond
the panel, so no face changes unless it was asked for on screen. `DCMF.ini`
is still useful as a scripted batch — set `tri` to a head mesh and `morph` to a
morph that mesh contains, then press **Apply the INI**
under Diagnostics to replay it.

What the log should show: the runtime version accepted, the hook installed, the
TRI's vertex and morph counts, and one line per head shape giving its vertex
count and the **derived stride** of the dynamic vertex buffer. That stride is a
real unknown — it is measured rather than assumed, and it is the first thing to
check if nothing moves.

### Finding the deeper apply point

Set `analyze = 1` in the INI and press **Apply the INI**. This dumps the facegen
call graph two levels below `UpdateDownwardPass`, with every target resolved to
an Address Library id.

This has to run in-process: the Steam build of `SkyrimSE.exe` ships with its
`.text` section encrypted (SteamStub, entropy 8.000), so the code only exists in
readable form inside the running game. `docs/hook-analysis.md` has the details.

## Known limits of milestone 1

- **Loose `.tri` files only.** BSA indexing is milestone 2. In practice this
  costs nothing here: Expressive Facial Animation already replaces the
  vanilla-path head loosely, so all five topologies in this load order are
  reachable (see `docs/head-topologies.md`).
- **Whether a held morph freezes vanilla animation is measured, not assumed.**
  The apply writes `baseline + delta` each frame rather than accumulating onto
  its own previous output, which would compound without bound. Whether that
  baseline stays current depends on something not yet established: does the
  engine rewrite the dynamic vertex buffer every frame, or only when its own
  morph weights change? The apply samples three vertices as it leaves them and
  re-checks on the next pass, re-capturing the baseline if the engine moved
  them, and logs which behaviour it saw:

  > `shape 'Head': the engine rewrites this buffer between our passes ...`
  > `shape 'Head': the engine left our last write intact ...`

  The first means vanilla lip sync and blink keep running underneath us. The
  second means they are frozen on that shape while a morph is held, and the
  compositor in milestone 2 is what fixes it. **This log line is the single most
  useful thing the first run produces** — it decides how the compositor has to
  be built.
- **No compositor, masks, sequences, conditions, inspector or shims.** All later
  milestones.
- **The Papyrus functions are registered but unusable.** They need a compiled
  `DCMF.pex`, which needs the Creation Kit's Papyrus compiler — not installed
  here. The INI and hotkeys exist precisely so milestone 1 does not depend on it.

# The UI, as built

Every window, every section, every widget, and what talks to what. Written
against the tree on 2026-09-23, from the source rather than from a sketch --
`ui-current.txt` (the drafted target) and `ui-built.txt` (what the build did)
are both dated 2026-09-13 and are stale in several places called out below.

This is a map for refactoring: it says where each thing is drawn, what state
it hangs off, and which rules a move would break.

Outline notation, as in `ui-built.txt`: `>` a collapsible node, `-` a control,
`[ ]` a widget.

---

## 1. The files that draw

Fifteen translation units touch ImGui. Nothing else in the plugin does.

| File | Lines | What it owns |
|---|---|---|
| `src/plugin/Overlay.cpp` | 793 | The host: hooks, input, the frame, the hotkey, the main-menu banner |
| `src/plugin/Panel.cpp` | 1887 | The main window, its three tabs, the Entries tab, the Patch tab, the Actor window's Diagnostics probes |
| `src/plugin/editor/Windows.cpp` | 1013 | The bottom bar, the Problems modal, the log window, the settings window |
| `src/plugin/editor/Groups.cpp` | 399 | The Editor tab's top level: mod list, save, reload. Defines `editor::Draw` (declared in `EntryEditor.h`) |
| `src/plugin/editor/Mode.cpp` | 68 | The mode bar (filter + inspect/user/author) and the read-only rule |
| `src/plugin/editor/EntryRows.cpp` | 301 | One modifier; one appearance preset |
| `src/plugin/editor/SequenceRows.cpp` | 846 | One clip, its triggers, its variants, its frames |
| `src/plugin/editor/Timeline.cpp` | 786 | The Timeline window and its Frame window |
| `src/plugin/editor/Channels.cpp` | 2057 | Channel groups, the channel union, one channel row, every value widget, the pickers, the pools, the transition row |
| `src/plugin/editor/ChannelSections.cpp` | 456 | The one channel listing (Engine / Face / Body / Overlays / Skin / Eyes / Hair / ...) and the channel-list popup |
| `src/plugin/editor/Conditions.cpp` | 683 | Condition rows, the type picker, the typed component rows, condition presets |
| `src/plugin/editor/Functions.cpp` | 247 | Function rows and function sets |
| `src/plugin/editor/Sections.cpp` | 1057 | Draw pools, channel group presets, registered sources, overlay slots, skin sets |
| `src/plugin/editor/ActorWindow.cpp` | 432 | The Actor window and its held pose |
| `src/plugin/editor/Sliders.cpp` | 276 | `MorphsFor` (used) and the morph picker (**dead**, see §11) |
| `src/plugin/editor/Widgets.cpp` | 520 | The shared widget vocabulary |

Two more draw images but no controls: `Thumbs.cpp` (a texture card) and
`MeshThumbs.cpp` (a rendered head-part / overlay card).

---

## 2. The host: `Overlay.cpp`

Four call-site hooks, ported from Open Animation Replacer:

| Hook | Site | Does |
|---|---|---|
| `InputDispatchHook` | `kInputDispatchID` | Queues every button and char event; feeds the game an **empty** event list while the panel is visible, so input is swallowed without pausing |
| `RegisterClassAHook` | `kRegisterClassID` (an IAT call, 6-byte) | Chains `WndProcHook`, which exists only to notice `WM_KILLFOCUS` |
| `CreateD3D11Hook` | `kCreateD3D11ID` | Calls `Init()`: reads the swap chain, creates the ImGui context, inits the Win32 + DX11 backends |
| `PresentHook` | `kPresentID` | The per-frame pump (below), then `Render()` |

### The frame

`Render()` runs on the render thread and is the **only** place ImGui is
touched:

1. `DrainInputQueue()` -- applies the queued events; handles focus loss, the
   menu-key capture, and the toggle.
2. Early out: if the panel is hidden **and** the log window is shut **and**
   the main-menu banner is over, nothing is drawn at all.
3. `io.DisplaySize` and the mouse position are corrected after the Win32
   backend has run (the backend uses the *window's* client size; the render
   target is the swap chain).
4. `thumbs::BeginFrame()`, `meshthumbs::Advance(device, context)` -- the
   texture drop and one pending mesh render, before `NewFrame`.
5. `ImGui::NewFrame()`.
6. If visible: `stillness::HoldCamera()` then `panel::Draw(g_visible)`.
7. The banner, if it is still fading.
8. `editor::DrawLogWindow(visible)` -- **outside** the visibility check, timed
   into `panel::NoteLog`.
9. `ImGui::Render()` + `ImGui_ImplDX11_RenderDrawData`.

### What else `PresentHook` pumps (not UI, but it is the same hook)

`headtrack::Tick()`; a once-a-second `SweepDriven()` + `basemorph::Sweep()`
onto the task queue; `entries::Advance(frameDelta)` off `BSTimer::delta`;
`bodymorph::Upload()`; `entries::Tick()` onto the task queue at
`entries::TickRate()`; `look::FitPendingNecks()` when necks are pending.

### The hotkey

Packed into one `std::atomic<uint32_t>`: scancode in the low byte, shift/ctrl/
alt at `0x100/0x200/0x400`. Default `Shift+L`. Modifiers are tracked by the
drain itself (`g_shiftHeld`/`g_ctrlHeld`/`g_altHeld`), reset on focus loss.
Modifiers must match **exactly**. `CaptureMenuKey()` arms a capture of the next
non-modifier key; Escape cancels; the key is kept from ImGui; `settings::Save()`
runs when it lands. `HotkeyName()` formats through `GetKeyNameTextA`, handling
the DirectInput extended-key bit.

`SetVisible(false)` -- from the hotkey, from Escape, or from the panel's own
close box -- also calls `preview::Stop()`, clears `stillness::HoldHead()` and
`StopIdles()`, and `stillness::Release()`. **Every route out of the panel must
release.**

### The banner

`##dcmfannounce`, bottom-left, no decoration, no input, no nav. Shown from the
first frame the main menu is seen, held 6s, faded over 2s, and dropped the
moment the main menu goes (a game began). Toggled by the Settings window's
"Show in main menu".

---

## 3. Window inventory

| ImGui name | Drawn by | Shown when | Position / size |
|---|---|---|---|
| `DCMF` | `panel::Draw` | panel visible | 720x740 first use, free |
| `Actor` | `editor::DrawActorWindow` via `panel::DrawActorWindow` | `editor::ShowActor()` | first use: right edge, at the main window's top, 640 wide by the main window's height |
| `Settings` | `editor::DrawSettingsWindow` | `g_showSettings` | pinned to the main window's top-right corner, auto-resize, capped to the screen, not movable |
| `DCMF log` | `editor::DrawLogWindow` | `g_showLog` (panel open **or not**) | pinned top-right, fixed 560 wide, auto-height capped to the display |
| `Timeline` | `editor::timeline::Draw` | `timeline::IsOpen()` | 900x360 first use, free |
| `Frame N at X.XXs -- <clip>###timelineframe` | `timeline::DrawFrameWindow` | a frame is selected on the sheet | 640x520 first use, free |
| `##dcmfannounce` | `overlay::Render` | main menu, once a session | bottom-left |

Popups and modals:

| Id | Opened from | Kind |
|---|---|---|
| `Problems` | the bottom bar's status button | modal, centred, 760x520 |
| `Update frame time` | a frame's "Update time" | modal, auto-resize |
| `##editlist` | "Edit list" on a channel group or a channel group preset | popup, 560x620 |
| `##itempicker` | "Edit list" on a draw pool or a random reference's own list | popup, 560x620 |

---

## 4. The draw call tree

```
overlay::Render
├─ panel::Draw(visible)                                  [Measure -> Timings::panel]
│  ├─ ImGui::Begin("DCMF")
│  │  ├─ editor::SetActorWindowAnchor(pos.y, size.y)
│  │  ├─ Drive radios (Disabled / Player only / All) + "Driven actors" table
│  │  ├─ DrawTargetPicker -> resolves `actor`
│  │  ├─ Watch / Stop watching        (journal::Watch)
│  │  ├─ actor changed -> preview::Stop();  stillness::SetSubject
│  │  ├─ BeginChild("Tabs", {0, -bottomBarHeight})
│  │  │  └─ BeginTabBar("##tabs")
│  │  │     ├─ "Entries" -> DrawEntries(actor)           [Timings::entries]
│  │  │     ├─ "Editor"  -> editor::Draw(actor)          [Timings::editor]
│  │  │     └─ "Patch"   -> DrawPatches()
│  │  └─ editor::DrawBottomBar()                          (outside the child)
│  ├─ editor::DrawSettingsWindow(actor)                   [Timings::settings]
│  ├─ editor::timeline::Draw(actor)                       [Timings::editor]
│  └─ panel::DrawActorWindow(actor)                       [Timings::actor]
│     └─ editor::DrawActorWindow(actor, DrawActorDiagnostics)
└─ editor::DrawLogWindow(visible)                         [panel::NoteLog]
```

`editor::Draw` (Groups.cpp) expands to:

```
editor::Draw(actor)
├─ MorphsFor(actor)                                       (read before the lock)
├─ DrawModeBar()                                          (Mode.cpp)
├─ [New mod name] [New mod] [Reload all] + status
├─ read LiveState: PlayingFor, LastFiredFor, RecentTags,
│    animations::Playing / Known / KnownAnywhere          (before the lock)
├─ SetAnimationsForDraw(...)   SetSourcesForDraw(&sources)
└─ entries::Update([&](groups){                           (takes the group lock)
     BeginChild("Mods")
     for each LoadedGroup:
       ├─ snapshot (inspect mode only)
       ├─ tree node + DirtyMark + "(user file)"
       ├─ Mod name / Author / Mod description             (disabled in user mode)
       ├─ path, warnings
       ├─ DrawSources(context, group)                     (Sections.cpp)
       ├─ DrawSlots(context, group, catalogue)            (Sections.cpp)
       ├─ DrawSkins(context, group, skinSets)             (Sections.cpp)
       ├─ "Presets (N)"
       │   ├─ DrawConditionPresets(group)                 (Conditions.cpp)
       │   ├─ "Appearance presets (N)" -> DrawAppearancePreset  (EntryRows.cpp)
       │   ├─ DrawChannelGroupPresets(context, group)     (Sections.cpp)
       │   └─ DrawPools(context, group)                   (Sections.cpp)
       ├─ "Modifiers:" -> DrawModifier x N     (EntryRows.cpp)
       ├─ "Clips:" -> DrawClip x N                        (SequenceRows.cpp)
       ├─ on edit: DeriveTransitionClips + ResolveGroups; dirty = true
       └─ [Save mod config | Save as user file] [Reload mod config]
     EndChild
   })
└─ SaveGroup / ReloadGroup  (outside the mutator -- they take the lock again)
```

`DrawModifier` expands to:

```
DrawModifier
├─ header: [enabled] name [active dot] | Priority | [Preview]
└─ open:
   ├─ [Delete modifier]  Name  Description  Priority
   ├─ DrawAppearanceReferences  (per preset: Make a copy / Remove; Add from preset)
   ├─ DrawChannelGroups(context, groups, channels, showRequired=true, locked)
   ├─ DrawChannelUnion(owned, applied.groups, channels, {}, {}, &transition, lockedRows)
   │   └─ DrawTransitionRow -> DrawChannelRow x N / DrawComputedRow x N
   ├─ "Conditions (N)" -> DrawConditionSet
   └─ "Functions (N)"  -> On activate / On deactivate -> DrawFunctionSet
```

`DrawClip` expands to:

```
DrawClip
├─ header: [enabled] name [active dot] | length | [Timeline] [Preview]
└─ open:
   ├─ [Delete clip] Name Description Priority
   ├─ [Interruptible] [Stop when conditions fail]
   ├─ "Triggers (N)"  -> DrawTrigger x N      (13 types, each with its fields)
   ├─ "Conditions (N)" -> DrawConditionSet
   ├─ DrawChannelGroups(..., showRequired=false)
   └─ one variant:  "Keyframes (N)" -> DrawFrames + [Add variant]
      several:      "Variants (N)"  -> Mode slider, No repeat, DrawVariant x N
                    DrawVariant -> DrawFrames -> DrawKeyframe -> DrawFrameBody
```

`DrawFrameBody` is shared by the mod list's frame rows **and** the Timeline's
Frame window. It draws: `[Update time]` (modal), `[Copy frame]`,
`[Delete frame]`, `DrawChannelUnion` with the computed/crossing rows, and the
frame's `Functions (N)` set.

---

## 5. Screen by screen

### 5.1 Main window `DCMF`

```
> Main window "DCMF"                                     (720x740 first use)
  - Drive  [radio Disabled] [radio Player only] [radio All]   N actor(s) driven  (?)
  > Driven actors                                        (collapsing header)
    - table: actor | form | modifiers | channels | refs  (+"(clip)" beside a name)
  - separator
  > Target
    - [radio] Console selection  [radio] Player  [radio] Form id  [hex field]
    - "<name>  [00000000]"   or a red line saying why nothing resolved
    - yellow line when the target is the player: head not drawn in first person
    - [Watch | Stop watching]                            (journal::Watch)
  > child "Tabs"  (height reserved for the bar)
    > tab Entries
    > tab Editor
    > tab Patch
  - bottom bar: <problems status, a button when there is something> [Actor >] [Log >] [Settings >]
```

Closing the window (`!open`) does: `a_visible = false`, cursor off,
`preview::Stop()`, `stillness::HoldHead/StopIdles = false`,
`stillness::Release()`, `thumbs::Clear()`, `meshthumbs::Clear()`.

**Stale in `ui-built.txt`:** the window is titled `DCMF`, not
`OpenExpressionReplacer`; Drive is a three-way radio at the top of the main
window, not a checkbox in the Entries tab; there is a third tab, Patch.

### 5.2 Tab "Entries" (`Panel.cpp::DrawEntries`) -- read-only

```
> tab Entries
  - [Reload from disk]                                   (entries::Reload)
  - "Data/SKSE/Plugins/DCMF/*.json"
  > Mods
    - red line per unreadable file
    > "<name> (N modifier(s), M clip(s))"                (collapsing header)
      - path  + "(user file)"
      - warnings (yellow)
      - one line per modifier: name, priority, condition/group/channel counts, [disabled]
      - one line per clip: name, trigger/variant counts, [disabled]
  > This actor                                           (only with an actor)
    - [Evaluate now]   "clip(s) playing: <names>"
    - matched modifiers (green)
    - withdrawn modifiers (yellow, with which channel lost to whom)
    - channels in none of their channel groups (yellow)
    - every resolved channel by type: numbers, colours, texts, switches
    - "<morph> -- not on this head" (orange)
```

Live from the last tick when driving; otherwise from the static `preview`
snapshot that `[Evaluate now]` fills.

### 5.3 Tab "Editor"

The mode bar, then the mod list. See the call tree in §4 and the sections
below.

```
- Filter [##modfilter]   [radio inspect] [radio user] [radio author]
- [Mod name...] [New mod]   [Reload all]   <status>
- yellow line when the actor has no TRI loaded yet
- separator
- "Inspect mode: ... discarded at once. Previews work."   (read-only only)
> child "Mods"
  > <mod>  *  (user file)
```

#### Registered sources (`Sections.cpp::DrawSources`)

Section header with a hint. Per source, in a box:

- header: name (orange `!` + tooltip when another mod registers it), then
  `<kind>, <type>` and a `!` with `core::CheckSource`'s problem.
- open: `Name` (rename refused onto another mod's name, with `refused` text),
  `Kind` combo of 8 (Mod event, StorageUtil, JContainers, Papyrus function,
  Faction rank, Global variable, Actor value, Graph variable), then per kind:
  - **Mod event**: `Event`, `Field` combo (string/number/bool/fired),
    `Within (s)` when fired, `Actor` combo (sender / form id in the number /
    id in the string / nobody).
  - **StorageUtil / JContainers / Papyrus**: `Script`+`Function`, or
    `Key`/`Path`; `Type` combo; `Stored as` (float/int, StorageUtil numbers
    only); `Actor` combo (self / player / nobody).
  - **Faction rank / Global**: a `Plugin.esp|hexid` field; "the player's rank"
    checkbox for a faction.
  - **Actor value / Graph variable**: `Variable`, a `Bool` checkbox for a graph
    variable, a "the player's" checkbox.
  - `[Delete source]`
- `[Register source]` -- names itself via `core::GenerateResourceName`.

#### Overlay slots (`Sections.cpp::DrawSlots`)

Own slots and extensions of other mods' slots, then `[Add slot]`
`[Extend another mod's slot]`.

```
> slot 1                       <part> <blend>, N overlay(s)
  - Name;  "defined first by '<mod>'" note when shared
  - Part [combo face|body|hands|feet]   Blend [combo normal|multiply] (?)
  - Pool:  -> DrawPool
  - [Delete slot]
> extends '<slot>'             N overlay(s), defined by '<mod>'  (or a warning)
  - Slot [pick combo of every slot in the load order]
  - Added to that slot's pool: -> DrawPool
  - [Delete extension]
```

`DrawPool` -> `DrawOverlay` per entry, in a box:

```
> [on] <name>                  N key(s)
  - Name (?)
  - one row per key (the known layouts for the part, then this overlay's own):
    [tick] <layout label>
      <mesh thumbnail of the texture on a figure of that layout>
      [##diffuse]  [registered... combo from the packs' .pex]  [normal map]
        [##normal]
  - [other key] [Add key] (?)   [this actor's... combo]
  - [Delete overlay]
[Add overlay]
```

#### Skin sets (`Sections.cpp::DrawSkins`)

```
> [on] <name>                  N layout(s)
  - Name (?);  "'<mod>' defines a skin of this name first" warning
  - per body layout key: [tick] <label>
      Body / Hands / Feet:  Diffuse, Normal, Specular, Subsurface
      Head:  "Diffuse: the facegen's bake; not swapped here", Normal, Specular, Subsurface
  - [Delete skin]
[Add skin set]
```

#### Presets (a plain collapsing header holding four sections)

- **Condition presets** (`Conditions.cpp::DrawConditionPresets`): per preset a
  header `<name>   N condition(s)`, `Name`, `[Delete preset]`, a condition set
  drawn with **no actor** (so no verdicts) and **no presets offered**.
  `[Add new preset]`.
- **Appearance presets** (`EntryRows.cpp::DrawAppearancePreset`): header with a
  `[Preview]`; open gives `[Delete preset]`, `Name`, `DrawChannelGroups`,
  `DrawChannelUnion`. `[Add appearance preset]`.
- **Channel group presets** (`Sections.cpp::DrawChannelGroupPresets`): the
  built-ins first, greyed and read-only with their channel list on hover; then
  the mod's own, each with `Name`, `[Edit list]` -> the channel popup, a bullet
  per channel, `[Delete preset]`. `[Add channel group preset]`.
- **Draw pools** (`Sections.cpp::DrawPools`): per pool `Name`, a `Channel` pick
  combo (hair, brows, beard, scars, eyes, skin, bodypreset, plus every slot),
  then `DrawItemTicks`. Changing the channel clears the items.
  `[Add new pool]`.

### 5.4 Tab "Patch" (`Panel.cpp::DrawPatches`)

```
> tab Patch
  - [checkbox] UBE: eyes turn by their rotation morphs  (?)
  - (enabled only with the box ticked)
    Left / Right / Up / Down / Rotation strength  [sliders 0..kMaxEyeRotationWeight]  (?)
```

Writes through `patches::Set` + `settings::Save`.

### 5.5 Window "Actor" (`ActorWindow.cpp`)

```
> Window "Actor"
  - "No actor selected in the panel."                    (and the pose is released)
  - [checkbox] Don't drive this actor                    (entries::SetExcluded)
  - <pose status>  [Clear all]
      "nothing set" | "N channel(s) held on the actor"
      | "N channel(s) set, not applied: something else is being previewed" (yellow)
  - [##channelfilter]
  - yellow warning when Drive is Disabled
  > every ChannelSection, as a 3-column grid (name | value | clear)
    - Engine            (open)
    - Face morphs       (closed by default)
    - Body              (open)
    - Body morphs       (closed by default)
    - Overlays          (open)
    - Skin / Eyes / Hair / Brows and beard / Scars   (open)
  > Diagnostics                                          (panel's probes)
```

Rows: the left column is the label (yellow when the pose claims it), with the
entry's tip on hover; the middle is `DrawChannelValue(..., compact=true)`; the
right is an `x` that clears the row. An **unclaimed** row shows, in order:
what `entries::HeldOn` says the actor holds now (`g_mirror`); else the engine's
final keyframe value for that channel (`EngineValue`, off `expression3` /
`modifier3` / `phoneme3` / `custom3`); else the actor's own
(`OwnValue`: weight, alpha, hair colour for the three tints); else
`core::DefaultValue`. Editing an unclaimed row claims it.

Gating: a spine/head/eye aim row is greyed with "Does nothing until X is on
above" until its control switch is on **in this pose**.

The pose is published through `preview::ShowPose` on every edit, and again
every 20s while held (the preview's deadman would otherwise let go). It is
kept while the window is shut and taken up again when it reopens; only the
actor changing or `ReleaseActorPose()` drops it.

#### Diagnostics (`Panel.cpp::DrawActorDiagnostics`) -- 12 tree nodes

1. **Engine animation (the old controls)** -- Stop blinking / lip sync /
   dialogue emotion / eye tracking / Head control; with eye tracking stopped:
   look at camera, heading, pitch drags, eye rotation morphs slider, `[Centre]`;
   with head control on: takeover / offset / camera radios, pitch, yaw,
   `[Forward]`.
2. **Morphs (the old list, with shape hiding)** -- shape count, a `shapes` node
   with a `hide` box per head shape, a filter, then a collapsing header per
   `MorphCategory` with a slider per morph; paired morphs get one signed
   slider; marks `(base)` `(empty)` `(only <shape>)`; Look rows disabled.
3. **Body morph sliders (direct)** -- filter, `[Discover again]`,
   `[Clear all]`, `bodymorph::GetStats` line, then a header per `.tri` source
   with a slider per morph (-1..1), written straight through `bodymorph::Set`.
4. **Body shapes (inspection)** -- `[Inspect]` then an 8-column scrolling
   table: shape, parts (verts), shared, stride, prec, raw, buffer, geom.
5. **Look (head parts probe)** -- `[Watch this actor]`, counts, eyes combo,
   hair combo, `[Own hair back]`, a "wearing ..." line, `[Release]`.
6. **Effect shader (probe)** -- a filtered combo of every `TESEffectShader`
   (named off their records), `[Play]` `[Stop]` `[Stop all]` `[List]`, a report
   line and a "what to look for" line.
7. **Refraction and alpha (probe)** -- refraction and alpha sliders applied on
   release, `[Put back]`.
8. **Art object (probe)** -- a filtered combo of every `BGSArtObject`,
   `[Play]` `[Stop]` `[List]`.
9. **Headtracking target (probe)** -- the engine's target, `[Console
   selection]` `[Player]` `[Clear]`, and a combo of actors within 4096 units
   sorted by distance.
10. **Skeleton dump (probe)** -- one button that logs every driven actor's
    spine chain.
11. **Engine facegen state** -- `activity::Sample` rates, the low-water mark
    and its reset, the three keyframe tiers (input / dialogue / final) through
    `DrawKeyframes`, the gaze readout, the dialogue-data line, and an
    expression index/value pair with `[Apply engine expression]` /
    `[Clear engine expression]`.
12. **Diagnostics** -- registry counts, `[Reload TRIs from the INI]`,
    `[Apply the INI]`, `[Clear this actor]`, the base-morph probe
    (`[Report] [Nudge +2] [Nudge -2] [Restore base]`), and the vertex-writer
    watch (`[Arm on this head] [Report writers]`).

**Stale in `ui-current.txt`:** the Paint section, the RaceMenu node-rotation
probes, the per-frame head-bone probe, the RaceMenu body-morph probe and
"What this window costs" are all gone. Tints, gloss, eye glow and eye texture
are channels now, not probe controls.

### 5.6 Window "Settings" (`Windows.cpp::DrawSettingsWindow`)

```
> Window "Settings"
  - "Driving"      "Set at the top of the main window: ..."
  - "Menu key"     <name>  [Change] [Reset]   (or "Press the new key...")  (?)
  - [checkbox] Show in main menu  (?)
  - "Spine turn, shared down the bones"   Spine1 / Spine2 / Neck [sliders 0..1]  (?)
  - "Headtracking reach, degrees"  Spine1 / Spine2 / Neck / Head [sliders 0..90]  (?)
  - [checkbox] Pause while the race menu is open  (?)
  - [checkbox] Preview clears other channels  (?)
  - "Holding still"   [Stay idle in preview] [Keep head still in preview]
                      (both forced off and disabled -- a TODO)
  - "Log"    "Recording follows the log window." (?)   Max entries [slider 50..5000] (?)
             "N recorded"
  - "Layouts"  Head [combo auto + every head key] + reason
               Female/Male body [combo] + reason
               "Body mod: <label>" + reason (hover: skin mesh and geometry names)  (?)
  - "Mod events heard"   last 12, newest first  (?)
  - "What a tick costs"  Ticks per second [slider]  (?)
      table: usual | last | worst for
        whole tick, conditions, morph lists,
        panel window, entries tab, editor tab, actor window, settings window, log window
      counts line; morph-list cache hit rate; (?) ; [Reset]
```

Every toggle here calls `settings::Save()` immediately. Changing a layout
override also calls `facts::ForgetAll()`.

### 5.7 Window "DCMF log" (`Windows.cpp::DrawLogWindow`)

Recording follows the window: `journal::SetEnabled(g_showLog)` on every change,
because the traces are the expensive half.

```
> Window "DCMF log"        (pinned top-right, 560 wide, no move/resize/nav)
  - "No actor watched. Select one in the panel and press Watch" when nothing is
  > child "Entries"  (capped at min(55% of the display, 520))
    - table "LogTable", one column, bordered
      - either every entry, or the one being traced plus its trace
  - (only while the panel is open)
    - [##filter] "Filter... (affects new entries)"  (?)   + invalid-pattern warning
    - [Clear] [Stop watching] [Save to file] (?)
    - "Saved to <path>" [Copy path]  or the error
```

One entry is four lines, in OAR's `DrawLogEntry` shape:

```
Event [Badge] (Badge) xN                        Mod: <name>
Name: <item, ellipsised>                        at <t>s
Actor: <actor>                                  [Show trace]
Detail: <detail, ellipsised>
```

A new entry's cell background glows white at alpha 0.25 and fades over 1.5s.
Event colour comes from `ColourFor(JournalEvent)` -- 13 cases plus a default.

The trace replaces the list: `DrawTrace` draws a 400-tall child of
`TreeNodeEx` steps (each `<mod> | <item> <detail>` with its `StepResult` in the
right column at 85%), recursing into `DrawReason` children, then
`[Close trace]`.

### 5.8 The bottom bar (`Windows.cpp::DrawBottomBar`)

Drawn **outside** the `Tabs` child, in the height it reserved
(`GetTextLineHeight() + FramePadding.y*4 + ItemSpacing.y*4`).

Left: a child holding the status. `FindProblems()` counts unreadable files,
warnings (`loaded.warnings + loaded.loadOrderWarnings`), mfgfix.dll, and OBody
beside a `$bodypreset` row. With anything wrong it is a coloured button -- red
for unreadable/mfgFix, orange otherwise -- that opens the `Problems` modal;
otherwise it is plain text "No problems detected".

Right: `[Actor >|<] [Log >|<] [Settings >|<]`, each toggling its flag. The bar
also records `g_settingsAt` -- the main window's top-right corner -- which is
where the Settings window hangs.

The `Problems` modal lists, in full: the mfgfix error and what to do; the OBody
warning; every unreadable file with its path and error; then every mod with
warnings, bulleted.

### 5.9 Window "Timeline" (`Timeline.cpp`)

Opened from a clip row's `[Timeline]` or a variant row's. One at a time,
identified by `core::SequenceKey(group.path, clipIndex)` plus a variant index.
It takes the group lock **itself**, so it is drawn from `panel::Draw`, never
from inside the mod list's pass.

```
> Window "Timeline"                                      (900x360 first use)
  - "<mod> / <clip>"  "variant <name>"  "N frame(s)"  "inspect mode: nothing here is kept"
  - Snap [combo off|0.01|0.05|0.1|0.25|0.5|1.00]  [Fit]  [Play|Stop]  [Release scrub]
  > child "##sheet"                (horizontal scroll, no scroll-with-mouse)
    - ruler: ticks at TickStep(zoom), an invisible "##scrub" button over it
    - one triangle handle per frame; drag retimes (snapped unless Ctrl),
      click selects and opens the Frame window; a dot on the handle when the
      frame has functions
    - one row per keyed channel, grouped under its channel group's heading
      (headings fold; a channel two groups hold sits under the first; anything
       keyed outside every group goes under "in no channel group")
      - number: the curve sampled from core::ValueAt every 3px, over a range
        widened to the keys and always holding zero
      - colour: a gradient sampled every 4px
      - stepped: a block from each key to the next, with its word
      - a diamond per key, clickable, at the key's value for a number
    - the playhead (red) and the scrub line (blue)
    - the label column, drawn last over everything so scrolling cannot move it
> Window "Frame N at X.XXs -- <clip>###timelineframe"
  - DrawFrameBody (the same editor the mod list uses)
```

Zoom: Ctrl+wheel about the mouse; wheel alone pans. `[Fit]` recomputes
`pixelsPerSecond` from the content width minus `kLabelWidth` (190) and
`kTailWidth` (40).

Scrubbing publishes `preview::ShowFrame(..., kScrubLabel, ..., toggle=false)`
on press and drag; an edit while scrubbing republishes it, so a key change or a
handle dragged past the scrub time shows at once.

---

## 6. The shared widget vocabulary (`Widgets.cpp`)

Everything the editor draws is built from these. A refactor that moves rows
between files must keep them available.

**Text and names**

- `EditText(label, value, capacity=128)` -- a `std::string` through
  `InputText`, honouring `ReadOnly()` (inspect mode) with
  `ImGuiInputTextFlags_ReadOnly`. **This is the single choke point for
  read-only**: every name, path and free-text field goes through it.
- `EditName(label, value)` -- `EditText` capped at `core::kMaxNameLength`, so
  the limit is something you cannot type past.
- `EditDescription(label, value)` -- `InputTextMultiline`, 512 capacity,
  `-150` wide by 4 text lines, `WordWrap`.
- `TextEllipsis(text, maxWidth=0)` -- cut with `...` and the whole of it on
  hover. Ported from OAR's UICommon.

**Pick lists**

- `PickItem { value, label, dim, dimWhy }`.
- `DrawPickList(current, items, needle={}, cap=0)` -- the body alone, for a
  combo that adds its own filter or headings; a `cap` prints "... more; narrow
  the filter".
- `DrawPickCombo(label, current, items, placeholder, emptyText, flags=0)`.

**Boxes and layout**

- `BeginBox()` / `EndBox()` -- OAR's bordered one-cell table, cell padding
  zeroed. The id is the fixed `##box` under the caller's `PushID`, *not* an
  address -- addresses move when a vector grows, which closed every open row on
  any edit that added an item.
- `SecondColumn(percent=0.55)` -- `SameLine` at that fraction of the content
  width.
- `RightAlign(width)` -- measured from what is left on the line, not from the
  window edge (inside a tree node and a child those differ). Submits nothing;
  returns early when there is no room.
- `BottomPadding()` -- a `Dummy` of one `ItemSpacing.y`.
- `LeafNode(id)` -- a tree node that is a leaf: no arrow, still a row.
- `OpenNext()` -- `SetNextItemOpen(true, Always)`, for the row just added.

**Marks, drawn rather than written** (the overlay loads no font, so ImGui's
built-in ASCII-only one is all there is):

- `VerdictMark(Verdict)` -- a green tick or a red cross in an 18px strip at the
  right edge, via `IndicatorSlot()` which submits the `Dummy` ImGui needs.
- `ActiveDot(bool)` -- a green dot centred on the item it follows.
- `DirtyMark()` -- an orange `*`.
- `TreeLines { TreeLines(start); Item(); End(); }` -- the lines joining a set
  of boxes to their parent. Construct before the first box, `Item()` after each,
  `End()` after the last.
- `ToggleSwitch(label, on)` -- a pill with a knob that slides over ~8 frames
  via `GetStateStorage()->GetFloatRef(GetItemID(), ...)`. Used **only** for
  compact control switches in the Actor window.

**Tooltips**

- `Hint(text)` -- a `SameLine` `(?)` with the text on hover. Every explanation
  goes through one of these; inline paragraphs were wider than the panel.
- `SectionHeader(label, hint)` -- a `CollapsingHeader` with its `(?)` on the
  header line.
- `Tooltip(text)` -- `BeginItemTooltip`, wrapped at 26 font sizes.
- `TooltipText(fmt, ...)` -- the same, formatted, for when the caller has
  already checked the hover. 2048-byte buffer.

**Transitions**

- `DrawEasing(label, type, duration, rate)` -- type combo (Instant, Linear,
  EaseIn, EaseOut, EaseInOut, Bounce, Spring), `Duration` when not instant,
  `Overshoot` for bounce/spring. Picking a non-instant type with no duration
  sets 0.35s; picking bounce/spring with no rate sets 0.25.
- `DrawCurve(label, type, rate)` -- the same without a duration, for a key: the
  gap between a channel's own keys supplies one.

---

## 7. What a row is drawn against

### `ChannelContext` (`Channels.h`)

Built **once per editor frame** and handed down; the group lock is already
held, and nothing in it takes another.

| Field | From |
|---|---|
| `group` | the `core::EntryGroup` being edited |
| `available` | `MorphsFor(actor)` -- morphs on this head |
| `catalogue` | `entries::Slots()` -- every slot in the load order |
| `skins` | `entries::SkinSets()` |
| `sources` | `entries::Sources()` |
| `slotNames` | the catalogue's slots by name |
| `actor` | the selected actor, or null |
| `owner` | the modifier whose rows these are (set by `DrawModifier` only, for Reroll) |

Three places build one: `Groups.cpp` (per mod), `Timeline.cpp` (per draw), and
`ActorWindow.cpp` (a partial one -- `catalogue`, `skins`, `actor`, `slotNames`
only; `group` is **null** there, so anything reading `*context.group` must not
be reachable from the Actor window).

### `RowKind` (`Channels.h`)

Whether a row is a key on a timeline (`frames` + `time`) or a value on a
modifier (`frames == nullptr`). `LastKey(channel)` says whether this is the
channel's last key, which is the only place exit easing has to apply.

### `LiveState` (`SequenceRows.h`)

Everything the running side knows, **read before the group lock is taken** and
handed in: `playing`, `sinceFired`, `recentTags`, `knownAnimations`,
`actorAnimations`, `playingAnimations`. Asking for any of it while drawing
would take the transition lock inside the group lock, and the tick takes those
two the other way round.

### Ambient draw-time globals (`Channels.cpp`)

Set by whoever draws, read by widgets that have no context handed to them.
**Both are cleared to empty at the end of the pass**, in `Groups.cpp` and
`Timeline.cpp` alike.

- `SetSourcesForDraw(const SourceCatalogue*)` / `SourcesForDraw()` -- read by
  `DrawSourceCombo` and the numeric row.
- `SetAnimationsForDraw(known, playing, haveActor, actorKnown)` -- read by
  `DrawAnimationCombo` and `DrawPlayAnimation`.
- `g_typing` -- the one channel whose slider has been double-clicked open to
  typed input. One at a time.

---

## 8. Channels: the value layer

### The one channel listing (`ChannelSections.cpp`)

`ChannelSections(actor, slots)` returns `vector<ChannelSection>`, each holding
`ChannelSubsection`s of `ChannelEntry { id, label, tip, dim }`. It is the
single source for both the Actor window's grid and the channel popup a group is
edited through, so the two cannot disagree.

| Section | Built from | Open by default |
|---|---|---|
| Engine | hand-written head/eye controls + `MfgChannelNames()` split into Lip sync, Expressions, Modifiers | yes |
| Face morphs | `actormorphs::CatalogueFor(actor, targets, {})` by RaceMenu category; two-ended sliders collapse to one channel | no |
| Body | `$weight`, `$bodypreset`, `$alpha` | yes |
| Body morphs | `bodycategories::Categories()` (BodySlide's `SliderCategories`), plus an "Other" subsection for what the actor's `.tri`s carry in no category | no |
| Overlays | every slot in the `SlotCatalogue`, expanded by `core::SlotChannels` | yes |
| Skin / Eyes / Hair / Brows and beard / Scars | hand-written | yes |

Dimming with its reason: a Look\* morph (the engine rewrites it every frame);
a morph no TRI on this head names; a face morph named with every delta zero; a
UBE eye rotation morph while the UBE patch is on; a body slider no `.tri` of
the actor's meshes names.

`g_body` (a file-scope `BodyState`) caches the body `.tri` walk per actor and
3D root, redone when either changes or every 2 seconds -- an outfit change swaps
the meshes under the same root.

### `DrawChannelListPopup`

560x620, a filter, a `CollapsingHeader` per section with "(N picked)" on it,
a subheading per subsection, a checkbox per entry. A channel held by **another
group of the same owner** is shown ticked and disabled. A channel in the list
but in no section here gets an "Other" header so it is visible and removable.
Filtering forces every section open.

### `DrawChannelValue` -- the typed value widget

| `ChannelType` | Widget |
|---|---|
| Number | `SliderFloat` over `core::InfoFor`'s range (signed low for a two-ended pair). Double-click opens typed input; a modifier's typed value clamps to the range, a key's does not. A 3D-rebuilding channel reports only on `IsItemDeactivatedAfterEdit`, formats `%.0f`; an angle channel formats `%.2f rad` |
| Colour | `ColorEdit3` with `PickerHueWheel`, plus a read-only `#RRGGBB` |
| Reference | `DrawReferencePicker` -- see below |
| Switch | `ToggleSwitch` for a control channel in compact mode, else `Checkbox` |
| Mode | a combo over `InfoFor(channel).modes` |

### `DrawReferencePicker`

Options come from `OptionsFor(context, channel)`, which is the one place that
knows what a reference channel can hold:

- a **slot** channel -> the slot's overlays, each with the texture this actor's
  layout would get (greyed when there is none, still pickable);
- a **head part** channel -> `look::HeadPartOptions`, written as the editor id
  where there is one; the model is drawn on a mannequin;
- `$skin` -> every skin set with its layouts;
- `$bodypreset` -> every BodySlide preset with "N of M sliders" on the selected
  actor;
- `$eyetexture` -> the registered pack overlays of kind Face (and the value is
  a typed texture path, not a pick).

The combo's first row is always the empty value, named for what it means on
that channel ("(none -- no layer)", "(default -- the meshes' own skin)", ...) --
and it is a *value*, still claiming the channel at that priority. Rows the
actor cannot use are listed under a separator with the reason. Hovering a row
shows the picture at 320x320.

`thumbs::SetCardTooltips(false)` is set around the listing so the card's own
hover picture does not fire inside a row that shows its own.

### `DrawChannelRow` -- one channel, open

```
> [required?] <label>                    <summary from DescribeContribution>
  - "this morph is not on the selected actor's head"     (orange, when so)
  - Source [text slider: slider|picker / computed / random / source]   (?)
  - the source's own body:
      static    -> DrawChannelValue
      computed  -> DrawDriver: a numeric row + from/to -> low/high
      random    -> DrawRandom: Drawn per [reference|base], [Reroll],
                   low/high for a number, or all|draw pool + DrawDrawablePools
      source    -> DrawSourceCombo (?)
  - [Delete]
  - Blend mode [combo]                   (numbers: Overwrite/Add/Saturate/Average/Max/Min)
  - Colour space [combo sRGB|Oklab|Oklch] + a sampled gradient from the
    previous key (or from the actor's own hair colour, or from black) (?)
  - $skin: Blend mode [combo overwrite|merge] (?)
  - 3D-rebuilding channel: "instant -- a change here rebuilds the actor's 3D"
  - a key: Transition [curve] (?), and on the channel's last key
    [Same as arrival] (?) -> DrawEasing("Exit", ...)
```

### `DrawChannelGroups` and `DrawChannelUnion`

`DrawChannelGroups` draws the groups the owner sets: an appearance preset's
groups first, greyed and locked; then the owner's own, each either inline
(name + `[Edit list]` + a bullet per channel with a `required` box) or a
reference to a preset (`[Make a copy]`, channels shown but not edited).
Removing a group also drops the values of channels no other group holds.
`[Add group]` makes an inline one named by `GenerateResourceName`;
`[Add from preset...]` offers the mod's presets then the built-ins.

`DrawChannelUnion` draws the union, **under its group's heading in group
order** -- a channel two groups hold sits under the first. Per channel it
draws, in order of preference: a locked row from an appearance preset (greyed,
read-only); the owner's own row (`DrawChannelRow`); or `DrawComputedRow` -- a
greyed box at what the clip has the channel at on this frame (or at its rest),
with `[Set]`/`[Edit]` to claim it. A computed row is framed light orange when
the run is *moving* the channel through this frame. Rows set but in no group
are listed at the bottom in orange as ignored.

### `DrawTransitionRow`

Start / Exit, each `[combo smooth|instant|clip]`. When nothing the modifier
holds can ease (every channel is stepped or rebuilds the 3D), both are forced
to instant and the combo is disabled with the reason. A `clip` end shows which
clip declared itself the transition (derived by `core::DeriveTransitionClips`
from the clip's own `TransitionEnter`/`TransitionExit` trigger) and warns when
an enter clip keys a channel the modifier does not hold, or an exit clip's last
frame keys anything at all.

---

## 9. Conditions, functions, triggers

### Conditions (`Conditions.cpp`)

`DrawConditionSet` is `TreeLines` + a box per condition + `[Add new condition]`
(which adds an `IsFemale`). Empty reads "(nothing -- this matches every
actor)".

`DrawCondition` per row: `[enabled]`, `NOT ` prefix when negated, the type name
(blue for a preset, red for an unknown type), the argument summary in the
second column, and a `VerdictMark` from `entries::Test` -- only with an actor
and only while enabled. Open: `[Negate]`, the type combo, `[Delete]`, the
schema description, then one `DrawComponent` per schema component.

`DrawTypeCombo`: a filter focused on appearing; the mod's condition presets
first under a "Presets" heading; then every schema grouped by
`ConditionGroup`, each preceded by a **scope dot**:

| Scope | Colour | Meaning |
|---|---|---|
| World | green | asked once per tick and shared by everyone |
| Actor | yellow | asked once per actor per tick |
| Inventory | orange | one inventory walk per actor per tick, shared |
| Unshared | red | asked every time it appears |

Structure rows get a blank `Dummy` in the dot's place. Changing the type
rebuilds the components but keeps `negated` and `enabled`.

`DrawComponent` handles every `ComponentKind`: `Form` (`DrawFormRow`: plugin +
hex id), `Keyword`/`LocRefType` (`DrawKeywordRow`: a toggle between a form and
an editor id), `Text` (a source combo, an animation combo + `[Play]`, a
modifier combo, a closed choice combo, or a plain field), `Bool`, `Comparison`
(6 operators), `Numeric` (`DrawNumericRow`), `NiPoint3`, and `Conditions` --
which recurses into `DrawConditionSet`, and is what makes a nested set look
like a set.

`DrawNumericRow` is shared with a computed channel's driver: a source combo of
Value / Global variable / Actor value / Graph variable / Registered source,
then that source's fields (the actor-value list is a filtered combo over 164
names, with a reading combo of Value/Base/Max/Percentage).

`DrawFormRow` is shared with the trigger rows.

### Functions (`Functions.cpp`)

The same shape: `TreeLines`, a box per function, `[Add function]` (which adds a
`PlaySound`), and -- on a non-empty **outer** set only -- a `[Run]` that fires
it on the selected actor now through `functions::RunFor` and records it in the
journal. Open: `[enabled]`, the type combo (every `AllFunctions()` schema with
its description on hover), `[Delete]`, an optional `Weight` when the parent is a
weighted container (RANDOM), the description, the schema's components through
`DrawComponent`, then a nested `Conditions` set and/or a nested `Functions` set
where the schema has them.

Changing the type keeps `enabled`, `comment`, and the contained sets where the
new type has somewhere to put them.

### Triggers (`SequenceRows.cpp::DrawTrigger`)

Thirteen types in `kTriggerLabels`. The collapsed row shows
`core::DescribeTrigger` and, in the second column, "fired N.Ns ago" / "never
fired" from `LiveState::sinceFired` keyed by `core::TriggerKey(sequenceKey,
index)`.

| Type | Fields |
|---|---|
| Interval | `Every` (0.5..600s), phased per actor |
| Edge / EdgeEnd | a `watch` condition set |
| ModifierOn / ModifierOff | a modifier combo |
| DialogueLine | a topic-info form row |
| AnimationEvent | `Event`, a "Recently seen" combo of the last 20 graph tags (which also arms `triggers::WatchTags`), `Payload` |
| AnimationStart / AnimationEnd | an animation combo + `[Play]`, and a typed field normalised by `core::NormalizeAnimationPath` |
| TransitionEnter / TransitionExit | a modifier combo (this is a *declaration*, not a fire) |
| Hit / Death | nothing |

Every type but Interval and the two transitions also gets a `Cooldown`.

### The animation combo (`Channels.cpp::DrawAnimationCombo`)

A filter, then: what is playing on the selected actor now; then the loose files
on disk as a folder tree (`animfiles::Scan()` on first open, `[Rescan]`); then
every animation any graph has read, capped at 400, greyed where the selected
actor's own graph cannot play it. `DrawPlayAnimation` next to it sends the
behaviour-graph event that reaches the state playing the file
(`behavior::Routes`/`Send`), or plays it directly over the graph when no state
does, and says which in its tooltip.

---

## 10. Cross-cutting rules

These are the constraints a refactor has to preserve. Most are written down in
`Widgets.h` and `Preview.h`; this collects them.

### Lock order

> The editor takes the group lock (through `entries::Update`) and then the
> transition lock; the evaluation tick takes them the other way round.

So:

1. Anything from the running side -- what is playing, what is previewed, what
   triggers fired, which animations are known -- is read **before**
   `entries::Update` and handed in as `LiveState` / `actorID`. Asking for it
   while drawing closes the cycle and hangs the game (this actually happened:
   commit `22110e3`).
2. `actorID` is a plain `uint32_t`, read before the lock, not an `RE::Actor*`
   dereferenced under it.
3. Saving happens **after** the mutator returns: `SaveGroup` takes the group
   lock again, and taking it twice from one thread deadlocks. `Groups.cpp`
   records `saveRequested`/`reloadRequested` indices and acts on them outside.
4. `timeline::Draw` takes the group lock itself, so it is called from
   `panel::Draw` and never from inside the mod list's pass.

### Preview publishing

The editor publishes; the tick never reads back. Every held preview is
republished on edit with `toggle=false` so an edit refreshes the pose rather
than switching the preview off:

| Row | Label | Republished by |
|---|---|---|
| modifier | `<modifier name>` | `DrawModifier`, at the end of the row |
| appearance preset | `preset:<name>` | `DrawAppearancePreset` |
| clip / variant | `<clip>` or `<clip>@<variant>` | not republished -- edits need a replay |
| frame | `<clip>@<variant>#<index>` | `DrawFrameBody` |
| timeline scrub | `timeline` | `timeline::Draw`, after any sheet edit |
| Actor window pose | `Actor window` | `ActorWindow::Publish`, on edit and every 20s |

A preview is released on: the button again, another preview, the panel closing
(both routes -- `panel::Draw`'s `!open` and `overlay::SetVisible(false)`), the
selected actor changing, driving switched off, and a deadman timeout.

### Inspect / user / author

`editor::Mode()` reads `entries::Mode()`; `ReadOnly()` is inspect.

- **Every** text field is read-only through the one `EditText`.
- Non-text controls still work, and the mod list puts the group back from a
  `std::optional<core::EntryGroup>` snapshot taken at the top of the row when
  anything changed. The Timeline does the same with a `core::Clip` snapshot.
  So rows still draw, sections still open, and previews still run.
- **User** mode disables only the mod's identity (name, author, description)
  and saves to `<mod>.user.json`; the Save button's label changes.

### "Just added" rows

A row that arrives closed hides the thing you added it for. Three mechanisms:

- `Groups.cpp` keeps `addedModifier` / `addedClip` / `addedPreset` statics,
  passed as `justAdded` into the row, which calls `OpenNext()`.
- `SequenceRows.cpp` keeps `g_openFrame` / `g_openVariant` maps keyed by
  `FramesKey(sequenceKey, variant)` / `sequenceKey`, consumed by
  `OpenIfRequested`.
- `TakeRequestedOpenFrame(framesKey)` **takes** the request, because a request
  left standing would be followed again on every later edit -- the Timeline's
  Frame window uses it to follow a retimed or copied frame.

### Dirty and save

An edit anywhere in a mod sets `loaded.dirty` and re-derives
`core::DeriveTransitionClips` + `core::ResolveGroups`. The dirty mark is an
orange `*` on the mod's row. "Reload mod config" on a dirty mod is armed by a
first click (`armedReload`) and says what it would throw away on the second --
a second click rather than a modal the panel has no other use for.

### Timing

`panel::Timings` holds six `core::Stat`s, guarded by `g_timingMutex`, filled by
an RAII `Measure` around each draw. Note that `timeline::Draw` is measured into
`Timings::editor`, so the "editor tab" row in Settings includes the Timeline
window.

---

## 11. Dependencies out of the UI

Everything the drawing code calls into. A refactor of the UI layer has to keep
these reachable; a refactor of any of these has the UI as a caller.

| Module | Used for |
|---|---|
| `Entries` | `Update`, `Groups`, `Reload`, `ReloadGroup`, `SaveGroup`, `CreateGroup`, `Slots`, `SkinSets`, `Sources`, `PlayingFor`, `LastFiredFor`, `LastFor`, `EvaluateFor`, `Matches`, `Test`, `HeldOn`, `Drive`/`SetDrive`, `DrivenActors`/`DrivenCount`, `IsExcluded`/`SetExcluded`, `Mode`/`SetMode`, `TickRate`/`SetTickRate`, `Timing`/`ResetTiming`, `Reroll`, `PauseInRaceMenu`, `PreviewClearsOthers`, `Enabled` |
| `Preview` | `Showing`, `Current`, `Playhead`, `ShowModifier`, `ShowPose`, `ShowClip`, `ShowFrame`, `Stop` |
| `Journal` | `Watch`, `Watching`, `ForEach`, `Size`, `Clear`, `SetEnabled`, `Filter`/`SetFilter`/`FilterValid`, `Limit`/`SetLimit`, `Save`, `NameOf` |
| `Settings` | `settings::Save()` after every persisted toggle |
| `Overlay` | `MenuKey`/`SetMenuKey`/`CaptureMenuKey`/`CapturingMenuKey`/`HotkeyName`, `Announce`/`SetAnnounce` |
| `Thumbs` / `MeshThumbs` | picker cards, `Clear` on close, `BeginFrame` / `Advance` in the host |
| `Look` | `HeadPartTypeOf`, `HeadPartOptions`, `OwnColoursOf`, plus the probe's `Watch`/`EyeOptions`/`HairOptions`/`SetHeadPart`/`Release`/`GetStats`/`NecksPending` |
| `Facts` | `KeysFor` (an actor's layout keys), `ForgetAll` |
| `Families` | `LayoutsOf`, `Overrides`/`SetOverrides`, `Figures` |
| `BodyPresets` | `Presets`, `FitOn`, `FittingNames`, `OBodyLoaded` |
| `BodyCategories` | BodySlide's slider categories |
| `BodyTris` | `DiscoverBodyMorphs` |
| `BodyMorph` | the direct slider probe: `Set`, `Clear`, `GetStats`, `Inspect` |
| `ActorMorphs` / `Face` / `Catalogue` | `CatalogueFor`, `FindHeadTargets`, `LoadTrisForActor`, `Registry()`, `catalogue::Names` |
| `Animations` / `AnimationFiles` / `AnimationPlay` / `BehaviorRoute` | the animation combo and its Play button |
| `Triggers` | `RecentTags`, `WatchTags` |
| `Functions` | `RunFor` |
| `Patches` | `Current`, `Set`, `UbeEyeRotationScales` |
| `Tilt` | `Shares`/`SetShares`, `Reach`/`SetReach` |
| `Stillness` | `SetSubject`, `HoldCamera`, `HoldHead`, `StopIdles`, `Release` |
| `Sources` | `RecentEvents` |
| `Mfg` | `FixLoaded` |
| `OverlayCatalogue` | `overlays::Get()`, `Scanning()` -- the packs' registered overlays |
| `Activity` / `BaseMorph` / `VertexWatch` / `Headtrack` / `Control` | the probes |
| `dcmf::core` | `EntryConfig`, `ChannelGroups`, `Channels`, `Conditions`, `ConditionSchema`, `Functions`, `Sequence`, `Slots`, `Skins`, `Sources`, `Triggers`, `Transitions`, `Colour`, `Explain`, `MfgChannel`, `SliderIni`, `TriFile`, `EyeRotation`, `Settings`, `Stat`, `Journal` |

---

## 12. State the UI keeps, by file

Hidden state is what makes a move risky. This is all of it.

| File | State |
|---|---|
| `Overlay.cpp` | `g_menuKey`, `g_shift/ctrl/altHeld`, `g_capturing`, `g_announce`, `g_announceSince`, `g_announceDone`, `g_initialised`, `g_visible`, `g_focusLost`, `g_displaySize`, `g_cursorScale`, `g_window`, `g_queueMutex`+`g_queue`, `g_device`, `g_context`, the five relocations |
| `Panel.cpp` | `g_timingMutex`+`g_timings`, `g_target`, `g_formID`; and function-local statics: the morph filter, the body-morph discovery cache + values + filter, the eye/hair option lists and choices, the EFSH and ARTO option lists + filters + chosen + report, the refraction/alpha values, the Entries tab's `preview`/`previewFor`, the engine-expression index/value |
| `Windows.cpp` | `g_showLog`, `g_showSettings`, `g_showActor`, `g_savedTo`, `g_saveError`, `g_filter[64]`, `g_traced`+`g_hasTraced`, `g_settingsAt`, and `wasShowing` for the recording toggle |
| `Mode.cpp` | `g_filter`, `g_filterLowered` |
| `Groups.cpp` | statics inside `Draw`: `status`, `newName[64]`, `armedReload`, `addedModifier`, `addedClip`, `addedPreset` |
| `SequenceRows.cpp` | `g_openFrame`, `g_openVariant`, `g_pendingTime`, `g_editingFrame` |
| `Timeline.cpp` | one `State g_state`: open, sequenceKey, variant, pixelsPerSecond, snap, selected, frameOpen, dragging, dragFrom, folded, dragged, scrubAt |
| `Channels.cpp` | `g_sourcesForDraw`, `g_knownForDraw`, `g_playingForDraw`, `g_haveAnimationActor`, `g_actorKnownForDraw`, `g_typing`; statics: the item-picker filter, the animation-combo filter, `asked` for the first file scan |
| `ChannelSections.cpp` | `g_body` (the body `.tri` walk cache); a static filter in the popup |
| `ActorWindow.cpp` | `g_pose`, `g_mirror`, `g_face`, `g_filter[64]`, `g_anchor`, `g_anchored` |
| `Conditions.cpp` | a static filter in the type combo and another in the actor-value combo |
| `Sections.cpp` | `custom[32]` (the overlay's custom-key field), `refused` (the source-rename message) |
| `Sliders.cpp` | a static filter in the picker; a function-local `byChannel` map |
| `Widgets.cpp` | none beyond ImGui's own state storage (the toggle switch's knob position) |

Note how much of this is a `static` inside a draw function: a filter buffer
shared by every instance of that widget in the frame. `Sections.cpp`'s
`custom[32]` is shared by **every overlay's** "other key" field at once, and
`SequenceRows.cpp`'s `g_pendingTime` was a bug of exactly this kind -- one
static shared by every frame's retime field, so committing gave you the last
frame's time. It is now guarded by `g_editingFrame`.

---

## 13. Observations for the refactor

Facts, not proposals.

**Dead code**

- `editor::DrawFunctions()` (`Widgets.cpp:418`) has no caller. Its body is a
  "Not built" placeholder for the functions section, which now exists for real
  in `Functions.cpp`.
- `editor::DrawMorphPicker` and `DrawMorphPickerContents` (`Sliders.cpp`) have
  no caller outside `Sliders.cpp` itself. The channel popup
  (`DrawChannelListPopup`) replaced them. Only `MorphsFor` is still used, by
  `Groups.cpp` and `Timeline.cpp`. `HasChannel` is used only inside
  `Sliders.cpp`. Four of the five files that include `Sliders.h` want
  `MorphsFor` alone.
- `thumbs::GetStats()` and `meshthumbs::GetStats()` have no caller. The
  `Thumbs.h` comment says "for the settings window"; the settings window does
  not show them.

**Size and mixed concerns**

- `Panel.cpp` (1887) is the main window *and* twelve probe sections that are
  drawn inside the Actor window through a `std::function` callback. The window
  they appear in lives in `editor/ActorWindow.cpp`; the content lives in
  `Panel.cpp`. `DrawActorDiagnostics`'s twelve entries are already a table of
  `{ name, void(*)(RE::Actor*) }`.
- `Channels.cpp` (2057) holds five distinct things: the option/pool model
  (`OptionsFor`, `PoolOf`, `Drawable`), the pickers (`DrawItemPicker`,
  `DrawReferencePicker`, `DrawItemTicks`, `DrawDrawablePools`), the value
  widget, the row, and the group/union/transition sections. The ambient
  `SetSourcesForDraw` / `SetAnimationsForDraw` globals and the animation combo
  also live here, and have nothing to do with channels.
- `Sections.cpp` (1057) is four unrelated sections in one file (pools, channel
  group presets, sources, slots) plus skins in a second `namespace dcmf::editor`
  block at the bottom.

**Duplication**

- The "header + `[Preview]` / `[Stop preview]` right-aligned" block is written
  four times, with its own `RightAlign(CalcTextSize(label).x + FramePadding.x*2)`
  each time: `EntryRows.cpp::DrawPreviewButton`, `DrawKeyframe`, `DrawVariant`,
  and `DrawClip` (the last two also measuring a `[Timeline]` button beside it).
- The "greyed, dimmed whole when disabled" prologue --
  `PushStyleVar(Alpha, style.Alpha * style.DisabledAlpha)` with a matching pop
  (seventeen sites) -- appears in `DrawModifier`, `DrawClip`, `DrawVariant`, `DrawCondition`,
  `DrawFunction`, `DrawOverlay`, `DrawSkins`, `DrawComputedRow`, and twice in
  `DrawChannelGroups`/`DrawChannelUnion` for locked rows.
- The `TreeNodeEx("##node", AllowOverlap | FramePadding | SpanAvailWidth, "")`
  header is written eighteen times.
- The "known keys, then this thing's own custom keys, with a tick each"
  loop exists twice: `Sections.cpp::DrawOverlay` (textures) and
  `DrawSkins` (skin layouts).
- The list-with-removal idiom
  (`for (i = 0; i < v.size();) { bool remove; ...; if (remove) v.erase(...); else ++i; }`)
  appears seventeen times.

**Coupling worth naming**

- `ChannelContext::group` is null in the Actor window. Anything moved into a
  shared row that dereferences it becomes unreachable from there.
- `SetSourcesForDraw` / `SetAnimationsForDraw` are set-and-clear globals with
  two call sites each (`Groups.cpp`, `Timeline.cpp`). A third entry point into
  the channel rows would have to remember to do both, and the failure is silent
  (empty combos).
- `Windows.cpp` owns the three window-visible flags but draws only two of the
  three windows; the Actor window's flag is handed out by reference
  (`bool& ShowActor()`) and passed straight to `ImGui::Begin` as its close
  flag.
- `Panel.cpp` and `Overlay.cpp` both implement "the panel is going away":
  `panel::Draw`'s `!open` branch and `overlay::SetVisible(false)`. They release
  overlapping but not identical sets -- only `panel::Draw` clears the thumbnail
  caches.

**Naming drift**

The main ImGui window is `DCMF`; the log is `DCMF log`; the repo, the docs and
`ui-current.txt` say OpenExpressionReplacer / OER. The banner says "DCMF --
Dynamic Character Modifier Framework".

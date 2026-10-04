# Where the sizes are hardcoded

Every geometry number in the UI, found by search, with its call sites. The
input for a set of general rules -- this file only says what is there now.

Companion to `docs/ui-map.md`, which says what is drawn where.

---

## 0. There is no style setup

`Overlay.cpp:484` is the whole of it:

```cpp
ImGui::StyleColorsDark();
```

Nothing sets `FramePadding`, `ItemSpacing`, `CellPadding`, `WindowPadding`,
`IndentSpacing`, `FrameRounding`, `ScrollbarSize` or any colour. Every value
is either ImGui's dark-theme default or a literal at a call site.

The only `io` configuration is in `Init()`: `ConfigWindowsMoveFromTitleBarOnly`,
`IniFilename = nullptr`, `ConfigFlags |= NoMouseCursorChange`, and
`NavEnableKeyboard` toggled with visibility.

**There is no file a rule could currently live in.** `editor/Widgets.h` is the
nearest thing -- it holds four named constants and the shared primitives.

---

## 1. Item widths -- 128 call sites, 28 distinct values

`ImGui::SetNextItemWidth` is where almost all the hardcoding is. Frequency:

| Value | Count | Mostly |
|---|---|---|
| `160.0f` | 24 | combos: transitions, blend mode, colour space, mode, function type, trigger type, settings sliders |
| `200.0f` | 12 | names, short text fields, actor-value combo |
| `220.0f` | 10 | condition type combo, modifier combo, "Add from preset" combos |
| `240.0f` | 9 | value widgets, longer text, registered-source combo |
| `-150.0f` | 9 | "fill, leaving 150 for the label" -- every top-level Name field |
| `120.0f` | 9 | small combos: Part, Blend, Type, Field, Priority |
| `90.0f` | 7 | the driver's from/to/low/high, the form id, the Snap combo |
| `260.0f` | 5 | texture paths, StorageUtil keys |
| `110.0f` | 5 | hex field, the actor-value reading, "this actor's..." |
| `-FLT_MIN` | 5 | fill the cell (compact Actor-window rows, filters) |
| `-200.0f` | 4 | Panel probe combos, leaving room for a note |
| `140.0f` | 4 | Every / Cooldown / Weight / Mode drags |
| `360.0f` | 3 | the animation combo and its typed field |
| `150.0f` | 3 | plugin name, graph name, actor value |
| `100.0f` | 3 | random low/high, clip Priority |
| `-1.0f` | 3 | fill (the type-combo and actor-value filters) |
| `280.0f` | 2 | the source Actor combos (long labels) |
| `180.0f` | 2 | source Kind combo, the eye-texture pick |
| `-120.0f` | 2 | popup filters, leaving room for "N picked" |
| `80.0f` | 2 | graph type, function Weight |
| `320.0f` | 1 | skin texture paths |
| `250.0f` | 1 | Author |
| `130.0f` | 1 | numeric source combo |
| `70.0f` | 1 | comparison operator |
| `GetFontSize() * 18` | 1 | the new-mod name (`Groups.cpp:49`) |
| `GetFontSize() * 14` | 1 | the mod filter (`Mode.cpp:39`) |
| `-CalcTextSize("(?)").x - ItemSpacing.x*3` | 1 | the log filter (`Windows.cpp:369`) |

Two of these 128 scale with the font; the rest are pixels.

The same control gets different widths in different files -- a name field is
`-150.0f` at the top of a row (`EntryRows.cpp:166`, `Groups.cpp:200`,
`Sections.cpp:63/523/765/970`, `SequenceRows.cpp:563/677`) but `200.0f` inside
one (`Channels.cpp:1671`, `Sections.cpp:397`, `Conditions.cpp:343`) and
`240.0f` in another (`Conditions.cpp:496`).

Full list with line numbers: `grep -rn SetNextItemWidth src/plugin`.

---

## 2. Padding and spacing

### What is read from the style (good -- these already scale)

| Field | Sites | Used for |
|---|---|---|
| `FramePadding` | 14 | button width measurement (`CalcTextSize(label).x + FramePadding.x * 2.0f`), `TreeLines`' start offset, the bottom bar's height |
| `ItemSpacing` | 16 | `BottomPadding()`, the gaps in `RightAlign`, the bottom bar, the settings anchor, the popup row-height sums |
| `DisabledAlpha` / `Alpha` | 17 | every "dimmed whole" block |
| `IndentSpacing` | 1 | `TreeLines`' start x (`* 0.6f`) |
| `ScrollbarSize` | 1 | the timeline sheet's child height |
| `WindowPadding` | 1 | the timeline's `[Fit]` sum |

### What is hardcoded

| Value | Site | What |
|---|---|---|
| `ImVec2(0.0f, 0.0f)` | `Widgets.cpp:222` | `BeginBox`'s zeroed `CellPadding` -- the only `PushStyleVar` that is not Alpha |
| `7.0f` | `Windows.cpp:547` | the gap between the main window and the Settings window |
| `8.0f` | `ActorWindow.cpp:330` | the gap between the Actor window and the right screen edge |
| `16.0f` | `Overlay.cpp:548` | the banner's inset from the bottom-left |
| `20.0f` / `40.0f` | `Windows.cpp:580` | the log window's inset from the top-right, and its minimum x |
| `10.0f` | `Widgets.cpp:351` | `TreeLines::Item`'s stub length (`kStub`) |
| `10.0f` | `Timeline.cpp:219` | `labelPad`, the sheet's label inset |
| `0.6f` | `Widgets.cpp:341` | `TreeLines`' back-off, as a fraction of `IndentSpacing` |

`BottomPadding()` -- one `ItemSpacing.y` `Dummy` -- is called 10 times, against
18 `BeginBox` and 28 `CollapsingHeader` sites, so most sections do not use it.

Counts of the raw separators: `ImGui::Spacing()` 32, `ImGui::Separator()` 25,
`ImGui::SeparatorText()` 13, `ImGui::Dummy()` 9. None of them go through a
helper.

### Indentation

`ImGui::Indent()` / `Unindent()` are called 58 times across 8 files, always at
the default `IndentSpacing`, never through a helper, and unevenly:

| File | Calls |
|---|---|
| `Sections.cpp` | 14 |
| `Channels.cpp` | 8 |
| `EntryRows.cpp` | 10 |
| `SequenceRows.cpp` | 10 |
| `Conditions.cpp` | 6 |
| `Functions.cpp` | 4 |
| `Groups.cpp` | 4 |
| `Widgets.cpp` | 2 |
| `ActorWindow.cpp`, `ChannelSections.cpp`, `Mode.cpp`, `Sliders.cpp`, `Timeline.cpp`, `Windows.cpp`, `Panel.cpp` | 0 |

So a modifier's Conditions section indents and the Actor window's sections do
not; the nesting depth a row sits at depends on which file drew it.

---

## 3. The box

`Widgets.cpp:216-237`. The one place a row's frame is defined:

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
ImGui::BeginTable("##box", 1, ImGuiTableFlags_BordersOuter);
```

18 call sites, all of them `BeginBox()` -- no row draws its own frame. The
zeroed cell padding means a box hugs its contents exactly, so **all** of a box
row's breathing room comes from the `FramePadding` of the tree node inside it,
plus whatever `Spacing()` the body calls.

Its border colour is pushed from outside in exactly one place:
`Channels.cpp:839` pushes `ImGuiCol_TableBorderStrong` to orange for a computed
row the clip is moving through.

`ImGui::BeginTable` is called 6 times in total; the other five are real tables
(Actor-window grid, driven actors, body shapes, timing, the log).

---

## 4. Checkboxes and switches

**No checkbox anywhere is sized.** All 42 `ImGui::Checkbox` calls take ImGui's
default, which is `GetFrameHeight()` square -- i.e. `FontSize +
FramePadding.y * 2`. They are consistent by accident, not by rule: nothing
would stop a caller sizing one.

Where a checkbox's alignment is fought by hand:

| Site | What |
|---|---|
| `Channels.cpp:264-311` | the item picker's row centres the tick, the picture and the name on the middle of whichever is taller, with an explicit `middle(ImGui::GetFrameHeight())` / `middle(ImGui::GetTextLineHeight())` |
| `ActorWindow.cpp:235-241` | the grid's three columns: `220.0f` fixed name, stretch value, `24.0f` fixed clear -- the `24.0f` is a hand-picked "wide enough for a SmallButton x" |
| `Sections.cpp:117` | an overlay's key row draws its thumbnail at `GetFrameHeight() * 2.0f + ItemSpacing.y`, i.e. exactly two checkbox rows |

The only deliberately sized control is `ToggleSwitch` (`Widgets.cpp:55-94`),
whose whole geometry is derived but with hardcoded ratios:

```cpp
height = GetFrameHeight();
width  = height * 1.8f;
radius = height * 0.5f;
knob   = radius - 2.0f;        // the only pixel constant
speed  = DeltaTime * 8.0f;     // the slide
```

It is used at exactly one call site (`Channels.cpp:1247`), for a compact
control switch in the Actor window.

### Buttons

65 `SmallButton` against 56 `Button`, with no rule about which. The same action
is both: `[Delete overlay]` is a `SmallButton` (`Sections.cpp:209`) and
`[Delete modifier]` is a `Button` (`EntryRows.cpp:162`); `[Add overlay]` is a
`SmallButton` (`Sections.cpp:244`) and `[Add frame]` is a `Button`
(`SequenceRows.cpp:475`).

Only two buttons are given an explicit size, both in `Windows.cpp`:

- `ImVec2(0.0f, barHeight - style.ItemSpacing.y)` -- the three bottom-bar
  buttons (lines 526, 531, 537);
- `okWidth = 120.0f` -- the Problems modal's OK (line 510-513).

Plus five `InvisibleButton`s, four of which are the timeline's own hit targets
(`12.0f` wide handles, `12.0f` square diamonds, the fold strip, the scrub
strip).

---

## 5. The right-hand column

Three different mechanisms, three different constants:

| Helper | Constant | Sites |
|---|---|---|
| `SecondColumn(percent = 0.55f)` | `0.55` default; `0.85` passed twice | 20 call sites, 18 of them at the default |
| `RightAlign(width)` | the caller computes the width, always as `CalcTextSize(label).x + FramePadding.x * 2.0f` | 5 call sites, each recomputing the same expression |
| `IndicatorSlot()` | `kIndicatorWidth = 18.0f` (`Widgets.cpp:214`) | `VerdictMark` only |

`ActiveDot` uses a fourth: `radius = GetTextLineHeight() * 0.22f`
(`Widgets.cpp:279`). The condition scope dot uses the same `0.22f` but
recomputed from its own `size` (`Conditions.cpp:115`) rather than shared.

The `0.85f` at `Windows.cpp:210/253` is the log's trace rows -- a different
column position from every other row in the UI, for no stated reason.

---

## 6. Thumbnails and pictures

All in multiples of a text line, which is the one place a scale already exists:

| Size | Site | What |
|---|---|---|
| `GetTextLineHeight() * 3.0f` | `Channels.cpp:260` | the item picker's row picture |
| `GetTextLineHeight() * 5.0f` | `Channels.cpp:559` | a reference picker's row picture |
| `GetTextLineHeight() * 7.0f` | `Channels.cpp:642/645/648` | the current value, beside the combo |
| `GetFrameHeight() * 2.0f + ItemSpacing.y` | `Sections.cpp:117` | an overlay key row's thumbnail |
| `320.0f, 320.0f` | `Channels.cpp:589`, `Thumbs.cpp:142` | the hover tooltip, hardcoded in two places |

The combo popups that hold the 5-line rows size themselves to five of them:

```cpp
rowHeight = GetTextLineHeight() * 5.0f + ItemSpacing.y;
SetNextWindowSizeConstraints({0,0}, {FLT_MAX, rowHeight * 5.0f + GetTextLineHeightWithSpacing() * 2.0f});
```

written out twice, identically (`Channels.cpp:671-672` and `725-726`).

---

## 7. Window, popup and child sizes

| Size | Site | Window |
|---|---|---|
| `720.0f, 740.0f` | `Panel.cpp:1701` | main, first use |
| `640.0f` x main-window height (fallback `760.0f`) | `ActorWindow.cpp:329` | Actor, first use |
| `kLogWidth = 560.0f` | `Windows.cpp:48` | log, fixed |
| `900.0f, 360.0f` | `Timeline.cpp:666` | Timeline, first use |
| `640.0f, 520.0f` | `Timeline.cpp:596` | the timeline's Frame window |
| `760.0f, 520.0f` | `Windows.cpp:496` | the Problems modal, fixed |
| `560.0f, 620.0f` | `Channels.cpp:229`, `ChannelSections.cpp:349` | both list popups -- the one value that *is* consistent, written twice |
| `60.0f` | `ActorWindow.cpp:330` | the Actor window's fallback top |

Derived caps, which do scale:

- the log's height: `min(DisplaySize.y * 0.55f, 520.0f)` (`Windows.cpp:608`);
- the log's outer constraint: `max(DisplaySize.y - 40.0f, 200.0f)` (line 584);
- Settings' room: `max(DisplaySize.y - g_settingsAt.y - 16.0f, 200.0f)` (line 660).

Fixed-height children:

| Height | Site | What |
|---|---|---|
| `400.0f` | `Windows.cpp:227` | the trace child |
| `220.0f` | `Panel.cpp:1049` | the body-shapes table |
| `-ImGui::GetFrameHeightWithSpacing()` | `Channels.cpp:319`, `ChannelSections.cpp:366` | both popup lists, reserving one button row |
| `-bottomBarHeight` | `Panel.cpp:1809` | the tab child |

The bottom bar's height is the one composed-from-style value in the whole UI
(`Panel.cpp:1805`):

```cpp
GetTextLineHeight() + FramePadding.y * 4.0f + ItemSpacing.y * 4.0f
```

and `Windows.cpp:454` computes a *different* `barHeight` for the buttons in it:

```cpp
GetFrameHeightWithSpacing() + FramePadding.y * 2.0f
```

Two formulas for the same bar, in two files.

---

## 8. The timeline's private geometry

`Timeline.cpp` has its own coordinate system and does not use any of the above.
Named:

```cpp
constexpr float kLabelWidth = 190.0f;   // line 72
constexpr float kTailWidth  =  40.0f;   // line 73
```

Derived from the line height (lines 217-222):

```cpp
lineHeight    = GetTextLineHeight();
rulerHeight   = lineHeight + 12.0f;
numericHeight = lineHeight * 2.4f;
otherHeight   = lineHeight + 8.0f;
headingHeight = lineHeight + 4.0f;      // line 230
labelPad      = 10.0f;                  // line 219
```

Raw pixels inside the sheet, none named: handle half-width `6.0f`, handle
height `11.0f`, tick length `6.0f`, tick text offset `+3.0f/+2.0f`, function
dot radius `2.5f`, key diamond half `5.0f` and hit box `12.0f`, colour swatch
step `4.0f` and half-height `6.0f`, stepped block half-height `7.0f`, block
text inset `8.0f`, curve sample step `3.0f` and inset `4.0f`, row bottom
clearance `8.0f`, label clip inset `4.0f`, empty-state text at `+8.0f/+8.0f`,
playhead and scrub thickness `2.0f`, curve thickness `1.5f`, fold triangle
`lineHeight * 0.3f` with `0.7f`/`0.9f` ratios.

Zoom limits: `clamp(pixelsPerSecond, 20.0f, 2000.0f)` in three places
(lines 259, 738); pan step `wheel * 60.0f` (line 264); zoom factor
`1.25f` / `0.8f` (line 258); tick spacing wants `>= 60.0f` pixels (line 87).

---

## 9. Colours

Out of scope for padding, but the same shape of problem, so: **82
`ImGui::TextColored` calls**, over **51 distinct RGBA literals** and **12
distinct `IM_COL32` literals**, none named. 141 RGBA literals in total.

The recurring ones, counted:

| Literal | Meaning | Sites |
|---|---|---|
| `{1.0f, 0.8f, 0.3f, 1.0f}` | warning | 20 |
| `{1.0f, 0.6f, 0.4f, 1.0f}` | a softer warning / "not here" | 16 |
| `{0.7f, 0.7f, 0.7f, 1.0f}` | dim (where `TextDisabled` would do) | 10 |
| `{1.0f, 0.4f, 0.4f, 1.0f}` | error | 7 |
| `{0.6f, 1.0f, 0.6f, 1.0f}` | good / matched | 7 |
| `{1.0f, 0.3f, 0.3f, 1.0f}` | a *different* error red | 6 |
| `{0.5f, 0.5f, 0.5f, 1.0f}` | tree lines, disabled | 5 |

The remaining 44 literals appear once or twice each. Error is two reds
(`1.0,0.4,0.4` and `1.0,0.3,0.3`) and warning is at least four oranges
(`1.0,0.8,0.3`, `1.0,0.6,0.4`, `1.0,0.6,0.2`, `1.0,0.5,0.2`), depending on
which file you are in. Greens for "this is good" run to five
(`0.6,1.0,0.6`, `0.5,1.0,0.5`, `0.4,0.9,0.4`, `0.2,0.8,0.2`, `0.25,0.85,0.25`).

`Windows.cpp` has the only two colour *functions* (`ColourFor(JournalEvent)`
with 13 cases, `ColourFor(StepResult)` with 4) -- the only place colour choice
is named rather than inlined. `Conditions.cpp:94-111` is the only other place
colour is chosen by a switch, for the four scope dots.

The tick, cross and dot marks each carry their own:
`{0.2f,0.8f,0.2f}` / `{0.8f,0.2f,0.2f}` (`Widgets.cpp:323/328`) and
`{0.25f,0.85f,0.25f}` for the active dot (line 284) -- two greens for one
concept.

`TreeLines` uses `{0.5f,0.5f,0.5f,1.0f}` twice (`Widgets.cpp:353/366`).
`Thumbs.cpp:136-137` and `MeshThumbs.cpp` each define the card's backdrop
`IM_COL32(24,24,28,255)` and border `IM_COL32(70,70,78,255)`; the same border
colour is written a third time in `Channels.cpp:1368` for the gradient strip.

---

## 10. What already scales, and what does not

**Scales with the font / style today**

- every "dimmed whole" block (`Alpha * DisabledAlpha`);
- every button-width measurement (`CalcTextSize + FramePadding.x * 2`);
- `BottomPadding`, `TreeLines`, `ActiveDot`, `VerdictMark`'s offsets;
- every thumbnail (multiples of `GetTextLineHeight`);
- the timeline's row heights;
- the two `GetFontSize() *` item widths, and `PushTextWrapPos(GetFontSize() * 26)`;
- the bottom bar's height and the tab child's reservation;
- the log's and Settings' height caps.

**Does not scale**

- 126 of the 128 item widths;
- every window, popup, modal and fixed child size;
- `kIndicatorWidth`, `kStub`, `kLogWidth`, `kLabelWidth`, `kTailWidth`,
  `labelPad`, `okWidth`, the `24.0f` clear column, the `220.0f` name column;
- the `320.0f` tooltip picture;
- every inset between windows (`7.0f`, `8.0f`, `16.0f`, `20.0f`, `40.0f`);
- all of the timeline's sheet pixels;
- every colour.

At a larger font the panel's labels grow and its fields do not.

---

## 11. Where a rule would have to reach

Counted by call site, the surface any convention has to cover:

| Thing | Sites | Currently |
|---|---|---|
| item width | 128 | 28 literals |
| `SameLine` | 218 (205 bare) | -- |
| dimmed-whole prologue | 17 | copy-pasted |
| `SmallButton` vs `Button` | 65 / 56 | no rule |
| checkbox | 42 | ImGui default, never sized |
| `Spacing` / `Separator` / `SeparatorText` | 32 / 25 / 13 | raw |
| `Indent` / `Unindent` | 58 | raw, uneven |
| `TextColored` | 82 | 22 literals |
| `BeginBox` | 18 | the one shared frame |
| `CollapsingHeader` (raw) vs `SectionHeader` | 28 / 6 | two ways to open a section |
| `BottomPadding` | 10 | against 18 boxes and 28 headers |
| tree-node header flags | 18 | copy-pasted triple |

The four existing named constants are `kIndicatorWidth` (18), `kStub` (10),
`kLogWidth` (560) and the timeline's `kLabelWidth`/`kTailWidth` (190/40).
Everything else is a literal at the point of use.

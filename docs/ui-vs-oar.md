# Where we diverge from Open Animation Replacer

OAR is the reference the panel was built against, and most of its shapes were
ported faithfully. This lists every place the two differ, with both sides'
call sites, so a choice can be made deliberately rather than by drift.

Read against `reference/OpenAnimationReplacer/src/UI/`. Companions:
`docs/ui-map.md` (what we draw), `docs/ui-style-audit.md` (where our numbers are).

ImGui version note: we build against 1.92.8, OAR against an older one. A few
differences are forced by that -- `ImGuiTreeNodeFlags_AllowOverlap` (ours) vs
`AllowItemOverlap` (theirs), `ImGuiKey_ModShift` vs `ImGuiMod_Shift` -- and are
not choices. They are not listed below.

---

## 1. The structural ones

These are not widget details; they decide everything downstream.

### 1.1 There is no style object, and no UI scale

| | OAR | Us |
|---|---|---|
| style setup | `UIManager::GetDefaultStyle()` returns an `ImGuiStyle`, assigned wholesale at init (`UIManager.cpp:52-54`) | `ImGui::StyleColorsDark()` and nothing else (`Overlay.cpp:484`) |
| style overrides | `ModalWindowDimBg = (0,0,0,0.65)` (`UIManager.cpp:600`) | none |
| UI scale | `Settings::fUIScale`, 0.5-2.0, a slider with an explicit `[Apply]` (`UIMain.cpp:379-389`) | none |
| how it scales | `UICommon::ScaleAllSizes(style, scale)` scales 22 style fields, plus `io.FontGlobalScale = scale` (`UIManager.cpp:605-617`, `UICommon.cpp:579-603`) | -- |
| when | `UpdateStyle()` at the top of every `Render()`, re-applied only when the scale changed | -- |

This is the root of §2: OAR can afford derived widths because the style itself
moves; we hardcode because nothing moves.

### 1.2 Window placement is shared there, hand-rolled here

OAR has a `UIWindow` base class (`UI/UIWindow.h`, `.cpp`) that every window
derives from:

- `ShouldDrawImpl()` / `DrawImpl()` / `OnOpen()` / `OnClose()` -- open and
  close are **events**, so the animation log turns recording on in `OnOpen`
  and off in `OnClose` (`UIAnimationLog.cpp:50-58`).
- `SetWindowDimensions(offsetX, offsetY, sizeX, sizeY, alignment, sizeMin, sizeMax, cond)`
  with a 9-point `WindowAlignment` enum, a uniform **20px** screen margin, a
  **40px** minimum clearance, and a min size of `min(300, display.x - 40) x min(200, display.y - 40)`.
- `ForceSetWidth(w)` pins a window's width.

We have none of this. Every window positions itself inline, with a different
inset each time:

| Window | Our inset | Site |
|---|---|---|
| Settings | `+7.0f` from the main window's corner | `Windows.cpp:547` |
| Actor | `-8.0f` from the right screen edge, fallback top `60.0f` | `ActorWindow.cpp:330` |
| log | `-20.0f` from the right, `20.0f` top, min x `40.0f` | `Windows.cpp:580` |
| banner | `16.0f` from bottom-left | `Overlay.cpp:548` |

And our recording-follows-the-log-window is a `static bool wasShowing`
comparison (`Windows.cpp:565-569`) rather than an `OnOpen`/`OnClose` pair.

### 1.3 We do not persist the layout

| | OAR | Us |
|---|---|---|
| `io.IniFilename` | `Settings::imguiIni.data()` -- a real file (`UIManager.cpp:46`) | `nullptr`, "no stray ini beside the exe" (`Overlay.cpp:477`) |
| main window size | never set; the ini remembers it (`UIMain.cpp:33`) | `720x740` `FirstUseEver` (`Panel.cpp:1701`) |

So every window we open is back at its default size each session, and the
`FirstUseEver` conditions never do anything after the first frame.

### 1.4 Log geometry is settings there, constants here

| Thing | OAR | Us |
|---|---|---|
| log width | `Settings::fAnimationLogWidth` | `kLogWidth = 560.0f` (`Windows.cpp:48`) |
| log offset | `Settings::fAnimationLogsOffsetX/Y` | `20.0f` hardcoded |
| entry fade | `Settings::fAnimationLogEntryFadeTime` | `kFadeSeconds = 1.5f` (`Windows.cpp:176`) |
| event-log height | `Settings::fAnimationEventLogHeight` | n/a |
| trace child height | `ImVec2(0, 600)` (`UIAnimationLog.cpp:240`) | `ImVec2(0, 400)` (`Windows.cpp:227`) |

The fade maths is otherwise identical -- `lerp(0, 0.25, ...)` on
`ImGuiTableBgTarget_CellBg` -- we just fade 1.5s where they fade a
configurable time.

---

## 2. Item widths: 36 sites vs 128

The single biggest divergence.

| | OAR | Us |
|---|---|---|
| `SetNextItemWidth` call sites | 36 | 128 |
| of which hardcoded pixels | **9** | **126** |
| the main mechanism | `UICommon::FirstColumnWidth(percent)` -- 24 sites | none |
| font-relative | `GetFontSize() * {5,10,15,18}` -- 6 sites | `GetFontSize() * {14,18}` -- 2 sites |
| computed from avail | `filterWidth`, `avail`, `conditionComboWidth`, `functionComboWidth` | 5 `-FLT_MIN`, 3 `-1.0f`, 1 computed |
| pixel literals | `-150.f` x4, `-220.f` x2, `200.f` x2, `250.f` x1 | 25 distinct values, 116 sites |

`FirstColumnWidth` is the piece we never ported:

```cpp
// UICommon.h:103
inline float FirstColumnWidth(float a_firstColumnWidthPercent)
{
    return ImGui::GetContentRegionAvail().x
         - (ImGui::GetContentRegionMax().x * (1.f - a_firstColumnWidthPercent))
         - ImGui::GetStyle().ItemSpacing.x;
}
```

It is the partner of `SecondColumn(percent)`: the argument widget fills up to
the column line, and the right-hand text starts at it, from **one** number.
Every OAR condition component takes `a_firstColumnWidthPercent` as a parameter
and threads it down (`API/OpenAnimationReplacer-ConditionTypes.h:172` makes it
part of the public `DisplayInUI` signature).

We took `SecondColumn` and left `FirstColumnWidth` behind, so our left-hand
widths are 25 unrelated pixel constants that do not line up with our
`SecondColumn` at 0.55.

**And the default differs**: OAR's `SecondColumn` defaults to `0.5f`
(`UICommon.h:98`), ours to `0.55f` (`Widgets.h:104`). We copied their `0.85f`
override for trace rows exactly.

### The settings window in particular

OAR's settings window sets **no item width at all** -- every slider takes the
full available width (`UIMain.cpp:355-470`). We set `160.0f` on all eleven
(`Windows.cpp:719-924`).

---

## 3. Colours: 38 named constants vs 51 anonymous literals

OAR has **zero** `ImGui::TextColored` calls. Every coloured string goes through
`UICommon::TextUnformattedColored(NAMED_CONSTANT, text)` -- 45 sites -- and
every disabled one through `TextUnformattedDisabled` -- 31 sites.

We have **82 `TextColored` calls over 51 distinct RGBA literals**, none named.

### We inlined fourteen of their constants without the names

These are byte-identical to `UICommon.h:18-55`, written out at our call sites:

| Our literal | Their name | Our sites |
|---|---|---|
| `{0.2f, 0.8f, 0.2f, 1.0f}` | `SUCCESS_COLOR` | 2 |
| `{0.8f, 0.2f, 0.2f, 1.0f}` | `FAIL_COLOR` | 1 |
| `{0.5f, 0.5f, 0.5f, 1.0f}` | `TREE_LINE_COLOR` | 5 |
| `{1.0f, 0.3f, 0.3f, 1.0f}` | `ERROR_TEXT_COLOR` | 6 |
| `{0.8f, 0.1f, 0.1f, 0.75f}` | `ERROR_BUTTON_COLOR` | 1 |
| `{0.8f, 0.1f, 0.1f, 1.0f}` | `ERROR_BUTTON_HOVERED_COLOR` | 1 |
| `{0.6f, 0.07f, 0.07f, 1.0f}` | `ERROR_BUTTON_ACTIVE_COLOR` | 1 |
| `{1.0f, 0.5f, 0.1f, 1.0f}` | `WARNING_TEXT_COLOR` | 1 |
| `{0.8f, 0.3f, 0.1f, 0.5f}` | `WARNING_BUTTON_COLOR` | 1 |
| `{0.8f, 0.3f, 0.1f, 1.0f}` | `WARNING_BUTTON_HOVERED_COLOR` | 1 |
| `{0.7f, 0.25f, 0.08f, 1.0f}` | `WARNING_BUTTON_ACTIVE_COLOR` | 1 |
| `{1.0f, 0.5f, 0.2f, 1.0f}` | `DIRTY_COLOR` | 2 |
| `{1.0f, 0.6f, 0.2f, 1.0f}` | `KEY_TEXT_COLOR` | 2 |
| `{0.4f, 0.7f, 1.0f, 1.0f}` | `LOG_LOOP_COLOR` | 1 |

### And invented a second set for the same roles

Our four most-used colours are **not** OAR's, and duplicate roles OAR already
has a constant for:

| Our literal | Sites | Role | OAR's constant for that role |
|---|---|---|---|
| `{1.0f, 0.8f, 0.3f, 1.0f}` | 20 | warning | `WARNING_TEXT_COLOR (1, 0.5, 0.1)` |
| `{1.0f, 0.6f, 0.4f, 1.0f}` | 16 | soft warning / "not here" | -- |
| `{0.6f, 1.0f, 0.6f, 1.0f}` | 7 | good | `SUCCESS_COLOR (0.2, 0.8, 0.2)` |
| `{1.0f, 0.4f, 0.4f, 1.0f}` | 7 | error | `ERROR_TEXT_COLOR (1, 0.3, 0.3)` |

So "error" is two reds and "warning" four oranges in our tree, and neither
majority value is the one we use for the tick and cross marks we ported from
them.

`{0.7f, 0.7f, 0.7f, 1.0f}` x10 is a hand-rolled disabled grey where OAR would
call `TextUnformattedDisabled` (which reads `ImGuiCol_TextDisabled` from the
style, so it follows the theme).

---

## 4. Widget-by-widget

| Widget | OAR | Us | Note |
|---|---|---|---|
| **help marker** | `HelpMarker(desc)` calls `AlignTextToFramePadding()`, then disabled `(?)`, then `AddTooltip(desc, DelayShort)`. The caller writes `SameLine()`. 59 sites | `Hint(text)` does `SameLine()` **itself**, then `TextDisabled("(?)")`, then `Tooltip`. 103 sites | ours cannot be used at the start of a line; theirs can |
| **tooltip delay** | `AddTooltip` defaults to `ImGuiHoveredFlags_DelayNormal`; `HelpMarker` uses `DelayShort` | no delay anywhere | ours pop instantly on any hover |
| **tooltip padding** | pushes `WindowPadding {8,8}` around the tooltip (`UICommon.h:68`) | none | |
| **tooltip wrap** | `GetFontSize() * 50.0f` | `GetFontSize() * 26.0f` | ours is half as wide |
| **text ellipsis** | four overloads (`NoTooltip`, plain, `Colored`, `Short`) working on `const char*` ranges, no allocation in the common case; 27 sites | one `TextEllipsis(text, maxWidth)`, builds a `std::string` and a substring every frame it truncates; 5 sites | |
| **disabled text** | `TextUnformattedDisabled` (pushes `ImGuiCol_TextDisabled`) x31 | `ImGui::TextDisabled` x136 | same effect, ours is the raw call |
| **small vs normal button** | `Button` x41, `SmallButton` **x1** | `SmallButton` x65, `Button` x56 | they effectively never use SmallButton; we use it more than Button, with no rule |
| **destructive action** | `ButtonWithConfirmationModal(label, confirmation, fn, size)` -- a modal with OK/Cancel at 120px each, **Ctrl to skip** (`UICommon.cpp:256-299`) | a "click again to confirm" armed button for mod reload (`Groups.cpp:356-371`); plain buttons everywhere else | we delete modifiers, clips, conditions, slots and overlays with one unconfirmed click |
| **checkbox** | 40 sites, never sized | 42 sites, never sized | **no difference** -- both take `GetFrameHeight()` |
| **`BeginDisabled`** | 46 sites | 11 sites | they grey a control that cannot act; we more often hide it or let it act |
| **text input** | `UICommon::InputText(label, std::string*, maxLength, ...)` -- modified imgui_stdlib, resizes in place | `EditText` copies into a `std::vector<char>` **every frame**, then assigns back | ours allocates per field per frame |
| **key binding** | `UICommon::InputKey(label, key[4])` -- a reshade-style key box drawn inline | a `[Change]` button + "Press the new key..." text + a capture flag in `Overlay.cpp` | |
| **type combo** | `ComboFilter` with **fuzzy search** (`UIComboFilter.h`, `FuzzySearch`/`FuzzySearchRecursive` in `UICommon.cpp:605+`), keyboard nav with scroll-to-item helpers, per-item colour from `Info::textColor` | `BeginCombo` + a substring `find` on a lowercased needle | ours needs the exact substring |
| **leaf tree node** | `TreeNodeCollapsedLeaf` (three overloads, reimplemented from imgui internals) | `LeafNode(id)` -- `TreeNodeEx` with `Leaf \| Bullet \| NoTreePushOnOpen` | ours is the flags version, simpler |
| **arrow button with text** | `ArrowButtonText` + `PopupToggleButton` (reimplemented from imgui internals) | none | |
| **warning icon** | `DrawWarningIcon()` -- a drawn triangle with an exclamation mark, sized off `GetTextLineHeight()` | an orange `!` character | |
| **evaluate result** | `DrawConditionEvaluateResult` with **three** states: tick, cross, and a **drawn dice face** for a condition with a random result. Indicator at `ContentRegionMax.x - 15.f`, ends with `ImGui::NewLine()` | `VerdictMark` with **two**: tick and cross. `kIndicatorWidth = 18.0f`, ends with a `Dummy` | the dice is a real feature we do not have; our `Dummy` is a deliberate fix (see §6) |
| **dirty mark** | `TextUnformattedColored(DIRTY_COLOR, "*")` | `DirtyMark()` -- same `*`, same colour, inlined literal | |
| **tree lines** | inlined twice in `DrawConditionSet`/`DrawFunctionSet` | `TreeLines` class in `Widgets.cpp` | **we refactored theirs**; same geometry exactly |

---

## 5. Behaviour we did not port

| Feature | OAR | Us |
|---|---|---|
| **inspect mode** | `EditMode { kNone, kUser, kAuthor }` gates drawing: in `kNone` the add button, the delete, the type combo and drag & drop are **not drawn at all** (`a_editMode > EditMode::kNone`, ~30 sites) | everything draws, and the group is reverted from a snapshot at the end of the row (`Groups.cpp:144`, `329-330`; `Timeline.cpp:698`, `768-769`) |
| **copy / paste** | right-click context menu on a condition: Copy condition, Paste condition below, with the pasted condition **previewed in the tooltip** by re-entering `DrawCondition` at `EditMode::kNone` (`UIMain.cpp:2405-2437`) | none |
| **drag & drop** | conditions reorder by dragging, with a rendered drag preview (`BeginDragDropSourceEx`) | none |
| **comments** | per-condition comment, `COMMENT_COLOR`, an "Edit comment" menu item | none |
| **context menus** | `BeginPopupContextItem` on rows | none anywhere |
| **welcome banner** | a dismissible first-run banner window with its own class | one fading line in the main menu (`##dcmfannounce`) |
| **error banner** | a top-centre banner window for problems | a coloured status button in the bottom bar opening a modal |
| **progress bar** | an animation-queue progress window with a configurable linger | none |

---

## 6. Where we diverged deliberately, with the reason in the code

These are not drift. Each has a comment saying why, and a refactor should not
"fix" them back.

| Choice | Ours | Theirs | Why (from our source) |
|---|---|---|---|
| **box id** | a fixed `"##box"` under the caller's `PushID` (`Widgets.cpp:222`) | `std::format("{}conditionTable", (uintptr_t)a_condition.get())` -- the object's address (`UIMain.cpp:2360`) | "Adding an item reallocates the vector it lives in, every address changes, and ImGui treats widgets it has never seen as new -- which closed every open row on any edit that grew a list." Their conditions are `unique_ptr`s with stable addresses; ours are values in vectors. |
| **indicator slot** | submits a `Dummy` of the width it skips (`Widgets.cpp:259`) | moves the cursor and calls `ImGui::NewLine()` (`UICommon.cpp:224`) | "ImGui extends a window's bounds from the items in it, and moving the cursor past the edge without submitting one is what raises 'Code is using SetCursorPos() to extend window boundary'." |
| **`RightAlign`** | measures from `GetContentRegionAvail()` on the current line (`Widgets.cpp:296-306`) | `TextDescriptionRightAligned` measures from `GetContentRegionMax()` (`UICommon.h:90`) | "Inside a tree node and a child window those are not the same thing, and using the window's put the button half outside it." |
| **input swallowing** | feeds the game an empty event list from the dispatch hook (`Overlay.cpp:604-608`) | an input-consumer count (`AddInputConsumer`/`RemoveInputConsumer`) | ours keeps the game running with no menu open at all |
| **no ini** | `IniFilename = nullptr` | a real ini | "no stray ini beside the exe" |
| **backend patches** | corrects `io.DisplaySize` and the cursor position **after** the stock backend runs (`Overlay.cpp:526-532`) | vendors a modified `imgui_impl_win32.cpp` (`UI/ImGui/`) | "applying them here keeps the stock backend" |

---

## 7. Things we have that OAR does not

For completeness, so a "make it like OAR" pass does not delete them:

- `ToggleSwitch` -- a pill switch with an animated knob (`Widgets.cpp:55`).
- `SectionHeader(label, hint)` -- a collapsing header with its `(?)` on the
  header line; OAR always puts the marker on the row below.
- `ActiveDot` -- a green "live right now" dot.
- The **condition scope dots** (green/yellow/orange/red by evaluation cost,
  `Conditions.cpp:86-120`) -- OAR has no cost signalling.
- `BottomPadding` -- OAR has no equivalent; it uses `Spacing()` x65 where we
  use it x32.
- The **timeline** and its whole coordinate system -- nothing in OAR resembles
  it.
- `Thumbs` / `MeshThumbs` picture cards in pickers.
- `ImGui::SeparatorText` x13 -- OAR uses it nowhere (probably an ImGui version
  thing; it is newer than their build).
- Per-window **draw timings** in the settings window.

---

## 8. Summary table

| Axis | OAR | Us |
|---|---|---|
| style object | yes, scalable, 22 fields | none |
| UI scale setting | 0.5-2.0 + font scale | none |
| window placement | shared base class, 9-point alignment, 20px margin | four inline insets: 7, 8, 16, 20 |
| layout persisted | yes (ini) | no |
| item width sites | 36 (9 hardcoded) | 128 (126 hardcoded) |
| first-column helper | `FirstColumnWidth(percent)`, 24 sites | none |
| `SecondColumn` default | `0.5f` | `0.55f` |
| named colours | 38 constants, 0 `TextColored` | 0 constants, 82 `TextColored`, 51 literals |
| `SmallButton` : `Button` | 1 : 41 | 65 : 56 |
| `BeginDisabled` | 46 | 11 |
| destructive confirm | modal + Ctrl-skip, everywhere | one armed button, one place |
| tooltip delay | `DelayNormal` / `DelayShort` | none |
| tooltip wrap | 50 em | 26 em |
| combo search | fuzzy | substring |
| inspect mode | controls not drawn | controls drawn, edits reverted |
| copy/paste, drag & drop, comments | yes | no |

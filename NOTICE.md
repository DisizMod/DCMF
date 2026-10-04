# Third-party code and licensing

This plugin links **CommonLibSSE-NG**, which is **GPL-3.0-or-later**, so the
project as a whole is obliged to be GPL-3.0 compatible. That is what makes the
ports below permissible; it also means any distribution must honour those terms.

## Dependencies

| Component | Licence | How it is used |
|---|---|---|
| [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) | GPL-3.0-or-later | git submodule at `lib/commonlibsse-ng`, linked statically |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | via vcpkg, with the DX11 and Win32 backends; drawn from a `Present` hook |
| [MinHook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause | via vcpkg; used to hook a function entry, which the SKSE trampoline cannot do |
| spdlog, fmt, DirectXMath, DirectXTK, nlohmann-json, rapidcsv, simpleini, toml11, xbyak | see upstream | via vcpkg, as CommonLibSSE-NG requires |
| [Catch2](https://github.com/catchorg/Catch2) | BSL-1.0 | tests only, fetched at configure time |

## Ported code

Logic ported from **[Mfg Fix NG](https://github.com/KrisV-777/Mfg-Fix-NG)**
(GPL-3.0) by KrisV-777, with the corresponding source files noted at each site:

- **`src/core/Blink.cpp`** -- the eyelid state machine, from
  `BSFaceGenAnimationData::EyesBlinkingUpdate`. Vanilla's blinking is broken for
  the player; this replaces it.
- **`src/plugin/Keyframes.cpp`** -- forcing the `unk217` "keyframes changed"
  flag, and the merge of the console and script channels into the final ones,
  from `KeyframesUpdateHook` and `RegularUpdate`.
- **Address Library offsets** for `BSFaceGenAnimationData::KeyframesUpdate`
  (25983), `SetExpressionOverride` (25980) and `Reset` (25977), read from its
  `src/mfgfix/Offsets.h` rather than inferred. An earlier attempt to infer
  `KeyframesUpdate` by elimination produced the wrong id.

Approach and hook points ported from
**[Open Animation Replacer](https://github.com/ersh1/OpenAnimationReplacer)**
(GPL-3.0) by Ersh:

- **`src/plugin/Overlay.cpp`** -- the four hooks that put an ImGui overlay in
  the game's own render loop (`RegisterClassA`, the D3D11 device creation,
  `Present`, and the input dispatcher), the empty-event-list trick that
  swallows input without pausing anything, and the DirectInput scancode
  translation. Read from `src/Hooks.cpp` and `src/UI/UIManager.cpp`; the code
  here is written against the stock ImGui backends rather than OAR's vendored
  copy, so its two edits to `imgui_impl_win32.cpp` are applied from outside
  instead.
- **`src/plugin/Animations.cpp`** -- the `hkbClipGenerator::Activate` and
  `Deactivate` vtable hooks and the actor-from-`hkbCharacter` derivation
  (`Utils.h`), which is how an animation's start and end are known.
- **`src/plugin/AnimationPlay.cpp`** -- OAR's animation preview, ported:
  `FakeClipGenerator` (the `hkbClipGenerator` layout with the fields its
  update reads, the engine's own clip functions handed a pointer to it) and
  `ActiveAnimationPreview::OnGenerate` (sampling the clip and blending its
  pose over the graph's output), with the `hkbBehaviorGraph::Update` and
  `Generate` vtable hooks and every engine id from `src/Offsets.h`. What the
  editor's Play button plays a file by.

Mfg Fix is a hard conflict with this plugin *by choice*: both hook
`KeyframesUpdate`. See `docs/mfgfix-interop.md`.

## Reverse engineering

Skyrim SE 1.5.97 as distributed by Steam has its `.text` section encrypted
(SteamStub), so engine internals here were established by in-process analysis
and by reading published sources -- never by unpacking or redistributing the
executable. `docs/hook-analysis.md` records the method and its verification.

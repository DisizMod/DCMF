# Hook analysis — Skyrim SE 1.5.97

How the facegen apply point was located, what is established, and what is still
open. Reproducible: the tooling in `tools/` regenerates everything below.

## Inputs

| Input | Location |
|---|---|
| Game binary | `C:\Game\SkyrimSE\SkyrimSE.exe`, version 1.5.97.0 |
| Address Library | `version-1-5-97-0.bin`, format 1, 778 674 addresses |
| Type layout | CommonLibSSE-NG 7.5.1 (`bedcb1e`) |

## The database reader is verified, not assumed

`tools/addrlib.py` implements the format transcribed from CommonLibSSE-NG's
`REL::IDDB`. It consumes the file exactly (1 490 796 of 1 490 796 bytes) and is
checked against ids whose meaning CommonLibSSE already documents. Each must land
in the section its kind implies:

| id | Symbol | RVA | Section | Expected |
|---|---|---|---|---|
| 252410 | `VTABLE_BSFaceGenNiNode` | `0x016018C8` | `.rdata` | `.rdata` |
| 515572 | `NiRTTI_BSFaceGenNiNode` | `0x02F07D38` | `.data` | `.data` |
| 25977 | `BSFaceGenAnimationData::Reset` | `0x003C38E0` | `.text` | `.text` |
| 25980 | `BSFaceGenAnimationData::SetExpressionOverride` | `0x003C3F00` | `.text` | `.text` |
| 26259 | `BSFaceGenManager::PrepareHeadPartForShaders` | `0x003D2A60` | `.text` | `.text` |
| 514182 | `BSFaceGenManager` singleton | `0x01EBEB40` | `.data` | `.data` |
| 69564 | `BSDynamicTriShape::ctor` | `0x00C72300` | `.text` | `.text` |

Run `python tools/verify_addrlib.py <bin> <exe>`. It prints PASS or names the
failures. Nothing downstream should be trusted if this does not pass.

## The binary is DRM-packed, so static disassembly cannot work

| Section | Entropy | Note |
|---|---|---|
| `.text` | **8.000** | maximum — fully encrypted |
| `.bind` | 7.948 | SteamStub's own section |
| `.rdata` | 4.431 | readable |

The entry point is in `.bind`, not `.text`. Disassembling a *known-good*
function such as `BSFaceGenAnimationData::Reset` yields nonsense (`bswap edx`,
`jl ...`), which is the signature of reading encrypted bytes rather than of a
wrong offset.

Consequence: `.text` only exists in readable form inside the running process.
Anything requiring instruction-level analysis has to run in-process, which an
SKSE plugin already does. `src/plugin/Analysis.cpp` does exactly that and logs
Address Library ids rather than dumping code — that is all that is needed to
identify a function, and it keeps this interoperability work rather than the
production of an unpacked binary.

## What the vtable gives us for free

`.rdata` is *not* encrypted, so the vtable walk succeeds statically.
`BSFaceGenNiNode` overrides `NiNode::UpdateDownwardPass` at vtable slot `0x2C`:

```
[0x2A] rva 0x00C58390  id 68948     NiNode territory
[0x2B] rva 0x00C58E00  id 68958     NiNode territory
[0x2C] rva 0x003D8760  id 26404  <- BSFaceGenNiNode's own override
[0x2D] rva 0x00C58660  id 68951     NiNode territory
[0x2E] rva 0x00C58A00  id 68954     NiNode territory
```

Slot `0x2C` is the only one in the `0x003Dxxxx` facegen neighbourhood — the same
region as `Reset` (`0x003C38E0`) and `PrepareHeadPartForShaders` (`0x003D2A60`).
Its neighbours all sit in NiNode's `0x00C58xxx` range.

**`BSFaceGenNiNode::UpdateDownwardPass` = Address Library id 26404.**

This is a catalogued id, so it is a valid Address Library trampoline target
today. `src/plugin/Face.cpp` hooks it: the original runs first, so vanilla lip
sync, blink, look-at and dialogue emotion all keep computing, and our pass reads
their output instead of suppressing it.

## Open — the deeper apply point

Hooking `UpdateDownwardPass` gives a correctly-timed per-frame callback per face
node. It does **not** yet give authority to *replace* vanilla's vertex output,
because the routine that turns resolved keyframes into vertex deltas sits
somewhere beneath it and has not been identified.

`AnalyzeFaceGenCallGraph()` reports the call tree two levels down from id 26404
with every target resolved to an id. The apply routine is the one that touches
the resolved keyframe sets — `expression3` (`+0x0C0`), `modifier3` (`+0x100`),
`phoneme3` (`+0x140`), `custom3` (`+0x180`) — and writes vertex data.

Identifying it needs one game launch with the analysis build.

## Corrections to the design document

Found while reading CommonLibSSE-NG's headers against the spec.

1. **`BSFaceGenAnimationData` holds 13 keyframe sets, not 3.** They are staged:
   inputs (`expressionKeyFrame`, `modifierKeyFrame`, `phenomeKeyFrame`,
   `customKeyFrame`), dialogue values (`modifier1`, `phoneme1`), and **final**
   values (`expression3`, `modifier3`, `phoneme3`, `custom3`). The `*3` sets are
   where every upstream source has already been resolved, and are what the
   compositor should read.
2. **There is a fourth channel.** `custom` / `customKeyFrame` / `custom3`, whose
   sole member is `SkinnyMorph`. Undocumented in the spec, and present in real
   head TRIs — it is the first morph in the High Poly Head file. It must be
   passed through.
3. **The modifier enum is 17 wide, not 14.** Indices 14–16 are `HeadPitch`,
   `HeadRoll`, `HeadYaw`. Those are bone-driven, so a `float[14]` scratch channel
   is right for vertex work, but the `MfgConsoleFunc` shim indexes the real
   17-wide enum and must not assume 14.
4. **Reaching the animation data is a virtual call, not a pointer chain.**
   `actor->GetFaceNodeSkinned()` (slot `0x61`), then
   `faceNode->GetRuntimeData().animationData`. The spec's
   `currentProcess->middleHigh->faceAnimationData` path exists but is
   version-fragile; the virtual is not.
5. **`BSFaceGenBaseMorphExtraData` is the wrong write target for animation.** It
   holds the static neutral — SKEE's territory, per the spec's own §6.5. Per-frame
   deltas belong in the head's `BSDynamicTriShape::dynamicData`.
6. **The face node knows its actor.** `BSFaceGenNiNode::GetRuntimeData().unk15C`
   is an `ActorHandle`, so the per-frame callback resolves its own actor without
   a reverse lookup.

## Trap: `REL::Offset2ID::operator()` terminates on a miss

It does not throw. On a lookup failure it calls `stl::report_and_fail`, which
tears the process down:

```cpp
if (it == _offset2id.end() || it->offset != a_offset) {
    stl::report_and_fail(std::format("Failed to find the offset ... 0x{:08X}", a_offset));
}
```

A byte scan for `E8` opcodes hits non-function targets constantly, so wrapping
the call in `try`/`catch` does nothing and the game dies on the first stray
immediate. `Offset2ID` does expose `begin()`/`end()` over its sorted container,
so `Analysis.cpp` runs its own `lower_bound` and treats a miss as "not a
function" -- which is exactly the signal the scan needs anyway.

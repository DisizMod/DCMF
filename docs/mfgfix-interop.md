# Mfg Fix NG — what it actually does, and why it is not a conflict

Derived from the shipped PDB symbols, `mfgfix.ini`, and the Papyrus sources in
Mfg Fix NG 1.0.9. Verified in game: both plugins loaded together
(`mfgfix.dll` handle 41, `DCMF.dll` handle 48) and faces
animated normally.

## What it patches

| Target | Note |
|---|---|
| `BSFaceGenAnimationData::KeyframesUpdate` | the big one — it replaces the engine's keyframe update wholesale |
| `BSFaceGenAnimationData::Reset` | |
| `BSFaceGenAnimationData::SetExpressionOverride` | |
| `BSFaceGenAnimationData::sub_3DB770` | |
| `BSFaceGenNiNode::sub_3F0C90`, `sub_3F1800` | neither is our hook |
| `ModifyFaceGenCommand` | the `mfg` console command |
| `Papyrus::Actor::SetExpressionOverride` | |

## What it reimplements

`RegularUpdate`, `SmoothUpdate`, `DialogueModifiersUpdate`,
`DialoguePhonemesUpdate`, `EyesBlinkingUpdate`, `EyesDirectionUpdate`,
`EyesMovementUpdate`, `Reset`, `SetExpressionOverride`,
`ClearExpressionOverride`.

In other words it owns the **keyframe layer** end to end: it decides what the
expression, modifier and phoneme *weights* are, including blinking, eye
movement and dialogue lip sync.

## Why it does not conflict with the hook we have today

The spec's §6.1 declares Mfg Fix a hard conflict because "you would both hook
the same territory". That was written for a design that hooks the engine's
morph writers. It does not describe what got built.

| | Layer | Owns |
|---|---|---|
| Mfg Fix NG | `BSFaceGenAnimationData` | keyframe **weights** |
| This plugin | `BSFaceGenNiNode::UpdateDownwardPass` → `BSDynamicTriShape` | **vertex deltas** |

Different objects, different hooks, different stages of the same pipeline. They
stack rather than compete: Mfg Fix decides the weights, we decide what the mesh
does with them. `0x003D8760` (our hook, id 26404) appears nowhere in its patch
list.

**§6.1 is therefore wrong about *why*, but right about *what to do*.** They do
not technically conflict at the hooks we use today. The project has nonetheless
decided to take the keyframe layer, so Mfg Fix becomes a hard conflict by
choice rather than by accident: we want ownership of transitions and eye
movement, which are its job.

## Decision: we replace it

Owning transitions and eye movement means owning the keyframe layer, which
means hooking the same function it does.

Mfg Fix NG is open source -- **[KrisV-777/Mfg-Fix-NG](https://github.com/KrisV-777/Mfg-Fix-NG)**
-- so the offsets below are read from its `src/mfgfix/Offsets.h` rather than
inferred. Ids are `(SE, AE)`.

| Target | SE id | SE rva |
|---|---|---|
| `BSFaceGenAnimationData::KeyframesUpdate` | **25983** | `0x003C4030` |
| `BSFaceGenAnimationData::SetExpressionOverride` | 25980 | `0x003C3F00` |
| `BSFaceGenAnimationData::sub_3DB770` | 25979 | `0x003C3DF0` |
| `BSFaceGenAnimationData::Reset` | 25977 | `0x003C38E0` |
| `BSFaceGenNiNode::sub_3F1800` | 26417 | |
| `BSFaceGenNiNode::sub_3F0C90` | 26407 | |
| `ModifyFaceGenCommand` | 22542 | |
| `Papyrus::Actor::SetExpressionOverride` | 53926 | |

Our vertex hook is 26404, which appears in none of these.

> **Correction.** An earlier revision of this document claimed `KeyframesUpdate`
> was id 25979, inferred by assuming it had to lie between `Reset` (25977) and
> `SetExpressionOverride` (25980) and then confirming with a reference count in
> `mfgfix.dll`. The assumption was unfounded and the real answer is 25983. The
> reference count was evidence that 25979 *is patched* -- which it is, as
> `sub_3DB770` -- never evidence of *which function it is*. Hooking 25979 would
> have detoured the wrong function.

Taking that hook makes Mfg Fix genuinely incompatible, and transfers its whole
job to us:

- the seven vanilla bug fixes in the table above (§6.1)
- blink timing (§2.2's eyelid mask stays a separate, vertex-layer concern)
- per-emotion eye movement and saccades (§5)
- expression transitions, which become `blendIn`/`blendOut` (§3)
- the `mfg` console commands (§6.3)
- `MfgConsoleFunc` **and** `MfgConsoleFuncExt` Papyrus shims (§6.2 -- note the
  spec only lists the first; NG's Ext adds a `speed` parameter that existing
  content may rely on)

`mfgfix.ini` is a ready-made specification for the tuning surface: its blink
timings and per-emotion gaze offsets and delays are exactly the parameters
worth exposing.

## Load order, now that both hook the same function

Both plugins detour `KeyframesUpdate` (25983) at plugin load -- NG via Detours
in `SKSEPluginLoad`, us via MinHook in `Plugin.cpp`. SKSE loads plugins in
directory order, so `mfgfix.dll` patches first and our trampoline lands on
*NG's replacement*, not vanilla: we run after NG and layer over it. That, and
not any separation of layers, is why "both loaded, faces animated normally"
held once we took the keyframe hook. Reversed, NG's detour replaces ours
outright -- it never calls the original -- and our keyframe layer silently does
nothing.

Compared against NG at `c94a556`, ours is a partial port: the `unk217` fix and
the expression merge are exact; the dialogue-channel merges, the Look-group
rule and the phoneme threshold are not ported; `SmoothUpdate` is not. The full
table and the order in which to finish the replacement are in
`docs/backlog.md` under *Replacing Mfg Fix NG, in order*.

## What Mfg Fix is worth reading as

Not a competitor now, but a working reference implementation and a tuning
specification. It solves the same problems we are about to solve, and its INI
names the parameters that turned out to matter in practice.

Its Papyrus surface is also the compatibility contract we inherit: existing
content calls `MfgConsoleFunc.SetPhonemeModifier` and, on NG,
`MfgConsoleFuncExt.SetPhonemeModifierSmooth` with a `speed` argument. Whatever
we build has to answer to both names or that content breaks.

## Behaviour worth knowing for the compositor

- `fDialoguePhonemeThreshold = 50` — dialogue phoneme values below 50 are
  dropped entirely. The compositor's phoneme channel therefore sees a gated
  signal, not the raw one.
- `fDefaultSpeed = 0.00` under `[Transition]` controls expression transition
  speed. This is the same job as the spec's `blendIn`/`blendOut`, so the two
  need a documented precedence rather than both easing the same value.
- Blink timing is Mfg Fix's (`fBlinkDownTime` 0.04, `fBlinkUpTime` 0.14,
  delay 0.5–8.0s). §2.2's eyelid mask still applies, because that is a
  vertex-layer concern.

## Licensing

Mfg Fix NG is **GPL-3.0**. That is not an obstacle here: this plugin already
links **CommonLibSSE-NG, which is GPL-3.0-or-later**, so it is obliged to be
GPL-3.0 compatible regardless.

So its fixes can be *ported*, with attribution, rather than reverse-engineered
from behaviour -- which is both faster and far less error-prone than inferring
offsets from a stripped binary, as the correction above illustrates.

# How a morph reaches the screen

The single most important thing learned building milestone 1, and a correction
to the design document's assumptions.

## Two routes, both working

| Morph | Route | State |
|---|---|---|
| The engine's 17 expressions, 16 phonemes, 14 modifiers, `SkinnyMorph` | write the final keyframe channel; the engine applies and uploads | **works** |
| Everything else -- `EXPR2_*`, `LIP_*`, `NOSE_*`, the extended sets | write the *base* the engine morphs from, in `BSFaceGenBaseMorphExtraData` | **works** |

`BigAah` is phoneme index 1. `MoodHappy` is expression 10. `SquintLeft` is
modifier 12. For any of these the engine already owns the whole pipeline, so
using it is strictly better than replacing it.

For everything else, the answer turned out to be writing the engine's **input**
rather than its output.

## Why writing the output was unwinnable

Measured, not assumed. A `PAGE_GUARD` on a head's vertex buffer caught the
engine writing it at **id 75672, rva `0x00D78C50` + 0x65** -- a generic geometry
routine near `BSDynamicTriShape::ctor`, not facegen code, which is why walking
the call graph down from `UpdateDownwardPass` never found it.

The ordering was the problem:

1. **Scenegraph update** -- `UpdateDownwardPass`, where our vertex writes happened
2. **Rendering** -- id 75672, where the engine writes the same buffer

The engine always wrote second, so it won. The symptom was precise and was the
clue that cracked it: a held morph flickered past for a *single frame* now and
then -- those were the frames where the engine had no facegen work to do and
left our write alone.

Three attempts failed before the routing question was asked:

1. Write in `UpdateDownwardPass` after the original -- lost the race
2. Hook id 75672 and re-apply after it -- **crashed**; its signature cannot be
   read because the game's `.text` is encrypted, and the guess was wrong
3. Clear `unk217` for driven actors to suppress the engine's re-apply -- held for
   about a second, then reverted

All three were variations on the same mistake: writing
`BSDynamicTriShape::dynamicData`, which is what the engine *computes*.

## The base morph route

`BSFaceGenBaseMorphExtraData` is attached to head geometry and holds
`NiPoint3* vertexData` -- the base head shape, before any expression morph. The
engine's pass is, in effect:

    dynamicData = baseMorphExtraData.vertexData + sum(keyframe_i * triMorph_i)

So a delta written into `vertexData` is carried into **every** recompute instead
of being erased by one. That is the whole difference, and it was confirmed in
game: offsetting the base translates the head and it **holds**, with blinking and
lip sync still animating on top of it.

This gives the vertex route the same three properties the keyframe route has:

- **No race.** The engine reads our value rather than overwriting it.
- **No upload problem.** The engine uploads its own result.
- **Off-camera actors work.** Facegen work is skipped for faces that are not
  being rendered, and we no longer depend on that work happening.

It is also where RaceMenu holds its extended head sliders, which is why the
buffer is known to be writable and honoured.

### What this costs

- **Composition with RaceMenu.** We keep no copy of the head. Undoing means
  subtracting our own contribution -- the deltas are in the registry and the
  weights are in the state, so the original is reconstructible and never needs
  storing. That takes out only what is ours, rather than stamping a remembered
  head over a sculpt somebody else applied in the meantime. Three vertices of
  each shape are sampled as we leave it, and if they have moved by the next pass
  the buffer was rewritten by someone else: our bookkeeping no longer describes
  it, so we start from nothing on that shape instead of gouging theirs.
- **It persists until undone.** Unlike the dynamic buffer, which the engine
  rewrites every frame, a base edit stays until we put it back. Dropping an
  actor without restoring would strand its face permanently, so the apply pass
  restores whenever an actor stops being driven.
- **Three disagreeing vertex counts.** `modelVertexCount`, `vertexCount` and the
  geometry's own count need not match, and CommonLib notes `vertexData` is
  allocated against `modelVertexCount`. Writes are bounded by the smallest
  non-zero one and refused entirely if that is absent or implausible: getting it
  wrong is a heap overwrite, not a visual glitch.

## Consequence for the design

Section 1.1's D1 -- "hook the point where keyframes become vertex deltas, one
hook, full authority" -- is the wrong shape for this engine. There are two
places to write, both of them *inputs* the engine already reads, and neither
requires owning its output:

- morphs with an MFG channel -> the keyframe layer
- morphs without one -> the base morph data

`MfgSlotForMorph` makes that split, and the panel marks the second kind `(base)`.

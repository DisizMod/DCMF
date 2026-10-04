# Bone channels

Every bone any actor could have gets channels a modifier can drive: its mode,
scale, three rotations and length. Spine and head turns stay with the pose
segments.

## Intent

- An author can scale, turn and lengthen any bone, physics bones included,
  without anyone in the scene wearing what brings that bone.
- A mod that does its own bone maths can claim bones with a high-priority
  modifier set to **Released**, and DCMF then leaves those bones exactly as the
  animation or that mod left them.
- Spine1, Spine2, Neck and Head are turned only through the spine and head pose
  segments. Per bone, they take scale and nothing else.

## 1. Scan

A new file-stage scan source fills a new catalogue list, `bones`: one row per
bone name, with its category.

It reads node names with `Scan::Nif::ReadNodeNames` from:

- every race's skeleton model, male and female;
- every head part's model;
- every armour addon's models: male, female, and first person.

The records stage already collects the head parts and armour addons.

Category, decided per bone:

1. In a race's skeleton: `Skeleton · <race>`. A bone in several races'
   skeletons takes the first race that has it.
2. Otherwise, brought by one record only: `<kind> · <record name>`, such as
   `Hair · KS Ponytail` or `Armour · Elven Dress`.
3. Otherwise, brought by several records: `<kind> · <slot>`, such as
   `Armour · Body (32)` or `Hair`. When the records disagree on kind or slot,
   the category is `Shared`.

A NIF that cannot be read is reported as a scan problem, the same way the other
sources report theirs, and skipped.

## 2. Channels

`Entries::Populate` adds each scanned bone to the existing `bone` kind, the
table the `Bone` applier reads. A bone already in the table is not added again;
the table only grows within a session. The bone's category becomes the entry's
group.

Six entries per bone, in this order, so the applier's fixed stride holds:

| Entry | Type | Range | Neutral |
|---|---|---|---|
| `mode` | mode: Offset, Fixed, Released | — | Offset |
| `scale` | number | 0.25 to 4 | 1 |
| `x`, `y`, `z` | number, radians | −1.5 to 1.5 | 0 |
| `length` | number, game units, step 0.1 | −10 to 10 | 0 |

`length` is XPMSE's length slider. XPMSE moves a bone's CME child along its z
axis; DCMF cannot rely on CME nodes, so it moves each of the bone's children
outward along the line from the bone to that child.

Spine1, Spine2, Neck and Head have the same six entries. Only `scale` is in
the picker; `mode`, `x`, `y`, `z` and `length` are hidden. The pose segments
keep writing the hidden rotations, as they do today.

The spine and head pose segments gain **Released** as a mode. Gaze does not.

Channel names follow the existing scheme, `bone:<node>.<property>`, for
example `bone:NPC L Thigh [LThg].length`.

## 3. Applying

All of this is in the `Bone` applier, after animation, in
`BSFadeNode::UpdateDownwardPass`, as today.

Each frame, the pose segments go first (rests, then spine and head aims), as
now. Each bone's own channels go after.

- **Offset:** rotation is multiplied onto the animated rotation, and scale
  multiplies the animated scale.
- **Fixed:** the bone's rotation is replaced by its rest, with the channel
  angles on top. A skeleton bone's rest is the race skeleton's, which
  `Pose.cpp` already reads. A bone that is not in the skeleton has no skeleton
  rest, so its rest is the transform it had when DCMF first saw it, before any
  write of DCMF's.
- **Length:** each child's offset from the bone gets the distance added along
  its own direction. The child's original offset is recorded the first time, so
  the edit never compounds from frame to frame.
- **Released:** when a bone's winning mode becomes Released, DCMF puts back once
  everything it changed on that bone (its rotation, its scale and its children's
  offsets) to what was recorded before its first write. After that it does not
  touch the bone. The same holds for the bones a spine or head segment drives
  when the segment becomes Released. Leaving Released records fresh originals.
- **No longer driven:** a bone no modifier drives any more is restored the same
  way. This is the existing let-go rule for every route into the actor.
- **Missing bone:** a bone the actor does not have, such as a skirt bone when no
  skirt is worn, is logged once per actor and skipped.

## Testing

In game, through the Actor window's held rows:

- a thigh's length;
- a breast bone's scale with physics running;
- a skirt bone on Fixed;
- Released on a bone that RaceMenu's XPMSE sliders also edit.

What to check: nothing drifts over time, and Released hands each bone back
exactly as it was.

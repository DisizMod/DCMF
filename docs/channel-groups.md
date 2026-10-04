# Channel groups

What replaced tracks, and why.

## The problem with tracks

A track was a named set of channels, *registered* by a mod for the whole
load order, that a modifier or clip listed by name to say which channels it
may set and which of them it cannot do without. Three things made that hard
to use:

- The name said nothing about what it was for. "Track" reads as a timeline.
- Registration for the load order meant a modifier could be broken by
  another mod renaming a track, and the editor had to explain clashes.
- Built-in tracks (face, body, eyes, engine, look) and inferred family
  tracks meant a modifier had tracks it never asked for, and a channel could
  be "set but in no track" and silently ignored.

## What a channel group is

A **channel group** is a named list of channels a modifier or clip sets
together, with the ones it cannot do without marked **required**. If a
required channel is lost to a higher-priority modifier that set it to a
different value, the whole modifier is dropped -- every channel of every
group it lists lets go together, the way an expression is one thing. An
author who wants two parts to survive separately writes them as two
modifiers.

For a clip every channel of its groups is required, and the groups are
its **claim**: two clips run on the same actor at the same time as long as
their claims do not overlap, and a clip that fires into an overlap either
takes the running clip over whole (an equal or higher priority may, if
the running clip is interruptible; a lesser never) or is dropped. A clip may
list a channel in its groups that no key ever sets, to keep other clips off
it while it runs; the modifiers keep driving such a channel underneath.

A group is either written in place on the modifier (an *inline* group) or a
**reference** to one of the mod's channel group presets. The required marks
belong to the use, not the preset: one preset can be required in one
modifier and optional in another.

Groups are private to the mod. Nothing is registered for the load order and
nothing can refer across mods. The popup that edits a list offers every
channel there is, in the Actor window's sections, with a checkbox each. Two
presets exist without being written -- **Head controls** and **Eyes
control**, the Actor window's two control sections -- and a mod's own
preset of the same name stands in front of them.

## Presets

A mod has three kinds of preset, all private to it, all in one **Presets**
section of the editor:

- **Conditions** -- a named list of conditions, used by reference through
  the `PRESET` condition.
- **Appearance** -- named groups and channel values, used by reference from
  a modifier's `appearance` list. Every channel the preset touches is
  **locked** in the modifier: the modifier cannot set it itself, and the
  editor shows the preset's row read-only. To change it, edit the preset,
  or turn the reference into a copy.
- **Channel groups** -- a named channel list, used by reference from a
  modifier's, clip's or appearance preset's groups.

A referenced preset item in a modifier has a **Make a copy** button, which
writes the preset's content in place and drops the link.

## The file

```json
"channelGroupPresets": [
  { "name": "smile", "channels": ["moodhappy", "dialoguehappy"] }
],
"appearancePresets": [
  { "name": "shy",
    "groups": [ { "preset": "smile", "required": ["moodhappy"] } ],
    "channels": [ { "channel": "moodhappy", "value": 0.6 } ] }
],
"modifiers": [
  { "name": "blush",
    "appearance": ["shy"],
    "groups": [
      { "preset": "smile" },
      { "name": "blush layer", "channels": ["slot:blush", "slot:blush:alpha"],
        "required": ["slot:blush"] }
    ],
    "channels": [ ... ] }
]
```

A group is `{ "preset": name, "required": [...] }` or
`{ "name": ..., "channels": [...], "required": [...] }`. A modifier's own
`channels` row on a channel a referenced appearance preset touches is a
warning on load; the preset's value stands.

There is no `tracks` key any more. The files that used it were converted
once by `tools/convert_groups.py`, which keeps a `.bak` beside each.

## In the editor

- Modifiers and clips have a **Channel groups** section: add an inline
  group (the channel popup), add one from a preset, mark channels required,
  make a referenced group a copy.
- The **Channels** section and the **Timeline** list channels under their
  group, in group order; a channel in two groups sits under the first.
- The Actor window is unchanged: it holds a pose, not a modifier.

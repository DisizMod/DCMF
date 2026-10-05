# Backlog

Ideas that are worth building but are not being built now. Each entry says what
it is, what already exists to hang it on, and what is unresolved -- so that
picking one up starts from the questions rather than from scratch.

## Actions on sequences (OAR-style functions)

**Built 2026-09-18** as functions -- see
`docs/superpowers/specs/2026-09-18-functions-design.md`: `PlaySound`,
`CONDITION`, `RANDOM` and `ONE`, on a modifier's `onActivate` and
`onDeactivate` and on a keyframe's `functions`; fire and forget; run in
previews too. What follows is the thinking that led there, kept for the
questions still open at the end (stopping, save and load, voice).

A sequence step can currently only say what the face does. It should also be
able to *do* something at the moment it starts or ends -- first of all play a
sound: an effect, or a voice clip.

**Explicit non-goal: this does not touch the dialogue system.** No topic, no
`Say`, no lip sync driven by the engine's dialogue path. A sound is played at
the actor and that is all. Anything the mouth does during it is authored on our
own sliders, which is precisely the thing Open Animation Replacer cannot do and
we can.

### What OAR does, since it is the model

OAR hangs a *function set* off a sub-mod at three points
(`src/ReplacerMods.h:182`): `onActivate`, `onDeactivate`, and `onTrigger`, where
a trigger is an animation event plus a payload string
(`src/API/OpenAnimationReplacer-FunctionTypes.h:59`). Functions are registered
by name into a factory (`src/OpenAnimationReplacer.cpp:987`), built out of typed
components -- form, numeric, text, bool, NiPoint3, keyword, and a nested
condition set -- which is what gives each one both its JSON shape and its
editor row for free. `CONDITION`, `RANDOM` and `ONE` are themselves functions
containing function sets, so branching and weighted choice fall out of the same
mechanism rather than being special cases.

`PlaySound` itself is nine lines (`src/Functions.cpp:344`): resolve the FormID
to a `BGSSoundDescriptorForm`, `BSAudioManager::GetSoundHandle`,
`SetObjectToFollow(refr->Get3D())` so it moves with the actor, `Play()`. The
handle is then dropped -- OAR never stops what it started.

### Where it would attach here

The seam exists. Sequences already have a start, an end, and steps with enter
and exit moments, and entries already carry a condition set with the same
authoring machinery a function's condition component would want. The smallest
honest version is one action kind (`PlaySound`), one attachment point (step
enter), and the editor row for it -- not a component system. The component
system is what OAR needed because it accepts third-party functions through an
API; we would need it only when there is a second and third action kind worth
generalising over.

### What is unresolved

- **Stopping.** OAR fires and forgets, which is right for a footstep and wrong
  for anything that outlives the step that started it. A sequence can be
  interrupted, superseded by a higher-priority entry, or end early -- and every
  other route into the face in this project has needed its own explicit undo
  rather than merely going quiet. So: keep the `BSSoundHandle`, and decide per
  action whether release stops it or lets it finish. A one-shot grunt wants to
  finish; a held loop does not.
- **Save and load.** There is no cosave, so a sequence does not survive a save
  anyway -- but a sound handle it started might, with nothing left to stop it.
  Either both are made to survive together or neither is, and until the cosave
  exists that argues for actions that are short.
- **Voice without the dialogue path.** A sound descriptor plays the clip but
  moves no mouth. Driving the mouth from our own morphs is the interesting
  answer and also the expensive one: it means authoring visemes per clip, or
  reading the `.lip` file that ships beside the `.fuz`. Worth knowing that the
  data is there before assuming it has to be hand-authored.
- **Whose sound is it.** Played at the actor's 3D (OAR's choice), or 2D for the
  player? An emotion cue heard by the player and an audible effect in the world
  are different things and probably want different defaults.

## Holding a sequence until its trigger ends

**Done, the cheap way.** A clip's `stopWhenConditionsFail` lets it go
mid-run -- through each channel's exit easing -- the moment its own
conditions stop holding; with `IsPlayingAnimation` among them and a long
last frame, the face lasts exactly as long as the animation. The animation
itself is known from the behaviour graph: `hkbClipGenerator::Activate` and
`Deactivate` are hooked (the two calls OAR hooks to replace animations),
every actor has a set of animations playing now, and `AnimationStart` /
`AnimationEnd` triggers fire on the bound path -- vanilla's file, or an OAR
replacement's own path, both recorded. The deadman the old note asked for
is the condition itself: an animation cut short deactivates its clip like
any other.

An animation is named by its path below the animations folder
(`mt_idle.hkx`, `openanimationreplacer\<mod>\<sub-mod>\mt_idle.hkx`): the
graph's own entries are project-relative and OAR's are from the game
folder, and `NormalizeAnimationPath` cuts both to that key, so what the
graph names, what the hook records and what is on disk compare. The picker
lists the loose `.hkx` files under `actors\character\animations` beside
what is playing and the graph's list, so a replacer's file can be picked
without playing it. With OAR loaded, a replacement is named by the file OAR
says it chose (`GetCurrentReplacementAnimationInfo`, one read per clip
start): OAR filters duplicate files by hash, so two sub-mods shipping the
same bytes share one binding entry -- the first registered -- and the
binding alone named "NPC modesty" while "Player modesty" played. Not done:
matching a whole OAR sub-mod ("any animation of this moveset"), which would
be a prefix rule on the condition; and OAR sends no event of its own when
a replacement plays -- its `SendAnimEvent`
function is an author's annotation into the behaviour graph, which an
animation-event sink would hear if a source for those is ever wanted.

## Renaming breaks references

A name is how everything here refers to everything else: a modifier's name
in a clip's transition trigger, in `OnModifierEnabled`, in `IsModifierActive`;
a clip's name in a variant preview; a channel group preset, a slot, an overlay, a source by
name from any mod in the load order; a preset by name. Rename the thing and
every reference goes stale, silently except where a load-order check happens
to say so (sources do; the rest do not). Not solved yet. Two
shapes worth weighing:

- **Rename-with-fixup in the editor.** The editor knows every reference site
  within a file, so a rename there can rewrite them in the same file -- and,
  for load-order names (slots, sources), in every loaded file, which
  means editing other mods' files, which the user editor mode already does
  as user files. Cheap, covers the common case, leaves references from files
  not loaded stale.
- **Stable identities.** Give each named thing an id that never changes and
  refer by id, with the name as a label. Solves it for good, changes every
  file format and every reference site, and makes hand-authored files harder
  to write. Probably not worth it for a name that is also the identity
  authors reason in.

Until one lands, the editor should at least *say* what a rename breaks: a
count of references that will go stale, before the rename is committed.

## What variants left out

Variants are built -- see `docs/superpowers/specs/2026-09-12-variants-design.md`:
a sequence has `variants[]`, each a keyframe set with a `weight` and an
`enabled`, picked per fire at random (with an optional `noRepeat`) or in turn,
per actor. What stayed out, from OAR's `Variants.h`, each recorded here in
case it ever has a sequence meaning:

- **`playOnce` and its ordering** -- a variant that plays exactly once per
  actor and then leaves the pool. Needs per-actor history that survives being
  forgotten, which nothing else here does yet.
- **Conditions or flags per variant** -- the whole point of a variant is that
  everything but the keys is shared. A variant that needs its own conditions
  is a second sequence.
- **Blending between variants** -- two bodies mixed by weight rather than one
  chosen. No sequence today has two poses at once to mix.
- **History shared across actors** -- OAR's `SharedPlayedHistory`, so a
  sequential cycle advances once for a crowd rather than once per face.

## Driving a slider from a value

The more hurt an actor is, the wider the mouth. A contribution's `value` is a
number an author wrote; this reads it off the actor each tick instead.

The shape: an optional `driver` on `SliderContribution`, built on the
conditions rewrite's `Numeric` component (`ActorValue / Health / Percentage`,
a global, a graph variable -- nothing to invent), with two anchors rather than
a min/max pair because the mapping is usually inverted and "at X the slider is
Y" is how an author thinks:

```
driver: { source: Health %, at: 1.0, is: 0.0, to: 0.3, gives: 0.8 }
```

Linear between, clamped outside. Evaluated in the plugin at the point
`Entries.cpp:787` builds `matched`, so the resolver stays pure and blend
modes, priority and flags work unchanged.

**The problem, which is why this is here and not in a spec:** it collides
with the transition layer. `TransitionSet::Retarget` (`Transitions.h:82`) is
explicit -- *a target that has not changed is left alone; re-aiming every
evaluation would restart the easing four times a second and it would never
arrive*. A driven value changes every tick and would do exactly that. Two
answers, producing different faces at the moment health drops sharply, and one
has to be picked on purpose: the transition eases the entry's *presence* (0 to
1 on match, 1 to 0 on release) and the driven value is applied under it; or
driven channels get a follow mode that moves the target without restarting the
curve. Sequences get drivers for free afterwards, since a keyframe holds
contributions too, though a driven key on a timeline is a stranger thing.

### Where the value can come from (2026-09-13) -- done as registered sources

Built as **sources**: a mod registers them by name for the load order (a
`sources` section), typed number / string / bool with an
actor rule, of kind `modevent`, `storageutil`, `jcontainers`, `papyrus`,
`factionrank`, `global`, `actorvalue` or `graphvariable`. A number source is
a `NumericSource` (`{"registered": name}`) for drivers and every numeric
component; a string or bool source is a contribution's value
(`"source": "registered"`) on references, modes and switches, on modifiers
and clip keys (read when the clip starts); a `Source` condition compares
one. Mod events are heard by one sink for every mod's `SendModEvent`, kept
per event name and per actor; the VM kinds are polled and cached; the rest
read the engine. Not built: a `ModEvent` trigger kind for clips, and the
actor-tags INI (a StorageUtil string is a tag). The survey that led here:

Not only the actor's own numbers. The appearance survey made the case for
drivers concrete -- blush by arousal, gloss by wetness, a belly morph by a
scene's aftermath -- and every one of those reads another mod's state. The
channels other mods actually use, from what needs nothing to what needs a
bridge:

- **Global variables, actor values, graph variables** -- already
  `NumericSource`. Survival Mode's needs are globals.
- **Faction rank** -- SexLab Aroused stores arousal as a rank; OSL Aroused
  keeps a faction too. `FactionRank` is a condition; it is not yet a source.
- **Keywords, magic effects, spells, perks** -- drunk, skooma, cold, wet;
  conditions, and as a source they are a bool.
- **Papyrus mod events** (`SendModEvent(name, string, number)`) -- how OStim,
  SexLab, Dirt and Blood and most script mods announce state. SKSE hands them
  to C++ as `ModCallbackEvent`; remembering the last number per event name
  makes a source, and the event itself is the seventh trigger kind.
- **StorageUtil / JContainers** -- where SLA, Frostfall and many others keep
  per-actor floats. Their Papyrus natives can be called from C++ through the
  VM (`StorageUtil.GetFloatValue(actor, key, default)`), the same dispatch the
  `NiOverride` probe used; the result is polled on the tick and cached.
- **Any Papyrus global function** -- a mod's own API
  (`OSLArousedNative.GetArousal(actor)`), the same way. A generic
  `Papyrus { script, function }` source covers every mod that has one.
- **SKSE messaging** (plugin to plugin) -- per mod, needs their header; case
  by case, not a source kind.

The driver's `source` should be the same component the conditions use, so the
editor and the JSON shape come for free; `FactionRank`, `ModEvent` and
`Papyrus` are the additions. Once a source can be a mod's value, the mods in
the replacement table -- OBlush, ODF Body Blushing, Wet Function Redux --
become content rather than code.

### Two-ended sliders in authoring

RaceMenu's slider files pair morphs: `Name = cat, Slider, A, B` runs -1 to 1
with A on the negative half and B on the positive. 187 of the 827 sliders in
one load order are that shape, and the Actor window now draws them as one
signed slider. Entries and keyframes still name a morph and a 0..1 weight; an
author should be able to write the slider's name with a signed weight
(`Brows_Angle: -0.5`) and have it resolve through the pair. Small, once the
slider-naming rules for authoring are settled.

## Sending events from a sequence

Under the actions system above, one more action kind: `SendEvent`. Three
channels reach three different listeners.

| Channel | Who hears it | How |
|---|---|---|
| SKSE ModEvent | Papyrus in any mod: `RegisterForModEvent("DCMF_Wince", "OnWince")` gets `(name, strArg, numArg, sender)` | `SKSE::GetModCallbackEventSource()->SendEvent(...)` -- one call |
| Animation graph event | the actor's behaviour graph: OAR's `onTrigger` functions, Papyrus `RegisterForAnimationEvent`, anything with a graph sink | `actor->NotifyAnimationGraph("DCMF_Wince")` -- and since our own animation trigger listens to the same graph, this makes OAR and us two-way |
| SKSE messaging | other SKSE DLLs listening for our plugin name | `GetMessagingInterface()->Dispatch(...)` -- for C++ consumers wanting a struct |

Worth doing before any action row exists: emit `DCMF_SequenceStart` /
`DCMF_SequenceEnd` mod events with the sequence name as `strArg` and the actor
as `sender` on every start and end. It costs nothing, needs no authoring, and
"play a grunt when the wince starts" becomes a script author's five lines
rather than our sound system.

One caution on the graph route: sending a tag into the graph is a broadcast
into the actor's behaviour, not a message -- a behaviour file that handles that
tag will react. Our tags want an `DCMF_` prefix and the docs should say so.

## Replacing Mfg Fix NG, in order

**Done.** `mfgfix.dll` is detected at load (`Mfg.cpp`): a warning in the log,
a red line in the panel's problems bar, and this plugin's MfgConsoleFunc
natives are then *not* registered, so Mfg Fix's own stand until it is
removed. Without it the seven natives its scripts declare are ours --
`SetPhonemeModifier`, `GetPhonemeModifier`, `ApplyExpressionPreset`,
`ResetMFGSmooth`, `SetPhonemeModifierSmooth`, `GetPlayerSpeechTarget`,
`IsInDialogue` -- writing the console tier the merge carries into the finals
(a value taken to nothing clears the final too), with the smooth forms
stepped in the keyframe hook by the scripts' own speed. The merge gained NG's
Look-group rule. The README says *incompatible -- remove mfgfix.dll, keep the
scripts*. What is deliberately not ported: NG's `SmoothUpdate` on the
expression channel (vanilla's `TransitionUpdate` eases it underneath), the
phoneme threshold (vanilla's own), and the `sub_3F1800` byte patch (we do not
move the eyes update). Compared against NG at `c94a556` (2026-05-11).

## Asking a head only about the morphs anyone uses

`AvailableMorphs` (`Entries.cpp:294`) caches, per actor, the set of every morph
the head has. A hit is a hash lookup -- the 0.03ms figure. A miss -- first
sight, the face node changing, a new TRI, or the ten-second insurance timer --
runs `MorphsOf`, which walks the whole registry per head topology and
allocates a string for every morph the head has, hundreds of them, to answer
`Resolve`'s `contains(name)` for the dozen names anyone actually wrote.

The change: keep `used` -- every slider name in every entry and sequence key
across the loaded groups, rebuilt on group reload and on an editor edit -- and
make the miss path `for name in used: registry.Has(name, topology)`. A dozen
lookups, no allocation. Correct because `Resolve` only asks about names in
matched entries, which are a subset of `used`; the editor's picker is a
separate catalogue and does not touch this.

One step further, since the data allows it: key the cache by **topology, not
actor**. Availability depends only on (vertex count, registry, `used`), so two
Nords with the same head share the answer exactly. `usedByTopology[vertexCount]`
is rebuilt when the registry or `used` changes; per actor, a miss is the
face-node walk for its vertex counts plus a map lookup.

Where it would show: not in the steady-state figure, which is already a hit,
but in the miss spikes -- a cell load bringing in twenty NPCs is twenty rebuilds
in one tick, and the ten-second timer's rebuilds vanish into noise. The panel
already counts hits and misses, so the spike is readable on a cell transition
before anything is built.

Two things to get right: `used` must bump a version the cache compares when the
editor adds or renames a slider, or the new slider is unavailable until the
timer fires -- the `registrySize` compare is the pattern; and anything that
bypasses `Resolve` (the preview aims sliders directly; Papyrus `Morph()`
writes by name) must either not consult availability or consult the full
registry.

## Conditions on what an actor is wearing

`HasKeyword` exists but is the *actor's* keywords -- `actor->HasKeyword`, so
base and race (`src/plugin/Entries.cpp:161`). What is worn is not reachable
today.

The case for it: a mouth gag should silence lip sync. Nothing else needs
building for that -- an entry already carries `flagsSet`, so an entry conditioned
on the gag's keyword and setting `Stop lip sync` is the whole feature. That makes
this the cheapest of everything in this file, and the only one needing no new
release path, no player and no sink.

Unresolved:

- **Slot or no slot.** `WornHasKeyword` across every biped slot is simpler;
  `WornHasKeyword` restricted to a slot is what actually distinguishes a gag from
  a pouch that happens to share a keyword. Gag mods conventionally use slots 44
  and 45.
- **Cost.** Conditions are evaluated per driven actor on a tick, and walking
  worn equipment is much heavier than a keyword lookup on a base form. It
  probably wants a per-actor cache invalidated on an equip event -- which is
  another sink, so the cheap feature is only cheap if the cache can wait.

  Worth knowing before assuming it cannot: a tick has since been measured, and
  every condition in the load order came to **0.01ms** against **0.5ms** for
  asking each head what morphs it has. Conditions were never the expensive part.
  That one is cached now (`AvailableMorphs`, keyed on the face node and the
  registry's size, rebuilt on a ten-second timer as insurance) and fell to
  0.03ms. The settings window shows all three figures, so the same question
  about worn equipment can be answered with a number rather than a guess.

## Pictures of hair, eyes and overlays in the editor

Fitting Room (`reference/fitting-room`, v1.1.8, cloned 2026-09-13) shows a
picture on every card -- a hairstyle, a pair of eyes, brows, an overlay, a
whole preset -- and does it three different ways, each chosen for what the
thing is. Worth copying the *split*, not the code:

- **Meshes: a standalone offscreen draw.** `src/MeshExtractor.*` loads the
  part's NIF on its own through `NiStream`, walks every visible geometry into
  vertex and index buffers in the preview's own layout (with three
  out-of-bounds fixes on third-party bytes, all documented inline), and
  `src/PreviewRenderer.*` draws that set with its own tiny shader into a
  square render target on the game's immediate context, then reads the
  pixels back. It runs in the Present hook -- the one tick with the context in
  hand -- and saves and restores the whole pipeline state around the draw
  (OM/UAVs, RS, IA, all five stages, blend, depth, slot-0 constants, sampler,
  SRV). `src/PreviewFraming.h` is the camera as pure arithmetic over the
  mesh's AABB (poses: diagonal for weapons, upright for worn gear, `kEyes` a
  straight-on single-eye chip), tested without DirectX. Head scenes
  (`HeadScene::kHair/kFacialHair/kEyes`) put the part on a mannequin head --
  the actor's own head mesh -- and anchor it by the NIF's own `NPC Head`
  node against the mannequin's, which is how KS Hairdos' two authoring
  skeletons stop hanging 3.3 units low.
- **Textures: a CPU decode on a worker thread.** `src/OverlayThumbs.*` turns
  an overlay's DDS into a thumbnail with DirectXTex on the CPU, off the render
  thread, touching no device. WIC was measured and refuses BC7, which was 595
  of 1851 overlay textures in their rig, so DirectXTex it is.
- **Whole faces: photograph the engine.** `src/Portrait.*` crops the framed
  head out of the back buffer at Present while the editor is open (world
  paused, actor still), because rendering a preset offline would mean
  reimplementing facegen. That is what a picture of "this expression" would
  be for us too.
- **Around all three:** `src/PreviewCache.*` is a request-on-draw queue
  drained from the Present thunk (never inside the UI pass -- that hid their
  editor on loading frames), keyed by scene identity, holding strings and
  FormIDs rather than `RE::` pointers so a save load cannot corrupt it;
  `src/PreviewDiskCache.*` + `src/PreviewPng.*` persist the result as PNG.

For us the near uses are: the eyes and hairstyle combos in the Look probe
(mesh route with `kEyes`/`kHair`), the overlay picker (texture route), and a
still of each state or keyframe in the editor (portrait route). The mesh
route is the expensive one to port -- its own shader, pipeline save/restore,
NIF walking; the texture and portrait routes are a few hundred lines each.

## Conditions that say what an author means: IsUBE, Is3BA

**Done, as `HeadLayout`, `BodyLayout` and `Body`.** None reads a mod name:
the layouts are the families table's (`DCMF.families.json`)
-- the head part and skin the actor wears, the plugins loaded, the user's
overrides -- and the body mod is the geometry name BodySlide writes into the
actor's own skin mesh (`3BA`, `CBBE`, `BaseShape` + `BaseShapeCanal` for
BHUNP, `HIMBO - Body HP`), matched against the table's `shapes` rows, all
cached per 3D in `Facts`. "Is this 3BA" is `Body == 3BA`; `BodyLayout ==
CBBE` is the question an overlay has. The editor offers each closed list by
its display names. Vanilla's own body geometry name is not in the table yet:
read it off Settings -> Layouts (hover the body mod's reason) and add a row.

**Also done, each one condition:** `RaceFamily` (human / elf / beast, by the
vanilla races' editor ids, a custom race asked through its armour parent);
`IsNaked`, `HasHeadCover`, `HasHelmet` (body and head slots, armour type);
`TimeOfDay` (dawn / day / dusk / night by the climate's own sunrise and
sunset), `IsIndoors` (interior, or a No Sky worldspace), `IsInWater`.

Still open, in the same spirit:

- **Shared presets.** A `presets/` folder beside the groups, loaded once,
  referenced by name from any group; a group's own presets shadow shared ones
  of the same name. Same rule against nesting, same editor.

- **Actor tags, for what no engine fact says.** A per-actor set of strings,
  settable from an INI (SPID-style distribution by race, keyword, faction,
  editor id) and from Papyrus. `HasTag(x)` is then the whole condition, and a
  pack author who wants "my custom race counts as UBE" adds a line to a text
  file -- or, now, a path, plugin or shapes row to the families table.

- **What DCMF itself is doing:** `IsDriven`, `IsPlaying(sequence)`,
  `HasLayer(name)`, `SinceFired(trigger) < n` -- the mod's own state as
  conditions, so a sequence can wait on another or a paint can follow a
  state without both re-checking the world.

## Gags, blindfolds and bound arms: Devious Devices as conditions and as a mask

Devious Devices marks every worn device with a keyword on the rendered
armour -- `zad_DeviousGag` (with `zad_DeviousGagRing` and `zad_DeviousGagPanel`
for the kind), `zad_DeviousBlindfold`, `zad_DeviousHood`, `zad_DeviousCollar`,
`zad_DeviousArmbinder`, `zad_DeviousYoke`, `zad_DeviousElbowTie`,
`zad_DeviousStraitJacket`, `zad_DeviousPetSuit` (all also `zad_DeviousHeavyBondage`),
`zad_DeviousBondageMittens`, `zad_DeviousCorset`, `zad_DeviousBelt`,
`zad_DeviousPlugAnal/Vaginal`, `zad_DeviousClamps`, the piercings, the cuffs
-- so `IsWornHasKeyword` already asks the question, and a shared preset pack
(`IsGagged`, `IsRingGagged`, `IsBlindfolded`, `IsHooded`, `IsArmBound`)
is the whole condition side. Nothing to build for that beyond shared presets.

What is worth building is what a gag *means* for the face, because it is a
different kind of thing from an emotion:

- **A gag is a constraint, not an expression** -- and among entries the
  resolver already says it: a gag entry with `Overwrite` at a high priority,
  setting `Aah` and every other mouth phoneme, discards everything below on
  those channels by the pruning rule, and its suppression flags take the
  engine's lip sync off the mouth (a blindfold: blink and eye tracking). So
  most of a gag is authoring, today. What `Overwrite` does not say is two
  things. **Sequences sit above the entries by design** -- "a sequence
  always wins on its own sliders" -- so a nod sequence that keys the mouth
  opens it through the gag; the gag needs a way to hold channels against
  what is above it too: a `locked` flag on an overwrite that sequences
  respect for those channels. And an overwrite claims only what it names,
  so pinning the mouth means listing every phoneme at zero; a shorthand for
  "all mouth channels" (`mask: mouth`) is the same thing with less to get
  wrong. Both small; the first is the real one, and it is also what "eyes
  shut while asleep" and "mouth full while eating" want.

- **DD writes the face too, and has to be told not to.** Its gag effect sets
  the mouth through MfgConsoleFunc (the input channels), which our merge
  carries into the finals and our own writes then override every frame --
  so with a DCMF gag entry the two fight, and without one DD's mouth shows
  under everything else we do. The clean state is DD's gag expression off in
  its MCM and the gag entry ours, the same arrangement as with Conditional
  Expressions; the note for authors is that, not code. DD's other face work
  (the blindfold's imagespace, the hood hiding the head) is not ours.

- **Bound arms are not the face**, but they are a state the face should
  answer to: strain, discomfort, a glance down at the restraint. Those are
  ordinary entries and sequences on `IsArmBound`; the only thing the
  framework gives them is the keyword.

- **A gag as a body of its own.** The ring gag pulls the lips into a shape
  the phonemes cannot make; a gag pack could ship its mouth as a morph on
  the head TRI (a `GagRing` shaping morph) and the entry drive that through
  the base route, the way UBE's eye rotation is driven. Content, once the
  mask exists.

So: shared presets for the conditions (free once that lands), a `locked`
overwrite that sequences respect plus a channel-group shorthand (small, and
general), and a compatibility note about DD's own expression. Devious Devices is not in this load order;
the keyword names are from zadlibs and should be checked against the
installed version when the preset pack is written.

## Faces for animation packs (OAR)

**Levels 1 and 2 done.** OAR's `SendAnimEvent` function still works with the
`AnimationEvent` trigger (level 1), and the animation itself is now a
trigger and a condition without any OAR configuration (level 2): the clip
generator hooks record the path the graph's binding points at, which is the
replacement's own path when OAR has stood one in, so a pack's animation is
named by its file and the editor lists what plays on the selected actor to
pick from. Not built: level 3, a `face.json` beside the `.hkx` discovered by
our scan and bound with no trigger written -- a directory walk over
`OpenAnimationReplacer\<mod>\<submod>\` plus a generated `AnimationStart`
trigger. And the other direction, our facts as OAR conditions through
`AddCustomCondition`, which no animation author has asked for.

## What the appearance authoring build left out (2026-09-13)

The model is in (`docs/superpowers/specs/2026-09-13-appearance-authoring-design.md`).
What was cut to land it, each with where it would attach:

- **SPID / KID distribution.** A SPID-distributed keyword is already a
  condition (`HasKeyword`), so the keyword route works today; a keyword that
  names a slot's overlay for an actor (`DCMF_Blush:soft`) is the shortcut not
  built.
- **Slot texture crossfade.** A slot's reference steps: the old layer goes
  and the new one is built. A crossfade is two layers with opposite alphas
  over `kSmoothDuration`, in `slotdrive::Bind`.
- **Body morph names.** The body morph picker is a text field; the
  BodyMorph probe's Discover is where a list would come from.
- **Random for colours and modes.** Only numbers and references draw.
- **Per-channel transitions on modifiers.** Dropped on purpose; a clip is
  the finer control. The keyframe rows keep theirs.
- **Head-part references by picture.** The hair and eyes pickers are name
  lists; the Fitting Room note above is the route to a picture.
- **A gaze mode of `fixed` at the state level.** `$gazemode` carries it and
  the trace shows it, but the morph state has only `camera` beside offset:
  the engine writes the gaze back unless eye tracking is stopped, which is
  what `fixed` has always meant in practice.

## Weight that eases: the engine's lerp as a body morph (2026-09-17)

`$weight` is a rebuild channel today: the base's weight is written, the 3D
is built again (`DoReset3D(true)`), and the head is fitted to the new neck
(`TESNPC::UpdateNeck` once the new face node is up -- see `look::
FitPendingNecks`). So it steps, never eases, and every change is a visible
hitch. What the engine does with the weight is a plain linear lerp of vertex
positions between a piece's `_0` and `_1` meshes at build time -- which is
exactly a morph delta, `(v1 - v0) * t`, and the direct-buffer body morph
route already writes deltas per frame. A weight that eases is that delta
written live, with the rebuild kept only for the head parts.

What a spike would have to answer, in order:

1. **The neck.** Heads have no `_0/_1`; the engine fits the head's neck ring
   to the body through `UpdateNeck`, which reads `base->weight`. Set the
   base's weight to the interpolated value and call `UpdateNeck` each step,
   no rebuild: does the seam stay closed? If yes, `$weight` leaves
   `RebuildsThe3D` and becomes an ordinary easing number. If not, the seam is
   the cost, and the morph route is only for bodies whose head is hidden.
2. **Every worn piece is a pair.** Body, hands, feet and each armour piece
   carry their own `_0/_1` (the ARMA's two biped models). A geometry's delta
   comes from *its* other-weight NIF, loaded through the model database the
   way `MeshThumbs` loads a part, matched shape by shape (vertex counts are
   equal by construction). An outfit change reloads the deltas; the body
   morph code already tracks the roots and the swap under them.
3. **Faithfulness.** A vertex write moves positions only; normals are not
   re-lerped, so lighting lags the shape slightly. Probably invisible at body
   scale -- to be looked at, not assumed.

The spike: a probe slider that, for the body skin alone, loads the
other-weight mesh, builds the delta and writes it live while calling
`UpdateNeck` -- (1) and the look of (3) in one go, before any armour work.

Related findings from the same day, for the record:

- **Effect shaders (EFSH)** play through `TESObjectREFR::ApplyEffectShader`
  and are stopped the way Papyrus stops them: `ProcessLists::
  ForEachShaderEffect`, matching `target` and `effectData`, `finished =
  true`. Whether one loops or plays once is the record's own timeline --
  `fillTextureEffectAlphaFadeOutTime`, `PersistentAlphaRatio`,
  `particleShaderParticleBirthRampDownTime`, `FullParticleBirthRatio` -- not
  anything about how it is played; a played-once one stays attached,
  invisible, until finished. A `$effectshader` channel would be a held
  reference for the looping kind and a clip key or trigger for the once kind;
  the picker could read those fields and say which is which. The engine keeps
  no editor id for an EFSH after load; the probe reads it back off the record
  through `TESFile::SeekForm` and the `EDID` subrecord, which is the route for
  naming any form the editor shows. Open: whether an EFSH is baked into the
  save and comes back on load without us (the letting-go rule).
- **Invisibility** is not an effect shader: the archetype calls
  `Actor::SetRefraction(on, power)` (the refraction flag on every shader
  property) and `Actor::SetAlpha`. Both are a probe now; as channels they
  would be two numbers held per frame like the skin gloss, the actor's own
  put back on release. Probed 2026-09-17: both work. `SetAlpha` multiplies
  into every material's alpha, so alpha-tested geometry (thin fabric) drops
  below its test threshold and vanishes between 1.0 and about 0.8 before the
  rest of the body has faded; that is the engine's. What is ours: a paint
  layer writes its own alpha into its material every frame, so under an
  engine fade the overlay stays brighter than the skin at low values. An
  `$alpha` channel must multiply each layer's alpha by the actor's.


## What a condition costs (2026-09-22) -- built

Every condition kind carries a scope (`ConditionSchema::scope`), shown as
a dot in the editor's type picker and on each row: **world** rows
(`TimeOfDay`, weather, game time, menus, quest stages) are asked once per
tick and shared by every actor; **actor** rows once per actor per tick,
shared by every modifier that repeats the same row; **inventory** rows
(`IsWorn*`, `IsNaked`, `HasHeadCover`, `InventoryCount*`) read one walk of
the actor's inventory per tick, the snapshot, instead of walking it each;
**unshared** rows (`Random`, `IsModifierActive`) are asked every time. The
memo (`core::ConditionMemo`, keyed by the row's identity and the actor) and
the snapshots live one tick, on the tick's thread; the editor's Test and a
trace get neither and ask afresh. A row's form is resolved once per session
rather than every tick. Not batched, since their walks are short: active
effects and factions. A PLAYER block's children are keyed by the player,
so they are shared across actors on their own.

## Config files to ship with the mod (2026-09-22)

What the mod should do out of the box that is authoring, not code: each a
DCMF mod file under `Data/SKSE/Plugins/DCMF/`, written against what is
already built, disabled by default where it changes bodies or skins so a
user opts in. The mod's author writes these; the list is what is wanted and what each
one leans on.

- **Skins by race (RSV).** In `Example.json` already: eight per-race skin
  sets (`cbbe` / `vanillamale` keyed) and eight modifiers on `IsRace` and
  `NOT HasOwnSkin`, disabled. To review once the skin route is final.
- **ORefit -- clothes on the body.** A modifier on `NOT IsNaked` (add
  `IsWornInSlot` for the two chest slots to match OBody's test), priority
  over the base body modifier, driving OBody's generated table as `body:`
  channels, `Add` blend: `BreastsTogether +0.35`, `BreastHeight +0.15`,
  `BreastGravity2 -0.1`, `BreastTopSlope -0.35`, `Breasts`, `Butt`,
  `AppleCheeks` `-0.05` each; the nipple set (`AreolaSize -0.3`,
  `NipBGone 1`, `NippleDistance +0.05`) as a second modifier so it can be
  left off. Eased dress/undress. The "to target" rows (cleavage to 1, the
  dimple/fold sliders to 0) cannot be exact as `body:` adds to the preset --
  a "replace the preset's value" row option is the code change if wanted.
  A `-Refit` preset alternative: a clothed modifier driving `$bodypreset`
  to `<preset>-Refit` over the base one, for the ORefit JSON Master List's
  presets.
- **Body presets by race and sex.** Modifiers on `IsRace` / `IsFemale`
  driving `$bodypreset` from a random pool of installed presets, with the
  actor's weight; a `-Zeroed Sliders-` style pool excluded by name. What
  OBody's distribution config does, as conditions.
- **Faces for animation packs.** Clips keyed to `IsPlayingAnimation` /
  `animationStart` on the loose `.hkx` paths the picker lists -- the
  hornblow demo is the pattern; one per pack the mod wants to cover, each
  in its own file so a user can drop the packs they lack.
- **Framework hooks (SexLab, OStim).** Their scenes as sources by mod
  event (`HookAnimationStart`, `ostim_start`, `ostim_scenechanged`...)
  driving expression modifiers; waits on the API/sources redesign, since a
  source is declared in the file and set by the API there.
- **Player look sets.** Hair, brows, beard and scar swaps are player only;
  a file of named looks (`$hair`, `$brows`, `$beard`, `$scars`, `$eyes`,
  tints, gloss) an author or a user's own script switches by mod event.
- **Conditional expressions.** What Conditional Expressions / CEE do as
  one file of modifiers: combat (`IsInCombat`, `IsWeaponDrawn`), hurt by
  health through a source on the actor value and a driver, sneaking,
  sprinting, swimming, staggered, naked, talking, idle, cold and wet
  (`SubmergeLevel`, weather flags), needs and arousal as sources (actor
  value, global, StorageUtil, faction rank), effects (`HasMagicEffect`).
  Stacked by priority with `Add`/`Merge`, eased in and out, per actor with
  `IsPlayer` where a state is the player's alone; a few interval clips with
  random variants for glances and winces.
- **Demo.** `demo.json` and `probe.json` as they are, kept in step with the
  format; `docs/demo.md` is the checklist of what each one shows.

## Overlays that wipe in or grow out (2026-09-27)

An overlay today fades: the slot's `alpha` scales the whole mark. Wanted: a
mark that appears from the bottom up, or grows out of its centre, without
being deformed, on the marks that exist -- tattoo packs ship crisp shapes with
no ramp in their alpha, so nothing can be asked of the texture.

**What was tried and undone.** A `wipe` channel driving the clone's alpha
test threshold. It is one byte a frame and needs no shader, but which texels
go first is whatever the author painted into the alpha: a feathered blush
shrinks inward (gone by 0.67, its max), a crisp mark thins a pixel at the rim
and then pops at 1.0. Measured on the demo files. It also shares its byte
with the fade, since the test sees texture alpha times material alpha; the
applier scaled the threshold by the fade to keep them apart. Dropped: it is
a shrink for airbrushed marks and nothing for the rest.

**What would do it: cut the texture on the GPU, no shader.** When a layer
builds, make a second `ID3D11Texture2D` of the same size and format, fully
transparent, and point the clone's material at it, so the engine's cached
original stays whole for everyone else. Each frame the amount changes, copy
the rows below the line from the original with `CopySubresourceRegion` and
overwrite the rows above with prebuilt transparent blocks. Nothing is
altered, only present or absent: exact look, full opacity, the fade stays
independent. Bottom-up is texture rows, which run up the body on a body or
face sheet; left, right and top-down are other rectangles; a grow from the
centre is one span per block row widening with the radius, a box or a disc,
reversed for a shrink. Compressed formats cut in 4-texel blocks, 256 steps
on a 1024 sheet; mips are cut with the line scaled. The copies run on the
render thread: the update pass records the line, `Apply::Upload` (already
run from Present for body morphs) does them.

**Unresolved.**
- A texture object the material accepts: a `NiSourceTexture` of our own
  around our `BSGraphics::Texture`, or swapping `rendererTexture` on a clone
  of the loaded one. `MeshThumbs` may already answer how the engine binds
  one.
- The grow's centre. The mark sits somewhere on the sheet, not in its
  middle. From the file: the alpha's bounding box, a small decoder for BC1
  and BC3, with BC7 falling back to the sheet centre. Or set by the author:
  an optional centre on the overlay's texture block, which also allows a
  point that is not the middle.
- Where the shape lives: bottom-up, top-down, left, right, grow, shrink. On
  the slot unless a modifier needs to change it; the amount is the channel,
  `wipe`, 0 hidden to 1 shown, fully shown when not driven.
- Transparent blocks per format: zero bytes for uncompressed and for BC2/BC3
  alpha blocks, the known all-transparent block for BC1, one to compute for
  BC7.

## A stub mfgfix.dll, so the real one never loads (2026-09-27)

The Papyrus natives Mfg Fix's scripts declare, `MfgConsoleFunc` and NG's
`MfgConsoleFuncExt`, are ours once this plugin registers them; two DLLs
registering the same names is the one thing that must not happen, and today
that is a check for `mfgfix.dll` at load, a warning, and ours standing down.

**The idea.** Ship a stub `mfgfix.dll` in our mod's `SKSE/Plugins`: a real
SKSE plugin, the version and load entry points exported, doing nothing but
logging that it stood in. Wherever our mod wins the file conflict, MO2's
order, Vortex's rule, a manual install, the real one never reaches the load
order, and mod pages that list Mfg Fix as a requirement keep telling users to
install it harmlessly.

**What stays.** The load check, since a user who orders Mfg Fix after us gets
the real DLL back; the check tells the two apart by the stub's own version
string and stands down as now.

**What it costs.** It has to be a real plugin, not an empty file: SKSE refuses
a DLL without the entry points and logs an error on every launch. So a second,
tiny target, `stubs/mfgfix`, a few lines, deployed beside `DCMF.dll`.

## Batching conditions into set and range checks (2026-09-28)

Every condition compiles (`Compile(Program::Builder&)`, pure virtual on
`ConditionBase`): readings for Gather, tests for Evaluate, the compile run once
and never per tick. What is not done: an OR of 1000 `IsEquipped` rows is still
1000 readings and 1000 tests, where it could be one reading and one set lookup.

**The shape, with no per-condition exceptions.** Each condition's `Compile`
states one *atom*: a reading, a relation and an operand. Readings are typed:
number (as double, so a form id is exact), id, id list, bool, text and text
list, text hashed lowercase to an id on both sides so it is an id from then on.
The grouping is generic rewrite rules in the builder, at compile, over atoms
that share a reading and a relation, whatever condition made them:

- OR of `id == c` becomes `id in {set}`; OR of `list contains c` becomes
  `list overlaps {set}`; OR of number ranges a union of intervals.
- AND of `list contains c` becomes `{set} subset of list`; AND of number
  comparisons one interval; AND of different `id == c` false.
- NOT pushed inward (De Morgan), so negated rows still group. XOR, and atoms
  that share nothing, stay as they are: a rewrite only where it applies.

A new condition choosing an existing reading type gets every rule free.

**Readings it would add.** Lists gathered once per actor per tick however many
rows read them, and only when some row does: worn items, the actor's keywords,
factions, perks, spells, effects, morph names, open menus (world). IsWorn today
walks the inventory once per row; as a list reading it is walked once.

**Keywords as text.** A keyword typed as text needs no lookup: gather the
editor ids of the worn items' keywords (a keyword keeps its `formEditorID`),
hashed, and compare list against list. A keyword KID adds at runtime is then
found on the next tick with no recompile. Picked keyword forms keep their form
id, in a second list gathered only when a row picks one, so picking a form
still means that form and not every keyword sharing its editor id.

**Limits.** Only exact matches group; a contains or pattern match would be its
own relation, one test per row, still pure. The "answer is the reading" rows
(the bool type) merge when identical and do not group.

## Folding constants into comparisons (2026-09-28)

A row compared against a static number is two nodes, the constant and the
comparison; the comparison could carry the number itself and halve the nodes.
Only for a number written in the file: a global, an actor value, a random or an
external source stays a node of its own. Beside it, a flat node layout (the
children in one array, a probe's world or actor flag on the node) for fewer
cache misses in Evaluate.

## Source definitions: one value, defined once, used anywhere (2026-09-28)

A mod defines a named source once and uses it in several places: a
condition, a channel's number, a trigger's interval. It is also the way
another mod drives a value, limited to the sources that belong to its own
mod. The random roll's scope belongs here too: a source can say whether it
rolls per actor, per mod or per session, which is what the Random
condition's State block does for itself. Until this exists, Random is a
value type only on the Interval trigger and on channels, and it rolls when
the clip or the modifier activates or deactivates.

## Conditions on channels (2026-09-30)

Conditions that read what a channel is on the actor, so a modifier or clip can
depend on another's result: `body:weight >= 60`, `head:mode is Fixed`, the face
overlay picked, the preset worn.

**Proposed conditions**

- **ChannelValue** -- a channel, a comparison and a number; numbers and stepped
  numbers.
- **ChannelIs** -- a channel and a name; modes, picks, text (overlay, skin set,
  body preset, head part).
- **IsChannelDriven** -- whether any modifier or clip sets the channel on the
  actor.
- Close relatives people will ask for next: **IsModifierActive** and
  **IsClipPlaying**, picked as the OnModifier triggers pick theirs.

**What exists to hang it on**

- `ActorState::sampled` holds every channel's resolved value per frame, the
  preset mix included; one probe per actor per channel, keyed by the channel,
  fits the compiled program as every other reading does.
- The channel picker (`UIChannelComboFilter`, category headings) can be drawn
  inside a custom condition component, so the OAR API headers stay as they are.
- A "Channels" category in the condition picker.

**Open questions**

1. **Source.** DCMF's resolved value (cheap, every channel, falls back to the
   rest or the actor's own colour or weight when nothing drives it), the
   actor's real state read from the game (includes other mods, needs a reader
   per kind, some kinds have nothing to read), or both as an option.
2. **Scope.** The three channel conditions only, or IsModifierActive and
   IsClipPlaying too.
3. **Feedback loops.** A modifier whose condition reads a channel it drives,
   directly or through another modifier, can switch itself on and off each
   tick. Reading the value from before this tick's resolve keeps it to a
   tick's delay rather than a same-tick loop; beyond that, warn in the UI when
   a modifier reads a channel it drives itself, or refuse (the condition never
   holds then).

## Genitals as a part (2026-10-01)

A channel for the genitals as head parts have theirs: which one, its size, its
arousal pose. Not designed yet; what follows is how the mods that add them work,
read from The New Gentleman (TNG) as installed.

**How TNG works**

- **Meshes:** one per type in `meshes\actors\character\character assets\TNG\`
  (`m_`, `m2_`, `r_`, `r2_`, `c_genitals_0/1.nif`), each with its `_0/_1`
  weights and its own `.tri` of BodySlide morphs; `MaleBody_0/1.nif` replaced
  with a body open where they attach.
- **Records:** 31 ArmorAddons, a type by a race family (ManMer, Saxhleel,
  Khajiit, Elder, Afflicted, SnowElf), and about a thousand generated skin
  armors (`TNG_Skin_B00...`).
- **Worn as the skin, not equipped:** its DLL makes a copy of a race's or an
  NPC's skin armor with the genital ArmorAddon in it, so they show whenever the
  body does. A change of type is a change of skin, and rebuilds the actor's 3D.
- **What covers them:** keywords on armors (`TNG_Revealing`, `TNG_Covering`,
  `TNG_Underwear`, `TNG_RevealingOnlyWomen/Men`), per-armor revealing records
  kept by the DLL, a `TNG_GenitalCover` armor.
- **Size:** five, XS to XL (keywords `TNG_XS`...`TNG_XL`), by scaling genital
  bones of the skeleton (XPMSSE's), with a multiplier per race.
- **Arousal:** SOS-style behavior animations in `meshes\auxbones\SOS\Animations`
  (`TngErect`, `TngBendUp`, `TngBendDown`), played through animation events.
- **API:** Papyrus natives from its DLL (`GetActorAddon`, `GetActorAddons`,
  `GetActorSize`, `SetActorSize`, `GetAllPossibleAddons`, `GetAddonStatus`...);
  its MCM's `TNGSetAddon` also sends `TNGSetMyAddon` from the actor.
- **Female addons:** supported (`TNG_AddonFemale`), for mods that register into
  it.

Schlongs of Skyrim, the older way, from memory: a real armor item equipped in
slot 52 by its quests, the body armor patched to reveal it, size by
NiOverride's bone scaling, the same `SOS` bend animations.

**Open questions**

1. **Own it or drive TNG.** Swapping the ArmorAddon in the skin, or equipping
   one in slot 52, rebuilds the 3D as weight does, and fights TNG, which owns
   that skin and writes it again on load and on a race change. Calling TNG's
   natives for the type and the size leaves covering and races to it.
2. **Size and pose without TNG's API.** They are bone scales and animation
   events, which DCMF's bone channels and animation machinery could drive with
   nothing worn touched.

## What a save and a reload lose (2026-10-02)

DCMF writes nothing to the save; after a load it starts again from the
conditions. Not needed for the first release.

- **A clip mid-run** stops at the load and plays again only on its next
  trigger.
- **Activation-scope draws** are seeded from the activation's time, so they
  draw again after a load. Channel, clip or modifier, mod and actor scopes hash
  to the same draw and hold.
- **Random conditions** are drawn again: every resource is invalidated on load.
- **Trigger cooldowns, last fired, IdleTime and the Actor window's holds**
  start over.
- **The player's head parts, untested.** HeadPart changes the player's base
  record, which the save likely keeps as RaceMenu's hair is kept. Saved while
  DCMF holds a part, the load leaves DCMF thinking that part is the player's
  own, and letting go restores it instead of the real one. Test: hold a hair
  in the Actor window, save, reload, release. Fix: the originals in an SKSE
  cosave, or put back before each save.

## Bones beyond the spine and head (2026-10-03)

A bone layer over the animation, every bone the skeleton has, written after
animation each frame as the pose already is. Not an animation system: Havok
keeps playback, the behaviour graph, root motion and everything the game reads
from it. In order of what it gives for its cost:

1. **Inertialized transitions.** On a graph switch (`hkbClipGenerator`
   activation, already hooked), the difference between the last pose shown
   and the new one, per bone in local space, added back and decayed to zero
   with its velocity: motion carries through instead of a crossfade through a
   halfway pose. Off for transitions meant to be sharp (hits, attacks), by
   conditions; must not fight IK, root motion or physics bones.
2. **Expressive tails and ears.** Driven by state and feeds: a tail that lashes
   when angry, droops when afraid; ears flat in combat. DCMF's own territory.
3. **Secondary motion for bones nothing drives.** A spring per chain on the
   parent's acceleration, for accessory bones (capes, skirts, earrings) that
   ship without physics. Never on SMP or CBPC bones: those take target nudges,
   not writes.
4. **Finger poses.** Hand shapes as modifiers (relaxed, fist, pinch), off while
   the hand grips something.
5. **Hand IK.** Hand on a wall, rail, door: ray casts from the shoulder, the
   animated hand projected onto the surface (moved as little as possible),
   comfort zone waist to shoulder and ahead of it, flatness and layer checks,
   palm to the normal; contact locked in the world with hysteresis, stepped
   like a foot when walking, weight eased in and out; two-bone IK with a pole
   and wrist limits. The prototype for the rest.

What it is not: a different arm path or new hand movement (that is a new
animation, an OAR variant), combat or AI timing, smoother graph blend times
(behaviour patches), root motion (Animation Motion Revolution does it).

**The foundation.** A skeleton-aware bone table: rotation, position and scale
per discovered node, per skeleton type (creatures differ), reconciled with the
pose channels that drive the spine and head now. Node scale is uniform in
this engine.

**Living beside skeleton editors** (Mu Skeleton Editor, RaceMenu transforms):
they set static proportions, DCMF poses on top. Two things DCMF needs for
that: the rest pose read live off the skeleton instead of from `skeleton.nif`
(Fixed mode reads the file today), and scale composed with what is on the
bone instead of set absolute; the take-back must not undo an edit made while
DCMF held the bone. Untested against Mu Skeleton Editor.

Possibly a plugin of its own on DCMF's conditions and a plugin channel API
rather than DCMF itself: it is animation, not appearance and expression.

## CBPC colliders from the body preset (2026-10-04)

CBPC's colliders sized from the actor's own body instead of a hand-tuned file
per preset (OBody HotSwap's way: a faction per preset, a config per faction).

**Measuring works, offline.** On the 3BA BodySlide source mesh with a preset's
sliders applied: per collider bone, the vertices it moves (a limb's twist and
helper bones counted with it, a soft-weighted bone like the butt by half its
largest weight), in the bone's bind space; a least-squares sphere for the
breasts, butt and belly, a capsule along the bone's z for the limbs. Against
JR's HotSwap files: Breast02/03, butt, arms and calf within 0.2 to 0.9 of his
radii, and his weight 0 | weight 100 shift reproduced.

**Generate as template + measured difference.** One hand-tuned file for a
reference preset kept as it is, each value moved by (measured for this preset
- measured for the template's): the author's choices (a hip sphere bigger than
the thigh, breast spheres inside the skin) stay, only the shape changes.
Dominion generated from JR's BONOBODY file: radii within 0.8 of JR's own
Dominion file, the belly aside (JR left it alone). Along-the-bone positions of
capsule ends should stay the template's; Breast01's selection takes the chest.

**Live, per actor, no files.** CBPC 1.6.x (`CBPCPluginScript.psc`):
`AttachColliderSphere` / `AttachColliderCapsule` (actor, node, position,
radius, scale weight, index; -1 replaces the config file's collider),
`DetachCollider`, `RefreshActorCollisionSettings`. Bounce only blends:
`ApplyBounceInterpolation` toward a named `CBPCBounceInterpolationConfig_*.txt`
by percent, then `RefreshActorBounceSettings`. Papyrus natives only (cbp.dll
exports nothing else), called through the VM.

Untested: whether an attached collider survives CBPC rebuilding the actor
(cell change, 3D reload, equipment); the cost of many actors at once. First
step when picked up: an Actor window control attaching a deliberately wrong
collider to the selected actor. The offline scripts (NIF/OSD reader, fit,
generator) were scratch, not kept in the repo.

# Event-driven conditions — design

Status: draft for review, 2026-10-09.

## Goal

Conditions answer as soon as what they read changes, instead of waiting for the
next 4 Hz tick. Re-evaluation happens only when a value an actor's conditions
read has changed, and never more often than one global minimum gap per actor.
The tick stays, but only to poll the values the engine gives no event for.

## Today

- Every condition reads through a resource (`BuiltinResources::Number`, `Form`,
  `FormSet`, `TextSet`, or a third-party one). `Program::Builder` turns each
  read into a probe, keyed by what it reads.
- `Scheduler::Advance` fires a tick at `Settings::fTickRate` (4 Hz). Each tick:
  capture the actors in range → `Actors::Gather` reads every probe for every
  actor → `Program::Evaluate` answers every rule → `Resolve::Tick` and
  `ClipPlayer::Tick` update the active sets with the clock.
- Resources are pulled. On a read, `Resources::Due` decides whether to call the
  gatherer again (`EveryTick`: once per tick; `Interval`: after
  `intervalSeconds`; `Invalidated`: after an `Invalidate`; `Stored`: never).
- Almost every built-in resource is `EveryTick`. The equip sink is the one place
  where events invalidate resources (the worn and slot sets).

## Design

### 1. A resource declares how it updates

`Refresh` gains the modes it needs, and the declaration names its events:

| Mode | Meaning | Who re-reads it |
|---|---|---|
| `Stored` | pushed by its owner (feeds) | nobody; `SetNumber`/`SetForm` is the change |
| `Once` | read when the actor is tracked or their 3D loads | the tracker |
| `Event` | read again when one of its named events fires for the actor | the event sinks |
| `Interval` | polled every `intervalSeconds` | the tick |
| `EveryTick` | polled on every tick | the tick |
| `Invalidated` | kept for the API: read again after its owner calls `Invalidate` | the owner |

`Event` resources list their events in the declaration, from a fixed set DCMF
owns one sink for each:

- `Equip` (TESEquipEvent), `Container` (TESContainerChangedEvent),
  `MagicEffect` (TESActiveEffectApplyRemoveEvent), `Combat` (TESCombatEvent),
  `Death` (TESDeathEvent, TESEnterBleedoutEvent), `Cell` (actor cell change,
  TESCellAttachDetachEvent, location change, TESLocationClearedEvent), `Menu`
  (MenuOpenCloseEvent), `QuestStage` (TESQuestStageEvent), `Package`
  (TESPackageEvent), `Scene` (TESSceneEvent and its action/phase events),
  `Topic` (TESTopicInfoEvent), `Furniture` (TESFurnitureEvent), `SpellCast`
  (TESSpellCastEvent), `Race` (TESSwitchRaceCompleteEvent), `Level` (level-up),
  `Animation` (BSAnimationGraphEvent, by event name), `Load3D` (the actor's 3D
  rebuilt).

An `Event` resource may also give an `intervalSeconds` as a safety net: polled
that slowly as well, for a state whose "stopped" side has no event (spell cast
end, sneak, run).

`Event` is for DCMF's own resources. A third-party resource runs its own update
cycle: `Stored` with `SetNumber`/`SetForm`, or `Invalidated` with its own
`Invalidate` calls, or polled. Either call is a change like any event's
(section 2), so it is answered just as fast. The API gains no new mode.

### 2. Change detection

A resource re-read by any route compares the new value with the one it held. Only
a different value is a change. A change marks every tracked actor whose program
reads that resource dirty — the actor itself for an `Actor`-scoped resource,
every tracked actor for a `World`-scoped one.

The compiled program already knows which probes each rule reads, so "does this
actor read it" is the probe being in the program at all; no per-actor
dependency list is needed.

### 3. The tick, polling only

The tick keeps its clock (`fTickRate`) but changes its job:

1. Capture the actors in range, as now. A newly tracked actor is dirty.
2. For each tracked actor, re-read only the polled resources (`EveryTick`, and
   `Interval` ones that are due), and `Event` resources' safety-net intervals.
3. Each change marks the actor dirty (section 2).

A tick where every polled value comes back the same evaluates nothing.

### 4. Evaluation on change

Every frame, after the tick's polling and the events since the last frame:

- A dirty actor whose last evaluation is at least `fMinEvaluationGap` ago is
  evaluated now: gather its probes (cached values are returned for anything not
  stale), `Program::Evaluate`, then `Resolve::Tick` and `ClipPlayer::Tick` with
  the new held sets.
- A dirty actor inside the gap waits; all its changes are taken in one
  evaluation when the gap has passed.

Events arrive on several threads (animation-graph events come from Havok
workers). Sinks only mark resources stale and actors dirty, under a lock; the
reading and evaluating stay on the main thread, in `Scheduler::Advance`.

### 5. Time-driven work keeps running

`Resolve::Tick` and `ClipPlayer::Tick` also act on the clock alone: an exiting
modifier is dropped after its end transition, a clip variant ends after its
duration. Those keep running on every tick with the last held sets, without a
gather or an evaluation. This is cheap and keeps today's timing behaviour.

### 6. The setting

`fMinEvaluationGap`, in milliseconds, global, in the Settings window next to the
tick rate. 0 evaluates on the next frame after any change. Default 100 ms.

### 7. Built-in resources, re-declared

Each built-in resource gets its mode and events. From the survey of the 131
conditions:

- ~49 on a reliable engine event: worn and equipped (Equip), inventory
  (Container), magic effects, combat, death and bleedout, cell and location,
  menus, quest stages, packages, scenes, dialogue, furniture, spell casts, race,
  level, animation playing.
- ~14 on animation-graph events, with a safety-net interval: attacking, attack
  state and type, blocking, weapon drawn, in air, sprinting, staggered,
  swimming, riding and mount, sneaking.
- ~23 `Once` or `Load3D`: sex, base, class, voice, unique, child, plugin, form,
  name, summoned, body and head types and layouts, bones, morphs, height, scale,
  weight.
- ~34 polled: movement and rotation, target geometry, idle and fall, light,
  water, surface, stairs, wind, weather, game time, magic effect elapsed time,
  crime, perks, spells, factions, greeting, favours.

The re-declaration is done resource by resource; one left `EveryTick` behaves as
today, so the change can land in steps.

## What stays the same

- The condition files, components and the compiled program.
- The editor's per-row answers and the Modifier & Clip Log, which read the
  latest evaluation.
- Per-frame sampling and folding of channel values.

## Risks

- **Animation-graph event names** come from the behaviour files; a mod that
  renames them silences those conditions. The safety-net interval bounds the
  damage.
- **Event floods** (hundreds of equip events at load, combat flapping) are
  absorbed by the gap, per actor.
- **A missed event** leaves a condition stale until the safety net, or forever
  for an `Event` resource without one. Each `Event` declaration is checked
  against the engine's behaviour when it is converted.
- **World-scoped changes** (weather, game time) dirty every tracked actor at
  once; they are polled, so at most once a tick.
- **Mixed trees**: a rule combining an evented and a polled condition reacts
  at once to the evented one and at the poll rate to the other.

## Testing

In game, through the log and the Timing panel:

- Equip an item on the player: the IsWorn modifier starts in the next frame,
  not up to 250 ms later.
- With nothing changing, the Timing panel shows ticks firing and no
  evaluations.
- Rapid equip toggling: evaluations stay at most one per gap.
- A polled condition (movement speed) still reacts at the tick rate.
- Exit transitions and clip variants keep their timing with no condition
  changing.

## Decisions

- Third-party resources handle their own update cycle; DCMF's event sinks are
  internal, and `Event` is not offered through the API.
- `EveryTick` stays as a mode.

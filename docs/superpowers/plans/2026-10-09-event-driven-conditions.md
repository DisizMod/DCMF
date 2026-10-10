# Event-driven conditions — implementation plan

Spec: `docs/superpowers/specs/2026-10-09-event-driven-conditions-design.md`.

## Tasks

1. **Resources: declared updates and touches** (`src/Resources.h/.cpp`)
   - `Info` gains internal `events` (bit mask) and `safetySeconds`. An
     `Invalidated` resource with a safety interval is also due once it has
     aged past it.
   - `InvalidateEvent(event, subject)`: every resource naming the event is
     marked stale for the subject, or for everyone.
   - Touches: `Set` with a different value, `Invalidate` and
     `InvalidateEvent` record the subject (or the world) as touched.
     `TakeTouched` hands them to the scheduler.
2. **Event sinks** (`src/BuiltinResources.h/.cpp`)
   - `BuiltinResources::Event` bits and an `Updates` declaration
     (`events`, `safetySeconds`, `bOnce`) taken by `Number`, `Form`,
     `FormSet` and `TextSet`.
   - One sink object for equip, container, active effect, magic effect apply,
     combat, death, bleedout, furniture, race switch, cell attach and move,
     actor location change, quest stage, and menus, each forwarding to
     `InvalidateEvent`.
3. **Scheduler: evaluate on change** (`src/Scheduler.cpp`)
   - Per-actor kept state: last facts, last rule answers, last evaluation
     time, pending.
   - On a tick: gather every actor; evaluate those whose facts changed, the
     inspected and the traced; resolve every actor from its kept answers.
   - Between ticks: gather the touched actors (all of them for a world touch)
     and the pending ones; evaluate those whose facts changed; resolve those.
   - Every evaluation waits for `fMinEvaluationGap` since the actor's last.
4. **Setting** (`src/Settings.h/.cpp`, the settings UI): `fMinEvaluationGap`,
   ms, default 100, next to the tick rate.
5. **Built-in resources re-declared**, first batch: worn and equipped,
   inventory, magic effects, combat state, life state, furniture and
   sit/sleep, race and base, locations and cells, menus, quest stages, and the
   actor's fixed identity (class, combat style, voice type). Everything else
   stays `EveryTick`/`Interval`, behaving as today.

## Checks

Build clean (`error C|error LNK|Build FAILED`), DLL deployed to SE and AE. In
game: an IsWorn modifier reacts in the frame of the equip; with nothing
changing, the Timing panel shows ticks and no evaluations; exit transitions
and clip variants keep their timing.

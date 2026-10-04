# Before sharing

The list to work through before the mod and its demo go to other people.
`demo.json` is authored by hand in the editor; the facts that are easy to get
wrong are kept beside each item.

## Code

- [ ] (later) Warpaint masks as layers: convert a BC4 tint mask to an RGBA
      layer on first use, multiply blend, tinted through the slot's `tint`, so
      the makeup packs' blushes become animated overlays. Not needed for the
      demo now that OBlush's overlay textures are in.
- [ ] Side by side on the same actor: OBlush's own blush through RaceMenu vs
      ours, under the CS/ENB setup, close up.
- [ ] AE run by a tester: the panel (Shift+L), a slider on a face, headtracking held.
- [ ] A mod event source, a StorageUtil source and a Papyrus source each read
      in game once (only actor values have been tried).
- [ ] Vanilla body geometry name read off a vanilla-bodied actor (Settings ->
      Layouts) and added as a `shapes` row in the families table.
- [ ] Release archive: DLL, INI, families table, `DCMF/`
      mod files, `Textures/DCMF/`; page says SKSE, Address
      Library, VC++ redistributable, remove `mfgfix.dll` (keep its scripts).
- [ ] Before Nexus: strip the third-party textures out of
      `Textures/DCMF/demo/` -- OBlush's blushes and the
      LewdMarks marks are in only for the private test build; the public demo
      points at the mods' own paths (`actors\character\overlays\lewdmarks\<n>.dds`
      with LewdMarks installed) or asks their authors first

## demo.json

Static faces, one modifier each:
- [ ] shy when naked -- `IsNaked`
- [ ] disgust in water -- `IsInWater`
- [ ] squint while sneaking -- `IsSneaking`, priority 10
- [ ] vampire fangs while sneaking -- `IsSneaking` + `HasKeyword` editor id
      `Vampire`, priority 20; the morph is `vampiremorph` on the **mouth**
      mesh's chargen TRI (`!UBE\Mouth\MouthHumanF_tangentChargen.tri`; vanilla
      has the same name) -- confirm it is listed for the actor, add the TRI to
      the INI if not
- [ ] pupils indoors/outdoors -- `IsIndoors`; `eyes_eyesirisbigger` /
      `eyes_eyessmall` (also `eyes_catpupils`, `eyes_goatpupils`) from UBE's
      `EYES_FemaleHead.tri` -- confirm listed and not `(empty)`

Clips:
- [ ] fangs arriving through an enter clip (`transition.enter = clip`)
- [ ] the 7-second reaction clip, two variants, `random` + `noRepeat`
- [ ] stretch sync -- trigger `When an animation starts`, the animation
      picked from the list while the stretch plays (vanilla
      `CombatIdleStretching`, `Skyrim.esm|0F50F1`, `CombatIdleStretchingA/B.hkx`,
      ~3 s; weapon drawn, then `player.sae IdleCombatStretchingStart`);
      `Stop when conditions fail` on, with `IsPlayingAnimation` on the same
      path, so the face ends with the animation. An OAR replacement lists
      its own path beside the file it replaces

Driven and aimed:
- [ ] breasts by magicka -- `body:Breasts` computed from `Magicka`
      (percentage, inverted anchors)
- [ ] follower looking at the camera -- `$gazemode` / `$headmode` `camera`,
      `$stopeyetracking`, `$headcontrol`

Overlays and cross-mod:
- [ ] face blush slot `demo_blush` (`face`, `normal` blend) with one overlay
      `blush`: `DCMF\demo\blush_vanilla.dds` keyed
      `vanilla`, `DCMF\demo\blush_ube.dds` keyed `ube` --
      OBlush's `dd2blush.dds` (BC7 RGBA, 512) and its UBE edition (1024),
      copied to `dist/textures/DCMF/demo/`. OBlush applies
      it as a RaceMenu face overlay with tint `0xC80000` (200, 0, 0), so
      `slot:demo_blush:tint` (0.78, 0, 0) and alpha up to its MCM default
      reproduce its look; glow and specular off. Redistribution: OBlush's
      and the UBE conversion's permissions to check before the archive ships
- [ ] blush with the shy face
- [x] arousal source `demo_arousal` -- OSL Aroused `papyrus`, script
      `OSLArousedNative`, function `GetArousalNoSideEffects` (0..100; the pure
      read -- `GetArousal` advances arousal by time, its psc says so), actor
      `self`. Tested, reads. Not `factionrank` on `sla_Arousal`
      (`SexLabAroused.esm|3FC36`): OSL ships the faction but never writes it
      (its `slaFrameworkScr.psc` sets only `sla_Naked` and
      `sla_GenderPreference`); only SexLab Aroused Redux itself keeps arousal
      there, and it cannot be installed beside OSL
- [ ] arousal blush -- `slot:demo_blush:alpha` driven by `demo_arousal`
      20..90 -> 0..0.85, face along
- [ ] arousal mark -- body slot `demo_mark` (`body`, `normal` blend), alpha
      driven by `demo_arousal` 40..100 -> 0..1, four LewdMarks overlays keyed
      by body layout: `DCMF\demo\vanilla\mark_<n>.dds`
      (`vanilla`) and `...\ube\mark_<n>.dds` (`ube`), n = 029, 048, 080, 091 --
      the same marks in both sets. Extracted with `tools/bsa.py` from
      `LewdMarks/LewdMarks.bsa` (vanilla UV, 4096 DXT5) and `LewdMarks RaceMenu
      UBE Textures/LewdMarks.bsa` (UBE UV, 2048 BC7), reduced to their own 1024
      level with `tools/dds_mips.py` (1.4 MB each); the archives hold 001..096
      and the `-glow` set, numbers only
- [ ] hair by mod event -- source `modevent` `Demo_Hair` string, `$hair`
      from it; a two-line script or the probe's `DCMF_Probe_Hair` sends it
- [ ] scene running -- source `modevent` on the framework's start event,
      field `fired`

## Install for the demo

- [ ] OSL Aroused (or SLA)
- [ ] OStim Standalone, if the scene item is shown
- [ ] BHUNP enabled on one NPC only if a body-layout shot is wanted

## Verify before recording

- [ ] problems bar reads *No problems detected*
- [ ] every UBE morph above checked on the demo actor in the Actor window
- [ ] every source shows a value (Settings -> *Mod events heard* for events)

## Record

1. Faces react -- no UI: undress, swim, sneak as a vampire, walk indoors,
   cast, walk around the follower, `sae` the stretch.
2. Author one in a minute -- editor and Timeline on the long clip: drag,
   scrub, Play; a frame's window, a curve changed, Play again.
3. It plays with others -- the blush with the shy face on a UBE and a
   vanilla-head actor (the two textures picked by head layout); the hair
   event from the console with *Mod events heard* on screen; arousal pushed
   up in the MCM, blush and face following.

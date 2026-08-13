# MorphSyncTogether v0.2.7 — Morph Authority + RaceMenu Preset Guard

This version replaces the unsuccessful pointer-only/FaceGen-refresh recovery as
the primary makeup path. RaceMenu's public `IPresetInterface` is used instead.

## Why

The v0.2.5 log proved that `QueueNiNodeUpdate` and `RegenerateHead` were both
dispatched successfully and that the cached materials were restored, while the
remote makeup still stayed invisible. RaceMenu's public preset API explicitly
supports saving/loading an actor with a separate generated tint DDS, which is
the missing render artifact we were not preserving.


## v0.2.7 morph authority guard

The remote player snapshot is now treated as authoritative every tick. MorphSyncTogether captures the current proxy morph map and compares it with the received snapshot. If OBody or another local system changes the proxy, the mismatch is logged as `MST MORPH DRIFT` and the authoritative values are restored immediately instead of waiting for the periodic 5-second reapply.

After `SetMorph`, the plugin calls `ApplyBodyMorphs(..., false)` on the game thread and queues `UpdateModelWeight(..., false)` as a second RaceMenu-managed pass. It then re-reads the proxy morph map and logs `verified=1/0`.

`EvaluateBodyMorphs()` is no longer called. In RaceMenu that API evaluates BodyGen templates; it is not a visual update/verification function.

The v0.2.6 face-only RaceMenu preset+tint DDS guard is retained unchanged.

## v0.2.7 behavior

- BodyMorph networking is unchanged.
- Native PlayerCharacter TintMask APIs remain disabled.
- When a remote STR proxy is first fully rendered and healthy, MorphSyncTogether
  saves a local RaceMenu baseline:
  - `SKSE\Plugins\MorphSyncTogether\AppearanceCache\<name>.jslot`
  - `Textures\MorphSyncTogether\AppearanceCache\<name>.dds`
- Only dynamic `FFxxxxxx` proxy bases are eligible, because RaceMenu notes that
  preset loading may write details to the TESNPC and therefore requires a unique
  actor.
- On FaceGen tint drift, the plugin reloads the cached preset with
  `kPresetApplyFace` only. Body morphs, transforms and skin overrides are not
  loaded from the preset.
- The old QueueNiNodeUpdate / RegenerateHead mechanism remains only as a fallback
  when the Preset interface is unavailable or a preset load fails.

## Expected log

Healthy baseline:

```text
MST APPEARANCE interfaces READY ... preset=1 presetVersion=1 ...
MST RACEMENU PRESET SAVE label="Elir" ... saved=1 ...
```

When OStim changes the face tint:

```text
MST FACEGEN TINT DRIFT ...
MST RACEMENU PRESET LOAD label="Elir" ... loaded=1 apply=face-only ...
```

If `preset=0`, the installed RaceMenu build did not expose the expected public
Preset interface name and the log should be returned before further changes.

## Build

Use the existing PowerShell workflow. The produced Vortex package is:

```text
MorphSyncTogether-v0.2.7-Vortex.zip
```

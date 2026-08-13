# MorphSyncTogether v0.2.10 - Pubic Overlay Authority

This test version keeps the validated BodyMorph and FaceGen makeup guards, then
adds owner-authoritative synchronization for OPubes and
OPubesRaceMenuSelector.

## Why this version

The v0.2.9 test proved that the FaceGen path is now complete:

- Elir's baseline was created even before RaceMenu overlays appeared.
- Two OStim tint replacements were detected and restored.
- The makeup remained visible after the scene.

The remaining mismatch came from OPubes. Both OPubes NG and the installed
RaceMenu selector write a texture, color and alpha into a `Body [Ovl#]` node.
Those values are selected independently on each machine and are not BodyMorph
data, so the existing `MORPH` packets could not synchronize them.

## v0.2.10 behavior

- BodyMorph networking and immediate drift correction are unchanged.
- Baseline eligibility now depends on the data actually protected: at least one
  live `kFaceGen` material with a non-null tint texture.
- RaceMenu/SKEE overlays are no longer required to cache the FaceGen baseline.
- The first eligible FaceGen material state remains authoritative for each
  remote network identity.
- When OStim replaces the FaceGen tint texture, the plugin restores the cached
  tint pointer, clears the shader's render passes, runs geometry setup/finish,
  and marks the geometry material dirty for Skyrim's next render traversal.
- Three follow-up passes repeat the material restoration and rebind to
  cover late OStim render work.
- The previous preset save/load, `QueueNiNodeUpdate`, and `RegenerateHead`
  recovery paths are no longer used.
- Native `PlayerCharacter` TintMask APIs remain disabled.
- The local OPubes style is captured from its RaceMenu `Body [Ovl#]` override:
  texture, packed tint color and alpha are sent in a separate `PUBES` packet.
- The receiving client replaces an existing local pubic overlay in place. If
  none exists, it uses the owner's slot only when that slot is empty.
- Only recognized pubic texture folders are managed. Tattoos, body paint and
  temporary OCum overlays are not synchronized or overwritten.
- A shaved/absent owner state removes a locally randomized pubic overlay from
  the remote proxy.

## Expected log

At startup:

```text
MorphSyncTogether v0.2.10 loading
MST APPEARANCE interfaces READY ... mode=probe+face-material-rebind+pubic-overlay
MST APPEARANCE BASELINE ... faceGenTintMaterials=1 skeeOverlays=0 overlayNodes=0 eligibility=facegen-tint-ready
```

When OStim changes the makeup tint:

```text
MST FACEGEN TINT DRIFT ... action=restore+rebind
MST FACEGEN MATERIAL REBIND ... restored=1 rebound=1 setup=1 finish=1 followups=3
MST FACEGEN MATERIAL REBIND FOLLOWUP ... attemptsLeft=2
```

The important visual check is whether the remote makeup is visible again after
the OStim scene. The new rebind lines make it possible to distinguish a missing
drift event from a shader rebind that ran but did not affect the final pixels.

For OPubes synchronization:

```text
MST PUBES TX ... present=1 ... texture="Actors\\Character\\AK_RM_Pubic_Hair_all_in_one\\...dds"
MST PUBES RX ...
MST PUBES DRIFT ... action=restore
MST PUBES APPLY ... applied=1 verified=1
```

## Build

Run `build-vortex.ps1`. The produced package is:

```text
MorphSyncTogether-v0.2.10-Vortex.zip
```

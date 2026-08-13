# MorphSyncTogether v0.2.11 - FOMOD installer

MorphSyncTogether is an SKSE plugin that keeps remote Skyrim Together player
appearance data authoritative across clients. It synchronizes RaceMenu
BodyMorphs, preserves FaceGen makeup after local mod changes, and can optionally
synchronize OPubes overlays.

## Requirements

- Skyrim Together Reborn
- SKSE64
- RaceMenu
- Address Library for SKSE Plugins
- The same MorphSyncTogether version and FOMOD choices on every client

OBody, OStim and OPubes are supported integrations, not hard requirements.

## Installation

Install `MorphSyncTogether-v0.2.11-FOMOD.zip` with Vortex or another
FOMOD-compatible mod manager. The installer asks one required question:

- **No - I do not use OPubes** installs BodyMorph and FaceGen makeup sync with
  `[PubicOverlaySync] Enabled=0`.
- **Yes - I use OPubes** installs the same core plugin and enables OPubes or
  OPubesRaceMenuSelector texture, tint, alpha, and shaved-state sync.

The installer recommends the OPubes option when it detects `OPubes.esp`,
`AK_RM_PubicStyles_All_In_One.esp`, or
`AK_RM_PubicStyles_All_In_One_M.esp`. Install the same option on every Skyrim
Together client.

## Features

- Remote BodyMorph snapshots override locally randomized OBody presets.
- Morph drift is detected and corrected immediately, with periodic reassertion
  for late-created Skyrim Together proxies.
- The first healthy FaceGen tint material remains authoritative for each remote
  network identity.
- If OStim or another local system replaces a remote FaceGen tint, the cached
  texture is restored and rebound through the shader setup path.
- Follow-up material passes cover late render work without rebuilding the
  actor's full head.
- Optional OPubes synchronization sends the owner's texture, packed tint,
  alpha, overlay slot, and shaved/absent state.
- Existing pubic overlays are replaced in place. Tattoos, body paint, and
  temporary OCum overlays are deliberately ignored.
- Automatic LAN discovery uses UDP port `27992`; a manual peer can be configured
  in `Data/SKSE/Plugins/MorphSyncTogether.ini`.

## Expected log

At startup:

```text
MorphSyncTogether v0.2.11 loading
MST APPEARANCE interfaces READY ... mode=probe+face-material-rebind+pubic-overlay
MST APPEARANCE BASELINE ... eligibility=facegen-tint-ready
```

When FaceGen makeup is restored:

```text
MST FACEGEN TINT DRIFT ... action=restore+rebind
MST FACEGEN MATERIAL REBIND ... restored=1 rebound=1 setup=1 finish=1
```

With the OPubes FOMOD option enabled:

```text
MST PUBES TX ...
MST PUBES RX ...
MST PUBES DRIFT ... action=restore
MST PUBES APPLY ... applied=1 verified=1
```

## Build

Run `build-vortex.ps1`. It compiles the Release DLL, validates both INI
profiles and the FOMOD XML, stages the installer, creates the ZIP, and verifies
all required archive entries.

```text
MorphSyncTogether-v0.2.11-FOMOD.zip
```

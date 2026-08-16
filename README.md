# MorphSyncTogether v0.2.12 - BodyHairSliders overlay sync

MorphSyncTogether is an SKSE plugin that keeps remote Skyrim Together player
appearance data authoritative across clients. It synchronizes RaceMenu
BodyMorphs, preserves FaceGen makeup after local mod changes, and can optionally
synchronize the Body overlays used by BodyHairSliders and OPubes.

## Requirements

- Skyrim Together Reborn
- SKSE64
- RaceMenu
- Address Library for SKSE Plugins
- The same MorphSyncTogether version and FOMOD provider selections on every client

OBody, OStim, BodyHairSliders and OPubes are supported integrations, not hard
requirements for the core BodyMorph/FaceGen functionality.

## Installation

Install `MorphSyncTogether-v0.2.12-FOMOD.zip` with Vortex or another
FOMOD-compatible mod manager.

The Body Hair page now allows any combination of four independent provider
packs:

- **Pubes Forever - Female**
- **Pubes Forever - Male**
- **Nordic Warmaiden Body Hair**
- **Bodyhair for HIMBO**

Each checked option installs a small marker under:

```text
Data/SKSE/Plugins/MorphSyncTogether/Providers/
```

MorphSyncTogether only captures and applies textures belonging to providers
whose marker is present. This means an unchecked pack remains outside network
authority even if its assets are installed locally.

Use the same provider selections on every Skyrim Together client.

## v0.2.12 BodyHairSliders integration

The v0.2.11 network layer transported one pubic overlay state. v0.2.12 keeps the
same `PUBES` packet envelope for compatibility but the texture payload can now
contain a deterministic `BHS1` aggregate representing several independent
RaceMenu Body overlay regions at once.

Managed regions follow the current BodyHairSliders provider model:

- pubic
- armpits
- chest
- stomach / belly
- back
- arms
- legs
- butt

Recognized providers/paths include:

- Nordic Warmaiden Body Hair
  - `dePog - Pubes - ...`
  - `dePog - Pits - ...`
  - `dePog - Navel - ...`
  - `dePog - Crack - ...`
  - `dePog - Beast - ...`
- Pubic Hairstyles All In One / Pubes Forever female and male assets under
  `ak_rm_pubic_hair_all_in_one`
- HIMBO V3 Bodyhair Body Paints (`HIMBO_BodyHair_*`)
- OPubes-compatible pubic overlay paths, tied to the selected female/male pubic
  provider for the actor's sex

The synchronizer captures each managed region independently, including its
texture, packed tint color and alpha. On a remote STR proxy it replaces the
locally-randomized overlay for the same semantic region. If the owner no longer
has a selected region, the corresponding remote managed overlay is cleared.

RaceMenu slot numbers are deliberately not network-authoritative. If the same
semantic region occupies different `Body [Ovl#]` slots on two clients, that is
not considered drift. When a new local slot is required, MorphSyncTogether
reserves the highest free Body slot, matching BodyHairSliders' allocation
strategy and avoiding occupied unrelated Body Paint slots.

Only positively recognized, FOMOD-enabled body-hair texture families are
managed. Tattoos, generic body paints and temporary OCum overlays remain outside
this authority.

The old v0.2.11 single-pubic state is still accepted as a compatibility input.

## Other features

- Remote BodyMorph snapshots override locally randomized OBody presets.
- Morph drift is detected and corrected immediately, with periodic reassertion
  for late-created Skyrim Together proxies.
- The first healthy FaceGen tint material remains authoritative for each remote
  network identity.
- If OStim or another local system replaces a remote FaceGen tint, the cached
  texture is restored and rebound through the shader setup path.
- Follow-up material passes cover late render work without rebuilding the
  actor's full head.
- Automatic LAN discovery uses UDP port `27992`; a manual peer can be configured
  in `Data/SKSE/Plugins/MorphSyncTogether.ini`.

## Expected log

At startup:

```text
MorphSyncTogether v0.2.12 loading
MST APPEARANCE interfaces READY ...
```

When FaceGen makeup is restored:

```text
MST FACEGEN TINT DRIFT ... action=restore+rebind
MST FACEGEN MATERIAL REBIND ... restored=1 rebound=1 setup=1 finish=1
```

With one or more body-hair packs selected, the existing network envelope still
logs `PUBES`, while per-region application logs use Body Hair diagnostics:

```text
MST PUBES TX ...
MST BODYHAIR RX ... aggregate=1
MST BODYHAIR APPLY ... region=armpits ...
MST BODYHAIR APPLY ... region=pubic ...
MST BODYHAIR CLEAR ... region=chest ...
MST PUBES APPLY ... applied=1 verified=1
```

## Build

Run `build-vortex.ps1`. It compiles the Release DLL, validates the core INI,
all four provider marker packages and the FOMOD XML, stages the installer,
creates the ZIP under `dist/`, and verifies all required archive entries.

```text
dist/MorphSyncTogether-v0.2.12-FOMOD.zip
```

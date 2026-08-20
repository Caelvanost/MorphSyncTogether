# MorphSyncTogether v0.3.4 - STRPM transport

MorphSyncTogether keeps remote Skyrim Together player appearance authoritative across clients.

## v0.3.4

This branch uses **STRPluginMessagingAPI (STRPM)** instead of MorphSyncTogether's former private UDP/autodiscovery transport.

v0.3.4 fixes the resend optimization path. Repeated STRPM morph snapshots are now routed through the optimized apply handler instead of the legacy periodic apply path. If the remote proxy already matches the authoritative morph hash, MorphSyncTogether skips `ClearMorphs`, `SetMorph`, `ApplyBodyMorphs` and `UpdateModelWeight`. A real morph drift still triggers the full authoritative restore immediately.

Preserved functionality:

- RaceMenu BodyMorph synchronization
- OBody drift correction/reapplication
- FaceGen makeup preservation/rebind
- BodyHairSliders / OPubes semantic overlay synchronization
- independent FOMOD provider selection
- periodic authoritative resend and remote reapply safety net

STRPM transport details:

- channel: `morphsync.together.v1`
- target: all connected STR players
- flags: reliable + ordered
- sender identity comes from authenticated STRPM message metadata
- callbacks are queued onto the SKSE game thread before MorphSync processes them
- STRPM ProxyResolver is loaded for remote-player proxy resolution
- MorphSync no longer opens or discovers LAN UDP peers

## Requirements

- Skyrim Together Reborn 1.8.0-compatible setup
- SKSE64
- RaceMenu
- Address Library for SKSE Plugins
- **STRPluginMessagingAPI v0.8.2 or newer compatible API/ProxyResolver**
- the STRPM server relay required by your STRPluginMessagingAPI installation
- the same MorphSyncTogether version and provider selections on all clients

OBody, OStim, BodyHairSliders and OPubes remain optional integrations.

## BodyHairSliders providers

The FOMOD mirrors the provider list and detection used by BodyHairSliders. Detected providers are exposed as `Recommended` so compatible installers can pre-select them:

- **Nordic Warmaiden Body Hair** — `Nordic Warmaiden Body Hair.esp`
- **HIMBO V3 Bodyhair Overlays for Racemenu** — `HIMBOBodyhairOverlay.esp`
- **Pubes Forever Female / Pubic Hairstyles All In One CBBE** — `AK_RM_PubicStyles_All_In_One.esp`
- **Pubes Forever Male** — `AK_RM_PubicStyles_All_In_One_M.esp`
- **OPubes NG compatibility** — `OPubes.esp`
- **More Pubes for SlaveTats** — detected through `textures/actors/character/slavetats/ZckeZckTPubicHair/ZckeZcktPubic00Heart.dds`
- **Natural Pubic Hairstyles** — `NaturalPubicHairstyles.esp`
- **Natural Pubic Hairstyles - UBE** — `UBENaturalPubicHairstyles.esp`

Each selected provider installs a marker under:

```text
Data/SKSE/Plugins/MorphSyncTogether/Providers/
```

The female pubic providers use the existing generic female-pubic compatibility switch internally, and OPubes enables both female and male pubic compatibility. Tattoos, unrelated generic Body Paints and temporary OCum overlays remain excluded.

The FOMOD intentionally has **no module artwork**.

## Expected log

Successful startup should include:

```text
MorphSyncTogether v0.3.4 STRPM loading
MST STRPM transport READY channel=morphsync.together.v1 ...
Morph sync started ...
```

A real remote morph drift still produces:

```text
MST MORPH DRIFT ... action=restore
MST MORPH APPLY ... verified=1
```

A repeated authoritative snapshot that already matches the proxy now produces:

```text
MST MORPH RX COMPLETE ... repeated=1
MST MORPH APPLY SKIP ... reason=already-authoritative
```

## Build

Run:

```powershell
.\build-vortex.ps1
```

Output:

```text
dist/MorphSyncTogether-v0.3.4-FOMOD.zip
```

The build script validates all eight provider packages, validates the FOMOD XML, verifies the archive contents, and explicitly rejects an accidentally reintroduced `fomod/ModuleImage.png`.

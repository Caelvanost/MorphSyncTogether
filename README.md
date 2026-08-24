# MorphSyncTogether v0.4.0 - STRPM transport

MorphSyncTogether keeps remote Skyrim Together player appearance authoritative across clients.

## v0.4.0

MorphSyncTogether now optionally synchronizes **The New Gentleman (TNG) genital size** in addition to RaceMenu BodyMorphs, FaceGen makeup and supported BodyHairSliders overlays.

TNG does not represent genital size as a normal RaceMenu BodyMorph. Its current implementation scales the live skeleton node `NPC GenitalsBase [GenBase]` and compensates `NPC GenitalsScrotum [GenScrot]` with `1 / sqrt(scale)`. MorphSyncTogether therefore captures the player's **effective live TNG scale**, sends it through STRPluginMessagingAPI, and applies the same effective scale to the corresponding remote STR proxy.

This preserves custom TNG size settings and race multipliers instead of transmitting only the XS/S/M/L/XL category.

### TNG synchronization behavior

- optional FOMOD integration, detected through `TheNewGentleman.esp`
- enabled by `Data/SKSE/Plugins/MorphSyncTogether/Providers/TNG.enabled`
- captures the local player's effective `GenBase` scale on Skyrim's game thread
- sends `TNGSIZE` state through the existing `morphsync.together.v1` STRPM channel
- resolves the remote actor through STRPM ProxyResolver
- applies `GenBase = scale`
- applies `GenScrot = 1 / sqrt(scale)`, matching TNG's own scaling behavior
- checks once per second for remote drift
- resends the authoritative scale every 5 seconds
- skips node writes when the proxy already matches the authoritative scale
- retries automatically when the STR proxy or TNG skeleton nodes are not yet loaded

The existing v0.3.4 morph resend optimization remains unchanged: repeated STRPM morph snapshots are compared against the live proxy and expensive RaceMenu rebuilds are skipped when the proxy already matches.

## Features

- RaceMenu BodyMorph synchronization
- OBody drift correction/reapplication
- FaceGen makeup preservation/rebind
- BodyHairSliders / OPubes semantic overlay synchronization
- optional The New Gentleman genital-size synchronization
- independent FOMOD provider/integration selection
- periodic authoritative resend and remote reapply safety net

## STRPM transport

- channel: `morphsync.together.v1`
- target: all connected STR players
- flags: reliable + ordered
- sender identity comes from authenticated STRPM message metadata
- callbacks are queued onto the SKSE game thread before MorphSync processes them
- STRPM ProxyResolver is used for remote-player proxy resolution
- MorphSyncTogether does not open or discover LAN UDP peers

## Requirements

- Skyrim Together Reborn 1.8.0-compatible setup
- SKSE64
- RaceMenu
- Address Library for SKSE Plugins
- **STRPluginMessagingAPI v0.8.2 or newer compatible API/ProxyResolver**
- the STRPM server relay required by your STRPluginMessagingAPI installation
- the same MorphSyncTogether version on all clients

Optional integrations:

- OBody / OBody NG
- OStim
- BodyHairSliders
- OPubes
- **The New Gentleman (TNG)**

For TNG size synchronization, install TNG on the relevant clients and select **The New Gentleman (TNG)** in the MorphSyncTogether FOMOD. Detected installations are marked `Recommended`.

## BodyHairSliders providers

The FOMOD mirrors the provider list and detection used by BodyHairSliders:

- **Nordic Warmaiden Body Hair** — `Nordic Warmaiden Body Hair.esp`
- **HIMBO V3 Bodyhair Overlays for Racemenu** — `HIMBOBodyhairOverlay.esp`
- **Pubes Forever Female / Pubic Hairstyles All In One CBBE** — `AK_RM_PubicStyles_All_In_One.esp`
- **Pubes Forever Male** — `AK_RM_PubicStyles_All_In_One_M.esp`
- **OPubes NG compatibility** — `OPubes.esp`
- **More Pubes for SlaveTats** — detected through `textures/actors/character/slavetats/ZckeZckTPubicHair/ZckeZcktPubic00Heart.dds`
- **Natural Pubic Hairstyles** — `NaturalPubicHairstyles.esp`
- **Natural Pubic Hairstyles - UBE** — `UBENaturalPubicHairstyles.esp`

Selected providers install markers under:

```text
Data/SKSE/Plugins/MorphSyncTogether/Providers/
```

Tattoos, unrelated generic Body Paints and temporary OCum overlays remain excluded.

The FOMOD intentionally has **no module artwork**.

## Expected log

Successful startup with TNG selected should include:

```text
MorphSyncTogether v0.4.0 STRPM loading
MST STRPM transport READY channel=morphsync.together.v1 ...
Morph sync started ...
MST TNG sync started interval=1000ms resend=5000ms
```

Local TNG state:

```text
MST TNG TX player=... name="..." scale=... changed=... resend=...
```

Remote TNG state:

```text
MST TNG RX sender="..." scale=... repeated=...
MST TNG DRIFT ... action=restore
MST TNG APPLY ... verified=1
```

When the remote TNG scale already matches:

```text
MST TNG APPLY SKIP ... reason=already-authoritative
```

Existing morph diagnostics remain available:

```text
MST MORPH DRIFT ... action=restore
MST MORPH APPLY ... verified=1
MST MORPH APPLY SKIP ... reason=already-authoritative
```

## Build

Run:

```powershell
.\build-vortex.ps1
```

Output:

```text
dist/MorphSyncTogether-v0.4.0-FOMOD.zip
```

The build script validates the core package, all BodyHair provider packages, the TNG integration marker, both FOMOD XML files and the final archive contents. It also explicitly rejects an accidentally reintroduced `fomod/ModuleImage.png`.

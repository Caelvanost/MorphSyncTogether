# MorphSyncTogether v0.3.1 - STRPM transport

MorphSyncTogether keeps remote Skyrim Together player appearance authoritative across clients.

## v0.3.1

This branch replaces MorphSyncTogether's private UDP/autodiscovery transport with **STRPluginMessagingAPI (STRPM)**.

Preserved functionality:

- RaceMenu BodyMorph synchronization
- OBody drift correction/reapplication
- FaceGen makeup preservation/rebind
- BodyHairSliders / OPubes semantic overlay synchronization
- independent FOMOD provider selection
- periodic authoritative resend and remote reapply

STRPM transport details:

- channel: `morphsync.together.v1`
- target: all connected STR players
- flags: reliable + ordered
- sender identity comes from authenticated STRPM message metadata
- callbacks are queued onto the SKSE game thread before MorphSync processes them
- STRPM ProxyResolver is loaded and sender ConnectionIDs are retained by the adapter for proxy resolution
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

The FOMOD exposes the same provider names used by BodyHairSliders and marks detected plugins as `Recommended`:

- **Pubic Hairstyles All In One CBBE / Pubes Forever Female** — `AK_RM_PubicStyles_All_In_One.esp`
- **Pubes Forever Male** — `AK_RM_PubicStyles_All_In_One_M.esp`
- **Nordic Warmaiden Body Hair** — `Nordic Warmaiden Body Hair.esp`
- **HIMBO V3 Bodyhair Overlays for Racemenu** — `HIMBOBodyhairOverlay.esp`

Each selected provider installs a marker under:

```text
Data/SKSE/Plugins/MorphSyncTogether/Providers/
```

Only enabled provider families are network-authoritative. Tattoos, generic Body Paints and temporary OCum overlays are excluded.

## Expected log

Successful startup should include:

```text
MorphSyncTogether v0.3.1 STRPM loading
MST STRPM transport READY channel=morphsync.together.v1 ...
Morph sync started ...
```

Incoming traffic should produce diagnostics such as:

```text
MST STRPM RX sender="..." connection=... bytes=... sequence=...
MST MORPH APPLY ...
MST BODYHAIR RX ...
MST BODYHAIR APPLY ...
```

If STRPM is missing or incompatible:

```text
MST STRPM unavailable: STRPluginMessagingAPI.dll is not loaded
```

## Build

Run:

```powershell
.\build-vortex.ps1
```

Output:

```text
dist/MorphSyncTogether-v0.3.1-FOMOD.zip
```

The branch is intended as the first STRPM migration build and should be runtime-tested on two clients before merging into `main`.

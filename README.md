# MorphSyncTogether v0.5.0 - development branch

MorphSyncTogether keeps remote Skyrim Together player appearance authoritative across clients through STRPluginMessagingAPI (STRPM).

> `dev` is the active development branch for v0.5.0. The stable release remains on `main` until the new skeleton/height synchronization layer has been validated on two clients.

## v0.5.0 development goal

v0.5.0 extends MorphSyncTogether beyond RaceMenu BodyMorphs with an experimental **Skeleton Transform Sync** layer intended for character height and persistent XPMSSE/RaceMenu body-proportion overrides.

The existing BodyMorph synchronization already covers CBBE/3BA, HIMBO, OBody and other RaceMenu BodyMorph values. The new layer targets appearance data that is not represented as a BodyMorph:

- actor/reference scale used for character-height changes
- persistent RaceMenu `NiTransform` position overrides
- persistent RaceMenu `NiTransform` rotation overrides
- persistent RaceMenu `NiTransform` scale overrides
- RaceMenu scale-mode values when exposed correctly by the installed RaceMenu build

### Dedicated STRPM transport

The experimental layer uses a separate channel:

```text
morphsync.skeleton.v1
```

This intentionally leaves the proven appearance channel untouched:

```text
morphsync.together.v1
```

The skeleton channel uses STRPM reliable + ordered delivery and STRPM ProxyResolver for ConnectionID -> remote STR proxy resolution.

## XPMSSE safety filtering

MorphSyncTogether does **not** copy arbitrary live skeleton transforms. Animation systems continuously modify bone transforms, so treating the whole live skeleton as authoritative would cause animation fights and visual corruption.

v0.5.0 only synchronizes persistent RaceMenu NiTransform overrides on a filtered set of anatomical nodes. Examples include:

- pelvis
- spine
- neck/head
- clavicles
- upper arms / forearms / hands
- thighs / calves / feet / toes
- body / belly / breast / butt nodes
- RaceMenu `CME Body` / `CME LBody`
- root/body nodes used by persistent height/proportion overrides

Weapon-placement and camera-related XPMSSE nodes are explicitly excluded, including weapon, sword, dagger, axe, mace, bow, quiver, shield, staff, scabbard, arrow/bolt and camera families.

This is intended to synchronize body proportions without overwriting each player's local weapon-placement configuration.

## RaceMenu API safety

The implementation consumes RaceMenu's public `NiTransform` interface and captures transforms through `VisitNodes`.

The current RaceMenu source contains unsafe/inconsistent behavior in some `HasNodeTransform*` helpers, including a `HasNodeTransformScale` implementation that calls a removal path. MorphSyncTogether therefore does **not** use those helpers for drift checks. It captures non-destructive `VisitNodes` snapshots and compares them before applying changes.

## Current Racial Body Morphs Redux scope

This development work is motivated in part by **Racial Body Morphs Redux SSE AE (FKDRS)** and similar XPMSSE-based setups.

The current v0.5.0 prototype can synchronize:

- BodyMorph portions already handled by MorphSyncTogether
- actor/reference scale when a height mod changes the actor scale
- persistent RaceMenu NiTransform overrides on supported anatomical nodes

It does **not yet guarantee reproduction of race-specific base skeleton NIF geometry/transforms** that exist only in different skeleton files and are not exposed as actor scale or RaceMenu NiTransforms. That is the next research step after the first v0.5.0 two-client validation.

## SkeletonSync configuration

`Data/SKSE/Plugins/MorphSyncTogether.ini`:

```ini
[SkeletonSync]
Enabled=1
IntervalMs=1000
FullResendMs=5000
```

Set:

```ini
Enabled=0
```

if the experimental layer causes a compatibility issue. This disables only SkeletonSync; normal BodyMorph, BodyHair, FaceGen and optional TNG synchronization remain available.

## Existing features preserved

- RaceMenu BodyMorph synchronization
- owner-authoritative OBody drift correction/reapplication
- FaceGen makeup preservation/rebind
- BodyHairSliders / OPubes semantic overlay synchronization
- optional The New Gentleman genital-size synchronization
- automatic installer detection for supported BodyHair providers and TNG
- periodic authoritative resend and remote drift recovery

### Optional TNG support

The installer detects:

```text
TheNewGentleman.esp
```

and marks **The New Gentleman (TNG) support** as `Recommended` when present. Selecting it installs:

```text
Data/SKSE/Plugins/MorphSyncTogether/Providers/TNG.enabled
```

TNG synchronization sends the effective `NPC GenitalsBase [GenBase]` scale and applies TNG's corresponding `GenScrot = 1 / sqrt(scale)` compensation on the remote proxy.

## Requirements

- Skyrim Together Reborn 1.8.0-compatible setup
- SKSE64
- RaceMenu
- Address Library for SKSE Plugins
- STRPluginMessagingAPI v0.8.2 or newer compatible messaging API / ProxyResolver
- the same MorphSyncTogether version on both clients

Optional integrations include OBody/OBody NG, OStim, BodyHairSliders, OPubes and The New Gentleman.

## Expected v0.5.0 logs

Startup:

```text
MorphSyncTogether v0.5.0 STRPM loading
MST STRPM transport READY channel=morphsync.together.v1 ...
MST SkeletonSync NiTransform interface READY version=3
MST SkeletonSync STRPM READY channel=morphsync.skeleton.v1 messaging=v2 resolver=v1
MST SkeletonSync started interval=1000ms resend=5000ms actorScale=1 niTransform=1 maxTransforms=64
```

Local capture / transport:

```text
MST SKEL CAPTURE actor=... scale=... transforms=... filtered=... truncated=... hash=...
MST SKEL TX player=... scale=... transforms=... hash=...
```

Remote application:

```text
MST SKEL STRPM RX sender="..." ...
MST SKEL RX sender="..." scale=... transforms=... hash=...
MST SKEL APPLY sender="..." actor=... changed=1 verified=1
```

Already-authoritative state:

```text
MST SKEL APPLY SKIP ... reason=already-authoritative
```

Existing morph/TNG diagnostics remain unchanged.

## Build

On the `dev` branch:

```powershell
git pull
.\build-vortex.ps1
```

Output:

```text
dist/MorphSyncTogether-v0.5.0.zip
```

The build script retains the existing FOMOD/provider validation and archive checks.

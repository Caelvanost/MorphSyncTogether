# MorphSyncTogether v0.4.0 - STR auto-connect and FOMOD installer

MorphSyncTogether is an SKSE plugin that keeps remote Skyrim Together Reborn
player appearance data authoritative across clients. It synchronizes RaceMenu
BodyMorphs, preserves FaceGen makeup after local mod changes, and can
optionally synchronize OPubes overlays.

Version 0.4.0 removes the normal need to edit client INI files for Internet
play. Remote clients can reuse Skyrim Together Reborn's saved direct-connect
address automatically: if STR connects to `host:10578`, MorphSync sends its UDP
traffic to the same `host` on port `27992`.

## Requirements

- Skyrim Together Reborn
- SKSE64
- RaceMenu
- Address Library for SKSE Plugins
- The same MorphSyncTogether version and OPubes FOMOD choice on every client

OBody, OStim and OPubes are supported integrations, not hard requirements.

## Installation

Install `MorphSyncTogether-v0.4.0-FOMOD.zip` with Vortex or another
FOMOD-compatible mod manager. The installer asks two questions.

Internet role:

- **Client / LAN / no Internet relay** is the default for almost everyone. LAN
  discovery stays enabled, and distant clients automatically reuse STR's saved
  direct-connect address when available.
- **Player1 host / Internet relay** is only for the Skyrim Together host machine
  that forwards UDP `27992`. It enables MorphSync relay mode.

OPubes integration:

- **No - I do not use OPubes** installs BodyMorph and FaceGen makeup sync with
  `[PubicOverlaySync] Enabled=0`.
- **Yes - I use OPubes** installs the same core plugin plus a small
  `MorphSyncTogether_OPubes.ini` activation file. It enables OPubes or
  OPubesRaceMenuSelector texture, tint, alpha, and shaved-state sync without
  replacing the main network configuration.

The installer recommends the OPubes option when it detects `OPubes.esp`,
`AK_RM_PubicStyles_All_In_One.esp`, or
`AK_RM_PubicStyles_All_In_One_M.esp`.

## Recommended Internet setup

On Player1 / the STR host:

1. Forward **UDP** port `27992` from the router to the host PC.
2. Allow UDP `27992` through Windows Firewall.
3. In the FOMOD, choose **Player1 host / Internet relay**.

On each remote client:

1. Install the mod with the default **Client / LAN / no Internet relay** role.
2. Connect to Player1 with STR direct connect as usual, for example
   `82.65.51.103:10578`.
3. No MorphSync INI edit is normally required.

Clients send periodic discovery packets to the host. This opens their outbound
NAT mapping, so they normally need no inbound port forwarding. The host relay
learns each observed public endpoint and forwards gameplay packets to all other
active peers. Relayed packets are marked and never relayed a second time,
preventing routing loops.

## STR auto-detection

The default client configuration is:

```ini
[Network]
Disabled=0
AutoDiscovery=1
RelayMode=0
LocalPort=27992
AutoRemoteFromSTR=1
AutoRemotePort=27992
AutoSharedSecretFromSTR=0
RemotePeers=
SharedSecret=
```

`AutoRemoteFromSTR=1` reads STR's Chromium localStorage key
`last_connected_address` from:

```text
Data\SkyrimTogetherReborn\cache\Default\Local Storage\leveldb
```

If STR saved `82.65.51.103:10578`, MorphSync automatically configures
`82.65.51.103:27992` as a remote peer. The setting is refreshed while the game
runs, so a client can connect to STR after MorphSync has already started.

By default, the zero-INI Internet setup is unauthenticated because it is the
most forgiving path for remote clients. Optional STR-password authentication is
available with `AutoSharedSecretFromSTR=1` when `SharedSecret=` is empty:

- Relay host: reads `sPassword` from
  `Data\SkyrimTogetherReborn\config\STServer.ini`.
- Remote client: reads STR's saved direct-connect password when available.

No password or shared secret is logged or transmitted by MorphSync. Only enable
this option when every remote client saves the STR password locally; otherwise
clients without the saved password will not authenticate with a strict relay.
For stricter Internet play, an explicit `SharedSecret=` remains the clearest
option.

Manual `RemotePeers=` and `SharedSecret=` are still supported. If
`SharedSecret` is set explicitly, it overrides STR password auto-detection and
every client must use the exact same value.

## LAN configuration

The default configuration still discovers players by LAN broadcast:

```ini
[Network]
Disabled=0
AutoDiscovery=1
RelayMode=0
LocalPort=27992
RemotePeers=
SharedSecret=
```

LAN broadcast does not cross Internet routers. `AutoDiscovery=1` can remain
enabled when STR auto-detection or manual `RemotePeers` are also used, allowing
LAN and Internet players in the same session.

## Direct Internet peers

For a full-mesh setup without a relay, keep `RelayMode=0` and list every other
public endpoint. Each listed player must forward their own UDP port:

```ini
RemotePeers=player-two.example:27992,203.0.113.8:27993
```

IPv4 addresses and DNS names are supported. Entries can be separated with a
comma or semicolon and can use different external ports. The legacy
`PeerHost`/`PeerPort` pair remains supported and is added to `RemotePeers`.

## Appearance features

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

## Expected log

Client startup after STR direct-connect address detection:

```text
MorphSyncTogether v0.4.0 loading
MSTNET STR auto remote configured address="82.65.51.103:10578" endpoint=82.65.51.103:27992
UDP transport started AUTO=1 RELAY=0 AUTH=... port=27992 configuredPeers=1 ...
```

Relay startup:

```text
MorphSyncTogether v0.4.0 loading
UDP transport started AUTO=0 RELAY=1 AUTH=0 ... port=27992
MSTNET DISCOVERED peer="..." addr=... instance=...
MSTNET RELAY source=... peers=... sender="..."
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

Run `build-vortex.ps1`. It compiles the Release DLL, validates the INI profiles
and FOMOD XML, stages the installer, creates the ZIP, and verifies all required
archive entries.

```text
MorphSyncTogether-v0.4.0-FOMOD.zip
```

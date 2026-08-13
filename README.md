# MorphSyncTogether v0.3.0 - Internet relay and FOMOD installer

MorphSyncTogether is an SKSE plugin that keeps remote Skyrim Together player
appearance data authoritative across clients. It synchronizes RaceMenu
BodyMorphs, preserves FaceGen makeup after local mod changes, and can optionally
synchronize OPubes overlays.

Version 0.3.0 adds direct Internet peers and a multi-player UDP relay. A single
relay machine can expose one forwarded port; remote clients connect outbound and
do not need their own port forwarding.

## Requirements

- Skyrim Together Reborn
- SKSE64
- RaceMenu
- Address Library for SKSE Plugins
- The same MorphSyncTogether version, shared secret, and OPubes FOMOD choice on
  every client

OBody, OStim and OPubes are supported integrations, not hard requirements.

## Installation

Install `MorphSyncTogether-v0.3.0-FOMOD.zip` with Vortex or another
FOMOD-compatible mod manager. The installer asks one required question:

- **No - I do not use OPubes** installs BodyMorph and FaceGen makeup sync with
  `[PubicOverlaySync] Enabled=0`.
- **Yes - I use OPubes** installs the same core plugin plus a small
  `MorphSyncTogether_OPubes.ini` activation file. It enables OPubes or
  OPubesRaceMenuSelector texture, tint, alpha, and shaved-state sync without
  replacing the main network configuration.

The installer recommends the OPubes option when it detects `OPubes.esp`,
`AK_RM_PubicStyles_All_In_One.esp`, or
`AK_RM_PubicStyles_All_In_One_M.esp`. Install the same option on every Skyrim
Together client.

## LAN configuration

The default configuration discovers players by LAN broadcast:

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
enabled when `RemotePeers` is also used, allowing LAN and Internet players in
the same session.

## Recommended Internet configuration: one relay

Choose the machine that hosts the Skyrim Together session or has the easiest
router access. Forward **UDP** port `27992` from its router to that computer and
allow the same port through Windows Firewall.

The relay must have a reachable public IPv4 address. If the Internet provider
uses carrier-grade NAT (CGNAT), ordinary router forwarding will not work; use a
publicly reachable machine or a trusted mesh VPN instead.

Relay machine:

```ini
[Network]
Disabled=0
AutoDiscovery=0
RelayMode=1
LocalPort=27992
RemotePeers=
SharedSecret=replace-this-with-the-same-long-private-value
```

Every remote client:

```ini
[Network]
Disabled=0
AutoDiscovery=0
RelayMode=0
LocalPort=27992
RemotePeers=relay-public-ip-or-dns-name:27992
SharedSecret=replace-this-with-the-same-long-private-value
```

Clients send periodic authenticated discovery packets to the relay. This opens
their outbound NAT mapping, so they normally need no inbound port forwarding.
The relay learns each observed public endpoint and forwards gameplay packets to
all other active peers. Relayed packets are marked and never relayed a second
time, preventing routing loops.

`SharedSecret` enables HMAC-SHA256 authentication for both discovery and
gameplay packets. It is strongly recommended whenever a UDP port is exposed to
the Internet. The secret itself is never transmitted. All clients in the
session must use the exact same value.

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

Relay startup with authentication:

```text
MorphSyncTogether v0.3.0 loading
UDP transport started AUTO=0 RELAY=1 AUTH=1 ... port=27992
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

Run `build-vortex.ps1`. It compiles the Release DLL, validates both INI
profiles and the FOMOD XML, stages the installer, creates the ZIP, and verifies
all required archive entries.

```text
MorphSyncTogether-v0.3.0-FOMOD.zip
```

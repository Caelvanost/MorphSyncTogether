#pragma once

#include <cstdint>
#include <string>

namespace MorphSyncTogether
{
    struct Config
    {
        bool networkEnabled{ true };
        bool autoDiscovery{ true };
        std::uint16_t localPort{ 27992 };
        std::string peerHost{};
        std::uint16_t peerPort{ 27992 };
        std::uint32_t discoveryIntervalMs{ 1000 };
        std::uint32_t peerTimeoutMs{ 10000 };

        // Read local RaceMenu morphs this often. A packet is sent only when
        // the snapshot changes, except for the bounded full-resend heartbeat.
        std::uint32_t syncIntervalMs{ 1000 };
        std::uint32_t fullResendMs{ 5000 };

        // Reassert the last authoritative remote snapshot at this cadence.
        // This is intentionally slow: it exists to defeat late OBody/SPID-like
        // reassignment without creating a per-frame morph fight.
        std::uint32_t remoteReapplyMs{ 5000 };

        bool clearRemoteMorphs{ true };

        // Crash-safe RaceMenu/scenegraph diagnostics. Native TintMask access
        // remains disabled. Remote proxy preservation uses only SKEE overlay
        // registration plus cached FaceGen render materials.
        bool appearanceProbeEnabled{ true };
        std::uint32_t appearanceProbeIntervalMs{ 1000 };
        bool appearanceProbeVerbose{ true };

        // Preserve the last healthy visual state of a remote STR proxy.
        // This is local-only: no OStim event/API dependency is required.
        bool appearancePreserveRemote{ true };
        std::uint32_t appearanceRecoveryAttempts{ 3 };

        // v0.2.5: after restoring a drifted FaceGen tint pointer, request an
        // actual SKSE actor mesh/head refresh. QueueNiNodeUpdate is the first
        // stage; RegenerateHead is an optional fallback if drift persists.
        bool appearanceRefreshOnTintDrift{ true };
        bool appearanceRegenerateHeadFallback{ true };
        std::uint32_t appearanceRefreshCooldownMs{ 2500 };

        // v0.2.6: RaceMenu public preset/tint DDS guard for remote faces.
        bool raceMenuPresetGuardEnabled{ true };
        std::uint32_t raceMenuPresetReloadCooldownMs{ 1000 };

        static Config Load();
    };
}

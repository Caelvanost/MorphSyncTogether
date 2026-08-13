#include "PCH.h"
#include "Config.h"

#include <Windows.h>  // after PCH/CommonLib on purpose

namespace MorphSyncTogether
{
    namespace
    {
        constexpr wchar_t kIniPath[] =
            L".\\Data\\SKSE\\Plugins\\MorphSyncTogether.ini";
        constexpr wchar_t kOPubesIniPath[] =
            L".\\Data\\SKSE\\Plugins\\MorphSyncTogether_OPubes.ini";

        std::string ReadString(
            const wchar_t* section,
            const wchar_t* key,
            const wchar_t* fallback)
        {
            wchar_t buffer[2048]{};
            GetPrivateProfileStringW(
                section, key, fallback, buffer,
                static_cast<DWORD>(std::size(buffer)), kIniPath);

            if (buffer[0] == L'\0') {
                return {};
            }

            const int needed = WideCharToMultiByte(
                CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 1) {
                return {};
            }

            std::string converted(static_cast<std::size_t>(needed), '\0');
            const int written = WideCharToMultiByte(
                CP_UTF8, 0, buffer, -1, converted.data(), needed, nullptr, nullptr);
            if (written <= 1) {
                return {};
            }

            converted.resize(static_cast<std::size_t>(written - 1));
            return converted;
        }

        bool ReadBool(
            const wchar_t* section,
            const wchar_t* key,
            bool fallback,
            const wchar_t* path = kIniPath)
        {
            return GetPrivateProfileIntW(
                       section, key, fallback ? 1 : 0, path) != 0;
        }

        std::uint32_t Clamp(
            std::uint32_t value,
            std::uint32_t minValue,
            std::uint32_t maxValue)
        {
            return std::max(minValue, std::min(value, maxValue));
        }

        std::string Trim(std::string value)
        {
            const auto isSpace = [](unsigned char ch) {
                return std::isspace(ch) != 0;
            };
            value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
            value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
            return value;
        }

        std::optional<Config::RemotePeer> ParseRemotePeer(
            std::string value,
            std::uint16_t defaultPort)
        {
            value = Trim(std::move(value));
            if (value.empty()) {
                return std::nullopt;
            }

            Config::RemotePeer result{};
            result.port = defaultPort;

            const auto separator = value.rfind(':');
            if (separator != std::string::npos) {
                const auto portText = Trim(value.substr(separator + 1));
                try {
                    const auto parsed = std::stoul(portText);
                    if (parsed == 0 || parsed > 65535) {
                        return std::nullopt;
                    }
                    result.port = static_cast<std::uint16_t>(parsed);
                    value.resize(separator);
                } catch (...) {
                    return std::nullopt;
                }
            }

            result.host = Trim(std::move(value));
            if (result.host.empty() || result.host.find('|') != std::string::npos) {
                return std::nullopt;
            }
            return result;
        }

        std::vector<Config::RemotePeer> ParseRemotePeers(
            std::string value,
            std::uint16_t defaultPort)
        {
            std::vector<Config::RemotePeer> result;
            std::unordered_set<std::string> seen;

            std::size_t start = 0;
            while (start <= value.size()) {
                const auto end = value.find_first_of(",;", start);
                const auto item = value.substr(
                    start,
                    end == std::string::npos ? std::string::npos : end - start);
                if (auto peer = ParseRemotePeer(item, defaultPort)) {
                    auto key = peer->host;
                    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });
                    key = fmt::format("{}:{}", key, peer->port);
                    if (seen.insert(key).second) {
                        result.push_back(std::move(*peer));
                    }
                } else if (!Trim(item).empty()) {
                    SKSE::log::warn(
                        "MSTNET ignored invalid RemotePeers entry: \"{}\"",
                        Trim(item));
                }

                if (result.size() >= 64) {
                    SKSE::log::warn("MSTNET RemotePeers limited to 64 entries");
                    break;
                }

                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return result;
        }
    }

    Config Config::Load()
    {
        Config cfg{};

        cfg.networkEnabled = !ReadBool(L"Network", L"Disabled", false);
        cfg.autoDiscovery = ReadBool(L"Network", L"AutoDiscovery", true);
        cfg.relayMode = ReadBool(L"Network", L"RelayMode", false);

        auto localPort = static_cast<std::uint32_t>(GetPrivateProfileIntW(
            L"Network", L"LocalPort", cfg.localPort, kIniPath));
        if (localPort == 0 || localPort > 65535) {
            localPort = 27992;
        }
        cfg.localPort = static_cast<std::uint16_t>(localPort);
        cfg.peerPort = cfg.localPort;

        cfg.discoveryIntervalMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"Network", L"DiscoveryIntervalMs", cfg.discoveryIntervalMs, kIniPath)),
            250, 5000);

        cfg.peerTimeoutMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"Network", L"PeerTimeoutMs", cfg.peerTimeoutMs, kIniPath)),
            3000, 60000);

        cfg.peerHost = ReadString(L"Network", L"PeerHost", L"");
        auto peerPort = static_cast<std::uint32_t>(GetPrivateProfileIntW(
            L"Network", L"PeerPort", cfg.peerPort, kIniPath));
        if (peerPort == 0 || peerPort > 65535) {
            peerPort = cfg.localPort;
        }
        cfg.peerPort = static_cast<std::uint16_t>(peerPort);

        cfg.remotePeers = ParseRemotePeers(
            ReadString(L"Network", L"RemotePeers", L""),
            cfg.peerPort);
        if (auto legacyPeer = ParseRemotePeer(cfg.peerHost, cfg.peerPort)) {
            const auto duplicate = std::ranges::any_of(
                cfg.remotePeers,
                [&](const Config::RemotePeer& peer) {
                    return _stricmp(peer.host.c_str(), legacyPeer->host.c_str()) == 0 &&
                           peer.port == legacyPeer->port;
                });
            if (!duplicate) {
                cfg.remotePeers.push_back(std::move(*legacyPeer));
            }
        }
        cfg.sharedSecret = ReadString(L"Network", L"SharedSecret", L"");

        cfg.syncIntervalMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"MorphSync", L"SyncIntervalMs", cfg.syncIntervalMs, kIniPath)),
            250, 10000);

        cfg.fullResendMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"MorphSync", L"FullResendMs", cfg.fullResendMs, kIniPath)),
            1000, 60000);

        cfg.remoteReapplyMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"MorphSync", L"RemoteReapplyMs", cfg.remoteReapplyMs, kIniPath)),
            1000, 60000);

        cfg.clearRemoteMorphs = ReadBool(
            L"MorphSync", L"ClearRemoteMorphs", cfg.clearRemoteMorphs);

        cfg.pubicOverlaySyncEnabled = ReadBool(
            L"PubicOverlaySync", L"Enabled", cfg.pubicOverlaySyncEnabled);
        if (GetFileAttributesW(kOPubesIniPath) != INVALID_FILE_ATTRIBUTES) {
            cfg.pubicOverlaySyncEnabled = ReadBool(
                L"PubicOverlaySync",
                L"Enabled",
                cfg.pubicOverlaySyncEnabled,
                kOPubesIniPath);
        }

        cfg.appearanceProbeEnabled = ReadBool(
            L"AppearanceProbe", L"Enabled", cfg.appearanceProbeEnabled);

        cfg.appearanceProbeIntervalMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"AppearanceProbe", L"IntervalMs", cfg.appearanceProbeIntervalMs, kIniPath)),
            250, 10000);

        cfg.appearanceProbeVerbose = ReadBool(
            L"AppearanceProbe", L"Verbose", cfg.appearanceProbeVerbose);

        cfg.appearancePreserveRemote = ReadBool(
            L"AppearancePreserve", L"Enabled", cfg.appearancePreserveRemote);

        cfg.appearanceRecoveryAttempts = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"AppearancePreserve", L"RecoveryAttempts", cfg.appearanceRecoveryAttempts, kIniPath)),
            1, 10);

        cfg.faceMaterialRebindEnabled = ReadBool(
            L"FaceMaterialRebind", L"Enabled", cfg.faceMaterialRebindEnabled);

        cfg.faceMaterialRebindFollowups = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"FaceMaterialRebind", L"FollowupPasses", cfg.faceMaterialRebindFollowups, kIniPath)),
            1, 10);

        cfg.faceMaterialRebindIntervalMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"FaceMaterialRebind", L"FollowupIntervalMs", cfg.faceMaterialRebindIntervalMs, kIniPath)),
            100, 2000);

        return cfg;
    }
}

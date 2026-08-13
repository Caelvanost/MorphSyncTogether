#pragma once

#include "PCH.h"
#include "SkeeInterfaces.h"

namespace MorphSyncTogether
{
    class AppearanceProbe
    {
    public:
        static AppearanceProbe& GetSingleton();

        void Initialize(SKEE::IInterfaceMap* interfaceMap);
        void Configure(
            bool enabled,
            std::uint32_t intervalMs,
            bool verbose,
            bool preserveRemote,
            std::uint32_t recoveryAttempts,
            bool refreshOnTintDrift,
            bool regenerateHeadFallback,
            std::uint32_t refreshCooldownMs,
            bool presetGuardEnabled,
            std::uint32_t presetReloadCooldownMs);
        void Reset();

        // Must be called on Skyrim's game thread.
        void ProbeLocalPlayer(RE::PlayerCharacter* player);
        void ProbeRemoteProxy(std::string_view sender, RE::Actor* actor);

    private:
        struct Snapshot
        {
            std::uint64_t hash{ 0 };
            std::chrono::steady_clock::time_point sampledAt{};
            bool initialized{ false };
            bool skeeHasOverlays{ false };
            std::uint32_t sceneOverlayNodes{ 0 };
        };

        struct ProbeData
        {
            std::vector<std::string> details;
            std::uint32_t tintCount{ 0 };
            std::uint32_t overlayTintCount{ 0 };
            std::uint32_t sceneObjects{ 0 };
            std::uint32_t sceneOverlayNodes{ 0 };
            std::uint32_t nodeOverrideValues{ 0 };
            std::uint32_t nodePropertyValues{ 0 };
            bool skeeHasOverlays{ false };
        };

        struct FaceMaterialSnapshot
        {
            std::string nodeName;
            std::uint32_t ordinal{ 0 };
            std::uint32_t slot{ 0 };
            RE::BSShaderMaterial::Feature feature{};
            RE::NiPointer<RE::NiSourceTexture> diffuseTexture;
            RE::NiPointer<RE::BSTextureSet> textureSet;
            RE::NiPointer<RE::NiSourceTexture> tintTexture;
            RE::NiColor tintColor{};
            float materialAlpha{ 1.0F };
            bool hasTintTexture{ false };
            bool hasTintColor{ false };
        };

        struct AppearanceCache
        {
            RE::FormID actorFormID{ 0 };
            std::vector<FaceMaterialSnapshot> faceMaterials;
            bool valid{ false };
            bool recoveryPending{ false };
            std::uint32_t recoveryAttemptsLeft{ 0 };

            // v0.2.5 refresh state. The baseline is keyed by remote network
            // identity rather than the transient FFxxxxxx proxy FormID.
            std::uint8_t refreshStage{ 0 };  // 0=idle, 1=QueueNiNodeUpdate issued, 2=RegenerateHead issued
            std::uint32_t refreshFollowupAttemptsLeft{ 0 };
            std::chrono::steady_clock::time_point lastRefreshRequestAt{};
            std::chrono::steady_clock::time_point lastTintDriftAt{};
            std::chrono::steady_clock::time_point refreshFollowupNotBefore{};

            // v0.2.6 RaceMenu public preset baseline. The generated tint DDS
            // is the important part: it preserves the baked face makeup pixels.
            bool presetSaved{ false };
            std::string presetFilePath;
            std::string tintFilePath;
            std::chrono::steady_clock::time_point lastPresetLoadAt{};
        };

        AppearanceProbe() = default;

        void Probe(
            std::string key,
            std::string_view scope,
            std::string_view label,
            RE::Actor* actor,
            RE::PlayerCharacter* player);

        ProbeData Capture(
            RE::Actor* actor,
            RE::PlayerCharacter* player) const;

        void CaptureRaceMenuOverlays(
            RE::Actor* actor,
            ProbeData& data) const;

        void CaptureSceneGraph(
            RE::Actor* actor,
            ProbeData& data) const;

        void CaptureHeadShaderProbe(
            RE::Actor* actor,
            ProbeData& data) const;

        std::vector<FaceMaterialSnapshot> CaptureFaceMaterials(RE::Actor* actor) const;
        std::uint32_t RestoreFaceMaterials(
            RE::Actor* actor,
            const std::vector<FaceMaterialSnapshot>& cached) const;
        void CacheHealthyRemoteAppearance(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor,
            const ProbeData& data);
        std::uint32_t GuardFaceGenTintTextures(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);
        bool SaveRaceMenuFaceBaseline(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);
        bool ReloadRaceMenuFacePreset(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor,
            bool force);
        static std::string SanitizeCacheName(std::string_view label);

        void HandleFaceGenRefresh(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor,
            std::uint32_t restoredTintCount);
        void RunFaceGenRefreshFollowup(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);
        bool DispatchActorPapyrusNoArgs(
            RE::Actor* actor,
            std::string_view functionName) const;
        void BeginRemoteRecovery(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);
        void RunRemoteRecoveryAttempt(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);

        std::vector<std::string> BuildOverlayNodeNames() const;

        static std::uint64_t HashDetails(const std::vector<std::string>& details);
        static bool ContainsInsensitive(std::string_view haystack, std::string_view needle);
        static std::string ShaderKeyName(std::uint16_t key);
        static std::string MaterialFeatureName(RE::BSShaderMaterial::Feature feature);

        mutable std::mutex _interfaceMutex;
        SKEE::IOverlayInterface* _overlay{ nullptr };
        SKEE::IOverrideInterface* _override{ nullptr };
        SKEE::IPresetInterface* _preset{ nullptr };

        bool _enabled{ true };
        bool _verbose{ true };
        bool _preserveRemote{ true };
        std::uint32_t _intervalMs{ 1000 };
        std::uint32_t _recoveryAttempts{ 3 };
        bool _refreshOnTintDrift{ true };
        bool _regenerateHeadFallback{ true };
        std::uint32_t _refreshCooldownMs{ 2500 };
        bool _presetGuardEnabled{ true };
        std::uint32_t _presetReloadCooldownMs{ 1000 };

        mutable std::mutex _stateMutex;
        std::unordered_map<std::string, Snapshot> _snapshots;
        std::unordered_map<std::string, AppearanceCache> _appearanceCaches;
    };
}

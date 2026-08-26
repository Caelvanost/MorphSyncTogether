#pragma once

#include "PCH.h"
#include "SkeeNiTransformInterface.h"
#include "STRPMCompat.h"

namespace MorphSyncTogether
{
    class SkeletonSync
    {
    public:
        static SkeletonSync& GetSingleton();

        void Initialize();
        void Start();
        void Stop();
        void Reset();

        struct TransformState
        {
            std::string node;
            std::string key;
            bool hasPosition{ false };
            bool hasRotation{ false };
            bool hasScale{ false };
            bool hasScaleMode{ false };
            SKEE::INiTransformInterface::Position position{};
            SKEE::INiTransformInterface::Rotation rotation{};
            float scale{ 1.0F };
            std::uint32_t scaleMode{ 0 };
        };

        static bool IsSafeBodyNode(std::string_view node);

    private:
        struct Snapshot
        {
            float actorScale{ 1.0F };
            std::vector<TransformState> transforms;
            std::uint64_t hash{ 0 };
        };

        struct AppliedKey
        {
            std::string node;
            std::string key;
        };

        struct RemoteState
        {
            Snapshot snapshot;
            RE::FormID lastActorFormID{ 0 };
            bool everApplied{ false };
            std::vector<AppliedKey> appliedKeys;
        };

        SkeletonSync() = default;
        ~SkeletonSync();
        SkeletonSync(const SkeletonSync&) = delete;
        SkeletonSync& operator=(const SkeletonSync&) = delete;

        void SyncLoop(std::stop_token stopToken);
        void QueueTick();
        void TickOnGameThread();
        void TryApplyRemote(const std::string& sender, bool force);

        std::optional<Snapshot> CaptureSnapshot(RE::Actor* actor) const;
        bool ApplySnapshot(RE::Actor* actor, RemoteState& state, bool& changed);
        bool SnapshotMatches(RE::Actor* actor, const Snapshot& snapshot) const;

        bool StartTransport();
        void StopTransport();
        void SendSnapshot(const Snapshot& snapshot);
        RE::Actor* ResolveProxyBySender(std::string_view sender) const;
        static void STRPM_CALL OnMessage(const STRPM::Message* message, void* userData);
        void HandleMessage(const STRPM::Message& message);
        static const char* ResultName(STRPM::Result result) noexcept;

        static bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001F);
        static std::uint64_t HashSnapshot(const Snapshot& snapshot);
        static std::string SerializeTransforms(const Snapshot& snapshot);
        static std::optional<Snapshot> ParseSnapshot(std::string_view payload);
        static std::string HexEncode(std::string_view value);
        static std::optional<std::string> HexDecode(std::string_view value);
        static std::optional<std::string> ReadField(std::string_view payload, std::string_view key);

        SKEE::INiTransformInterface* _niTransform{ nullptr };
        bool _initialized{ false };

        const STRPM::Interface* _api{ nullptr };
        const STRPM::ProxyResolverInterface* _resolver{ nullptr };
        STRPM::ListenerHandle _listener{};
        HMODULE _module{ nullptr };
        mutable std::mutex _senderMutex;
        std::unordered_map<std::string, STRPM::ConnectionID> _senderConnections;

        std::jthread _syncThread;
        std::atomic_bool _running{ false };
        std::atomic_bool _tickQueued{ false };

        std::uint64_t _lastSentHash{ 0 };
        std::chrono::steady_clock::time_point _lastSentAt{};

        mutable std::mutex _remoteMutex;
        std::unordered_map<std::string, RemoteState> _remoteStates;
    };
}

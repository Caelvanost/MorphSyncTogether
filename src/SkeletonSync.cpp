#include "PCH.h"
#include "SkeletonSync.h"

#include <Windows.h>

namespace MorphSyncTogether
{
    namespace
    {
        constexpr char kSkeletonChannel[] = "morphsync.skeleton.v1";
        constexpr std::size_t kMaxTransforms = 64;
        constexpr std::size_t kMaxNameBytes = 96;
        constexpr std::size_t kMaxPacketBytes = 20 * 1024;
        constexpr float kMinActorScale = 0.20F;
        constexpr float kMaxActorScale = 5.00F;
        constexpr float kMinNodeScale = 0.05F;
        constexpr float kMaxNodeScale = 10.0F;

        std::string Lower(std::string_view value)
        {
            std::string result(value);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return result;
        }

        bool ContainsAny(std::string_view value, std::initializer_list<std::string_view> needles)
        {
            return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
                return value.find(needle) != std::string_view::npos;
            });
        }

        std::vector<std::string> Split(std::string_view text, char delimiter)
        {
            std::vector<std::string> result;
            std::size_t start = 0;
            while (start <= text.size()) {
                const auto end = text.find(delimiter, start);
                if (end == std::string_view::npos) {
                    result.emplace_back(text.substr(start));
                    break;
                }
                result.emplace_back(text.substr(start, end - start));
                start = end + 1;
            }
            return result;
        }

        std::string SanitizeSender(std::string value)
        {
            for (auto& c : value) {
                if (c == '|' || c == '\r' || c == '\n') {
                    c = '_';
                }
            }
            return value.empty() ? std::string("Player") : value;
        }

        class TransformVisitor final : public SKEE::INiTransformInterface::NodeVisitor
        {
        public:
            explicit TransformVisitor(std::vector<SkeletonSync::TransformState>& values) : _values(values) {}

            bool VisitPosition(
                const char* node,
                const char* key,
                SKEE::INiTransformInterface::Position& position) override
            {
                auto* value = Find(node, key);
                if (!value || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
                    return false;
                }
                value->position = position;
                value->hasPosition = true;
                return false;
            }

            bool VisitRotation(
                const char* node,
                const char* key,
                SKEE::INiTransformInterface::Rotation& rotation) override
            {
                auto* value = Find(node, key);
                if (!value || !std::isfinite(rotation.heading) || !std::isfinite(rotation.attitude) || !std::isfinite(rotation.bank)) {
                    return false;
                }
                value->rotation = rotation;
                value->hasRotation = true;
                return false;
            }

            bool VisitScale(const char* node, const char* key, float scale) override
            {
                auto* value = Find(node, key);
                if (!value || !std::isfinite(scale) || scale < kMinNodeScale || scale > kMaxNodeScale) {
                    return false;
                }
                value->scale = scale;
                value->hasScale = true;
                return false;
            }

            bool VisitScaleMode(const char* node, const char* key, std::uint32_t scaleMode) override
            {
                auto* value = Find(node, key);
                if (!value || scaleMode > 3) {
                    return false;
                }
                value->scaleMode = scaleMode;
                value->hasScaleMode = true;
                return false;
            }

            std::size_t Filtered() const noexcept { return _filtered; }
            std::size_t Truncated() const noexcept { return _truncated; }

        private:
            SkeletonSync::TransformState* Find(const char* nodeRaw, const char* keyRaw)
            {
                if (!nodeRaw || !*nodeRaw || !keyRaw || !*keyRaw) {
                    ++_filtered;
                    return nullptr;
                }

                const std::string_view node(nodeRaw);
                const std::string_view key(keyRaw);
                if (node.size() > kMaxNameBytes || key.size() > kMaxNameBytes || !SkeletonSync::IsSafeBodyNode(node)) {
                    ++_filtered;
                    return nullptr;
                }

                const auto it = std::find_if(_values.begin(), _values.end(), [&](const auto& value) {
                    return value.node == node && value.key == key;
                });
                if (it != _values.end()) {
                    return &*it;
                }
                if (_values.size() >= kMaxTransforms) {
                    ++_truncated;
                    return nullptr;
                }

                _values.push_back(SkeletonSync::TransformState{ std::string(node), std::string(key) });
                return &_values.back();
            }

            std::vector<SkeletonSync::TransformState>& _values;
            std::size_t _filtered{ 0 };
            std::size_t _truncated{ 0 };
        };
    }

    SkeletonSync& SkeletonSync::GetSingleton()
    {
        static SkeletonSync singleton;
        return singleton;
    }

    SkeletonSync::~SkeletonSync()
    {
        Stop();
    }

    const char* SkeletonSync::ResultName(STRPM::Result result) noexcept
    {
        switch (result) {
        case STRPM::Result::kOk: return "ok";
        case STRPM::Result::kNotAvailable: return "not-available";
        case STRPM::Result::kUnsupportedVersion: return "unsupported-version";
        case STRPM::Result::kInvalidArgument: return "invalid-argument";
        case STRPM::Result::kNotConnected: return "not-connected";
        case STRPM::Result::kChannelAlreadyRegistered: return "channel-already-registered";
        case STRPM::Result::kChannelNotRegistered: return "channel-not-registered";
        case STRPM::Result::kPayloadTooLarge: return "payload-too-large";
        case STRPM::Result::kRateLimited: return "rate-limited";
        case STRPM::Result::kTransportError: return "transport-error";
        case STRPM::Result::kTargetNotFound: return "target-not-found";
        default: return "unknown";
        }
    }

    void SkeletonSync::Initialize()
    {
        if (_initialized) {
            return;
        }
        _initialized = true;

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            SKSE::log::critical("MST SkeletonSync init failed: no SKSE messaging interface");
            return;
        }

        SKEE::InterfaceExchangeMessage exchange{};
        const bool dispatched = messaging->Dispatch(
            SKEE::InterfaceExchangeMessage::kMessageExchangeInterface,
            &exchange,
            sizeof(exchange),
            "skee");
        if (!dispatched || !exchange.interfaceMap) {
            SKSE::log::critical(
                "MST SkeletonSync RaceMenu interface exchange failed dispatched={} map={}",
                dispatched ? 1 : 0,
                exchange.interfaceMap ? 1 : 0);
            return;
        }

        auto* base = exchange.interfaceMap->QueryInterface("NiTransform");
        if (!base) {
            SKSE::log::warn("MST SkeletonSync disabled: RaceMenu NiTransform interface unavailable");
            return;
        }

        _niTransform = static_cast<SKEE::INiTransformInterface*>(base);
        const auto version = _niTransform->GetVersion();
        if (version < 3) {
            SKSE::log::warn("MST SkeletonSync disabled: RaceMenu NiTransform version={} expected>=3", version);
            _niTransform = nullptr;
            return;
        }

        SKSE::log::info("MST SkeletonSync NiTransform interface READY version={}", version);
    }

    bool SkeletonSync::StartTransport()
    {
        _module = GetModuleHandleW(L"STRPluginMessagingAPI.dll");
        if (!_module) {
            SKSE::log::critical("MST SkeletonSync STRPM unavailable: STRPluginMessagingAPI.dll not loaded");
            return false;
        }

        const auto queryApi = reinterpret_cast<STRPM::QueryInterfaceFn>(
            GetProcAddress(_module, STRPM::kQueryInterfaceExportName));
        const auto queryResolver = reinterpret_cast<STRPM::QueryProxyResolverFn>(
            GetProcAddress(_module, STRPM::kQueryProxyResolverExportName));
        if (!queryApi || !queryResolver) {
            SKSE::log::critical(
                "MST SkeletonSync STRPM exports missing messaging={} resolver={}",
                queryApi ? 1 : 0,
                queryResolver ? 1 : 0);
            return false;
        }

        auto result = queryApi(STRPM::kInterfaceVersion, &_api);
        if (result != STRPM::Result::kOk || !_api) {
            SKSE::log::critical("MST SkeletonSync STRPM messaging query failed result={}", ResultName(result));
            _api = nullptr;
            return false;
        }

        result = queryResolver(STRPM::kProxyResolverVersion, &_resolver);
        if (result != STRPM::Result::kOk || !_resolver) {
            SKSE::log::critical("MST SkeletonSync STRPM resolver query failed result={}", ResultName(result));
            _api = nullptr;
            _resolver = nullptr;
            return false;
        }

        result = _api->registerChannel(kSkeletonChannel, &SkeletonSync::OnMessage, this, &_listener);
        if (result != STRPM::Result::kOk) {
            SKSE::log::critical(
                "MST SkeletonSync registerChannel failed channel={} result={}",
                kSkeletonChannel,
                ResultName(result));
            _api = nullptr;
            _resolver = nullptr;
            _listener = {};
            return false;
        }

        SKSE::log::info(
            "MST SkeletonSync STRPM READY channel={} messaging=v{} resolver=v{}",
            kSkeletonChannel,
            STRPM::kInterfaceVersion,
            STRPM::kProxyResolverVersion);
        return true;
    }

    void SkeletonSync::StopTransport()
    {
        if (_api && _listener.value != 0) {
            const auto result = _api->unregisterChannel(_listener);
            if (result != STRPM::Result::kOk) {
                SKSE::log::warn("MST SkeletonSync unregisterChannel failed result={}", ResultName(result));
            }
        }

        {
            std::scoped_lock lock(_senderMutex);
            _senderConnections.clear();
        }
        _listener = {};
        _resolver = nullptr;
        _api = nullptr;
        _module = nullptr;
    }

    void SkeletonSync::Start()
    {
        if (_running.load()) {
            return;
        }

        _config = Config::Load();
        if (!_config.networkEnabled || !_config.skeletonSyncEnabled) {
            SKSE::log::info(
                "MST SkeletonSync disabled network={} enabled={}",
                _config.networkEnabled ? 1 : 0,
                _config.skeletonSyncEnabled ? 1 : 0);
            return;
        }
        if (!_niTransform) {
            SKSE::log::warn("MST SkeletonSync not started: NiTransform unavailable");
            return;
        }

        _running.store(true);
        if (!StartTransport()) {
            _running.store(false);
            return;
        }

        _syncThread = std::jthread([this](std::stop_token token) {
            SyncLoop(token);
        });
        SKSE::log::info(
            "MST SkeletonSync started interval={}ms resend={}ms actorScale=1 niTransform=1 maxTransforms={}",
            _config.skeletonSyncIntervalMs,
            _config.skeletonFullResendMs,
            kMaxTransforms);
        QueueTick();
    }

    void SkeletonSync::Stop()
    {
        if (!_running.exchange(false)) {
            StopTransport();
            return;
        }
        if (_syncThread.joinable()) {
            _syncThread.request_stop();
            _syncThread.join();
        }
        _tickQueued.store(false);
        StopTransport();
        SKSE::log::info("MST SkeletonSync stopped");
    }

    void SkeletonSync::Reset()
    {
        _lastSentHash = 0;
        _lastSentAt = {};
        {
            std::scoped_lock lock(_remoteMutex);
            _remoteStates.clear();
        }
        SKSE::log::info("MST SkeletonSync state reset");
    }

    void SkeletonSync::SyncLoop(std::stop_token stopToken)
    {
        while (!stopToken.stop_requested() && _running.load()) {
            QueueTick();

            const auto total = std::chrono::milliseconds(_config.skeletonSyncIntervalMs);
            constexpr auto slice = std::chrono::milliseconds(100);
            auto slept = std::chrono::milliseconds(0);
            while (slept < total && !stopToken.stop_requested() && _running.load()) {
                std::this_thread::sleep_for(slice);
                slept += slice;
            }
        }
    }

    void SkeletonSync::QueueTick()
    {
        if (!_running.load() || _tickQueued.exchange(true)) {
            return;
        }
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            _tickQueued.store(false);
            return;
        }
        tasks->AddTask([this]() {
            _tickQueued.store(false);
            TickOnGameThread();
        });
    }

    void SkeletonSync::TickOnGameThread()
    {
        if (!_running.load() || !_niTransform || !_api) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->Get3D()) {
            const auto snapshot = CaptureSnapshot(player);
            if (snapshot) {
                const bool changed = snapshot->hash != _lastSentHash;
                const bool resendDue = _lastSentAt.time_since_epoch().count() == 0 ||
                    now - _lastSentAt >= std::chrono::milliseconds(_config.skeletonFullResendMs);
                if (changed || resendDue) {
                    if (SendSnapshot(*snapshot)) {
                        _lastSentHash = snapshot->hash;
                        _lastSentAt = now;
                        SKSE::log::info(
                            "MST SKEL TX player={:08X} scale={:.6f} transforms={} hash={:016X} changed={} resend={}",
                            player->GetFormID(),
                            snapshot->actorScale,
                            snapshot->transforms.size(),
                            snapshot->hash,
                            changed ? 1 : 0,
                            resendDue ? 1 : 0);
                    }
                }
            }
        }

        std::vector<std::string> senders;
        {
            std::scoped_lock lock(_remoteMutex);
            senders.reserve(_remoteStates.size());
            for (const auto& [sender, state] : _remoteStates) {
                senders.push_back(sender);
            }
        }
        for (const auto& sender : senders) {
            TryApplyRemote(sender, false);
        }
    }

    bool SkeletonSync::SendSnapshot(const Snapshot& snapshot)
    {
        if (!_api) {
            return false;
        }

        const auto data = SerializeTransforms(snapshot);
        const auto packet = fmt::format(
            "SKEL|v=1|scale={:.9g}|hash={:016X}|count={}|data={}",
            snapshot.actorScale,
            snapshot.hash,
            snapshot.transforms.size(),
            data);
        if (packet.size() > kMaxPacketBytes) {
            SKSE::log::warn("MST SKEL TX dropped oversized packet bytes={} max={}", packet.size(), kMaxPacketBytes);
            return false;
        }

        const STRPM::Target target{ STRPM::TargetKind::kAllPlayers, 0, nullptr };
        const auto result = _api->send(
            kSkeletonChannel,
            target,
            packet.data(),
            packet.size(),
            STRPM::kMessageReliable | STRPM::kMessageOrdered);
        if (result != STRPM::Result::kOk) {
            SKSE::log::warn("MST SKEL TX failed bytes={} result={}", packet.size(), ResultName(result));
            return false;
        }
        return true;
    }

    void STRPM_CALL SkeletonSync::OnMessage(const STRPM::Message* message, void* userData)
    {
        auto* self = static_cast<SkeletonSync*>(userData);
        if (!self || !message || !message->data || message->size == 0 || !self->_running.load()) {
            return;
        }
        self->HandleMessage(*message);
    }

    void SkeletonSync::HandleMessage(const STRPM::Message& message)
    {
        std::string sender = message.sender.displayName && *message.sender.displayName ?
            message.sender.displayName : fmt::format("connection-{}", message.sender.connectionID);
        sender = SanitizeSender(std::move(sender));

        {
            std::scoped_lock lock(_senderMutex);
            _senderConnections[sender] = message.sender.connectionID;
        }

        const std::string payload(static_cast<const char*>(message.data), message.size);
        if (!std::string_view(payload).starts_with("SKEL|")) {
            return;
        }

        SKSE::log::trace(
            "MST SKEL STRPM RX sender=\"{}\" connection={} bytes={} sequence={}",
            sender,
            message.sender.connectionID,
            message.size,
            message.sequence);

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            SKSE::log::warn("MST SKEL RX dropped: SKSE task interface unavailable");
            return;
        }
        tasks->AddTask([sender = std::move(sender), payload]() {
            SkeletonSync::GetSingleton().HandlePacket(sender, payload);
        });
    }

    RE::Actor* SkeletonSync::ResolveProxyBySender(std::string_view sender) const
    {
        if (!_resolver || sender.empty()) {
            return nullptr;
        }

        STRPM::ConnectionID connectionID{};
        {
            std::scoped_lock lock(_senderMutex);
            const auto it = _senderConnections.find(std::string(sender));
            if (it == _senderConnections.end()) {
                return nullptr;
            }
            connectionID = it->second;
        }

        STRPM::ProxyFormID formID{};
        const auto result = _resolver->resolve(connectionID, &formID);
        if (result != STRPM::Result::kOk || formID == 0) {
            return nullptr;
        }
        return RE::TESForm::LookupByID<RE::Actor>(formID);
    }

    std::optional<SkeletonSync::Snapshot> SkeletonSync::CaptureSnapshot(RE::Actor* actor) const
    {
        if (!actor || !actor->Get3D() || !_niTransform) {
            return std::nullopt;
        }

        Snapshot snapshot{};
        snapshot.actorScale = actor->GetScale();
        if (!std::isfinite(snapshot.actorScale) || snapshot.actorScale < kMinActorScale || snapshot.actorScale > kMaxActorScale) {
            return std::nullopt;
        }

        auto* base = actor->GetActorBase();
        const bool female = base && base->GetSex() == RE::SEXES::kFemale;
        TransformVisitor visitor(snapshot.transforms);
        _niTransform->VisitNodes(actor, false, female, visitor);

        snapshot.transforms.erase(
            std::remove_if(snapshot.transforms.begin(), snapshot.transforms.end(), [](const auto& value) {
                return !value.hasPosition && !value.hasRotation && !value.hasScale && !value.hasScaleMode;
            }),
            snapshot.transforms.end());
        std::sort(snapshot.transforms.begin(), snapshot.transforms.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.node == rhs.node ? lhs.key < rhs.key : lhs.node < rhs.node;
        });

        snapshot.hash = HashSnapshot(snapshot);
        SKSE::log::trace(
            "MST SKEL CAPTURE actor={:08X} scale={:.6f} transforms={} filtered={} truncated={} hash={:016X}",
            actor->GetFormID(),
            snapshot.actorScale,
            snapshot.transforms.size(),
            visitor.Filtered(),
            visitor.Truncated(),
            snapshot.hash);
        return snapshot;
    }

    void SkeletonSync::HandlePacket(std::string_view senderView, std::string_view payload)
    {
        if (!_running.load() || !_niTransform) {
            return;
        }

        const auto snapshot = ParseSnapshot(payload);
        if (!snapshot) {
            SKSE::log::warn("MST SKEL RX invalid packet sender=\"{}\"", senderView);
            return;
        }

        const std::string sender(senderView);
        bool repeated = false;
        {
            std::scoped_lock lock(_remoteMutex);
            auto& state = _remoteStates[sender];
            repeated = state.everApplied && state.snapshot.hash == snapshot->hash;
            state.snapshot = *snapshot;
            if (!repeated) {
                state.everApplied = false;
            }
        }

        SKSE::log::info(
            "MST SKEL RX sender=\"{}\" scale={:.6f} transforms={} hash={:016X} repeated={}",
            sender,
            snapshot->actorScale,
            snapshot->transforms.size(),
            snapshot->hash,
            repeated ? 1 : 0);
        TryApplyRemote(sender, !repeated);
    }

    void SkeletonSync::TryApplyRemote(const std::string& sender, bool force)
    {
        RemoteState state;
        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remoteStates.find(sender);
            if (it == _remoteStates.end()) {
                return;
            }
            state = it->second;
        }

        auto* actor = ResolveProxyBySender(sender);
        if (!actor) {
            if (force) {
                SKSE::log::info("MST SKEL APPLY WAIT sender=\"{}\" reason=proxy-not-found", sender);
            }
            return;
        }
        if (!actor->Get3D()) {
            if (force) {
                SKSE::log::info(
                    "MST SKEL APPLY WAIT sender=\"{}\" actor={:08X} reason=proxy-3d-not-loaded",
                    sender,
                    actor->GetFormID());
            }
            return;
        }

        if (SnapshotMatches(actor, state.snapshot)) {
            std::scoped_lock lock(_remoteMutex);
            auto it = _remoteStates.find(sender);
            if (it != _remoteStates.end() && it->second.snapshot.hash == state.snapshot.hash) {
                it->second.lastActorFormID = actor->GetFormID();
                it->second.everApplied = true;
            }
            SKSE::log::trace(
                "MST SKEL APPLY SKIP sender=\"{}\" actor={:08X} hash={:016X} reason=already-authoritative",
                sender,
                actor->GetFormID(),
                state.snapshot.hash);
            return;
        }

        bool changed = false;
        const bool verified = ApplySnapshot(actor, state, changed) && SnapshotMatches(actor, state.snapshot);
        {
            std::scoped_lock lock(_remoteMutex);
            auto it = _remoteStates.find(sender);
            if (it != _remoteStates.end() && it->second.snapshot.hash == state.snapshot.hash) {
                it->second.lastActorFormID = actor->GetFormID();
                it->second.everApplied = verified;
                it->second.appliedKeys = state.appliedKeys;
            }
        }

        SKSE::log::info(
            "MST SKEL APPLY sender=\"{}\" actor={:08X} scale={:.6f} transforms={} hash={:016X} changed={} verified={}",
            sender,
            actor->GetFormID(),
            state.snapshot.actorScale,
            state.snapshot.transforms.size(),
            state.snapshot.hash,
            changed ? 1 : 0,
            verified ? 1 : 0);
    }

    bool SkeletonSync::ApplySnapshot(RE::Actor* actor, RemoteState& state, bool& changed)
    {
        if (!actor || !_niTransform) {
            return false;
        }

        auto* base = actor->GetActorBase();
        const bool female = base && base->GetSex() == RE::SEXES::kFemale;
        const bool actorChanged = state.lastActorFormID != 0 && state.lastActorFormID != actor->GetFormID();
        const auto liveSnapshot = CaptureSnapshot(actor);
        if (!liveSnapshot) {
            return false;
        }

        if (!NearlyEqual(liveSnapshot->actorScale, state.snapshot.actorScale)) {
            SKSE::log::info(
                "MST SKEL DRIFT actor={:08X} component=actor-scale live={:.6f} authoritative={:.6f} action=restore",
                actor->GetFormID(),
                liveSnapshot->actorScale,
                state.snapshot.actorScale);
            actor->SetScale(state.snapshot.actorScale);
            changed = true;
        }

        if (!actorChanged) {
            for (const auto& old : state.appliedKeys) {
                const bool stillPresent = std::any_of(state.snapshot.transforms.begin(), state.snapshot.transforms.end(), [&](const auto& value) {
                    return value.node == old.node && value.key == old.key;
                });
                if (!stillPresent) {
                    _niTransform->RemoveNodeTransform(actor, false, female, old.node.c_str(), old.key.c_str());
                    changed = true;
                }
            }
        }

        state.appliedKeys.clear();
        for (const auto& value : state.snapshot.transforms) {
            const auto liveIt = std::find_if(liveSnapshot->transforms.begin(), liveSnapshot->transforms.end(), [&](const auto& live) {
                return live.node == value.node && live.key == value.key;
            });
            const TransformState* live = liveIt == liveSnapshot->transforms.end() ? nullptr : &*liveIt;
            bool entryChanged = false;

            if (value.hasPosition &&
                (!live || !live->hasPosition ||
                 !NearlyEqual(live->position.x, value.position.x) ||
                 !NearlyEqual(live->position.y, value.position.y) ||
                 !NearlyEqual(live->position.z, value.position.z))) {
                auto position = value.position;
                _niTransform->AddNodeTransformPosition(actor, false, female, value.node.c_str(), value.key.c_str(), position);
                entryChanged = true;
            }

            if (value.hasRotation &&
                (!live || !live->hasRotation ||
                 !NearlyEqual(live->rotation.heading, value.rotation.heading) ||
                 !NearlyEqual(live->rotation.attitude, value.rotation.attitude) ||
                 !NearlyEqual(live->rotation.bank, value.rotation.bank))) {
                auto rotation = value.rotation;
                _niTransform->AddNodeTransformRotation(actor, false, female, value.node.c_str(), value.key.c_str(), rotation);
                entryChanged = true;
            }

            if (value.hasScale && (!live || !live->hasScale || !NearlyEqual(live->scale, value.scale))) {
                _niTransform->AddNodeTransformScale(actor, false, female, value.node.c_str(), value.key.c_str(), value.scale);
                entryChanged = true;
            }

            if (value.hasScaleMode && (!live || !live->hasScaleMode || live->scaleMode != value.scaleMode)) {
                _niTransform->AddNodeTransformScaleMode(actor, false, female, value.node.c_str(), value.key.c_str(), value.scaleMode);
                entryChanged = true;
            }

            state.appliedKeys.push_back(AppliedKey{ value.node, value.key });
            changed = changed || entryChanged;
        }

        if (changed) {
            _niTransform->UpdateNodeAllTransforms(actor);
        }
        return true;
    }

    bool SkeletonSync::SnapshotMatches(RE::Actor* actor, const Snapshot& snapshot) const
    {
        const auto live = CaptureSnapshot(actor);
        if (!live || !NearlyEqual(live->actorScale, snapshot.actorScale)) {
            return false;
        }

        for (const auto& expected : snapshot.transforms) {
            const auto it = std::find_if(live->transforms.begin(), live->transforms.end(), [&](const auto& value) {
                return value.node == expected.node && value.key == expected.key;
            });
            if (it == live->transforms.end()) {
                return false;
            }

            if (expected.hasPosition &&
                (!it->hasPosition ||
                 !NearlyEqual(it->position.x, expected.position.x) ||
                 !NearlyEqual(it->position.y, expected.position.y) ||
                 !NearlyEqual(it->position.z, expected.position.z))) {
                return false;
            }
            if (expected.hasRotation &&
                (!it->hasRotation ||
                 !NearlyEqual(it->rotation.heading, expected.rotation.heading) ||
                 !NearlyEqual(it->rotation.attitude, expected.rotation.attitude) ||
                 !NearlyEqual(it->rotation.bank, expected.rotation.bank))) {
                return false;
            }
            if (expected.hasScale && (!it->hasScale || !NearlyEqual(it->scale, expected.scale))) {
                return false;
            }
            if (expected.hasScaleMode && (!it->hasScaleMode || it->scaleMode != expected.scaleMode)) {
                return false;
            }
        }
        return true;
    }

    bool SkeletonSync::IsSafeBodyNode(std::string_view node)
    {
        if (node.empty()) {
            return false;
        }
        const auto lower = Lower(node);

        if (lower == "npc" || lower == "npc root [root]" || lower == "npc com [com ]" ||
            lower == "cme body [body]" || lower == "cme lbody [lbody]") {
            return true;
        }

        if (ContainsAny(lower, {
                "weapon", "sword", "dagger", "axe", "mace", "bow", "quiver", "shield", "staff",
                "camera", "cam", "scabbard", "bolt", "arrow" })) {
            return false;
        }

        const bool bodyFamily = lower.starts_with("npc ") || lower.starts_with("cme ");
        if (!bodyFamily) {
            return false;
        }

        return ContainsAny(lower, {
            "pelvis", "spine", "neck", "head", "clavicle", "upperarm", "forearm", "hand",
            "thigh", "calf", "foot", "toe", "leg", "arm", "body", "belly", "breast", "butt"
        });
    }

    bool SkeletonSync::NearlyEqual(float lhs, float rhs, float epsilon)
    {
        return std::isfinite(lhs) && std::isfinite(rhs) && std::abs(lhs - rhs) <= epsilon;
    }

    std::uint64_t SkeletonSync::HashSnapshot(const Snapshot& snapshot)
    {
        const auto canonical = fmt::format("{:.9g}|{}", snapshot.actorScale, SerializeTransforms(snapshot));
        std::uint64_t hash = 1469598103934665603ULL;
        for (const unsigned char byte : canonical) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash == 0 ? 1 : hash;
    }

    std::string SkeletonSync::SerializeTransforms(const Snapshot& snapshot)
    {
        std::string result;
        for (std::size_t i = 0; i < snapshot.transforms.size(); ++i) {
            const auto& value = snapshot.transforms[i];
            if (i != 0) {
                result.push_back(';');
            }
            std::uint32_t mask = 0;
            if (value.hasPosition) mask |= 1U;
            if (value.hasRotation) mask |= 2U;
            if (value.hasScale) mask |= 4U;
            if (value.hasScaleMode) mask |= 8U;

            result += fmt::format(
                "{},{},{},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{}",
                HexEncode(value.node),
                HexEncode(value.key),
                mask,
                value.position.x,
                value.position.y,
                value.position.z,
                value.rotation.heading,
                value.rotation.attitude,
                value.rotation.bank,
                value.scale,
                value.scaleMode);
        }
        return result;
    }

    std::optional<SkeletonSync::Snapshot> SkeletonSync::ParseSnapshot(std::string_view payload)
    {
        const auto version = ReadField(payload, "v");
        const auto scaleField = ReadField(payload, "scale");
        const auto hashField = ReadField(payload, "hash");
        const auto countField = ReadField(payload, "count");
        const auto dataField = ReadField(payload, "data");
        if (!version || *version != "1" || !scaleField || !hashField || !countField || !dataField) {
            return std::nullopt;
        }

        Snapshot snapshot{};
        std::size_t expectedCount = 0;
        try {
            snapshot.actorScale = std::stof(*scaleField);
            snapshot.hash = std::stoull(*hashField, nullptr, 16);
            expectedCount = static_cast<std::size_t>(std::stoull(*countField));
        } catch (...) {
            return std::nullopt;
        }

        if (!std::isfinite(snapshot.actorScale) || snapshot.actorScale < kMinActorScale || snapshot.actorScale > kMaxActorScale ||
            snapshot.hash == 0 || expectedCount > kMaxTransforms) {
            return std::nullopt;
        }

        if (expectedCount == 0) {
            if (!dataField->empty()) {
                return std::nullopt;
            }
        } else {
            const auto entries = Split(*dataField, ';');
            if (entries.size() != expectedCount) {
                return std::nullopt;
            }

            snapshot.transforms.reserve(entries.size());
            for (const auto& entry : entries) {
                const auto fields = Split(entry, ',');
                if (fields.size() != 11) {
                    return std::nullopt;
                }

                const auto node = HexDecode(fields[0]);
                const auto key = HexDecode(fields[1]);
                if (!node || !key || node->empty() || key->empty() || node->size() > kMaxNameBytes || key->size() > kMaxNameBytes || !IsSafeBodyNode(*node)) {
                    return std::nullopt;
                }

                TransformState value{};
                value.node = *node;
                value.key = *key;
                try {
                    const auto mask = static_cast<std::uint32_t>(std::stoul(fields[2]));
                    if ((mask & ~0x0FU) != 0) {
                        return std::nullopt;
                    }
                    value.hasPosition = (mask & 1U) != 0;
                    value.hasRotation = (mask & 2U) != 0;
                    value.hasScale = (mask & 4U) != 0;
                    value.hasScaleMode = (mask & 8U) != 0;
                    value.position = { std::stof(fields[3]), std::stof(fields[4]), std::stof(fields[5]) };
                    value.rotation = { std::stof(fields[6]), std::stof(fields[7]), std::stof(fields[8]) };
                    value.scale = std::stof(fields[9]);
                    value.scaleMode = static_cast<std::uint32_t>(std::stoul(fields[10]));
                } catch (...) {
                    return std::nullopt;
                }

                if ((value.hasPosition && (!std::isfinite(value.position.x) || !std::isfinite(value.position.y) || !std::isfinite(value.position.z))) ||
                    (value.hasRotation && (!std::isfinite(value.rotation.heading) || !std::isfinite(value.rotation.attitude) || !std::isfinite(value.rotation.bank))) ||
                    (value.hasScale && (!std::isfinite(value.scale) || value.scale < kMinNodeScale || value.scale > kMaxNodeScale)) ||
                    (value.hasScaleMode && value.scaleMode > 3)) {
                    return std::nullopt;
                }
                snapshot.transforms.push_back(std::move(value));
            }
        }

        std::sort(snapshot.transforms.begin(), snapshot.transforms.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.node == rhs.node ? lhs.key < rhs.key : lhs.node < rhs.node;
        });
        if (HashSnapshot(snapshot) != snapshot.hash) {
            return std::nullopt;
        }
        return snapshot;
    }

    std::string SkeletonSync::HexEncode(std::string_view value)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string result;
        result.reserve(value.size() * 2);
        for (const unsigned char byte : value) {
            result.push_back(digits[(byte >> 4) & 0x0F]);
            result.push_back(digits[byte & 0x0F]);
        }
        return result;
    }

    std::optional<std::string> SkeletonSync::HexDecode(std::string_view value)
    {
        if ((value.size() & 1U) != 0) {
            return std::nullopt;
        }
        auto nibble = [](char c) -> std::optional<std::uint8_t> {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + c - 'a');
            if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + c - 'A');
            return std::nullopt;
        };

        std::string result;
        result.reserve(value.size() / 2);
        for (std::size_t i = 0; i < value.size(); i += 2) {
            const auto high = nibble(value[i]);
            const auto low = nibble(value[i + 1]);
            if (!high || !low) {
                return std::nullopt;
            }
            result.push_back(static_cast<char>((*high << 4) | *low));
        }
        return result;
    }

    std::optional<std::string> SkeletonSync::ReadField(std::string_view payload, std::string_view key)
    {
        const auto needle = fmt::format("{}=", key);
        const auto pos = payload.find(needle);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        const auto start = pos + needle.size();
        const auto end = payload.find('|', start);
        return std::string(payload.substr(start, end == std::string_view::npos ? payload.size() - start : end - start));
    }
}

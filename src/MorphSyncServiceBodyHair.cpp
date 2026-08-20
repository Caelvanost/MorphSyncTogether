#include "PCH.h"
#include "MorphSyncService.h"

// Compile the proven legacy service under alternate names for the packet
// entry points and morph-apply path replaced by the adapters on this branch.
#define HandleUdpPacket HandleUdpPacketLegacy
#define HandlePubicPacket HandlePubicPacketLegacy
#define TryApplyRemote TryApplyRemoteLegacy
#include "MorphSyncService.cpp"
#undef TryApplyRemote
#undef HandlePubicPacket
#undef HandleUdpPacket

namespace MorphSyncTogether
{
    void MorphSyncService::HandleUdpPacket(std::string packet)
    {
        constexpr std::string_view prefix = "MSTUDP|v1|";
        if (!packet.starts_with(prefix)) {
            return;
        }

        const auto sender = ReadField(packet, "from");
        if (!sender || sender->empty()) {
            SKSE::log::warn("MST RX packet missing sender");
            return;
        }

        const auto pubicPos = packet.find("|PUBES|");
        if (pubicPos != std::string::npos) {
            HandlePubicPacket(*sender, std::string_view(packet).substr(pubicPos + 1));
            return;
        }

        const auto morphPos = packet.find("|MORPH|");
        if (morphPos != std::string::npos) {
            HandleMorphPacket(*sender, std::string_view(packet).substr(morphPos + 1));
        }
    }

    void MorphSyncService::HandlePubicPacket(
        std::string_view senderView,
        std::string_view payload)
    {
        if (!_config.pubicOverlaySyncEnabled) {
            return;
        }

        const std::string sender(senderView);
        const auto hash = ParseHex64(ReadField(payload, "hash"));
        const auto present = ParseSize(ReadField(payload, "present"));
        const auto female = ParseSize(ReadField(payload, "female"));
        const auto slot = ParseSize(ReadField(payload, "slot"));
        const auto textureField = ReadField(payload, "texture");
        const auto color = ParseHex32(ReadField(payload, "color"));
        const auto alphaField = ReadField(payload, "alpha");

        if (hash == 0 || !present || *present > 1 || !female || *female > 1 ||
            !slot || *slot >= 64 || !textureField || !color || !alphaField) {
            SKSE::log::warn("MST BODYHAIR RX invalid packet sender=\"{}\"", sender);
            return;
        }

        const auto texture = HexDecode(*textureField);
        float alpha = 0.0F;
        try {
            alpha = std::stof(*alphaField);
        } catch (...) {
            SKSE::log::warn("MST BODYHAIR RX invalid alpha sender=\"{}\"", sender);
            return;
        }

        constexpr std::size_t kMaxBodyHairAggregateBytes = 4096;
        if (!texture || texture->size() > kMaxBodyHairAggregateBytes ||
            !std::isfinite(alpha) || (*present != 0 && texture->empty())) {
            SKSE::log::warn(
                "MST BODYHAIR RX invalid values sender=\"{}\" bytes={} max={}",
                sender,
                texture ? texture->size() : 0,
                kMaxBodyHairAggregateBytes);
            return;
        }

        AppearanceProbe::PubicOverlayState state{};
        state.present = *present != 0;
        state.female = *female != 0;
        state.sourceSlot = static_cast<std::uint32_t>(*slot);
        state.texturePath = *texture;
        state.color = static_cast<std::int32_t>(*color);
        state.alpha = std::clamp(alpha, 0.0F, 1.0F);

        const auto dataHash = HashPubicOverlay(state);
        if (dataHash != hash) {
            SKSE::log::warn(
                "MST BODYHAIR RX hash mismatch sender=\"{}\" net={:016X} data={:016X}",
                sender,
                hash,
                dataHash);
            return;
        }

        {
            std::scoped_lock lock(_remoteMutex);
            auto& snapshot = _remotePubicSnapshots[sender];
            snapshot.hash = hash;
            snapshot.state = state;
            snapshot.lastActorFormID = 0;
            snapshot.lastApply = {};
            snapshot.everApplied = false;
        }

        SKSE::log::info(
            "MST BODYHAIR RX sender=\"{}\" present={} female={} bytes={} aggregate={} hash={:016X}",
            sender,
            state.present ? 1 : 0,
            state.female ? 1 : 0,
            state.texturePath.size(),
            state.texturePath.starts_with("BHS1:") ? 1 : 0,
            hash);

        TryApplyRemotePubic(sender, true);
    }

    void MorphSyncService::TryApplyRemote(
        const std::string& sender,
        bool force)
    {
        RemoteSnapshot snapshot;
        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remoteSnapshots.find(sender);
            if (it == _remoteSnapshots.end()) {
                return;
            }
            snapshot = it->second;
        }

        auto* actor = ResolveRemoteProxyByName(sender);
        if (!actor) {
            if (force) {
                SKSE::log::info(
                    "MST MORPH APPLY WAIT sender=\"{}\" reason=proxy-not-found",
                    sender);
            }
            return;
        }

        if (!actor->Get3D()) {
            if (force) {
                SKSE::log::info(
                    "MST MORPH APPLY WAIT sender=\"{}\" actor={:08X} reason=proxy-3d-not-loaded",
                    sender,
                    actor->GetFormID());
            }
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto authoritativeDataHash = HashMorphs(snapshot.values);
        const auto liveBefore = CaptureMorphs(actor);
        const auto liveBeforeHash = HashMorphs(liveBefore);
        const bool drift = liveBeforeHash != authoritativeDataHash;

        // STRPM can periodically resend the same authoritative snapshot. If the
        // proxy already matches it, refresh bookkeeping only and avoid another
        // ClearMorphs/SetMorph/ApplyBodyMorphs/UpdateModelWeight pass.
        if (!drift) {
            {
                std::scoped_lock lock(_remoteMutex);
                const auto it = _remoteSnapshots.find(sender);
                if (it != _remoteSnapshots.end() && it->second.hash == snapshot.hash) {
                    it->second.everApplied = true;
                    it->second.lastActorFormID = actor->GetFormID();
                    it->second.lastApply = now;
                }
            }

            if (force) {
                SKSE::log::trace(
                    "MST MORPH APPLY SKIP sender=\"{}\" actor={:08X} values={} hash={:016X} reason=already-authoritative",
                    sender,
                    actor->GetFormID(),
                    liveBefore.size(),
                    liveBeforeHash);
            }
            return;
        }

        SKSE::log::info(
            "MST MORPH DRIFT sender=\"{}\" actor={:08X} liveValues={} liveHash={:016X} authoritativeValues={} authoritativeHash={:016X} action=restore",
            sender,
            actor->GetFormID(),
            liveBefore.size(),
            liveBeforeHash,
            snapshot.values.size(),
            authoritativeDataHash);

        if (_config.clearRemoteMorphs) {
            _bodyMorph->ClearMorphs(actor);
        }

        for (const auto& value : snapshot.values) {
            _bodyMorph->SetMorph(
                actor,
                value.morphName.c_str(),
                value.morphKey.c_str(),
                value.value);
        }

        _bodyMorph->ApplyBodyMorphs(actor, false);
        _bodyMorph->UpdateModelWeight(actor, false);

        const auto liveAfter = CaptureMorphs(actor);
        const auto liveAfterHash = HashMorphs(liveAfter);
        const bool verified = liveAfterHash == authoritativeDataHash;

        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remoteSnapshots.find(sender);
            if (it != _remoteSnapshots.end() && it->second.hash == snapshot.hash) {
                it->second.everApplied = true;
                it->second.lastActorFormID = actor->GetFormID();
                it->second.lastApply = now;
            }
        }

        auto* base = actor->GetActorBase();
        SKSE::log::info(
            "MST MORPH APPLY sender=\"{}\" actor={:08X} base={:08X} values={} netHash={:016X} dataHash={:016X} clear={} verified={} liveAfterValues={} liveAfterHash={:016X} modelUpdateQueued=1",
            sender,
            actor->GetFormID(),
            base ? base->GetFormID() : 0,
            snapshot.values.size(),
            snapshot.hash,
            authoritativeDataHash,
            _config.clearRemoteMorphs ? 1 : 0,
            verified ? 1 : 0,
            liveAfter.size(),
            liveAfterHash);
    }
}

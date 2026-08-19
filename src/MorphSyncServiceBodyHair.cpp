#include "PCH.h"
#include "MorphSyncService.h"
#include "UdpTransport.h"

// Compile the proven legacy service under alternate names for the entry points
// replaced by the BodyHair + STRPM adapter on this branch.
#define HandleUdpPacket HandleUdpPacketLegacy
#define HandlePubicPacket HandlePubicPacketLegacy
#define ResolveRemoteProxyByName ResolveRemoteProxyByNameLegacy
#include "MorphSyncService.cpp"
#undef ResolveRemoteProxyByName
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

    RE::Actor* MorphSyncService::ResolveRemoteProxyByName(std::string_view sender) const
    {
        auto* actor = UdpTransport::GetSingleton().ResolveProxyBySender(sender);
        if (!actor) {
            return nullptr;
        }

        SKSE::log::trace(
            "MST STRPM PROXY RESOLVE sender=\"{}\" formId={:08X}",
            sender,
            actor->GetFormID());
        return actor;
    }
}

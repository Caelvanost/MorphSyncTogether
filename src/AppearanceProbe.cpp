#include "PCH.h"
#include "AppearanceProbe.h"

#include <RE/F/FunctionArguments.h>
#include <RE/I/IObjectHandlePolicy.h>
#include <RE/I/IStackCallbackFunctor.h>
#include <RE/V/VirtualMachine.h>

namespace MorphSyncTogether
{
    namespace
    {
        constexpr std::uint8_t kNoIndex = 0xFF;
        constexpr std::size_t kMaxOverlayNodes = 96;
        constexpr std::size_t kMaxSceneNames = 96;

        class CaptureVariant final : public SKEE::IOverrideInterface::GetVariant
        {
        public:
            void Int(const SKEE::i32 value) override
            {
                present = true;
                text = fmt::format("I:{}", value);
            }

            void Float(float value) override
            {
                present = true;
                text = fmt::format("F:{:.9g}", value);
            }

            void String(const char* value) override
            {
                present = true;
                text = fmt::format("S:{}", value ? value : "");
            }

            void Bool(bool value) override
            {
                present = true;
                text = value ? "B:1" : "B:0";
            }

            void TextureSet(const RE::BGSTextureSet* value) override
            {
                present = true;
                text = fmt::format("T:{:p}", static_cast<const void*>(value));
            }

            bool present{ false };
            std::string text;
        };

        std::string OverlayTypeName(SKEE::IOverlayInterface::OverlayType type)
        {
            return type == SKEE::IOverlayInterface::OverlayType::Normal ? "Normal" : "Spell";
        }

        std::string OverlayLocationName(SKEE::IOverlayInterface::OverlayLocation location)
        {
            switch (location) {
            case SKEE::IOverlayInterface::OverlayLocation::Body:
                return "Body";
            case SKEE::IOverlayInterface::OverlayLocation::Hand:
                return "Hand";
            case SKEE::IOverlayInterface::OverlayLocation::Feet:
                return "Feet";
            case SKEE::IOverlayInterface::OverlayLocation::Face:
                return "Face";
            default:
                return "Unknown";
            }
        }

        bool InterestingSceneName(std::string_view name)
        {
            if (name.empty()) {
                return false;
            }

            const std::array<std::string_view, 12> needles{
                "[Ovl", "[SOvl", "face", "head", "tint", "makeup",
                "paint", "freck", "lip", "eye", "cum", "pube" };

            for (const auto needle : needles) {
                const bool found = name.size() >= needle.size() &&
                    std::search(
                        name.begin(), name.end(), needle.begin(), needle.end(),
                        [](char a, char b) {
                            return std::tolower(static_cast<unsigned char>(a)) ==
                                   std::tolower(static_cast<unsigned char>(b));
                        }) != name.end();
                if (found) {
                    return true;
                }
            }
            return false;
        }

        void VisitScene(
            RE::NiAVObject* object,
            std::uint32_t& total,
            std::uint32_t& overlayNodes,
            std::vector<std::string>& names)
        {
            if (!object) {
                return;
            }

            ++total;
            const char* rawName = object->name.c_str();
            const std::string_view name = rawName ? std::string_view(rawName) : std::string_view{};

            if (name.find("[Ovl") != std::string_view::npos ||
                name.find("[SOvl") != std::string_view::npos) {
                ++overlayNodes;
            }

            if (InterestingSceneName(name) && names.size() < kMaxSceneNames) {
                names.emplace_back(name);
            }

            if (auto* node = object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    if (child) {
                        VisitScene(child.get(), total, overlayNodes, names);
                    }
                }
            }
        }


        struct LiveFaceMaterial
        {
            std::string nodeName;
            std::uint32_t ordinal{ 0 };
            std::uint32_t slot{ 0 };
            RE::BSShaderMaterial::Feature feature{};
            RE::BSShaderProperty* shader{ nullptr };
            RE::BSLightingShaderMaterialBase* base{ nullptr };
        };

        bool IsFaceMaterialFeature(RE::BSShaderMaterial::Feature feature)
        {
            return feature == RE::BSShaderMaterial::Feature::kFaceGen ||
                   feature == RE::BSShaderMaterial::Feature::kFaceGenRGBTint;
        }

        std::string FaceMaterialKey(
            std::string_view nodeName,
            std::uint32_t slot,
            RE::BSShaderMaterial::Feature feature,
            std::uint32_t ordinal)
        {
            return fmt::format("{}|{}|{}|{}", nodeName, slot, static_cast<std::uint32_t>(feature), ordinal);
        }

        void VisitLiveFaceMaterials(
            RE::NiAVObject* object,
            std::unordered_map<std::string, std::uint32_t>& ordinals,
            std::vector<LiveFaceMaterial>& out)
        {
            if (!object) {
                return;
            }

            if (auto* geometry = object->AsGeometry()) {
                auto& runtime = geometry->GetGeometryRuntimeData();

                // BSGeometry stores two distinct property slots. In practice the
                // lighting shader used by FaceGen may live in kEffect, while
                // kProperty can contain a different NiProperty. v0.2.2 only
                // inspected kProperty, which produced materials=0 on Elir.
                for (const auto state : { RE::BSGeometry::States::kProperty, RE::BSGeometry::States::kEffect }) {
                    const auto slot = static_cast<std::uint32_t>(state);
                    auto* property = runtime.properties[state].get();
                    auto* shader = property ? skyrim_cast<RE::BSShaderProperty*>(property) : nullptr;
                    auto* material = shader ? shader->material : nullptr;

                    if (shader && material) {
                        const auto feature = material->GetFeature();
                        if (IsFaceMaterialFeature(feature)) {
                            auto* base = static_cast<RE::BSLightingShaderMaterialBase*>(material);
                            const char* rawName = object->name.c_str();
                            const std::string nodeName = rawName ? rawName : "";
                            const auto ordinalKey = fmt::format("{}|{}|{}", nodeName, slot, static_cast<std::uint32_t>(feature));
                            const auto ordinal = ordinals[ordinalKey]++;
                            out.push_back(LiveFaceMaterial{ nodeName, ordinal, slot, feature, shader, base });
                        }
                    }
                }
            }

            if (auto* node = object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    if (child) {
                        VisitLiveFaceMaterials(child.get(), ordinals, out);
                    }
                }
            }
        }
    }

    AppearanceProbe& AppearanceProbe::GetSingleton()
    {
        static AppearanceProbe singleton;
        return singleton;
    }

    void AppearanceProbe::Initialize(SKEE::IInterfaceMap* interfaceMap)
    {
        if (!interfaceMap) {
            SKSE::log::warn("MST APPEARANCE init skipped: SKEE interface map unavailable");
            return;
        }

        std::scoped_lock lock(_interfaceMutex);

        if (auto* base = interfaceMap->QueryInterface("Overlay")) {
            _overlay = static_cast<SKEE::IOverlayInterface*>(base);
        }
        if (auto* base = interfaceMap->QueryInterface("Override")) {
            _override = static_cast<SKEE::IOverrideInterface*>(base);
        }
        if (auto* base = interfaceMap->QueryInterface("Preset")) {
            _preset = static_cast<SKEE::IPresetInterface*>(base);
        }

        SKSE::log::info(
            "MST APPEARANCE interfaces READY overlay={} overlayVersion={} override={} overrideVersion={} preset={} presetVersion={} mode=probe+racemenu-preset-guard+facegen-refresh-fallback",
            _overlay ? 1 : 0,
            _overlay ? _overlay->GetVersion() : 0,
            _override ? 1 : 0,
            _override ? _override->GetVersion() : 0,
            _preset ? 1 : 0,
            _preset ? _preset->GetVersion() : 0);
    }

    void AppearanceProbe::Configure(
        bool enabled,
        std::uint32_t intervalMs,
        bool verbose,
        bool preserveRemote,
        std::uint32_t recoveryAttempts,
        bool refreshOnTintDrift,
        bool regenerateHeadFallback,
        std::uint32_t refreshCooldownMs,
        bool presetGuardEnabled,
        std::uint32_t presetReloadCooldownMs)
    {
        _enabled = enabled;
        _intervalMs = std::clamp<std::uint32_t>(intervalMs, 250, 10000);
        _verbose = verbose;
        _preserveRemote = preserveRemote;
        _recoveryAttempts = std::clamp<std::uint32_t>(recoveryAttempts, 1, 10);
        _refreshOnTintDrift = refreshOnTintDrift;
        _regenerateHeadFallback = regenerateHeadFallback;
        _refreshCooldownMs = std::clamp<std::uint32_t>(refreshCooldownMs, 1000, 10000);
        _presetGuardEnabled = presetGuardEnabled;
        _presetReloadCooldownMs = std::clamp<std::uint32_t>(presetReloadCooldownMs, 250, 10000);

        SKSE::log::info(
            "MST APPEARANCE configured enabled={} interval={}ms verbose={} preserveRemote={} recoveryAttempts={} presetGuard={} presetReloadCooldown={}ms refreshOnTintDrift={} regenerateHeadFallback={} refreshCooldown={}ms",
            _enabled ? 1 : 0,
            _intervalMs,
            _verbose ? 1 : 0,
            _preserveRemote ? 1 : 0,
            _recoveryAttempts,
            _presetGuardEnabled ? 1 : 0,
            _presetReloadCooldownMs,
            _refreshOnTintDrift ? 1 : 0,
            _regenerateHeadFallback ? 1 : 0,
            _refreshCooldownMs);
    }

    void AppearanceProbe::Reset()
    {
        std::scoped_lock lock(_stateMutex);
        _snapshots.clear();
        _appearanceCaches.clear();
        SKSE::log::info("MST APPEARANCE probe/preserve state reset");
    }

    void AppearanceProbe::ProbeLocalPlayer(RE::PlayerCharacter* player)
    {
        if (!player) {
            return;
        }

        const char* rawName = player->GetName();
        const std::string label = rawName ? rawName : "Player";
        Probe("LOCAL", "LOCAL", label, player, player);
    }

    void AppearanceProbe::ProbeRemoteProxy(std::string_view sender, RE::Actor* actor)
    {
        if (!actor || sender.empty()) {
            return;
        }

        Probe(
            fmt::format("REMOTE:{}:{:08X}", sender, actor->GetFormID()),
            "REMOTE",
            sender,
            actor,
            nullptr);
    }

    void AppearanceProbe::Probe(
        std::string key,
        std::string_view scope,
        std::string_view label,
        RE::Actor* actor,
        RE::PlayerCharacter* player)
    {
        if (!_enabled || !actor) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _snapshots.find(key);
            if (it != _snapshots.end() && it->second.initialized &&
                now - it->second.sampledAt < std::chrono::milliseconds(_intervalMs)) {
                return;
            }
        }

        auto data = Capture(actor, player);
        std::sort(data.details.begin(), data.details.end());
        const auto hash = HashDetails(data.details);

        bool changed = false;
        bool first = false;
        bool previousHasOverlays = false;
        std::uint32_t previousOverlayNodes = 0;
        std::uint64_t previous = 0;
        {
            std::scoped_lock lock(_stateMutex);
            auto& state = _snapshots[key];
            first = !state.initialized;
            previous = state.hash;
            previousHasOverlays = state.skeeHasOverlays;
            previousOverlayNodes = state.sceneOverlayNodes;
            changed = first || state.hash != hash;
            state.hash = hash;
            state.sampledAt = now;
            state.initialized = true;
            state.skeeHasOverlays = data.skeeHasOverlays;
            state.sceneOverlayNodes = data.sceneOverlayNodes;
        }

        if (scope == "REMOTE" && _preserveRemote) {
            // STR proxy FormIDs are transient. Keep the detailed probe snapshot
            // form-specific, but bind the authoritative appearance baseline to
            // the network identity so it survives FFxxxxxx proxy replacement.
            const auto appearanceKey = fmt::format("REMOTE:{}", label);

            if (data.skeeHasOverlays && data.sceneOverlayNodes > 0) {
                CacheHealthyRemoteAppearance(appearanceKey, label, actor, data);
            }

            // OStim can replace the FaceGen tint texture while SKEE overlays
            // remain fully present. v0.2.5 still restores the immutable
            // baseline pointer, then asks SKSE to rebuild the actor NiNode.
            const auto restoredTintCount = GuardFaceGenTintTextures(appearanceKey, label, actor);
            bool facePresetReloaded = false;
            if (restoredTintCount > 0) {
                facePresetReloaded = ReloadRaceMenuFacePreset(appearanceKey, label, actor, true);
            }
            // The RaceMenu preset+tint DDS path is authoritative in v0.2.6.
            // Only fall back to the v0.2.5 NiNode/RegenerateHead experiment if
            // RaceMenu could not reload the cached face preset.
            if (!facePresetReloaded) {
                HandleFaceGenRefresh(appearanceKey, label, actor, restoredTintCount);
            }
            RunFaceGenRefreshFollowup(appearanceKey, label, actor);

            if (!first &&
                (previousHasOverlays || previousOverlayNodes > 0) &&
                (!data.skeeHasOverlays || data.sceneOverlayNodes == 0)) {
                ReloadRaceMenuFacePreset(appearanceKey, label, actor, true);
                BeginRemoteRecovery(appearanceKey, label, actor);
            }

            RunRemoteRecoveryAttempt(appearanceKey, label, actor);
        }

        if (!changed) {
            return;
        }

        auto* base = actor->GetActorBase();
        SKSE::log::info(
            "MST APPEARANCE CHANGE scope={} label=\"{}\" actor={:08X} base={:08X} first={} oldHash={:016X} newHash={:016X} tint={} overlayTint={} skeeOverlays={} nodeOverrides={} nodeProperties={} sceneObjects={} sceneOverlayNodes={} details={}",
            scope,
            label,
            actor->GetFormID(),
            base ? base->GetFormID() : 0,
            first ? 1 : 0,
            previous,
            hash,
            data.tintCount,
            data.overlayTintCount,
            data.skeeHasOverlays ? 1 : 0,
            data.nodeOverrideValues,
            data.nodePropertyValues,
            data.sceneObjects,
            data.sceneOverlayNodes,
            data.details.size());

        if (_verbose) {
            for (const auto& detail : data.details) {
                SKSE::log::info(
                    "MST APPEARANCE DETAIL scope={} label=\"{}\" actor={:08X} {}",
                    scope,
                    label,
                    actor->GetFormID(),
                    detail);
            }
        }
    }

    AppearanceProbe::ProbeData AppearanceProbe::Capture(
        RE::Actor* actor,
        RE::PlayerCharacter* player) const
    {
        ProbeData data{};

        // Crash guard: do not call PlayerCharacter::GetNumTints/GetTintMask/
        // GetOverlayTintMask. On AE 1.6.1170 with our CommonLib build the
        // overlay association path can dereference invalid relocated tint
        // storage. Keep the appearance probe strictly on SKEE + scenegraph.
        if (player) {
            data.details.emplace_back("NATIVE_TINT probe=disabled-crash-guard");
        }
        CaptureRaceMenuOverlays(actor, data);
        CaptureSceneGraph(actor, data);
        CaptureHeadShaderProbe(actor, data);

        return data;
    }

    void AppearanceProbe::CaptureRaceMenuOverlays(
        RE::Actor* actor,
        ProbeData& data) const
    {
        SKEE::IOverlayInterface* overlay = nullptr;
        SKEE::IOverrideInterface* overrides = nullptr;
        {
            std::scoped_lock lock(_interfaceMutex);
            overlay = _overlay;
            overrides = _override;
        }

        if (!overlay) {
            data.details.emplace_back("SKEE_OVERLAY interface=null");
            return;
        }

        data.skeeHasOverlays = overlay->HasOverlays(actor);
        data.details.push_back(fmt::format(
            "SKEE_OVERLAY hasOverlays={}",
            data.skeeHasOverlays ? 1 : 0));

        if (!overrides) {
            data.details.emplace_back("SKEE_OVERRIDE interface=null");
            return;
        }

        const auto nodes = BuildOverlayNodeNames();
        for (const auto& node : nodes) {
            for (const bool female : { false, true }) {
                for (std::uint16_t key = 0; key <= 9; ++key) {
                    const std::uint8_t firstIndex = key == 9 ? 0 : kNoIndex;
                    const std::uint8_t lastIndex = key == 9 ? 8 : kNoIndex;

                    for (std::uint16_t rawIndex = firstIndex; rawIndex <= lastIndex; ++rawIndex) {
                        const auto index = static_cast<std::uint8_t>(rawIndex);

                        if (overrides->HasNodeOverride(
                                actor,
                                female,
                                node.c_str(),
                                key,
                                index)) {
                            CaptureVariant value;
                            if (overrides->GetNodeOverride(
                                    actor,
                                    female,
                                    node.c_str(),
                                    key,
                                    index,
                                    value) && value.present) {
                                ++data.nodeOverrideValues;
                                data.details.push_back(fmt::format(
                                    "NODE_OVERRIDE node=\"{}\" female={} key={}({}) index={} value=\"{}\"",
                                    node,
                                    female ? 1 : 0,
                                    key,
                                    ShaderKeyName(key),
                                    static_cast<unsigned>(index),
                                    value.text));
                            }
                        }

                        // GetNodeProperty reads the live third-person scenegraph
                        // state. It is independent of the male/female storage
                        // bucket, so probe it once on the female=false pass.
                        if (!female) {
                            CaptureVariant live;
                            if (overrides->GetNodeProperty(
                                    actor,
                                    false,
                                    node.c_str(),
                                    key,
                                    index,
                                    live) && live.present) {
                                ++data.nodePropertyValues;
                                data.details.push_back(fmt::format(
                                    "NODE_PROPERTY node=\"{}\" key={}({}) index={} value=\"{}\"",
                                    node,
                                    key,
                                    ShaderKeyName(key),
                                    static_cast<unsigned>(index),
                                    live.text));
                            }
                        }

                        if (key != 9) {
                            break;
                        }
                    }
                }
            }
        }
    }

    void AppearanceProbe::CaptureSceneGraph(
        RE::Actor* actor,
        ProbeData& data) const
    {
        std::vector<std::string> names;
        VisitScene(
            actor->Get3D(),
            data.sceneObjects,
            data.sceneOverlayNodes,
            names);

        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());

        data.details.push_back(fmt::format(
            "SCENEGRAPH total={} overlayNodes={} interestingNames={}",
            data.sceneObjects,
            data.sceneOverlayNodes,
            names.size()));

        for (const auto& name : names) {
            data.details.push_back(fmt::format("SCENE_NAME \"{}\"", name));
        }
    }


    void AppearanceProbe::CaptureHeadShaderProbe(
        RE::Actor* actor,
        ProbeData& data) const
    {
        if (!actor || !actor->Get3D()) {
            data.details.emplace_back("HEAD_SHADER probe=3d-unavailable");
            return;
        }

        constexpr std::size_t kMaxHeadShaderLines = 96;
        std::size_t lines = 0;

        const auto isHeadMarker = [](std::string_view name) {
            return ContainsInsensitive(name, "head") ||
                   ContainsInsensitive(name, "face") ||
                   ContainsInsensitive(name, "eye") ||
                   ContainsInsensitive(name, "brow") ||
                   ContainsInsensitive(name, "mouth") ||
                   ContainsInsensitive(name, "lip");
        };

        std::function<void(RE::NiAVObject*, bool)> visit;
        visit = [&](RE::NiAVObject* object, bool underHead) {
            if (!object || lines >= kMaxHeadShaderLines) {
                return;
            }

            const char* rawName = object->name.c_str();
            const std::string_view name = rawName ? std::string_view(rawName) : std::string_view{};
            const bool nowUnderHead = underHead || isHeadMarker(name);

            if (nowUnderHead) {
                if (auto* geometry = object->AsGeometry()) {
                    auto& runtime = geometry->GetGeometryRuntimeData();
                    for (const auto state : { RE::BSGeometry::States::kProperty, RE::BSGeometry::States::kEffect }) {
                        if (lines >= kMaxHeadShaderLines) {
                            break;
                        }
                        const auto slot = static_cast<std::uint32_t>(state);
                        auto* property = runtime.properties[state].get();
                        if (!property) {
                            continue;
                        }
                        auto* shader = skyrim_cast<RE::BSShaderProperty*>(property);
                        auto* material = shader ? shader->material : nullptr;
                        if (material) {
                            const auto feature = material->GetFeature();
                            data.details.push_back(fmt::format(
                                "HEAD_SHADER geom=\"{}\" slot={} property={:p} shader={:p} material={:p} feature={}({})",
                                name,
                                slot,
                                static_cast<const void*>(property),
                                static_cast<const void*>(shader),
                                static_cast<const void*>(material),
                                static_cast<std::uint32_t>(feature),
                                MaterialFeatureName(feature)));
                        } else {
                            data.details.push_back(fmt::format(
                                "HEAD_SHADER geom=\"{}\" slot={} property={:p} shader={:p} material=null",
                                name,
                                slot,
                                static_cast<const void*>(property),
                                static_cast<const void*>(shader)));
                        }
                        ++lines;
                    }
                }
            }

            if (auto* node = object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    if (child) {
                        visit(child.get(), nowUnderHead);
                    }
                }
            }
        };

        visit(actor->Get3D(), false);
        data.details.push_back(fmt::format("HEAD_SHADER_SUMMARY lines={}", lines));
    }

    std::vector<AppearanceProbe::FaceMaterialSnapshot> AppearanceProbe::CaptureFaceMaterials(RE::Actor* actor) const
    {
        std::vector<FaceMaterialSnapshot> result;
        if (!actor || !actor->Get3D()) {
            return result;
        }

        std::unordered_map<std::string, std::uint32_t> ordinals;
        std::vector<LiveFaceMaterial> live;
        VisitLiveFaceMaterials(actor->Get3D(), ordinals, live);
        result.reserve(live.size());

        for (const auto& item : live) {
            if (!item.base) {
                continue;
            }

            FaceMaterialSnapshot snapshot{};
            snapshot.nodeName = item.nodeName;
            snapshot.ordinal = item.ordinal;
            snapshot.slot = item.slot;
            snapshot.feature = item.feature;
            snapshot.diffuseTexture = item.base->diffuseTexture;
            snapshot.textureSet = item.base->textureSet;
            snapshot.materialAlpha = item.base->materialAlpha;

            if (item.feature == RE::BSShaderMaterial::Feature::kFaceGen) {
                auto* facegen = static_cast<RE::BSLightingShaderMaterialFacegen*>(item.base);
                snapshot.tintTexture = facegen->tintTexture;
                snapshot.hasTintTexture = true;
            } else if (item.feature == RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
                auto* tint = static_cast<RE::BSLightingShaderMaterialFacegenTint*>(item.base);
                snapshot.tintColor = tint->tintColor;
                snapshot.hasTintColor = true;
            }

            result.push_back(std::move(snapshot));
        }

        return result;
    }

    std::uint32_t AppearanceProbe::RestoreFaceMaterials(
        RE::Actor* actor,
        const std::vector<FaceMaterialSnapshot>& cached) const
    {
        if (!actor || !actor->Get3D() || cached.empty()) {
            return 0;
        }

        std::unordered_map<std::string, const FaceMaterialSnapshot*> byKey;
        byKey.reserve(cached.size());
        for (const auto& snapshot : cached) {
            byKey.emplace(
                FaceMaterialKey(snapshot.nodeName, snapshot.slot, snapshot.feature, snapshot.ordinal),
                &snapshot);
        }

        std::unordered_map<std::string, std::uint32_t> ordinals;
        std::vector<LiveFaceMaterial> live;
        VisitLiveFaceMaterials(actor->Get3D(), ordinals, live);

        std::uint32_t restored = 0;
        for (const auto& item : live) {
            const auto it = byKey.find(FaceMaterialKey(item.nodeName, item.slot, item.feature, item.ordinal));
            if (it == byKey.end() || !item.base || !item.shader) {
                continue;
            }

            const auto& snapshot = *it->second;
            item.base->diffuseTexture = snapshot.diffuseTexture;
            item.base->textureSet = snapshot.textureSet;
            item.base->materialAlpha = snapshot.materialAlpha;

            if (snapshot.hasTintTexture && item.feature == RE::BSShaderMaterial::Feature::kFaceGen) {
                auto* facegen = static_cast<RE::BSLightingShaderMaterialFacegen*>(item.base);
                facegen->tintTexture = snapshot.tintTexture;
            }
            if (snapshot.hasTintColor && item.feature == RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
                auto* tint = static_cast<RE::BSLightingShaderMaterialFacegenTint*>(item.base);
                tint->tintColor = snapshot.tintColor;
            }

            item.shader->DoClearRenderPasses();
            ++restored;
        }

        return restored;
    }

    void AppearanceProbe::CacheHealthyRemoteAppearance(
        const std::string& key,
        std::string_view label,
        RE::Actor* actor,
        const ProbeData& data)
    {
        const auto materials = CaptureFaceMaterials(actor);
        if (materials.empty()) {
            return;
        }

        bool firstCache = false;
        bool reboundActor = false;
        RE::FormID previousActorFormID = 0;
        std::vector<FaceMaterialSnapshot> baselineForLog;
        {
            std::scoped_lock lock(_stateMutex);
            auto& cache = _appearanceCaches[key];
            firstCache = !cache.valid;
            previousActorFormID = cache.actorFormID;
            reboundActor = cache.valid && cache.actorFormID != actor->GetFormID();

            // The first fully-rendered state seen for this network identity is
            // authoritative. STR is free to replace the FFxxxxxx proxy later;
            // that rebind must NOT overwrite the baseline with a possibly
            // already-corrupted OStim state.
            if (firstCache) {
                cache.actorFormID = actor->GetFormID();
                cache.faceMaterials = materials;
                cache.valid = true;
                cache.recoveryPending = false;
                cache.recoveryAttemptsLeft = 0;
                cache.refreshStage = 0;
                cache.refreshFollowupAttemptsLeft = 0;
                baselineForLog = cache.faceMaterials;
            } else if (reboundActor) {
                cache.actorFormID = actor->GetFormID();
                cache.recoveryPending = false;
                cache.recoveryAttemptsLeft = 0;
                cache.refreshStage = 0;
                cache.refreshFollowupAttemptsLeft = 0;
                cache.lastRefreshRequestAt = {};
                cache.lastTintDriftAt = {};
                cache.refreshFollowupNotBefore = {};
                baselineForLog = cache.faceMaterials;
            }
        }

        if (firstCache) {
            SKSE::log::info(
                "MST APPEARANCE BASELINE label=\"{}\" actor={:08X} materials={} overlayNodes={} identityKey=stable",
                label,
                actor->GetFormID(),
                baselineForLog.size(),
                data.sceneOverlayNodes);

            if (_verbose) {
                for (const auto& material : baselineForLog) {
                    SKSE::log::info(
                        "MST FACE MATERIAL BASELINE label=\"{}\" actor={:08X} node=\"{}\" slot={} ordinal={} feature={} diffuse={:p} textureSet={:p} tintTexture={:p} tintColor=({:.4f},{:.4f},{:.4f}) alpha={:.4f}",
                        label,
                        actor->GetFormID(),
                        material.nodeName,
                        material.slot,
                        material.ordinal,
                        MaterialFeatureName(material.feature),
                        static_cast<const void*>(material.diffuseTexture.get()),
                        static_cast<const void*>(material.textureSet.get()),
                        static_cast<const void*>(material.tintTexture.get()),
                        material.tintColor.red,
                        material.tintColor.green,
                        material.tintColor.blue,
                        material.materialAlpha);
                }
            }
        } else if (reboundActor) {
            SKSE::log::info(
                "MST APPEARANCE REBIND label=\"{}\" oldActor={:08X} newActor={:08X} baselineMaterials={} action=retain-baseline",
                label,
                previousActorFormID,
                actor->GetFormID(),
                baselineForLog.size());
        }

        // Save through RaceMenu's public Preset interface while the proxy is
        // still visually healthy. This captures the generated face tint DDS
        // that pointer-only material snapshots cannot recreate. Retry on later
        // healthy ticks if the first save fails.
        SaveRaceMenuFaceBaseline(key, label, actor);
    }

    std::string AppearanceProbe::SanitizeCacheName(std::string_view label)
    {
        std::string out;
        out.reserve(label.size());
        for (const unsigned char ch : label) {
            if (std::isalnum(ch) || ch == '_' || ch == '-') {
                out.push_back(static_cast<char>(ch));
            } else {
                out.push_back('_');
            }
        }
        if (out.empty()) {
            out = "RemotePlayer";
        }
        return out;
    }

    bool AppearanceProbe::SaveRaceMenuFaceBaseline(
        const std::string& key,
        std::string_view label,
        RE::Actor* actor)
    {
        if (!_presetGuardEnabled || !actor) {
            return false;
        }

        SKEE::IPresetInterface* preset = nullptr;
        {
            std::scoped_lock lock(_interfaceMutex);
            preset = _preset;
        }
        if (!preset) {
            return false;
        }

        auto* base = actor->GetActorBase();
        if (!base || (base->GetFormID() & 0xFF000000u) != 0xFF000000u) {
            SKSE::log::warn(
                "MST RACEMENU PRESET SAVE label=\"{}\" actor={:08X} base={:08X} saved=0 reason=nonunique-proxy-base",
                label,
                actor->GetFormID(),
                base ? base->GetFormID() : 0);
            return false;
        }

        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it == _appearanceCaches.end() || !it->second.valid ||
                it->second.actorFormID != actor->GetFormID()) {
                return false;
            }
            if (it->second.presetSaved) {
                return true;
            }
        }

        const auto safe = SanitizeCacheName(label);
        const std::string presetFilePath =
            "SKSE\\Plugins\\MorphSyncTogether\\AppearanceCache\\" + safe + ".jslot";
        const std::string tintFilePath =
            "Textures\\MorphSyncTogether\\AppearanceCache\\" + safe + ".dds";

        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" /
                "MorphSyncTogether" / "AppearanceCache",
            ec);
        if (ec) {
            SKSE::log::warn(
                "MST RACEMENU PRESET CACHE DIR type=jslot error={} message=\"{}\"",
                ec.value(),
                ec.message());
            ec.clear();
        }
        std::filesystem::create_directories(
            std::filesystem::current_path() / "Data" / "Textures" /
                "MorphSyncTogether" / "AppearanceCache",
            ec);
        if (ec) {
            SKSE::log::warn(
                "MST RACEMENU PRESET CACHE DIR type=tint error={} message=\"{}\"",
                ec.value(),
                ec.message());
        }

        const bool saved = preset->SavePreset(
            presetFilePath.c_str(),
            tintFilePath.c_str(),
            actor);

        if (saved) {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it != _appearanceCaches.end() && it->second.valid &&
                it->second.actorFormID == actor->GetFormID()) {
                it->second.presetSaved = true;
                it->second.presetFilePath = presetFilePath;
                it->second.tintFilePath = tintFilePath;
            }
        }

        SKSE::log::info(
            "MST RACEMENU PRESET SAVE label=\"{}\" actor={:08X} base={:08X} saved={} jslot=\"{}\" tint=\"{}\"",
            label,
            actor->GetFormID(),
            base->GetFormID(),
            saved ? 1 : 0,
            presetFilePath,
            tintFilePath);
        return saved;
    }

    bool AppearanceProbe::ReloadRaceMenuFacePreset(
        const std::string& key,
        std::string_view label,
        RE::Actor* actor,
        bool force)
    {
        if (!_presetGuardEnabled || !actor) {
            return false;
        }

        SKEE::IPresetInterface* preset = nullptr;
        {
            std::scoped_lock lock(_interfaceMutex);
            preset = _preset;
        }
        if (!preset) {
            return false;
        }

        auto* base = actor->GetActorBase();
        if (!base || (base->GetFormID() & 0xFF000000u) != 0xFF000000u) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        std::string presetFilePath;
        std::string tintFilePath;
        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it == _appearanceCaches.end() || !it->second.valid ||
                !it->second.presetSaved ||
                it->second.actorFormID != actor->GetFormID()) {
                return false;
            }

            const auto& cache = it->second;
            if (!force && cache.lastPresetLoadAt.time_since_epoch().count() != 0 &&
                now - cache.lastPresetLoadAt <
                    std::chrono::milliseconds(_presetReloadCooldownMs)) {
                return false;
            }
            if (cache.lastPresetLoadAt.time_since_epoch().count() != 0 &&
                now - cache.lastPresetLoadAt <
                    std::chrono::milliseconds(_presetReloadCooldownMs)) {
                return false;
            }

            presetFilePath = cache.presetFilePath;
            tintFilePath = cache.tintFilePath;
        }

        // Apply FACE ONLY. RaceMenu defines kPresetApplyFace as zero; this
        // deliberately excludes body morphs/transforms/overrides so ownership
        // of Elir's BodyMorph remains with MorphSyncTogether's network layer.
        const bool loaded = preset->LoadPreset(
            presetFilePath.c_str(),
            tintFilePath.c_str(),
            actor,
            SKEE::IPresetInterface::kPresetApplyFace);

        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it != _appearanceCaches.end() && it->second.valid &&
                it->second.actorFormID == actor->GetFormID()) {
                it->second.lastPresetLoadAt = now;
            }
        }

        SKSE::log::info(
            "MST RACEMENU PRESET LOAD label=\"{}\" actor={:08X} base={:08X} loaded={} apply=face-only jslot=\"{}\" tint=\"{}\"",
            label,
            actor->GetFormID(),
            base->GetFormID(),
            loaded ? 1 : 0,
            presetFilePath,
            tintFilePath);
        return loaded;
    }

    std::uint32_t AppearanceProbe::GuardFaceGenTintTextures(
        const std::string& key,
        std::string_view label,
        RE::Actor* actor)
    {
        if (!actor || !actor->Get3D()) {
            return 0;
        }

        AppearanceCache baseline;
        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it == _appearanceCaches.end() || !it->second.valid ||
                it->second.actorFormID != actor->GetFormID()) {
                return 0;
            }
            baseline = it->second;
        }

        std::unordered_map<std::string, const FaceMaterialSnapshot*> baselineByKey;
        baselineByKey.reserve(baseline.faceMaterials.size());
        for (const auto& snapshot : baseline.faceMaterials) {
            if (snapshot.feature == RE::BSShaderMaterial::Feature::kFaceGen &&
                snapshot.hasTintTexture && snapshot.tintTexture) {
                baselineByKey.emplace(
                    FaceMaterialKey(snapshot.nodeName, snapshot.slot, snapshot.feature, snapshot.ordinal),
                    &snapshot);
            }
        }
        if (baselineByKey.empty()) {
            return 0;
        }

        std::unordered_map<std::string, std::uint32_t> ordinals;
        std::vector<LiveFaceMaterial> live;
        VisitLiveFaceMaterials(actor->Get3D(), ordinals, live);

        std::uint32_t restored = 0;
        for (const auto& item : live) {
            if (!item.base || !item.shader ||
                item.feature != RE::BSShaderMaterial::Feature::kFaceGen) {
                continue;
            }

            const auto it = baselineByKey.find(
                FaceMaterialKey(item.nodeName, item.slot, item.feature, item.ordinal));
            if (it == baselineByKey.end()) {
                continue;
            }

            const auto& snapshot = *it->second;
            auto* facegen = static_cast<RE::BSLightingShaderMaterialFacegen*>(item.base);
            const auto liveTint = facegen->tintTexture;
            if (liveTint.get() == snapshot.tintTexture.get()) {
                continue;
            }

            SKSE::log::info(
                "MST FACEGEN TINT DRIFT label=\"{}\" actor={:08X} node=\"{}\" slot={} ordinal={} baseline={:p} live={:p} action=restore",
                label,
                actor->GetFormID(),
                item.nodeName,
                item.slot,
                item.ordinal,
                static_cast<const void*>(snapshot.tintTexture.get()),
                static_cast<const void*>(liveTint.get()));

            // Keep the baseline NiPointer alive in the cache and assign it to
            // the live FaceGen material. Only the tint texture is touched; the
            // diffuse/texture set and all RaceMenu overlay materials are left
            // alone so OCum and other scene overlays can coexist.
            facegen->tintTexture = snapshot.tintTexture;
            item.shader->DoClearRenderPasses();
            ++restored;
        }

        if (restored > 0) {
            SKSE::log::info(
                "MST FACEGEN TINT GUARD label=\"{}\" actor={:08X} restored={}",
                label,
                actor->GetFormID(),
                restored);
        }

        return restored;
    }

    bool AppearanceProbe::DispatchActorPapyrusNoArgs(
        RE::Actor* actor,
        std::string_view functionName) const
    {
        if (!actor || functionName.empty()) {
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::warn(
                "MST FACEGEN REFRESH actor={:08X} fn=\"{}\" dispatch=failed reason=vm-null",
                actor->GetFormID(),
                functionName);
            return false;
        }

        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) {
            SKSE::log::warn(
                "MST FACEGEN REFRESH actor={:08X} fn=\"{}\" dispatch=failed reason=handle-policy-null",
                actor->GetFormID(),
                functionName);
            return false;
        }

        const auto handle = policy->GetHandleForObject(actor->GetFormType(), actor);
        if (handle == policy->EmptyHandle()) {
            SKSE::log::warn(
                "MST FACEGEN REFRESH actor={:08X} fn=\"{}\" dispatch=failed reason=empty-handle",
                actor->GetFormID(),
                functionName);
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        const RE::BSFixedString actorClass("Actor");
        const std::string functionNameStorage(functionName);
        const RE::BSFixedString function(functionNameStorage.c_str());

        // MakeFunctionArguments() allocates the zero-argument Papyrus argument
        // object using the game allocator. DispatchMethodCall takes ownership
        // through the VM function-message path, just like SendEvent callers in
        // CommonLib.
        auto* args = RE::MakeFunctionArguments();
        return vm->DispatchMethodCall(
            handle,
            actorClass,
            function,
            args,
            callback);
    }

    void AppearanceProbe::HandleFaceGenRefresh(
        const std::string& key,
        std::string_view label,
        RE::Actor* actor,
        std::uint32_t restoredTintCount)
    {
        if (!_refreshOnTintDrift || !actor) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto cooldown = std::chrono::milliseconds(_refreshCooldownMs);

        enum class Action
        {
            None,
            QueueNiNodeUpdate,
            RegenerateHead
        };

        Action action = Action::None;
        std::uint8_t stageBefore = 0;
        bool becameStable = false;
        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it == _appearanceCaches.end() || !it->second.valid ||
                it->second.actorFormID != actor->GetFormID()) {
                return;
            }

            auto& cache = it->second;
            stageBefore = cache.refreshStage;

            if (restoredTintCount > 0) {
                cache.lastTintDriftAt = now;

                if (cache.refreshStage == 0) {
                    action = Action::QueueNiNodeUpdate;
                } else if (cache.refreshStage == 1 && _regenerateHeadFallback &&
                           cache.lastRefreshRequestAt.time_since_epoch().count() != 0 &&
                           now - cache.lastRefreshRequestAt >= cooldown) {
                    action = Action::RegenerateHead;
                }
            } else if (cache.refreshStage == 1 && _regenerateHeadFallback &&
                       cache.refreshFollowupAttemptsLeft == 0 &&
                       cache.lastRefreshRequestAt.time_since_epoch().count() != 0 &&
                       now - cache.lastRefreshRequestAt >= cooldown) {
                // We cannot observe the final pixels from the plugin. Even if
                // the material pointer now matches, v0.2.4 proved that the
                // rendered face can remain stale. Escalate once after the
                // lighter NiNode update so the test always exercises a genuine
                // FaceGen regeneration path.
                action = Action::RegenerateHead;
            } else if (cache.refreshStage != 0 &&
                       cache.refreshFollowupAttemptsLeft == 0 &&
                       cache.lastTintDriftAt.time_since_epoch().count() != 0 &&
                       now - cache.lastTintDriftAt >= cooldown * 2) {
                cache.refreshStage = 0;
                cache.lastRefreshRequestAt = {};
                cache.lastTintDriftAt = {};
                cache.refreshFollowupNotBefore = {};
                becameStable = true;
            }
        }

        if (becameStable) {
            SKSE::log::info(
                "MST FACEGEN REFRESH STABLE label=\"{}\" actor={:08X} previousStage={} action=reset-refresh-cycle",
                label,
                actor->GetFormID(),
                stageBefore);
        }

        if (action == Action::None) {
            return;
        }

        const char* functionName =
            action == Action::QueueNiNodeUpdate ? "QueueNiNodeUpdate" : "RegenerateHead";
        const bool dispatched = DispatchActorPapyrusNoArgs(actor, functionName);

        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it != _appearanceCaches.end() && it->second.valid &&
                it->second.actorFormID == actor->GetFormID() && dispatched) {
                auto& cache = it->second;
                cache.refreshStage =
                    action == Action::QueueNiNodeUpdate ? 1 : 2;
                cache.lastRefreshRequestAt = now;
                cache.refreshFollowupAttemptsLeft = _recoveryAttempts;
                // The Papyrus native is queued. Do not restore against the old
                // NiNode during the same tick; start on the next probe instead.
                cache.refreshFollowupNotBefore = now + std::chrono::milliseconds(500);
            }
        }

        SKSE::log::info(
            "MST FACEGEN REFRESH REQUEST label=\"{}\" actor={:08X} method={} stageBefore={} dispatched={} tintRestored={}",
            label,
            actor->GetFormID(),
            functionName,
            stageBefore,
            dispatched ? 1 : 0,
            restoredTintCount);
    }

    void AppearanceProbe::RunFaceGenRefreshFollowup(
        const std::string& key,
        std::string_view label,
        RE::Actor* actor)
    {
        if (!actor) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        AppearanceCache cacheCopy;
        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it == _appearanceCaches.end() || !it->second.valid ||
                it->second.actorFormID != actor->GetFormID() ||
                it->second.refreshFollowupAttemptsLeft == 0 ||
                now < it->second.refreshFollowupNotBefore) {
                return;
            }
            cacheCopy = it->second;
        }

        if (!actor->Get3D()) {
            SKSE::log::info(
                "MST FACEGEN REFRESH FOLLOWUP label=\"{}\" actor={:08X} stage={} result=wait-3d attemptsLeft={}",
                label,
                actor->GetFormID(),
                cacheCopy.refreshStage,
                cacheCopy.refreshFollowupAttemptsLeft);
            return;
        }

        SKEE::IOverlayInterface* overlay = nullptr;
        {
            std::scoped_lock lock(_interfaceMutex);
            overlay = _overlay;
        }

        const bool overlaysBefore = overlay ? overlay->HasOverlays(actor) : false;
        if (overlay && !overlaysBefore) {
            overlay->AddOverlays(actor, true);
        }

        // Reattach the immutable FaceGen baseline to whichever live material
        // the rebuilt proxy now owns. This is the part pointer-only v0.2.4
        // could not achieve without a genuine 3D refresh.
        const auto restoredMaterials = RestoreFaceMaterials(actor, cacheCopy.faceMaterials);
        const bool overlaysAfter = overlay ? overlay->HasOverlays(actor) : false;

        std::uint32_t attemptsLeft = 0;
        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it != _appearanceCaches.end() && it->second.valid &&
                it->second.actorFormID == actor->GetFormID()) {
                auto& cache = it->second;
                if (cache.refreshFollowupAttemptsLeft > 0) {
                    --cache.refreshFollowupAttemptsLeft;
                }
                attemptsLeft = cache.refreshFollowupAttemptsLeft;
                cache.refreshFollowupNotBefore = now + std::chrono::milliseconds(250);
            }
        }

        SKSE::log::info(
            "MST FACEGEN REFRESH FOLLOWUP label=\"{}\" actor={:08X} stage={} overlaysBefore={} overlaysAfter={} materialsRestored={} attemptsLeft={}",
            label,
            actor->GetFormID(),
            cacheCopy.refreshStage,
            overlaysBefore ? 1 : 0,
            overlaysAfter ? 1 : 0,
            restoredMaterials,
            attemptsLeft);
    }

    void AppearanceProbe::BeginRemoteRecovery(
        const std::string& key,
        std::string_view label,
        RE::Actor* actor)
    {
        bool hasCache = false;
        std::size_t materialCount = 0;
        {
            std::scoped_lock lock(_stateMutex);
            auto it = _appearanceCaches.find(key);
            if (it != _appearanceCaches.end() && it->second.valid &&
                it->second.actorFormID == actor->GetFormID()) {
                hasCache = true;
                materialCount = it->second.faceMaterials.size();
                it->second.recoveryPending = true;
                it->second.recoveryAttemptsLeft = _recoveryAttempts;
            }
        }

        SKSE::log::info(
            "MST APPEARANCE LOSS label=\"{}\" actor={:08X} cache={} materials={} action={}",
            label,
            actor->GetFormID(),
            hasCache ? 1 : 0,
            materialCount,
            hasCache ? "recover" : "observe-only");
    }

    void AppearanceProbe::RunRemoteRecoveryAttempt(
        const std::string& key,
        std::string_view label,
        RE::Actor* actor)
    {
        AppearanceCache cacheCopy;
        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _appearanceCaches.find(key);
            if (it == _appearanceCaches.end() || !it->second.valid ||
                !it->second.recoveryPending || it->second.recoveryAttemptsLeft == 0 ||
                it->second.actorFormID != actor->GetFormID()) {
                return;
            }
            cacheCopy = it->second;
        }

        SKEE::IOverlayInterface* overlay = nullptr;
        {
            std::scoped_lock lock(_interfaceMutex);
            overlay = _overlay;
        }

        const bool hadOverlaysBefore = overlay ? overlay->HasOverlays(actor) : false;
        if (overlay && !hadOverlaysBefore) {
            // Defer=true asks SKEE to rebuild its overlay holder on the game thread
            // without us touching individual NiNodes directly.
            overlay->AddOverlays(actor, true);
        }

        const auto restoredMaterials = RestoreFaceMaterials(actor, cacheCopy.faceMaterials);
        const bool hasOverlaysAfter = overlay ? overlay->HasOverlays(actor) : false;

        std::uint32_t attemptsLeft = 0;
        {
            std::scoped_lock lock(_stateMutex);
            auto it = _appearanceCaches.find(key);
            if (it != _appearanceCaches.end() && it->second.actorFormID == actor->GetFormID()) {
                if (it->second.recoveryAttemptsLeft > 0) {
                    --it->second.recoveryAttemptsLeft;
                }
                attemptsLeft = it->second.recoveryAttemptsLeft;
                if (attemptsLeft == 0) {
                    it->second.recoveryPending = false;
                }
            }
        }

        SKSE::log::info(
            "MST APPEARANCE RECOVER label=\"{}\" actor={:08X} overlaysBefore={} overlaysAfter={} materialsRestored={} attemptsLeft={}",
            label,
            actor->GetFormID(),
            hadOverlaysBefore ? 1 : 0,
            hasOverlaysAfter ? 1 : 0,
            restoredMaterials,
            attemptsLeft);
    }

    std::vector<std::string> AppearanceProbe::BuildOverlayNodeNames() const
    {
        std::vector<std::string> result;

        SKEE::IOverlayInterface* overlay = nullptr;
        {
            std::scoped_lock lock(_interfaceMutex);
            overlay = _overlay;
        }
        if (!overlay) {
            return result;
        }

        const std::array<SKEE::IOverlayInterface::OverlayType, 2> types{
            SKEE::IOverlayInterface::OverlayType::Normal,
            SKEE::IOverlayInterface::OverlayType::Spell };
        const std::array<SKEE::IOverlayInterface::OverlayLocation, 4> locations{
            SKEE::IOverlayInterface::OverlayLocation::Body,
            SKEE::IOverlayInterface::OverlayLocation::Hand,
            SKEE::IOverlayInterface::OverlayLocation::Feet,
            SKEE::IOverlayInterface::OverlayLocation::Face };

        for (const auto type : types) {
            for (const auto location : locations) {
                const auto count = std::min<std::uint32_t>(
                    overlay->GetOverlayCount(type, location),
                    64);
                const char* format = overlay->GetOverlayFormat(type, location);

                if (!format || !*format || count == 0) {
                    continue;
                }

                for (std::uint32_t i = 0; i < count && result.size() < kMaxOverlayNodes; ++i) {
                    std::array<char, 256> buffer{};
                    const int written = std::snprintf(
                        buffer.data(),
                        buffer.size(),
                        format,
                        i);
                    if (written > 0 && static_cast<std::size_t>(written) < buffer.size()) {
                        result.emplace_back(buffer.data());
                    }
                }
            }
        }

        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    std::uint64_t AppearanceProbe::HashDetails(const std::vector<std::string>& details)
    {
        std::uint64_t hash = 1469598103934665603ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;

        for (const auto& detail : details) {
            for (const unsigned char ch : detail) {
                hash ^= ch;
                hash *= prime;
            }
            hash ^= 0xFF;
            hash *= prime;
        }

        return hash;
    }

    bool AppearanceProbe::ContainsInsensitive(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty() || haystack.size() < needle.size()) {
            return false;
        }

        return std::search(
                   haystack.begin(), haystack.end(),
                   needle.begin(), needle.end(),
                   [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) ==
                              std::tolower(static_cast<unsigned char>(b));
                   }) != haystack.end();
    }

    std::string AppearanceProbe::ShaderKeyName(std::uint16_t key)
    {
        static constexpr std::array<std::string_view, 10> names{
            "EmissiveColor", "EmissiveMultiple", "Glossiness",
            "SpecularStrength", "LightingEffect1", "LightingEffect2",
            "TextureSet", "TintColor", "Alpha", "Texture" };

        return key < names.size() ? std::string(names[key]) : "Unknown";
    }


    std::string AppearanceProbe::MaterialFeatureName(RE::BSShaderMaterial::Feature feature)
    {
        switch (feature) {
        case RE::BSShaderMaterial::Feature::kFaceGen:
            return "FaceGen";
        case RE::BSShaderMaterial::Feature::kFaceGenRGBTint:
            return "FaceGenRGBTint";
        default:
            return fmt::format("{}", static_cast<std::uint32_t>(feature));
        }
    }

}

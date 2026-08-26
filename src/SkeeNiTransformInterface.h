#pragma once

#include "SkeeInterfaces.h"

namespace MorphSyncTogether::SKEE
{
    // ABI-compatible RaceMenu/SKEE NiTransform interface v3.
    // Source layout mirrors skee64/IPluginInterface.h from RaceMenu.
    class INiTransformInterface : public IPluginInterface
    {
    public:
        struct Position
        {
            float x{ 0.0F };
            float y{ 0.0F };
            float z{ 0.0F };
        };

        struct Rotation
        {
            float heading{ 0.0F };
            float attitude{ 0.0F };
            float bank{ 0.0F };
        };

        class NodeVisitor
        {
        public:
            virtual bool VisitPosition(const char* node, const char* key, Position& position) = 0;
            virtual bool VisitRotation(const char* node, const char* key, Rotation& rotation) = 0;
            virtual bool VisitScale(const char* node, const char* key, float scale) = 0;
            virtual bool VisitScaleMode(const char* node, const char* key, u32 scaleMode) = 0;
        };

        virtual bool HasNodeTransformPosition(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual bool HasNodeTransformRotation(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual bool HasNodeTransformScale(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual bool HasNodeTransformScaleMode(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;

        virtual void AddNodeTransformPosition(RE::TESObjectREFR*, bool, bool, const char*, const char*, Position&) = 0;
        virtual void AddNodeTransformRotation(RE::TESObjectREFR*, bool, bool, const char*, const char*, Rotation&) = 0;
        virtual void AddNodeTransformScale(RE::TESObjectREFR*, bool, bool, const char*, const char*, float) = 0;
        virtual void AddNodeTransformScaleMode(RE::TESObjectREFR*, bool, bool, const char*, const char*, u32) = 0;

        virtual Position GetNodeTransformPosition(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual Rotation GetNodeTransformRotation(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual float GetNodeTransformScale(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual u32 GetNodeTransformScaleMode(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;

        virtual bool RemoveNodeTransformPosition(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual bool RemoveNodeTransformRotation(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual bool RemoveNodeTransformScale(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual bool RemoveNodeTransformScaleMode(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;

        virtual bool RemoveNodeTransform(RE::TESObjectREFR*, bool, bool, const char*, const char*) = 0;
        virtual void RemoveAllReferenceTransforms(RE::TESObjectREFR*) = 0;
        virtual bool GetOverrideNodeTransform(RE::TESObjectREFR*, bool, bool, const char*, const char*, u16, RE::NiTransform*) = 0;
        virtual void UpdateNodeAllTransforms(RE::TESObjectREFR*) = 0;
        virtual void VisitNodes(RE::TESObjectREFR*, bool, bool, NodeVisitor&) = 0;
        virtual void UpdateNodeTransforms(RE::TESObjectREFR*, bool, bool, const char*) = 0;
    };
}

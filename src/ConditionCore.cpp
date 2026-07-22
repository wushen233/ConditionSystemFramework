#include "pch.h"
#include "ConditionCore.h"
#include "ConditionUI.h"
#include "ProfileManager.h"

#include <Windows.h>
#include <random>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <cctype>
#include <cstdio>
#include <shared_mutex>

namespace ConditionSystem
{
    // =========================================================================
    // 模块 1：全局变量与绝对安全常量
    // =========================================================================

    MCMSettings g_mcmSettings;

    std::atomic<RE::BGSInventoryItem*> g_highlightedItem{ nullptr };
    std::atomic<RE::BGSInventoryItem::Stack*> g_highlightedStack{ nullptr };
    std::atomic<bool> g_isGameRunning{ true };

    std::atomic<bool> g_isWeaponJammed{ false };
    std::atomic<std::uintptr_t> g_jammedWeaponUniqueID{ 0 };
    std::atomic<bool> g_isWeaponDrawn{ false };
    std::atomic<bool> g_isUnjamming{ false };
    std::atomic<bool> g_isRepairMenuOpen{ false };
    std::atomic<RE::TESBoundObject*> g_hoveredObject{ nullptr };
    std::atomic<std::uint32_t> g_hoveredStackIndex{ 0 };
    std::atomic<std::uint32_t> g_hoveredSelectedIndex{ 0 };

    std::shared_mutex g_uiNameMutex;
    std::unordered_map<RE::TESBoundObject*, std::string> g_uiNameCache;

    static float g_lastSentHUDHealth = -1.0f;
    static float g_lastSentHUDPoints = -1.0f;

    std::atomic<bool> g_initialUISyncDone{ false };
    std::atomic<std::uint16_t> g_pipboyHighlightedUID{ 0 };

    std::atomic<int> g_pipboyTransitionTimer{ -1 };
    std::atomic<std::uint32_t> g_hoveredHandleID{ 0 };

    std::shared_mutex g_cacheMutex;
    std::atomic<std::uint16_t> g_customUIDCounter{ 0x8000 };
    std::unordered_map<std::uint32_t, std::uint16_t> g_baseDamageCache;

    // =========================================================================
    // 模块 2：底层工具函数 & 动态抗性解析器
    // =========================================================================

    std::string GameUTF8ToLocal(const char* utf8Str) {
        if (!utf8Str) return "";
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, nullptr, 0);
        if (wideLen <= 0) return utf8Str;
        std::wstring wideStr(wideLen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr.data(), wideLen);

        int localLen = WideCharToMultiByte(CP_ACP, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (localLen <= 0) return utf8Str;
        std::string localStr(localLen, '\0');
        WideCharToMultiByte(CP_ACP, 0, wideStr.c_str(), -1, localStr.data(), localLen, nullptr, nullptr);

        while (!localStr.empty() && localStr.back() == '\0') {
            localStr.pop_back();
        }
        return localStr;
    }



    bool RollForJam(float durability) {
        float jamThreshold = g_mcmSettings.jamThreshold.load();
        // 这里的逻辑根据你当前耐久度和 MCM 设置判定
        if (jamThreshold <= 0.0f || durability > jamThreshold) return false;

        float maxJamChance = g_mcmSettings.maxJamChance.load();
        float chance = ((jamThreshold - durability) / jamThreshold) * maxJamChance;

        static std::mt19937 randEngine(std::random_device{}());
        std::uniform_real_distribution<float> randDist(0.0f, 1.0f);
        return randDist(randEngine) < chance;
    }

    float GetDecompressedPct(RE::TESBoundObject* obj, float engineHealth) {
        auto flags = GetItemFlags(obj);
        if (!flags.enableDurabilitySystem) {
            return engineHealth > 1.0f ? 1.0f : engineHealth;
        }
        // 10x 解码：0.1 → 1.0 (100%), 0.5 → 5.0 (500%)
        float result = engineHealth * 10.0f;
        if (!g_mcmSettings.enableOverCondition.load() && result > 1.0f) {
            result = 1.0f;
        }
        return result;
    }

    float GetCompressedPct(RE::TESBoundObject* obj, float realPct) {
        auto flags = GetItemFlags(obj);
        if (!flags.enableDurabilitySystem) {
            if (realPct > 1.0f) realPct = 1.0f;
            return realPct;
        }
        if (!g_mcmSettings.enableOverCondition.load() && realPct > 1.0f) {
            realPct = 1.0f;
        }
        // 10x 编码：1.0 (100%) → 0.1, 5.0 (500%) → 0.5
        float result = realPct * 0.1f;
        // 确保不超过阈值，避免与引擎默认值 (~1.0) 混淆
        if (result >= ENGINE_DEFAULT_HEALTH_THRESHOLD) {
            result = ENGINE_DEFAULT_HEALTH_THRESHOLD - 0.0001f;
        }
        return result;
    }

    std::vector<std::string> ExtractKeywords(RE::TESForm* form) {
        std::vector<std::string> kwds;
        if (!form) return kwds;
        RE::BGSKeywordForm* kf = form->As<RE::BGSKeywordForm>();
        if (!kf) {
            if (form->Is(RE::ENUM_FORM_ID::kWEAP)) kf = static_cast<RE::BGSKeywordForm*>(form->As<RE::TESObjectWEAP>());
            else if (form->Is(RE::ENUM_FORM_ID::kARMO)) kf = static_cast<RE::BGSKeywordForm*>(form->As<RE::TESObjectARMO>());
            else if (form->Is(RE::ENUM_FORM_ID::kAMMO)) kf = static_cast<RE::BGSKeywordForm*>(form->As<RE::TESAmmo>());
        }
        if (kf) {
            for (uint32_t i = 0; i < kf->numKeywords; ++i) {
                if (kf->keywords && kf->keywords[i]) {
                    const char* edid = kf->keywords[i]->GetFormEditorID();
                    if (edid) kwds.push_back(edid);
                }
            }
        }
        return kwds;
    }

    std::uint16_t GetPlayerEquippedWeaponUniqueID() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return 0;
        for (auto& item : player->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack->IsEquipped()) {
                        return static_cast<std::uint16_t>(item.object->GetFormID() & 0xFFFF);
                    }
                }
            }
        }
        return 0;
    }

    bool IsRealWeaponEquipped() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return false;
        for (auto& item : player->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack->IsEquipped()) {
                        auto weap = item.object->As<RE::TESObjectWEAP>();
                        if (weap && !GetProfileForWeapon(weap).isExcluded && GetItemFlags(weap).enableDurabilitySystem) {
                            auto wType = weap->weaponData.type.get();
                            if (wType != RE::WEAPON_TYPE::kGrenade && wType != RE::WEAPON_TYPE::kMine) return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    bool ShouldShowDurability(RE::TESBoundObject* a_obj) {
        if (!a_obj) return false;
        auto flags = GetItemFlags(a_obj);
        return flags.showDurabilityUI;
    }

    bool IsStackInPlayerInventory(RE::BGSInventoryItem::Stack* a_targetStack) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList || !a_targetStack) return false;
        for (auto& item : player->inventoryList->data) {
            for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (stack == a_targetStack) return true;
            }
        }
        return false;
    }

    void UpdateHUDDurabilityWidget(float a_percent, float a_points, bool a_forceUpdate) {
        if (!a_forceUpdate && std::abs(a_percent - g_lastSentHUDHealth) < 0.0001f && std::abs(a_points - g_lastSentHUDPoints) < 0.5f) return;
        ConditionUI::UpdateDurability(a_percent, a_points);
        g_lastSentHUDHealth = a_percent;
        g_lastSentHUDPoints = a_points;
    }

    float ExtractConditionPercent(RE::TESBoundObject* a_obj, RE::BGSInventoryItem::Stack* a_stack) {
        if (!a_stack || !a_stack->extra) return 1.0f;

        auto healthExtra = a_stack->extra->GetByType<RE::ExtraHealth>();
        if (healthExtra && healthExtra->health < ENGINE_DEFAULT_HEALTH_THRESHOLD) {
            float result = GetDecompressedPct(a_obj, healthExtra->health);
            if (result < 0.0f) result = 0.0f;
            return result;
        }

        auto chargeExtra = a_stack->extra->GetByType<RE::ExtraCharge>();
        if (chargeExtra) {
            float c = chargeExtra->charge;
            if (c > 1.0f) c /= 100.0f;
            return c > 1.0f ? 1.0f : c;
        }
        return 1.0f;
    }

    float GetVisualDurabilityPercent(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack) {
        return ExtractConditionPercent(a_object, a_stack);
    }

    float GetItemDurabilityPercent(RE::BGSInventoryItem* a_item, std::uint32_t a_stackID) {
        if (!a_item || !a_item->object) return 1.0f;

        // Apply Fallout 76-style condition value handling in the Pip-Boy.
        // 引擎(特别是Pip-Boy)查价时常传 0xFFFFFFFF(-1) 代表查询主要实例
        // 我们将其视为查询第 0 个 Stack，而不是直接返回 1.0f 满血！
        std::uint32_t targetStack = (a_stackID == 0xFFFFFFFF) ? 0 : a_stackID;

        RE::BGSInventoryItem::Stack* iter = a_item->stackData.get();
        std::uint32_t count = 0;
        while (iter && count < targetStack) {
            iter = iter->nextStack.get();
            count++;
        }

        return ExtractConditionPercent(a_item->object, iter);
    }

    float GetStackDurabilityPercent(RE::BGSInventoryItem::Stack* a_stack) {
        return ExtractConditionPercent(nullptr, a_stack);
    }

    float GetEquippedWeaponDurabilityPercent() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return 1.0f;
        for (auto& item : player->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack->IsEquipped() && stack->extra) {
                        auto healthExtra = stack->extra->GetByType<RE::ExtraHealth>();
                        if (healthExtra && healthExtra->health < ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                            return GetDecompressedPct(item.object, healthExtra->health);
                        }
                    }
                }
            }
        }
        return 1.0f;
    }

    float GetAverageEquippedArmorDurability() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return 1.0f;

        float totalDurability = 0.0f;
        int armorCount = 0;

        for (auto& item : player->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kARMO)) {
                auto armor = item.object->As<RE::TESObjectARMO>();

                // 排除动力甲（动力甲有原生破碎机制）和被你在配置里排除的护甲
                if (!armor || !GetItemFlags(armor).enableDurabilitySystem) continue;

                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    // 只计算【已穿戴】的护甲
                    if (stack->IsEquipped()) {
                        float realPercent = 1.0f;
                        if (stack->extra) {
                            auto healthExtra = stack->extra->GetByType<RE::ExtraHealth>();
                            if (healthExtra && healthExtra->health < ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                                // 复用你写的绝妙解压算法！
                                realPercent = GetDecompressedPct(armor, healthExtra->health);
                            }
                        }
                        totalDurability += realPercent;
                        armorCount++;
                        break; // 这件装备算过了，看背包下一件
                    }
                }
            }
        }

        // 如果玩家在裸奔，返回 1.0（因为裸奔本来就没防御，不需要打折）
        if (armorCount == 0) return 1.0f;

        return totalDurability / static_cast<float>(armorCount);
    }

    float GetEquippedWeaponDurabilityPoints() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return 1000.0f;
        for (auto& item : player->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack->IsEquipped() && stack->extra) {
                        auto healthExtra = stack->extra->GetByType<RE::ExtraHealth>();
                        if (healthExtra && healthExtra->health < ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                            WeaponProfile prof = GetProfileForWeapon(item.object->As<RE::TESObjectWEAP>());
                            float realPct = GetDecompressedPct(item.object, healthExtra->health);
                            return std::round(prof.maxDurability * realPct);
                        }
                    }
                }
            }
        }
        return 1000.0f;
    }

}

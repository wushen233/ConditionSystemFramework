#include "pch.h"
#include "ConditionCore.h"
#include "ConditionUI.h"
#include "ProfileManager.h"

#include <random>
#include <shared_mutex>

namespace ConditionSystem
{
    // =========================================================================
    // 战利品额外加成判定 (基于技能/属性)
    // =========================================================================

    static float RollForLootConditionBonus(RE::TESBoundObject* a_object, float baseRealPercent)
    {
        if (!GetItemFlagsRaw(a_object).enableDurabilitySystem) return baseRealPercent;

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return baseRealPercent;

        std::shared_lock<std::shared_mutex> lock(g_profileMutex);
        if (!g_lootBonusRule.enabled || g_lootBonusRule.requiredAV.runtimeFormID == 0 || g_lootBonusRule.tiers.empty()) return baseRealPercent;

        auto avForm = RE::TESForm::GetFormByID<RE::ActorValueInfo>(g_lootBonusRule.requiredAV.runtimeFormID);
        if (!avForm) return baseRealPercent;

        float avVal = player->GetActorValue(*avForm);

        const LootBonusTier* activeTier = nullptr;
        for (const auto& tier : g_lootBonusRule.tiers) {
            if (avVal >= tier.minAVLevel) {
                activeTier = &tier;
                break;
            }
        }

        if (!activeTier) return baseRealPercent;

        static std::mt19937 randEngine(std::random_device{}());
        std::uniform_real_distribution<float> chanceDist(0.0f, 100.0f);
        float roll = chanceDist(randEngine);

        if (roll <= activeTier->hitChance) {
            std::uniform_real_distribution<float> bonusDist(activeTier->bonusMinPct, activeTier->bonusMaxPct);
            float bonus = bonusDist(randEngine);
            float newPercent = baseRealPercent + bonus;
            float maxAllowedDrop = g_lootBonusRule.globalCap;
            if (!g_mcmSettings.enableOverCondition.load() && maxAllowedDrop > 1.0f) {
                maxAllowedDrop = 1.0f;
            }
            if (newPercent > maxAllowedDrop) newPercent = maxAllowedDrop;
            return newPercent;
        }

        return baseRealPercent;
    }

    // =========================================================================
    // 单物品耐久初始化
    // =========================================================================

    void WakeUpAndRandomizeSingleStack(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack, bool a_isLoot)
    {
        if (!a_object || !a_stack) return;
        if (!GetItemFlagsRaw(a_object).enableDurabilitySystem) return;

        if (!a_stack->extra) a_stack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());

        auto healthExtra = a_stack->extra->GetByType<RE::ExtraHealth>();
        bool isDefaultEngineHealth = (!healthExtra || healthExtra->health > ENGINE_DEFAULT_HEALTH_THRESHOLD);

        if (isDefaultEngineHealth) {
            float realPercent = 1.0f;

            if (g_mcmSettings.randomLootDurability.load()) {
                float minVal = 1.0f, maxVal = 1.0f;
                if (a_object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                    minVal = g_mcmSettings.weaponLootDurabilityMin.load();
                    maxVal = g_mcmSettings.weaponLootDurabilityMax.load();
                }
                else if (a_object->Is(RE::ENUM_FORM_ID::kARMO)) {
                    minVal = g_mcmSettings.armorLootDurabilityMin.load();
                    maxVal = g_mcmSettings.armorLootDurabilityMax.load();
                }
                if (minVal > maxVal) std::swap(minVal, maxVal);

                static std::mt19937 randEngine(std::random_device{}());
                std::uniform_real_distribution<float> dist(minVal, maxVal);
                realPercent = dist(randEngine);

                if (realPercent > 1.0f) realPercent = 1.0f;

                if (a_isLoot) {
                    realPercent = RollForLootConditionBonus(a_object, realPercent);
                }
            }
            float _compressed = GetCompressedPct(a_object, realPercent);
            a_stack->extra->SetHealthPerc(_compressed);
            // 直接覆盖 healthExtra->health，绕过 SetHealthPerc 可能的内部 clamp/重建
            auto _he = a_stack->extra->GetByType<RE::ExtraHealth>();
            if (_he) _he->health = _compressed;
        }
    }

    // =========================================================================
    // 容器/生物库存耐久随机化
    // =========================================================================

    void RandomizeInventoryDurability(RE::TESObjectREFR* a_refr)
    {
        if (!a_refr || !a_refr->inventoryList) return;
        bool isLoot = a_refr->Is(RE::ENUM_FORM_ID::kACHR) || a_refr->Is(RE::ENUM_FORM_ID::kCONT);
        for (auto& item : a_refr->inventoryList->data) {
            if (item.object && GetItemFlagsRaw(item.object).enableDurabilitySystem) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    auto healthExtra = stack->extra ? stack->extra->GetByType<RE::ExtraHealth>() : nullptr;
                    if (!stack->extra || !healthExtra || healthExtra->health > ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                        WakeUpAndRandomizeSingleStack(item.object, stack, isLoot);
                    }
                }
            }
        }
    }

    void RandomizeWorldObject(RE::TESObjectREFR* a_refr, RE::TESBoundObject* a_resolvedBase)
    {
        if (!a_refr || !a_resolvedBase) return;
        if (!GetItemFlagsRaw(a_resolvedBase).enableDurabilitySystem) return;

        if (!a_refr->extraList) a_refr->extraList = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());

        auto healthExtra = a_refr->extraList->GetByType<RE::ExtraHealth>();
        bool isDefaultEngineHealth = (!healthExtra || healthExtra->health > ENGINE_DEFAULT_HEALTH_THRESHOLD);

        if (isDefaultEngineHealth) {
            float realPercent = 1.0f;
            if (g_mcmSettings.randomLootDurability.load()) {
                float minVal = 1.0f;
                float maxVal = 1.0f;
                if (a_resolvedBase->Is(RE::ENUM_FORM_ID::kWEAP)) {
                    minVal = g_mcmSettings.weaponLootDurabilityMin.load();
                    maxVal = g_mcmSettings.weaponLootDurabilityMax.load();
                }
                else if (a_resolvedBase->Is(RE::ENUM_FORM_ID::kARMO)) {
                    minVal = g_mcmSettings.armorLootDurabilityMin.load();
                    maxVal = g_mcmSettings.armorLootDurabilityMax.load();
                }
                if (minVal > maxVal) std::swap(minVal, maxVal);

                static std::mt19937 randEngine(std::random_device{}());
                std::uniform_real_distribution<float> dist(minVal, maxVal);
                realPercent = dist(randEngine);
                if (realPercent > 1.0f) realPercent = 1.0f;
                realPercent = RollForLootConditionBonus(a_resolvedBase, realPercent);
            }
            else {
                realPercent = 1.0f;
            }
            float _compressed = GetCompressedPct(a_resolvedBase, realPercent);
            a_refr->extraList->SetHealthPerc(_compressed);
            auto _he = a_refr->extraList->GetByType<RE::ExtraHealth>();
            if (_he) _he->health = _compressed;
        }
    }

    // =========================================================================
    // 玩家背包初始化
    // =========================================================================

    void GrandfatherPlayerInventory()
    {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return;

        for (auto& item : player->inventoryList->data) {
            if (item.object && GetItemFlagsRaw(item.object).enableDurabilitySystem) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    auto healthExtra = stack->extra ? stack->extra->GetByType<RE::ExtraHealth>() : nullptr;
                    if (!stack->extra || !healthExtra || healthExtra->health > ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                        WakeUpAndRandomizeSingleStack(item.object, stack, false);
                    }
                }
            }
        }
    }

    void RandomizeNewPlayerItems()
    {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return;

        auto ui = RE::UI::GetSingleton();
        if (ui) {
            if (ui->GetMenuOpen("ArmorMenu") || ui->GetMenuOpen("WeaponMenu") ||
                ui->GetMenuOpen("WorkshopMenu") || ui->GetMenuOpen("CookingMenu") ||
                ui->GetMenuOpen("ExamineMenu") || ui->GetMenuOpen("ConsoleMenu")) {
                return;
            }
            if (ui->GetMenuOpen("DialogueMenu") && g_mcmSettings.dialogueFullDurability.load()) return;
        }
        GrandfatherPlayerInventory();
    }

    void InitializeEquippedWeapon(RE::Actor* a_actor)
    {
        if (!a_actor || !a_actor->inventoryList) return;

        for (auto& item : a_actor->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                auto weap = item.object->As<RE::TESObjectWEAP>();

                if (!weap || !GetItemFlags(weap).enableDurabilitySystem) continue;
                WeaponProfile prof = GetProfileForWeapon(weap);
                if (prof.isExcluded) continue;

                auto wType = weap->weaponData.type.get();
                if (wType == RE::WEAPON_TYPE::kGrenade || wType == RE::WEAPON_TYPE::kMine) continue;

                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack->IsEquipped()) {
                        if (!stack->extra) stack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());

                        auto healthExtra = stack->extra->GetByType<RE::ExtraHealth>();
                        float realPercent = 1.0f;

                        bool isDefaultEngineHealth = (!healthExtra || healthExtra->health > ENGINE_DEFAULT_HEALTH_THRESHOLD);

                        if (isDefaultEngineHealth) {
                            WakeUpAndRandomizeSingleStack(weap, stack, false);
                            healthExtra = stack->extra->GetByType<RE::ExtraHealth>();
                        }

                        float engineHealth = healthExtra ? healthExtra->health : 0.1f;
                        realPercent = GetDecompressedPct(weap, engineHealth);

                        float currentMax = prof.maxDurability;
                        float currentPtsForUI = std::round(currentMax * realPercent);

                        if (a_actor == RE::PlayerCharacter::GetSingleton()) {
                            UpdateHUDDurabilityWidget(realPercent, currentPtsForUI, true);
                        }
                        return;
                    }
                }
            }
        }
    }
}

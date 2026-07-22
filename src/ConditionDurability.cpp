#include "pch.h"
#include "ConditionCore.h"
#include "ConditionUI.h"
#include "ProfileManager.h"

#include <random>
#include <algorithm>
#include <chrono>
#include <shared_mutex>

#include <F4SE/Trampoline.h>
#include "RE/B/BGSObjectInstance.h"
#include "RE/B/BGSObjectInstanceExtra.h"
#include "RE/B/BGSMod.h"
#include "RE/A/ActorEquipManager.h"

namespace ConditionSystem
{
    // =========================================================================
    // 底层磨损工具函数
    // =========================================================================

    static float ApplyDurabilityDamage(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack, float a_damageCost, float a_maxDurability)
    {
        if (!a_object || !a_stack) return 1.0f;
        if (!GetItemFlags(a_object).enableDurabilitySystem) return 1.0f;

        if (!a_stack->extra) a_stack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());

        auto healthExtra = a_stack->extra->GetByType<RE::ExtraHealth>();
        bool isDefaultEngineHealth = (!healthExtra || healthExtra->health > ENGINE_DEFAULT_HEALTH_THRESHOLD);

        if (isDefaultEngineHealth) {
            WakeUpAndRandomizeSingleStack(a_object, a_stack, false);
            healthExtra = a_stack->extra->GetByType<RE::ExtraHealth>();
        }

        float engineHealth = healthExtra ? healthExtra->health : 0.1f;
        float realPercent = GetDecompressedPct(a_object, engineHealth);

        float currentPts = realPercent * a_maxDurability;
        currentPts -= a_damageCost;

        float newRealPercent = currentPts / a_maxDurability;
        if (newRealPercent < 0.0f) newRealPercent = 0.0f;
        float _compressed = GetCompressedPct(a_object, newRealPercent);
        a_stack->extra->SetHealthPerc(_compressed);
        // 直接覆盖 healthExtra->health，绕过 SetHealthPerc 可能的内部 clamp/重建
        healthExtra = a_stack->extra->GetByType<RE::ExtraHealth>();
        if (healthExtra) healthExtra->health = _compressed;

        return newRealPercent;
    }

    // =========================================================================
    // 武器性能衰减倍率
    // =========================================================================

    float GetWeaponDegradationMultiplier(float durabilityPercent)
    {
        int mode = g_mcmSettings.degradationMode.load();

        if (mode == 1) {
            // ===== FO76 线性衰减模式 =====
            // 耐久度从 100% 线性衰减到 0% 时，性能从 100% 线性下降到 linearMinMult
            float minMult = g_mcmSettings.linearMinMult.load();
            float result = minMult + (1.0f - minMult) * durabilityPercent;
            if (result < minMult) result = minMult;
            if (result > 1.0f) result = 1.0f;
            return result;
        }

        // ===== NV 阶梯式衰减模式 =====
        float threshold = g_mcmSettings.penaltyThreshold.load();
        if (durabilityPercent >= threshold) {
            return 1.0f;  // 高于阈值满效
        }

        float midThreshold = threshold * 0.5f;  // 默认 37.5%
        float midMult = g_mcmSettings.penaltyMultMid.load();
        float lowMult = g_mcmSettings.penaltyMultLow.load();

        if (durabilityPercent >= midThreshold) {
            // 中等耐久区间：阈值 ~ 阈值/2
            return midMult;
        } else if (durabilityPercent > 0.0f) {
            // 低耐久区间：阈值/2 ~ 0
            return lowMult;
        }

        return lowMult;  // 彻底损坏
    }

    // =========================================================================
    // 引擎原生抗性提取 (抛弃盲猜)
    // =========================================================================

    static float GetNativeEffectiveResistance(RE::BGSInventoryItem* a_item, RE::BGSInventoryItem::Stack* a_stack, RE::TESObjectARMO* a_armor, const RE::HitData* a_hitData, const std::vector<std::string>& a_magicKeywords)
    {
        if (!a_item || !a_armor) return 0.0f;

        RE::BSScrapArray<RE::BSTTuple<std::uint32_t, float>> resistValues;
        RE::PipboyInventoryUtils::FillResistTypeInfo(*a_item, a_stack, resistValues, 1.0f);

        float totalResist = 0.0f;
        bool hasTypedDamage = false;

        if (a_hitData && a_hitData->damageTypes) {
            for (const auto& dmgTuple : *a_hitData->damageTypes) {
                if (auto dmgType = dmgTuple.first->As<RE::BGSDamageType>()) {
                    if (auto avInfo = dmgType->data.resistance) {
                        hasTypedDamage = true;
                        std::string avName = avInfo->formEditorID.c_str() ? avInfo->formEditorID.c_str() : "";
                        std::uint32_t idx = 1;
                        if (avName == "PoisonResist") idx = 2;
                        else if (avName == "FireResist") idx = 3;
                        else if (avName == "EnergyResist") idx = 4;
                        else if (avName == "CryoResist") idx = 5;
                        else if (avName == "RadResistExposure" || avName == "RadResistIngestion") idx = 6;

                        for (const auto& res : resistValues) {
                            if (res.first == idx) { totalResist += res.second; break; }
                        }
                    }
                }
            }
        }
        else if (!a_magicKeywords.empty()) {
            for (const auto& kw : a_magicKeywords) {
                if (kw.find("DamageType") != std::string::npos && kw.find("DamageTypePhysical") == std::string::npos) {
                    hasTypedDamage = true;
                    std::uint32_t idx = 999;
                    if (kw.find("Energy") != std::string::npos || kw.find("Electrical") != std::string::npos) idx = 4;
                    else if (kw.find("Radiation") != std::string::npos) idx = 6;
                    else if (kw.find("Poison") != std::string::npos || kw.find("Acid") != std::string::npos) idx = 2;
                    else if (kw.find("Fire") != std::string::npos) idx = 3;
                    else if (kw.find("Cryo") != std::string::npos || kw.find("Frost") != std::string::npos) idx = 5;

                    for (const auto& res : resistValues) {
                        if (res.first == idx) { totalResist += res.second; break; }
                    }
                }
            }
        }

        if (!hasTypedDamage || (a_hitData && a_hitData->physicalDamage > 0.0f)) {
            float physResist = static_cast<float>(a_armor->armorData.rating);
            for (const auto& res : resistValues) {
                if (res.first == 1) { physResist = res.second; break; }
            }
            totalResist += physResist;
        }

        // 核心实战减伤：如果护甲耐久低于 50%，防御力随之崩塌
        float currentDurability = GetStackDurabilityPercent(a_stack);
        if (currentDurability < 0.5f) {
            float penaltyMult = currentDurability * 2.0f; // 50%耐久=100%抗性，25%耐久=50%抗性，0%耐久=0抗性
            if (penaltyMult < 0.0f) penaltyMult = 0.0f;
            totalResist *= penaltyMult;

            if (g_mcmSettings.enableLogging.load()) {
                REX::INFO("  -> [破损惩罚] 护甲耐久不足 50% (当前:{:.1f}%), 原生抗性衰减至原本的 {:.1f}%", currentDurability * 100.0f, penaltyMult * 100.0f);
            }
        }

        return totalResist;
    }

    // =========================================================================
    // 共享工具：遍历 ExtraDataList 上的所有 omod，累积磨损效果
    // =========================================================================

    struct OmodAccumulator {
        float degradeMult = 1.0f;         // 累乘
        float degradeRateFlat = 0.0f;     // 累加
        float maxDurabilityMult = 1.0f;   // 累乘
        bool immuneToJam = false;         // 逻辑或
    };

    static OmodAccumulator AccumulateOmodEffects(RE::ExtraDataList* a_extra, const char* a_logPrefix)
    {
        OmodAccumulator result;
        if (!a_extra) return result;

        auto objInstExtra = a_extra->GetByType<RE::BGSObjectInstanceExtra>();
        if (!objInstExtra) return result;
        if (!objInstExtra->values || objInstExtra->itemIndex == static_cast<std::uint16_t>(-1)) return result;

        auto idxDataSpan = objInstExtra->GetIndexData();
        for (const auto& idxData : idxDataSpan) {
            if (idxData.disabled) continue;
            auto omodForm = RE::TESForm::GetFormByID(idxData.objectID);
            if (!omodForm) continue;
            auto omod = omodForm->As<RE::BGSMod::Attachment::Mod>();
            if (!omod) continue;

            OmodProfile omodProf = GetProfileForOmod(omod);
            result.degradeMult *= omodProf.degradeMult;
            result.degradeRateFlat += omodProf.degradeRateFlat;
            result.maxDurabilityMult *= omodProf.maxDurabilityMult;
            if (omodProf.immuneToJam) result.immuneToJam = true;

            if (g_mcmSettings.enableLogging.load()) {
                REX::INFO("  -> [{}] {}: degradeMult={:.2f}, rateFlat={:.2f}, maxDurMult={:.2f}{}",
                    a_logPrefix, omodProf.name, omodProf.degradeMult, omodProf.degradeRateFlat, omodProf.maxDurabilityMult,
                    omodProf.immuneToJam ? ", immuneToJam=true" : "");
            }
        }
        return result;
    }

    // =========================================================================
    // 共享工具：耐久归零时发送 HUD 消息并钳制 ExtraHealth
    // =========================================================================

    static void NotifyDurabilityDepleted(
        RE::Actor* a_actor,
        RE::BGSInventoryItem::Stack* a_stack,
        const char* a_hudMessage,
        float a_clampHealth)
    {
        if (a_actor == RE::PlayerCharacter::GetSingleton()) {
            RE::SendHUDMessage::ShowHUDMessage(a_hudMessage, "UIActionDeny", true, true);
        }
        if (a_stack && a_stack->extra) {
            auto healthExtra = a_stack->extra->GetByType<RE::ExtraHealth>();
            if (healthExtra) {
                healthExtra->health = a_clampHealth;
            } else {
                a_stack->extra->SetHealthPerc(a_clampHealth);
            }
        }
    }

    // =========================================================================
    // 武器耐久扣除 (远程/近战)
    // =========================================================================

    void DeductEquippedWeaponDurability(RE::Actor* a_actor, bool a_requireMelee)
    {
        if (!a_actor || !a_actor->inventoryList) return;

        for (auto& item : a_actor->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                auto weap = item.object->As<RE::TESObjectWEAP>();
                if (!weap || !GetItemFlags(weap).enableDurabilitySystem) continue;

                WeaponProfile currentWeaponProfile = GetProfileForWeapon(weap);
                if (currentWeaponProfile.isExcluded) continue;

                auto wType = weap->weaponData.type.get();
                if (wType == RE::WEAPON_TYPE::kGrenade || wType == RE::WEAPON_TYPE::kMine) continue;

                bool isMelee = (weap->weaponData.ammo == nullptr);
                if (a_requireMelee && !isMelee) continue;
                if (!a_requireMelee && isMelee) continue;

                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack->IsEquipped()) {
                        float attachDegradeMult = 1.0f;
                        bool immuneToJam = false;
                        float omodDegradeRateFlat = 0.0f;
                        float omodMaxDurabilityMult = 1.0f;
                        RE::TESAmmo* currentAmmo = nullptr;

                        {
                            std::unique_lock<std::shared_mutex> lock(g_cacheMutex);
                            if (!stack->extra) stack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());
                            auto instExtra = stack->extra->GetByType<RE::ExtraInstanceData>();

                            auto legMod = stack->extra->GetLegendaryMod();
                            if (legMod) {
                                auto legForm = (RE::TESForm*)legMod;
                                ModifierProfile mProf = GetProfileForModifier(legForm);
                                attachDegradeMult *= mProf.degradeMult;
                                if (mProf.immuneToJam) immuneToJam = true;
                            }

                            // 共享 omod 遍历
                            auto omodResult = AccumulateOmodEffects(stack->extra.get(), "OMOD");
                            attachDegradeMult *= omodResult.degradeMult;
                            omodDegradeRateFlat += omodResult.degradeRateFlat;
                            omodMaxDurabilityMult *= omodResult.maxDurabilityMult;
                            if (omodResult.immuneToJam) immuneToJam = true;

                            if (instExtra && instExtra->data) {
                                auto instData = static_cast<RE::TESObjectWEAP::InstanceData*>(instExtra->data.get());
                                if (instData->keywords) {
                                    std::shared_lock<std::shared_mutex> pLock(g_profileMutex);
                                    for (const auto& prof : g_modifierProfiles) {
                                        if (prof.keywords.empty()) continue;
                                        for (const auto& andGroup : prof.keywords) {
                                            bool andMatched = true;
                                            for (const auto& kwd : andGroup) {
                                                bool foundKwd = false;
                                                for (std::uint32_t i = 0; i < instData->keywords->numKeywords; ++i) {
                                                    if (instData->keywords->keywords && instData->keywords->keywords[i]) {
                                                        const char* edid = instData->keywords->keywords[i]->GetFormEditorID();
                                                        if (edid && _stricmp(edid, kwd.c_str()) == 0) {
                                                            foundKwd = true; break;
                                                        }
                                                    }
                                                }
                                                if (!foundKwd) { andMatched = false; break; }
                                            }
                                            if (andMatched) {
                                                attachDegradeMult *= prof.degradeMult;
                                                if (prof.immuneToJam) immuneToJam = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                currentAmmo = instData->ammo;
                            }
                        }

                        if (!currentAmmo) currentAmmo = weap->weaponData.ammo;
                        AmmoProfile currentAmmoProfile = GetProfileForAmmo(currentAmmo);

                        float durabilityCost = currentWeaponProfile.degradeRate * attachDegradeMult * currentAmmoProfile.weaponWearMult;
                        if (currentWeaponProfile.degradeRate == 0.0f || attachDegradeMult == 0.0f) durabilityCost = 0.0f;
                        durabilityCost += omodDegradeRateFlat;
                        if (durabilityCost < 0.0f) durabilityCost = 0.0f;

                        float currentMax = currentWeaponProfile.maxDurability * omodMaxDurabilityMult;
                        float finalRealPercent = ApplyDurabilityDamage(weap, stack, durabilityCost, currentMax);
                        float remainingPoints = std::round(finalRealPercent * currentMax);

                        if (a_actor == RE::PlayerCharacter::GetSingleton()) {
                            UpdateHUDDurabilityWidget(
                                (std::max)(finalRealPercent, 0.0f),
                                (std::max)(remainingPoints, 0.0f),
                                false);

                            if (g_mcmSettings.enableLogging.load()) {
                                float degradeMult = GetWeaponDegradationMultiplier(finalRealPercent);
                                int mode = g_mcmSettings.degradationMode.load();
                                const char* modeName = (mode == 1) ? "FO76线性" : "NV阶梯";
                                REX::INFO("  -> [衰减模式][{}] 当前耐久:{:.1f}% -> 性能倍率:{:.2f}x", modeName, finalRealPercent * 100.0f, degradeMult);
                            }
                        }

                        // 武器报废判定
                        if (remainingPoints <= 0.0f) {
                            if (g_mcmSettings.enableLogging.load()) {
                                REX::INFO("  -> [武器报废] 武器耐久归零，执行强制卸载！");
                            }

                            NotifyDurabilityDepleted(a_actor, stack, "$CSF_WeaponDestroyed", -ARMOR_MIN_ENGINE_HEALTH);

                            auto taskActorHandle = a_actor->GetHandle();
                            auto taskWeapFormID = weap->GetFormID();

                            if (auto task = F4SE::GetTaskInterface()) {
                                task->AddTask([taskActorHandle, taskWeapFormID]() {
                                    auto ref = taskActorHandle.get();
                                    auto actor = ref ? ref->As<RE::Actor>() : nullptr;
                                    auto form = RE::TESForm::GetFormByID(taskWeapFormID);

                                    if (actor && form) {
                                        auto equipMgr = RE::ActorEquipManager::GetSingleton();
                                        if (equipMgr) {
                                            RE::BGSObjectInstance obj{ form, nullptr };
                                            equipMgr->UnequipObject(actor, &obj, 1, nullptr, 0xFFFFFFFF, false, true, false, true, nullptr);
                                        }
                                    }
                                    });
                            }
                            g_isWeaponJammed.store(false);
                            g_jammedWeaponUniqueID.store(0);
                            return;
                        }

                        bool canWeaponJam = currentWeaponProfile.canJam && !immuneToJam;
                        float jamThreshold = g_mcmSettings.jamThreshold.load();

                        if (ConditionSystem::g_mcmSettings.iJammingPhase.load() == 0) {
                            if (canWeaponJam && jamThreshold > 0.0f && a_actor == RE::PlayerCharacter::GetSingleton() && finalRealPercent < jamThreshold && finalRealPercent > 0.0f) {
                                float maxJamChance = g_mcmSettings.maxJamChance.load();
                                float currentJamChance = ((jamThreshold - finalRealPercent) / jamThreshold) * maxJamChance;
                                static std::mt19937 randEngine(std::random_device{}());
                                std::uniform_real_distribution<float> randDist(0.0f, 1.0f);
                                if (randDist(randEngine) < currentJamChance) {
                                    g_isWeaponJammed.store(true);
                                    g_jammedWeaponUniqueID.store(reinterpret_cast<std::uintptr_t>(stack));
                                    RE::SendHUDMessage::ShowHUDMessage("$CSF_JammedNeedReload", "WPNPistol10mmFireDry", true, true);
                                }
                            }
                        }
                        return;
                    }
                }
            }
        }
    }

    // =========================================================================
    // 共享工具：检测玩家是否在动力甲中，并返回各部位的覆盖状态
    // =========================================================================

    struct PACoverageResult {
        bool isInPAFrame;
        std::array<bool, 6> paCovers;
    };

    static PACoverageResult DetectPowerArmorCoverage(RE::Actor* a_actor)
    {
        PACoverageResult result{ false, {false, false, false, false, false, false} };
        if (a_actor != RE::PlayerCharacter::GetSingleton()) return result;

        result.isInPAFrame = RE::PowerArmor::PlayerInPowerArmor();
        if (!result.isInPAFrame || !a_actor->inventoryList) return result;

        for (auto& item : a_actor->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kARMO)) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    bool isEffectivelyEquipped = stack->IsEquipped() || stack->flags.any(RE::BGSInventoryItem::Stack::Flag::kInvShouldEquip);
                    if (!isEffectivelyEquipped) continue;

                    auto armor = item.object->As<RE::TESObjectARMO>();
                    if (armor && HasKeywordString(armor, nullptr, "ArmorTypePower")) {
                        if (HasKeywordString(armor, nullptr, "dn_PowerArmor_Helmet")) result.paCovers[0] = true;
                        if (HasKeywordString(armor, nullptr, "dn_PowerArmor_Torso")) result.paCovers[1] = true;
                        if (HasKeywordString(armor, nullptr, "dn_PowerArmor_LeftArm")) result.paCovers[2] = true;
                        if (HasKeywordString(armor, nullptr, "dn_PowerArmor_RightArm")) result.paCovers[3] = true;
                        if (HasKeywordString(armor, nullptr, "dn_PowerArmor_LeftLeg")) result.paCovers[4] = true;
                        if (HasKeywordString(armor, nullptr, "dn_PowerArmor_RightLeg")) result.paCovers[5] = true;
                    }
                }
            }
        }
        return result;
    }

    // =========================================================================
    // 共享工具：从玩家背包收集受击装甲，返回命中装甲列表和外层覆盖状态
    // =========================================================================

    struct HitArmorEntry {
        RE::BGSInventoryItem* item;
        RE::TESObjectARMO* armor;
        RE::BGSInventoryItem::Stack* stack;
        ArmorProfile profile;
        bool isInner;
    };

    struct ArmorCollectionResult {
        std::vector<HitArmorEntry> hitArmors;
        std::array<bool, 6> outerCovers;
    };

    static ArmorCollectionResult CollectHitArmors(RE::Actor* a_actor, int mainZone, bool isExplosion)
    {
        ArmorCollectionResult result{ {}, {false, false, false, false, false, false} };
        if (!a_actor || !a_actor->inventoryList) return result;

        for (auto& item : a_actor->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kARMO)) {
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    bool isEffectivelyEquipped = stack->IsEquipped() || stack->flags.any(RE::BGSInventoryItem::Stack::Flag::kInvShouldEquip);
                    if (!isEffectivelyEquipped) continue;

                    auto armor = item.object->As<RE::TESObjectARMO>();
                    if (!armor || HasKeywordString(armor, nullptr, "ArmorTypePower") || !GetItemFlags(armor).enableDurabilitySystem) continue;

                    ArmorProfile currentArmorProfile = GetProfileForArmor(armor);
                    if (currentArmorProfile.isExcluded) continue;

                    if (stack->count > 1) continue;

                    std::uint32_t slots = static_cast<std::uint32_t>(armor->bipedModelData.bipedObjectSlots);
                    auto hasSlot = [slots](int slotId) -> bool { return slotId >= 30 && (slots & (1 << (slotId - 30))) != 0; };

                    bool isInner = hasSlot(33);
                    bool zoneMatched = false;

                    if (isExplosion) {
                        zoneMatched = true;
                    } else {
                        switch (mainZone) {
                        case 0: if (hasSlot(30) || hasSlot(31) || hasSlot(32) || hasSlot(46) || hasSlot(47)) zoneMatched = true; break;
                        case 1: if (isInner || hasSlot(36) || hasSlot(41) || hasSlot(34) || hasSlot(35)) zoneMatched = true; break;
                        case 2: if (hasSlot(37) || hasSlot(42)) zoneMatched = true; break;
                        case 3: if (hasSlot(38) || hasSlot(43)) zoneMatched = true; break;
                        case 4: if (hasSlot(39) || hasSlot(44)) zoneMatched = true; break;
                        case 5: if (hasSlot(40) || hasSlot(45)) zoneMatched = true; break;
                        }
                    }

                    if (zoneMatched) {
                        if (!isInner) {
                            if (isExplosion) {
                                for (int i = 0; i < 6; i++) result.outerCovers[i] = true;
                            } else {
                                result.outerCovers[mainZone] = true;
                            }
                        }
                        result.hitArmors.push_back({ &item, armor, stack, currentArmorProfile, isInner });
                    }
                }
            }
        }
        return result;
    }

    // =========================================================================
    // 物理受击核心：护甲耐久扣除 (原生抗性 + 动力甲 + 次世代公式)
    // =========================================================================

    void DeductEquippedArmorDurability(RE::Actor* a_actor, const RE::TESHitEvent& a_event)
    {
        if (!a_actor || !a_actor->inventoryList) return;

        float baseDamage = 0.0f;
        bool isExplosion = false;

        if (a_event.usesHitData) {
            float hitTotal = a_event.hitData.totalDamage;
            if (hitTotal <= 0.0f) hitTotal = a_event.hitData.healthDamage;
            if (hitTotal <= 0.0f) hitTotal = a_event.hitData.physicalDamage;
            if (hitTotal > 0.0f) baseDamage = hitTotal;
            if (a_event.hitData.flags.any(RE::HitData::Flag::kExplosion)) isExplosion = true;
        }

        auto sourceForm = RE::TESForm::GetFormByID(a_event.sourceFormID);
        auto projForm = RE::TESForm::GetFormByID(a_event.projectileFormID);

        if (!sourceForm && a_event.usesHitData && a_event.hitData.sourceRef) {
            auto ref = a_event.hitData.sourceRef.get();
            if (ref) sourceForm = ref->GetObjectReference();
        }

        if (sourceForm && sourceForm->Is(RE::ENUM_FORM_ID::kEXPL)) {
            isExplosion = true;
            if (!a_event.usesHitData) {
                if (auto expl = sourceForm->As<RE::BGSExplosion>()) baseDamage = expl->data.damage;
            }
        }

        std::vector<std::string> damageKeywords;
        RE::TESForm* formToCheck = sourceForm ? sourceForm : projForm;

        if (formToCheck) {
            damageKeywords = ExtractKeywords(formToCheck);

            if (baseDamage <= 2.0f) {
                if (auto weap = formToCheck->As<RE::TESObjectWEAP>()) {
                    auto wType = weap->weaponData.type.get();
                    if (wType == RE::WEAPON_TYPE::kGrenade || wType == RE::WEAPON_TYPE::kMine) {
                        float explDamage = 0.0f;
                        if (weap->weaponData.rangedData && weap->weaponData.rangedData->overrideProjectile) {
                            auto proj = weap->weaponData.rangedData->overrideProjectile;
                            if (proj->data.explosionType) explDamage = proj->data.explosionType->data.damage;
                        }
                        if (isExplosion) baseDamage = explDamage > 0.0f ? explDamage : 100.0f;
                        else baseDamage = static_cast<float>(weap->weaponData.attackDamage);
                    }
                    else {
                        baseDamage = static_cast<float>(weap->weaponData.attackDamage);
                    }
                }
                else if (auto ammoForm = formToCheck->As<RE::TESAmmo>()) baseDamage = ammoForm->data.damage;
                else if (formToCheck->Is(RE::ENUM_FORM_ID::kPROJ)) baseDamage = 50.0f;
                else if (formToCheck->Is(RE::ENUM_FORM_ID::kSPEL) || formToCheck->Is(RE::ENUM_FORM_ID::kHAZD)) baseDamage = 20.0f;
            }
        }

        if (baseDamage <= 0.0f) baseDamage = isExplosion ? 50.0f : 1.0f;
        if (baseDamage <= 1.5f && !isExplosion) return;

        float armorDamageMult = 1.0f;
        auto cause = a_event.cause.get();
        if (cause) {
            if (auto attacker = cause->As<RE::Actor>()) {
                if (attacker->inventoryList) {
                    for (auto& item : attacker->inventoryList->data) {
                        if (item.object && item.object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                            for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                                if (stack->IsEquipped()) {
                                    RE::TESAmmo* ammo = nullptr;
                                    if (stack->extra) {
                                        if (auto instExtra = stack->extra->GetByType<RE::ExtraInstanceData>()) {
                                            if (instExtra->data) ammo = static_cast<RE::TESObjectWEAP::InstanceData*>(instExtra->data.get())->ammo;
                                        }
                                    }
                                    if (!ammo) {
                                        auto w = item.object->As<RE::TESObjectWEAP>();
                                        if (w) ammo = w->weaponData.ammo;
                                    }
                                    if (ammo) armorDamageMult = GetProfileForAmmo(ammo).armorDamageMult;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        float totalImpact = baseDamage * armorDamageMult;
        if (totalImpact <= 0.1f) totalImpact = 1.0f;

        RE::BGSBodyPartDefs::LIMB_ENUM precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kTorso;
        std::string limbName = "躯干";
        int mainZone = 1;

        if (!isExplosion && a_event.usesHitData) {
            precisionLimb = a_event.hitData.damageLimb.get();
            switch (precisionLimb) {
            case RE::BGSBodyPartDefs::LIMB_ENUM::kHead1:
            case RE::BGSBodyPartDefs::LIMB_ENUM::kHead2:
            case RE::BGSBodyPartDefs::LIMB_ENUM::kEye1:
                limbName = "头部"; mainZone = 0; break;
            case RE::BGSBodyPartDefs::LIMB_ENUM::kTorso:
                limbName = "躯干"; mainZone = 1; break;
            case RE::BGSBodyPartDefs::LIMB_ENUM::kLeftArm1:
            case RE::BGSBodyPartDefs::LIMB_ENUM::kLeftArm2:
                limbName = "左臂"; mainZone = 2; break;
            case RE::BGSBodyPartDefs::LIMB_ENUM::kRightArm1:
            case RE::BGSBodyPartDefs::LIMB_ENUM::kRightArm2:
                limbName = "右臂"; mainZone = 3; break;
            case RE::BGSBodyPartDefs::LIMB_ENUM::kLeftLeg1:
            case RE::BGSBodyPartDefs::LIMB_ENUM::kLeftLeg2:
            case RE::BGSBodyPartDefs::LIMB_ENUM::kLeftLeg3:
                limbName = "左腿"; mainZone = 4; break;
            case RE::BGSBodyPartDefs::LIMB_ENUM::kRightLeg1:
            case RE::BGSBodyPartDefs::LIMB_ENUM::kRightLeg2:
            case RE::BGSBodyPartDefs::LIMB_ENUM::kRightLeg3:
                limbName = "右腿"; mainZone = 5; break;
            default:
                limbName = "躯干(代偿)"; mainZone = 1; break;
            }
        }
        else if (!isExplosion) {
            static std::mt19937 randEngine(std::random_device{}());
            std::uniform_int_distribution<int> dist(1, 100);
            int roll = dist(randEngine);
            if (roll <= 10) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kHead1; limbName = "头部"; mainZone = 0; }
            else if (roll <= 50) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kTorso; limbName = "躯干"; mainZone = 1; }
            else if (roll <= 65) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kLeftArm1; limbName = "左臂"; mainZone = 2; }
            else if (roll <= 80) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kRightArm1; limbName = "右臂"; mainZone = 3; }
            else if (roll <= 90) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kLeftLeg1; limbName = "左腿"; mainZone = 4; }
            else { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kRightLeg1; limbName = "右腿"; mainZone = 5; }
        }

        if (g_mcmSettings.enableLogging.load()) {
            REX::INFO("[事件拦截] 遭遇物理打击！最终穿甲力:{:.1f}。精密肢体:[{}]", totalImpact, isExplosion ? "全身(大范围爆炸)" : limbName);
        }        auto [isInPAFrame, paCovers] = DetectPowerArmorCoverage(a_actor);
        auto [hitArmorsVec, outerCovers] = CollectHitArmors(a_actor, mainZone, isExplosion);
        auto& hitArmors = hitArmorsVec;

        for (auto& hitData : hitArmors) {
            std::string armorName = "未知服装";
            if (auto fullName = hitData.armor->As<RE::TESFullName>()) {
                if (fullName->GetFullName()) armorName = GameUTF8ToLocal(fullName->GetFullName());
            }

            int actualHitZone = mainZone;
            if (isExplosion) {
                if (hitData.isInner) {
                    actualHitZone = 1;
                }
                else {
                    std::uint32_t slots = static_cast<std::uint32_t>(hitData.armor->bipedModelData.bipedObjectSlots);
                    if (slots & (1 << (30 - 30)) || slots & (1 << (31 - 30))) actualHitZone = 0;
                    else if (slots & (1 << (37 - 30)) || slots & (1 << (42 - 30))) actualHitZone = 2;
                    else if (slots & (1 << (38 - 30)) || slots & (1 << (43 - 30))) actualHitZone = 3;
                    else if (slots & (1 << (39 - 30)) || slots & (1 << (44 - 30))) actualHitZone = 4;
                    else if (slots & (1 << (40 - 30)) || slots & (1 << (45 - 30))) actualHitZone = 5;
                    else actualHitZone = 1;
                }
            }

            if (isInPAFrame && paCovers[actualHitZone]) {
                if (g_mcmSettings.enableLogging.load()) REX::INFO("  -> [动力甲绝对防护] 完美吸收了打向内部 [{}] 的伤害！", armorName);
                continue;
            }

            float paFrameMultiplier = 1.0f;
            if (isInPAFrame) paFrameMultiplier = 0.5f;

            float layerMultiplier = 1.0f;
            if (hitData.isInner && outerCovers[actualHitZone]) layerMultiplier = 0.15f;

            float durabilityCost = 0.01f;
            if (hitData.profile.useFlatDegrade) {
                durabilityCost = hitData.profile.degradeRate * armorDamageMult * layerMultiplier * paFrameMultiplier;
                if (g_mcmSettings.enableLogging.load()) REX::INFO("  -> [固定磨损] 触发，忽略机制使用固定值。");
            }
            else {
                if (a_event.usesHitData) {
                    float physResisted = a_event.hitData.resistedPhysicalDamage;
                    float typeResisted = a_event.hitData.resistedTypedDamage;
                    float totalResisted = physResisted + typeResisted;

                    float totalRawDamage = a_event.hitData.totalDamage;
                    if (totalRawDamage <= 0.0f) {
                        totalRawDamage = a_event.hitData.healthDamage + totalResisted;
                    }
                    if (totalRawDamage <= 0.0f) totalRawDamage = 1.0f;

                    float blockPercentage = totalResisted / totalRawDamage;
                    blockPercentage = std::clamp(blockPercentage, 0.0f, 1.0f);

                    float scratchMultiplier = 1.0f;
                    std::string resultStr = "常规拦截吸收";

                    if (blockPercentage >= g_damageFormula.glanceThreshold || blockPercentage >= 0.90f) {
                        resultStr = "完美防御/装甲全额承受冲击";
                        scratchMultiplier = g_damageFormula.glanceMultiplier;
                    }
                    else if (blockPercentage <= g_damageFormula.scratchThreshold || blockPercentage <= 0.20f) {
                        resultStr = "护甲被贯穿/未起到有效拦截";
                        scratchMultiplier = g_damageFormula.scratchMultiplier;
                    }

                    durabilityCost = totalResisted * g_damageFormula.durabilityDamageConstant * scratchMultiplier * layerMultiplier * paFrameMultiplier * armorDamageMult;

                    if (g_mcmSettings.enableLogging.load()) {
                        REX::INFO("  -> [次世代拦截结算] 原始总伤: {:.1f} | 真实吸收: {:.1f} (拦截率: {:.1f}%)", totalRawDamage, totalResisted, blockPercentage * 100.0f);
                        REX::INFO("  -> [装甲损耗判定] {} -> 状态: [{}], 最终扣除外壳耐久: {:.3f}", armorName, resultStr, durabilityCost);
                    }
                }
                else {
                    const RE::HitData* resistHitData = (!isExplosion && a_event.usesHitData) ? &a_event.hitData : nullptr;
                    float effectiveResist = GetNativeEffectiveResistance(hitData.item, hitData.stack, hitData.armor, resistHitData, damageKeywords);
                    float hardness = g_damageFormula.baseHardness + effectiveResist;
                    if (hardness < 1.0f) hardness = 1.0f;

                    durabilityCost = (totalImpact / hardness) * g_damageFormula.durabilityDamageConstant * layerMultiplier * paFrameMultiplier * armorDamageMult;

                    if (g_mcmSettings.enableLogging.load()) {
                        REX::INFO("  -> [次世代引擎抗性联动] 提取护甲有效抗性: {:.1f} -> 计算得出最终硬度: {:.1f}", effectiveResist, hardness);
                        REX::INFO("  -> [装甲损耗判定] {} -> 扣除护甲耐久基数: {:.3f}", armorName, durabilityCost);
                    }
                }
            }

            // 共享 omod 遍历：护甲上已安装的改装件影响耐久损耗
            auto omodResult = AccumulateOmodEffects(hitData.stack->extra ? hitData.stack->extra.get() : nullptr, "OMOD-护甲");
            durabilityCost = durabilityCost * omodResult.degradeMult + omodResult.degradeRateFlat;
            if (durabilityCost < 0.01f) durabilityCost = 0.01f;
            hitData.profile.maxDurability *= omodResult.maxDurabilityMult;

            float finalRealPercent = ApplyDurabilityDamage(hitData.armor, hitData.stack, durabilityCost, hitData.profile.maxDurability);
            float remainingPoints = finalRealPercent * hitData.profile.maxDurability;

            std::string layerTypeLog = hitData.isInner ? "内衬软甲" : "外层硬甲";
            if (g_mcmSettings.enableLogging.load()) {
                REX::INFO("  -> [最终结算] [{}] ({}) 最终扣除:{:.2f}, 当前剩余:{:.1f}%", armorName, layerTypeLog, durabilityCost, finalRealPercent * 100.0f);
            }

            // 护甲耐久归零保护：钳制到最低安全值，不再强制卸载
            if (remainingPoints <= 0.0f) {
                CS_LOG("  -> [耐久耗尽] [{}] 护甲已完全磨损(剩余0%)，但仍可穿戴。", armorName);
                NotifyDurabilityDepleted(a_actor, hitData.stack, "$CSF_ArmorWornOut", ARMOR_MIN_ENGINE_HEALTH);
                // 不再强制卸载，玩家可继续穿戴残废护甲（属性衰减由 GetWeaponDegradationMultiplier 处理）
            }
        }
    }

    // =========================================================================
    // 魔法/环境侵蚀核心
    // =========================================================================

    void DeductEquippedArmorDurabilityFromMagic(RE::Actor* a_actor, const RE::TESMagicEffectApplyEvent& a_event)
    {
        if (!a_actor || !a_actor->inventoryList) return;

        auto effectForm = RE::TESForm::GetFormByID(a_event.magicEffectFormID);
        if (!effectForm) return;

        std::vector<std::string> damageKeywords = ExtractKeywords(effectForm);
        std::string damageName = "Unknown Magic";
        if (auto fullName = effectForm->As<RE::TESFullName>()) {
            if (fullName->GetFullName()) damageName = GameUTF8ToLocal(fullName->GetFullName());
        }

        bool hasElementalKeyword = false;
        for (const auto& kw : damageKeywords) {
            if (kw.find("DamageType") != std::string::npos && kw.find("DamageTypePhysical") == std::string::npos) {
                hasElementalKeyword = true; break;
            }
            if (kw.find("Hazard") != std::string::npos) {
                hasElementalKeyword = true; break;
            }
        }

        std::string lowerName = damageName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        bool hasDamageName = (lowerName.find("fire") != std::string::npos || lowerName.find("burn") != std::string::npos ||
            lowerName.find("poison") != std::string::npos || lowerName.find("acid") != std::string::npos ||
            lowerName.find("radiation") != std::string::npos || lowerName.find("rads") != std::string::npos ||
            lowerName.find("cryo") != std::string::npos || lowerName.find("electric") != std::string::npos ||
            lowerName.find("火") != std::string::npos || lowerName.find("毒") != std::string::npos ||
            lowerName.find("酸") != std::string::npos || lowerName.find("辐射") != std::string::npos);

        if (!hasElementalKeyword && !hasDamageName) return;

        static auto s_lastMagicHitTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastMagicHitTime).count() < 1000) return;
        s_lastMagicHitTime = now;

        float totalImpact = 4.0f;

        RE::BGSBodyPartDefs::LIMB_ENUM precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kTorso;
        std::string limbName = "躯干";
        int mainZone = 1;
        static std::mt19937 randEngine(std::random_device{}());
        std::uniform_int_distribution<int> dist(1, 100);
        int roll = dist(randEngine);
        if (roll <= 10) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kHead1; limbName = "头部"; mainZone = 0; }
        else if (roll <= 50) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kTorso; limbName = "躯干"; mainZone = 1; }
        else if (roll <= 65) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kLeftArm1; limbName = "左臂"; mainZone = 2; }
        else if (roll <= 80) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kRightArm1; limbName = "右臂"; mainZone = 3; }
        else if (roll <= 90) { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kLeftLeg1; limbName = "左腿"; mainZone = 4; }
        else { precisionLimb = RE::BGSBodyPartDefs::LIMB_ENUM::kRightLeg1; limbName = "右腿"; mainZone = 5; }

        if (g_mcmSettings.enableLogging.load()) {
            REX::INFO("[事件拦截] 遭到魔法/环境 [{}] 侵蚀！最终环境冲击力:{:.1f}。受蚀精密区域:[{}], 开始结算...", damageName, totalImpact, limbName);
        }

        auto [isInPAFrame, paCovers] = DetectPowerArmorCoverage(a_actor);
        auto [hitArmorsVec, outerCovers] = CollectHitArmors(a_actor, mainZone, false); // isExplosion = false
        auto& hitArmors = hitArmorsVec;

        for (auto& hitData : hitArmors) {
            std::string armorName = "未知服装";
            if (auto fullName = hitData.armor->As<RE::TESFullName>()) {
                if (fullName->GetFullName()) armorName = GameUTF8ToLocal(fullName->GetFullName());
            }

            int actualHitZone = mainZone;

            if (isInPAFrame && paCovers[actualHitZone]) {
                if (g_mcmSettings.enableLogging.load()) REX::INFO("  -> [动力甲绝对防护] 动力甲片完美阻绝了外界侵蚀！");
                continue;
            }

            float paFrameMultiplier = 1.0f;
            if (isInPAFrame) paFrameMultiplier = 0.5f;

            float layerMultiplier = 1.0f;
            if (hitData.isInner && outerCovers[actualHitZone]) layerMultiplier = 0.15f;

            float durabilityCost = 0.01f;

            if (hitData.profile.useFlatDegrade) {
                durabilityCost = hitData.profile.degradeRate * layerMultiplier * paFrameMultiplier;
            }
            else {
                float effectiveResist = GetNativeEffectiveResistance(hitData.item, hitData.stack, hitData.armor, nullptr, damageKeywords);
                float hardness = g_damageFormula.baseHardness + effectiveResist;
                if (hardness < 1.0f) hardness = 1.0f;
                durabilityCost = (totalImpact / hardness) * g_damageFormula.durabilityDamageConstant * layerMultiplier * paFrameMultiplier;

                if (g_mcmSettings.enableLogging.load()) {
                    REX::INFO("  -> [原生元素抗性联动] 提取护甲有效元素抗性: {:.1f} -> 计算得出魔法硬度: {:.1f}", effectiveResist, hardness);
                    REX::INFO("  -> [装甲元素损耗判定] {} -> 最终扣除绝缘层耐久基数: {:.3f}", armorName, durabilityCost);
                }
            }

            // 共享 omod 遍历：护甲上已安装的改装件影响耐久损耗（魔法/环境版）
            auto omodResult = AccumulateOmodEffects(hitData.stack->extra ? hitData.stack->extra.get() : nullptr, "OMOD-护甲/元素");
            durabilityCost = durabilityCost * omodResult.degradeMult + omodResult.degradeRateFlat;
            if (durabilityCost < 0.01f) durabilityCost = 0.01f;
            hitData.profile.maxDurability *= omodResult.maxDurabilityMult;

            float finalRealPercent = ApplyDurabilityDamage(hitData.armor, hitData.stack, durabilityCost, hitData.profile.maxDurability);
            float remainingPoints = finalRealPercent * hitData.profile.maxDurability;

            std::string layerTypeLog = hitData.isInner ? "内衬软甲" : "外层硬甲";

            if (g_mcmSettings.enableLogging.load()) {
                REX::INFO("  -> [元素磨损结算] [{}] ({}) 最终扣除:{:.2f}, 当前剩余:{:.1f}%", armorName, layerTypeLog, durabilityCost, finalRealPercent * 100.0f);
            }

            // 护甲耐久归零保护：钳制到最低安全值，不再强制卸载
            if (remainingPoints <= 0.0f) {
                CS_LOG("  -> [耐久耗尽] [{}] 护甲已被元素彻底侵蚀(剩余0%)，但仍可穿戴。", armorName);
                NotifyDurabilityDepleted(a_actor, hitData.stack, "$CSF_ArmorWornOut", ARMOR_MIN_ENGINE_HEALTH);
                // 不再强制卸载，玩家可继续穿戴残废护甲（属性衰减由 GetWeaponDegradationMultiplier 处理）
            }
        }
    }
}

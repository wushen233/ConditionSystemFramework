#include "pch.h"
#include "RepairSystem.h"
#include "../ConditionCore.h"
#include "../ConditionUI.h"
#include "../ProfileManager.h" 
#include "../ConditionWorkbench.h"
#include "../Translation.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <fstream>
#include <filesystem>
#include <cstdio> 

#include <algorithm>
#include <string>
#include <cstdint>
#include <thread>
#include <chrono>
#include <utility>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <iomanip>

namespace ConditionSystem::Repair
{
    std::atomic<RE::TESBoundObject*> g_lockedTargetObject{ nullptr };
    std::atomic<RE::BGSInventoryItem::Stack*> g_lockedTargetStack{ nullptr };

    struct LedgerEntry {
        std::uint32_t objectFormID{ 0 };
        std::uint32_t stackIndex{ 0xFFFFFFFFu };
        bool isWeapon{ false };
        bool isKit{ false };
        float kitRepairAmount{ 0.0f };
        float kitMaxLimit{ 1.0f };
    };

    std::shared_mutex g_ledgerMutex;
    std::unordered_map<std::uint16_t, LedgerEntry> g_sessionLedger;
    std::uint16_t g_ledgerCounter = 1;

    std::mutex g_deletionMutex;
    struct PendingDeletion {
        std::uint32_t objectFormID{ 0 };
        std::uint32_t stackIndex{ 0xFFFFFFFFu };
    };
    std::vector<PendingDeletion> g_pendingDeletions;
    std::atomic<std::uint64_t> g_deletionGeneration{ 0 };

    std::vector<ConditionSystem::RepairKitProfile> g_repairKitProfiles;
    std::shared_mutex g_kitMutex;

    static float GetArmorRepairPreviewMultiplier(float a_durability)
    {
        if (a_durability < 0.5f) return std::max(0.0f, a_durability * 2.0f);
        if (a_durability > 1.0f) return 1.0f + 0.2f * (a_durability - 1.0f);
        return 1.0f;
    }

    static float GetValueRepairPreviewMultiplier(float a_durability)
    {
        if (a_durability > 1.0f) return 1.0f + 0.2f * (a_durability - 1.0f);
        return std::max(0.0f, a_durability);
    }

    static float GetArmorBaseResistance(RE::BGSInventoryItem* a_item, RE::BGSInventoryItem::Stack* a_stack)
    {
        if (!a_item || !a_item->object || !a_item->object->Is(RE::ENUM_FORM_ID::kARMO)) return 0.0f;
        RE::BSScrapArray<RE::BSTTuple<std::uint32_t, float>> resistValues;
        RE::PipboyInventoryUtils::FillResistTypeInfo(*a_item, a_stack, resistValues, 1.0f);

        float total = 0.0f;
        for (const auto& res : resistValues) {
            total += res.second;
        }
        return total;
    }

    static std::uint32_t GetStackIndex(RE::BGSInventoryItem* a_item, RE::BGSInventoryItem::Stack* a_stack)
    {
        if (!a_item || !a_stack) return 0xFFFFFFFFu;

        std::uint32_t index = 0;
        for (auto stack = a_item->stackData.get(); stack; stack = stack->nextStack.get(), ++index) {
            if (stack == a_stack) return index;
        }

        return 0xFFFFFFFFu;
    }

    static RE::BGSInventoryItem::Stack* GetStackByIndexSafe(RE::BGSInventoryItem& a_item, std::uint32_t a_stackIndex)
    {
        auto stack = a_item.stackData.get();
        while (stack && a_stackIndex-- > 0) {
            stack = stack->nextStack.get();
        }
        return stack;
    }

    static bool ResolveLedgerEntry(const LedgerEntry& a_entry, RE::BGSInventoryItem*& a_item, RE::BGSInventoryItem::Stack*& a_stack)
    {
        a_item = nullptr;
        a_stack = nullptr;
        if (a_entry.objectFormID == 0) return false;

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return false;

        for (auto& item : player->inventoryList->data) {
            if (!item.object || item.object->GetFormID() != a_entry.objectFormID) continue;
            a_item = &item;
            if (a_entry.stackIndex != 0xFFFFFFFFu) {
                a_stack = GetStackByIndexSafe(item, a_entry.stackIndex);
                if (!a_stack) {
                    a_item = nullptr;
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    void ResetRuntimeState()
    {
        g_lockedTargetObject.store(nullptr);
        g_lockedTargetStack.store(nullptr);
        {
            std::unique_lock<std::shared_mutex> lock(g_ledgerMutex);
            g_sessionLedger.clear();
            g_ledgerCounter = 1;
        }
        {
            std::lock_guard<std::mutex> lock(g_deletionMutex);
            g_pendingDeletions.clear();
            g_deletionGeneration.fetch_add(1);
        }
    }

    static RE::BGSInventoryItem::Stack* ResolveSelectedStack(RE::BGSInventoryItem& a_item, bool a_isEquipped, std::uint32_t a_exactStackIndex, bool a_stackIndexMode)
    {
        RE::BGSInventoryItem::Stack* stack = nullptr;

        if (a_stackIndexMode) {
            stack = GetStackByIndexSafe(a_item, a_exactStackIndex);
        }
        else if (a_exactStackIndex > 0) {
            stack = a_item.GetStackByID(a_exactStackIndex);
        }
        if (!stack) {
            for (auto s = a_item.stackData.get(); s; s = s->nextStack.get()) {
                if (s->IsEquipped() == a_isEquipped) {
                    stack = s;
                    break;
                }
            }
        }
        if (!stack) {
            for (auto s = a_item.stackData.get(); s; s = s->nextStack.get()) {
                if (s->IsEquipped() != a_isEquipped) {
                    stack = s;
                    break;
                }
            }
        }

        return stack ? stack : a_item.stackData.get();
    }

    static float GetBaseValue(RE::BGSInventoryItem* a_item, RE::BGSInventoryItem::Stack* a_stack)
    {
        if (!a_item || !a_item->object) return 0.0f;

        RE::TBO_InstanceData* instanceData = nullptr;
        std::uint32_t stackIndex = GetStackIndex(a_item, a_stack);
        if (stackIndex != 0xFFFFFFFFu) {
            instanceData = a_item->GetInstanceData(stackIndex);
        }

        std::uint32_t value = RE::TESValueForm::GetFormValue(a_item->object, instanceData);
        if (value == 0 && instanceData) {
            std::int32_t instanceValue = instanceData->GetValue();
            if (instanceValue > 0) value = static_cast<std::uint32_t>(instanceValue);
        }
        if (value == 0) {
            auto valueForm = a_item->object->As<RE::TESValueForm>();
            if (valueForm && valueForm->value > 0) value = static_cast<std::uint32_t>(valueForm->value);
        }

        return static_cast<float>(value);
    }

    static std::string BuildRepairPreviewStats(RE::BGSInventoryItem* a_item, RE::BGSInventoryItem::Stack* a_stack, bool a_isWeapon, float a_currentPct, float a_repairedPct)
    {
        if (!a_item || !a_item->object) return "";

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1);

        if (a_isWeapon && g_mcmSettings.weaponConditionAffectsDamage.load()) {
            auto weapon = a_item->object->As<RE::TESObjectWEAP>();
            float baseDamage = weapon ? static_cast<float>(weapon->weaponData.attackDamage) : 0.0f;
            float currentDamage = baseDamage * ConditionSystem::GetWeaponDegradationMultiplier(a_currentPct);
            float repairedDamage = baseDamage * ConditionSystem::GetWeaponDegradationMultiplier(a_repairedPct);
            ss << "DMG:" << currentDamage << ":" << repairedDamage;
        }
        else if (!a_isWeapon && g_mcmSettings.armorConditionAffectsResistance.load()) {
            float baseResist = GetArmorBaseResistance(a_item, a_stack);
            float currentResist = baseResist * GetArmorRepairPreviewMultiplier(a_currentPct);
            float repairedResist = baseResist * GetArmorRepairPreviewMultiplier(a_repairedPct);
            ss << "DR:" << currentResist << ":" << repairedResist;
        }

        bool valueAffected = (a_isWeapon && g_mcmSettings.weaponConditionAffectsValue.load()) ||
                             (!a_isWeapon && g_mcmSettings.armorConditionAffectsValue.load());
        if (valueAffected) {
            float baseValue = GetBaseValue(a_item, a_stack);
            float currentValue = baseValue * GetValueRepairPreviewMultiplier(a_currentPct);
            float repairedValue = baseValue * GetValueRepairPreviewMultiplier(a_repairedPct);
            if (ss.tellp() > 0) ss << ",";
            ss << "VAL:" << currentValue << ":" << repairedValue;
        }

        return ss.str();
    }

    static std::string BuildStackUiMeta(RE::BGSInventoryItem::Stack* a_stack)
    {
        bool favorite = false;
        bool legendary = false;
        if (a_stack && a_stack->extra) {
            favorite = a_stack->extra->IsFavorite();
            legendary = a_stack->extra->GetLegendaryMod() != nullptr;
        }

        return std::string("META:F") + (favorite ? "1" : "0") +
            ":L" + (legendary ? "1" : "0") +
            ":P" + (g_mcmSettings.confirmConsumeFavoriteMaterial.load() ? "1" : "0") +
            ":G" + (g_mcmSettings.confirmConsumeLegendaryMaterial.load() ? "1" : "0");
    }

    static std::pair<float, float> GetRepairThresholdPcts(RE::TESBoundObject* a_obj)
    {
        if (!a_obj) return { -1.0f, -1.0f };

        auto flags = ConditionSystem::GetItemFlags(a_obj);
        if (!flags.enableDurabilitySystem) return { -1.0f, -1.0f };

        if (a_obj->Is(RE::ENUM_FORM_ID::kARMO) && g_mcmSettings.armorConditionAffectsResistance.load()) {
            return { 0.5f, -1.0f };
        }
        if (a_obj->Is(RE::ENUM_FORM_ID::kWEAP) && g_mcmSettings.weaponConditionAffectsDamage.load()) {
            return { g_mcmSettings.penaltyThreshold.load(), -1.0f };
        }

        return { -1.0f, -1.0f };
    }

    std::string CleanForMatch(const std::string& in) {
        std::string out; bool inHtml = false;
        for (char c : in) {
            if (c == '<') { inHtml = true; continue; }
            if (c == '>') { inHtml = false; continue; }
            if (inHtml || (c > 0 && c <= 32)) continue;
            if (c >= 'A' && c <= 'Z') out += static_cast<char>(c + 32);
            else out += c;
        }
        return out;
    }

    std::string GetSafeName(RE::TESBoundObject* a_obj, RE::BGSInventoryItem::Stack* a_stack) {
        if (!a_obj) return LOC("$CSF_UnnamedItem");

        if (a_stack && a_stack->extra) {
            auto textData = a_stack->extra->GetByType<RE::ExtraTextDisplayData>();
            if (!textData && a_stack->extra->GetByType<RE::ExtraInstanceData>()) {
                textData = ::ConditionSystem::SafeCreateExtraData<RE::ExtraTextDisplayData>(RE::EXTRA_DATA_TYPE::kTextDisplayData);
                if (textData) a_stack->extra->AddExtra(textData);
            }
            if (textData) {
                if (textData->displayName.c_str() && strlen(textData->displayName.c_str()) > 0) return textData->displayName.c_str();
                auto dynName = textData->GetDisplayName(a_obj);
                if (dynName.c_str() && strlen(dynName.c_str()) > 0) return dynName.c_str();
            }
        }
        auto fullNameObj = a_obj->As<RE::TESFullName>();
        if (fullNameObj && fullNameObj->GetFullName()) return fullNameObj->GetFullName();
        return LOC("$CSF_UnnamedItem");
    }

    void LoadRepairKitProfiles() {
        std::unique_lock<std::shared_mutex> lock(g_kitMutex);
        g_repairKitProfiles.clear();
        auto dirKits = "Data\\F4SE\\Plugins\\ConditionSystemFramework\\Repair\\RepairKits";
        std::error_code ec;
        if (!std::filesystem::exists(dirKits, ec)) std::filesystem::create_directories(dirKits, ec);
        ::ConditionSystem::LoadRepairKitProfilesFromTemplate(g_repairKitProfiles);
    }

    ConditionSystem::RepairKitProfile GetRepairKitProfile(RE::TESBoundObject* a_kit) {
        if (!a_kit) return ConditionSystem::RepairKitProfile();
        std::shared_lock<std::shared_mutex> lock(g_kitMutex);
        for (const auto& profile : g_repairKitProfiles) {
            if (std::find(profile.formIDs.begin(), profile.formIDs.end(), a_kit->GetFormID()) != profile.formIDs.end()) return profile;
            for (const auto& kwd : profile.keywords) if (::ConditionSystem::HasKeywordString(a_kit, nullptr, kwd)) return profile;
        }
        return ConditionSystem::RepairKitProfile();
    }

    RepairAmountData CalculateRepairAmount(const ConditionSystem::RepairKitProfile& a_profile) {
        RepairAmountData data;
        data.isFlatPoints = a_profile.useFlatPoints;
        data.value = data.isFlatPoints ? a_profile.baseRepairPoints : a_profile.baseRepairPercent;
        return data;
    }

    bool LockHoveredTarget(std::uint32_t a_formID, bool a_isEquipped, std::uint32_t a_exactStackIndex, bool a_stackIndexMode) {
        g_lockedTargetObject.store(nullptr);
        g_lockedTargetStack.store(nullptr);
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList || a_formID == 0) return false;

        RE::TESForm* rawForm = RE::TESForm::GetFormByID(a_formID);
        if (!rawForm) return false;
        RE::TESBoundObject* engineObj = rawForm->As<RE::TESBoundObject>();
        if (!engineObj) return false;

        RE::TESBoundObject* bestObj = nullptr;
        RE::BGSInventoryItem::Stack* bestStack = nullptr;

        for (auto& item : player->inventoryList->data) {
            if (item.object == engineObj) {
                bestObj = item.object;
                bestStack = ResolveSelectedStack(item, a_isEquipped, a_exactStackIndex, a_stackIndexMode);
                break;
            }
        }

        if (bestObj && bestStack) {
            if (!::ConditionSystem::ShouldShowDurability(bestObj)) return false;

            auto flags = ::ConditionSystem::GetItemFlags(bestObj);
            if (!flags.enableDurabilitySystem) return false;

            auto hExtra = bestStack->extra ? bestStack->extra->GetByType<RE::ExtraHealth>() : nullptr;
            if (!bestStack->extra || !bestStack->extra->GetByType<RE::ExtraInstanceData>() || !hExtra || hExtra->health > ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                ConditionSystem::WakeUpAndRandomizeSingleStack(bestObj, bestStack, true);
            }
            g_lockedTargetObject.store(bestObj);
            g_lockedTargetStack.store(bestStack);
            return true;
        }
        return false;
    }

    void ProcessPendingDeletions() {
        const auto generation = g_deletionGeneration.load();
        std::vector<PendingDeletion> pending;
        { std::lock_guard<std::mutex> lock(g_deletionMutex); if (g_pendingDeletions.empty()) return; pending = g_pendingDeletions; g_pendingDeletions.clear(); }
        if (auto task = F4SE::GetTaskInterface()) {
            task->AddTask([pending, generation]() {
                if (generation != g_deletionGeneration.load()) return;
                auto p = RE::PlayerCharacter::GetSingleton();
                if (p && p->inventoryList) {
                    for (const auto& pendingEntry : pending) {
                        auto rawForm = RE::TESForm::GetFormByID(pendingEntry.objectFormID);
                        auto object = rawForm ? rawForm->As<RE::TESBoundObject>() : nullptr;
                        if (!object) continue;

                        RE::TESObjectREFR::RemoveItemData rmData(object, 1);
                        std::uint32_t matIdx = 0; bool found = false;
                        for (auto& item : p->inventoryList->data) {
                            if (item.object == object) {
                                if (pendingEntry.stackIndex == 0xFFFFFFFFu) {
                                    found = true;
                                    break;
                                }
                                if (GetStackByIndexSafe(item, pendingEntry.stackIndex)) {
                                    found = true;
                                    matIdx = pendingEntry.stackIndex;
                                }
                                break;
                            }
                        }
                        if (found) { rmData.stackData.push_back(matIdx); p->RemoveItem(rmData); }
                    }
                }
                });
        }
    }

    static void EnsureRepairCondition(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack)
    {
        // The initializer itself preserves every existing CSF condition and
        // only initializes missing health or the engine's default sentinel.
        ConditionSystem::WakeUpAndRandomizeSingleStack(a_object, a_stack, false);
    }

    bool RepairEquippedWeaponLogic(RE::Actor* a_actor, RepairAmountData a_amount, float a_materialLimit) {
        if (!a_actor || !a_actor->inventoryList) return false;
        if (!std::isfinite(a_amount.value) || a_amount.value <= 0.0f) return false;
        bool repaired = false;

        for (auto& item : a_actor->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kWEAP)) { // 修正为新版枚举
                if (!ConditionSystem::GetItemFlags(item.object).enableDurabilitySystem) continue;

                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (!stack->IsEquipped()) continue;
                    EnsureRepairCondition(item.object, stack);
                    if (stack->extra) {
                        auto healthExtra = stack->extra->GetByType<RE::ExtraHealth>();
                        float realPct = 1.0f;
                        if (healthExtra && healthExtra->health < ConditionSystem::ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                            realPct = ConditionSystem::GetDecompressedPct(item.object, healthExtra->health);
                        }

                        float finalMaxLimit = ConditionSystem::GetRepairKitLimit(a_materialLimit);

                        float amountToAdd = a_amount.value;
                        float maxDura = ConditionSystem::GetEffectiveMaxDurability(item.object, stack->extra.get());
                        if (!std::isfinite(maxDura) || maxDura <= 0.0f) continue;

                        if (a_amount.isFlatPoints) {
                            amountToAdd = maxDura > 0.0f ? (a_amount.value / maxDura) : 0.0f;
                        }

                        if (realPct < finalMaxLimit - 0.005f) {
                            float newTotal = realPct + amountToAdd;
                            if (newTotal > finalMaxLimit) newTotal = finalMaxLimit;

                            float currentPts = std::round(newTotal * maxDura);
                            float alignedPercent = currentPts / maxDura;

                            stack->extra->SetHealthPerc(ConditionSystem::GetCompressedPct(item.object, alignedPercent)); // 修正为 SetHealthPerc
                            repaired = true;

                            float jamThreshold = ConditionSystem::g_mcmSettings.jamThreshold.load();
                            if (alignedPercent > jamThreshold) {
                                const auto identity = ConditionSystem::BuildWeaponInstanceIdentity(item.object->As<RE::TESObjectWEAP>(), stack);
                                if (ConditionSystem::ClearWeaponFault(identity)) {
                                    ConditionSystem::ConditionUI::ShowUnjammingUI(false);
                                }
                            }
                        }
                    }
                }
            }
        }
        return repaired;
    }

    bool SmartCascadeArmorRepair(RE::Actor* a_actor, RepairAmountData a_amount, float a_materialLimit) {
        if (!a_actor || !a_actor->inventoryList) return false;
        if (!std::isfinite(a_amount.value) || a_amount.value <= 0.0f) return false;

        struct ArmorData { RE::BGSInventoryItem::Stack* stack; float totalHealth; RE::TESBoundObject* obj; };
        std::vector<ArmorData> armorStacks;

        for (auto& item : a_actor->inventoryList->data) {
            if (item.object && item.object->Is(RE::ENUM_FORM_ID::kARMO)) { // 修正为新版枚举

                if (ConditionSystem::HasKeywordString(item.object, nullptr, "ArmorTypePower")) continue;
                if (!ConditionSystem::GetItemFlags(item.object).enableDurabilitySystem) continue;

                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    bool isEffectivelyEquipped = stack->IsEquipped() || stack->flags.any(RE::BGSInventoryItem::Stack::Flag::kInvShouldEquip); // Flags -> Flag

                    if (!isEffectivelyEquipped) continue;
                    EnsureRepairCondition(item.object, stack);
                    if (stack->extra) {
                        auto hExtra = stack->extra->GetByType<RE::ExtraHealth>();
                        float realPct = 1.0f;
                        if (hExtra && hExtra->health < ConditionSystem::ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                            realPct = ConditionSystem::GetDecompressedPct(item.object, hExtra->health);
                        }

                        float finalMaxLimit = ConditionSystem::GetRepairKitLimit(a_materialLimit);

                        if (realPct < finalMaxLimit - 0.005f) {
                            armorStacks.push_back({ stack, realPct, item.object });
                        }
                    }
                }
            }
        }
        if (armorStacks.empty()) return false;

        if (a_amount.isFlatPoints) {
            float poolPoints = a_amount.value;
            while (poolPoints > 0.001f && !armorStacks.empty()) {
                std::sort(armorStacks.begin(), armorStacks.end(), [](auto& a, auto& b) { return a.totalHealth < b.totalHealth; });
                auto& lowest = armorStacks.front();

                float finalMaxLimit = ConditionSystem::GetRepairKitLimit(a_materialLimit);
                float maxDura = ConditionSystem::GetEffectiveMaxDurability(
                    lowest.obj, lowest.stack->extra ? lowest.stack->extra.get() : nullptr);

                float missingPct = finalMaxLimit - lowest.totalHealth;
                if (missingPct <= 0.0f || !std::isfinite(maxDura) || maxDura <= 0.0f) {
                    armorStacks.erase(armorStacks.begin());
                    continue;
                }

                float missingPts = missingPct * maxDura;
                float ptsToApply = (std::min)(poolPoints, missingPts);
                float pctToApply = ptsToApply / maxDura;

                lowest.totalHealth += pctToApply;
                poolPoints -= ptsToApply;

                float currentPts = std::round(lowest.totalHealth * maxDura);
                float alignedPercent = currentPts / maxDura;

                lowest.stack->extra->SetHealthPerc(ConditionSystem::GetCompressedPct(lowest.obj, alignedPercent)); // 修正为 SetHealthPerc

                if (alignedPercent >= finalMaxLimit - 0.001f) armorStacks.erase(armorStacks.begin());
            }
        }
        else {
            float a_repairPool = a_amount.value;
            while (a_repairPool > 0.001f && !armorStacks.empty()) {
                std::sort(armorStacks.begin(), armorStacks.end(), [](auto& a, auto& b) { return a.totalHealth < b.totalHealth; });
                auto& lowest = armorStacks.front();

                float finalMaxLimit = ConditionSystem::GetRepairKitLimit(a_materialLimit);
                float maxDura = ConditionSystem::GetEffectiveMaxDurability(
                    lowest.obj, lowest.stack->extra ? lowest.stack->extra.get() : nullptr);
                if (!std::isfinite(maxDura) || maxDura <= 0.0f) {
                    armorStacks.erase(armorStacks.begin());
                    continue;
                }

                float toApply = (std::min)(a_repairPool, finalMaxLimit - lowest.totalHealth);
                if (toApply <= 0.0f) {
                    armorStacks.erase(armorStacks.begin());
                    continue;
                }

                lowest.totalHealth += toApply;
                a_repairPool -= toApply;

                float currentPts = std::round(lowest.totalHealth * maxDura);
                float alignedPercent = currentPts / maxDura;

                lowest.stack->extra->SetHealthPerc(ConditionSystem::GetCompressedPct(lowest.obj, alignedPercent)); // 修正为 SetHealthPerc
                if (alignedPercent >= finalMaxLimit - 0.001f) armorStacks.erase(armorStacks.begin());
            }
        }
        return true;
    }

    class RepairKitEquipSink : public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>
    {
    public:
        static RepairKitEquipSink* GetSingleton() { static RepairKitEquipSink singleton; return &singleton; }

        RE::BSEventNotifyControl ProcessEvent(const RE::ActorEquipManagerEvent::Event& a_event, RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>*) override {
            if (!a_event.actorAffected) return RE::BSEventNotifyControl::kContinue;
            auto player = RE::PlayerCharacter::GetSingleton();
            if (a_event.actorAffected != player) return RE::BSEventNotifyControl::kContinue;

            auto baseForm = a_event.itemAffected ? a_event.itemAffected->object : nullptr;
            if (!baseForm) return RE::BSEventNotifyControl::kContinue;

            bool isEquipped = (a_event.changeType == RE::ActorEquipManagerEvent::Type::kEquip);

            if (baseForm->Is(RE::ENUM_FORM_ID::kARMO)) {
                // Equip events can leave Actor::armorRating cached from the
                // previous loadout. Rebuild it on the game task thread.
                ConditionSystem::QueuePlayerArmorRatingRefresh(true);
            }

            if (baseForm->Is(RE::ENUM_FORM_ID::kWEAP)) {
                if (isEquipped) {
                    ConditionSystem::WeaponInstanceIdentity activeWeapon{};
                    auto weapon = baseForm->As<RE::TESObjectWEAP>();
                    for (auto& item : player->inventoryList->data) {
                        if (item.object == baseForm) {
                            for (auto s = item.stackData.get(); s; s = s->nextStack.get()) {
                                if (s->IsEquipped()) {
                                    activeWeapon = ConditionSystem::BuildWeaponInstanceIdentity(weapon, s);
                                    break;
                                }
                            }
                        }
                    }
                    ConditionSystem::SetActiveWeaponInstance(activeWeapon);
                }
                else {
                    ConditionSystem::SetActiveWeaponInstance({});
                    ConditionSystem::ConditionUI::ShowUnjammingUI(false);
                }
            }

            if (isEquipped && baseForm->Is(RE::ENUM_FORM_ID::kALCH)) {
                auto boundObj = baseForm->As<RE::TESBoundObject>();
                if (boundObj) {
                    auto profile = GetRepairKitProfile(boundObj);
                    if (profile.isValid) {
                        RepairAmountData amount = CalculateRepairAmount(profile);
                        bool didRepair = false;

                        if (profile.canRepairWeapon) didRepair |= RepairEquippedWeaponLogic(player, amount, profile.maxConditionLimit);
                        if (profile.canRepairArmor) didRepair |= SmartCascadeArmorRepair(player, amount, profile.maxConditionLimit);

                        if (didRepair) {
                            RE::SendHUDMessage::ShowHUDMessage("$CSF_ToolRepairDone", "UIPipBoySDCardEquip", true, true);
                            // 无需移除：kALCH 辅助品由引擎自动消耗
                        }
                        else {
                            RE::SendHUDMessage::ShowHUDMessage(
                                ConditionSystem::GetGameSettingText("sNoNeedToRepairMessage", "$CSF_ToolRepairNotNeeded"),
                                nullptr,
                                true,
                                true);
                            if (auto task = F4SE::GetTaskInterface()) {
                                task->AddTask([player, boundObj]() {
                                    if (player && boundObj) player->AddObjectToContainer(boundObj, nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
                                    });
                            }
                        }
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    static RepairKitEquipSink g_equipEventSink;

    void RegisterEquipEventSink() {
        // Route equipment changes through the engine equipment manager.
        auto source = RE::ActorEquipManager::GetSingleton();
        if (source) source->RegisterSink(&g_equipEventSink);
    }

    uint32_t GetPipboyColor() {
        auto r = RE::GetINISetting("fPipboyEffectColorR:Pipboy"); auto g = RE::GetINISetting("fPipboyEffectColorG:Pipboy"); auto b = RE::GetINISetting("fPipboyEffectColorB:Pipboy");
        return (static_cast<uint8_t>((r ? r->GetFloat() : 0.09f) * 255) << 16) | (static_cast<uint8_t>((g ? g->GetFloat() : 1.0f) * 255) << 8) | static_cast<uint8_t>((b ? b->GetFloat() : 0.09f) * 255);
    }

    uint32_t GetHudColor() {
        const auto readChannel = [](std::string_view a_name, std::int32_t a_fallback) {
            const auto setting = RE::GetINISetting(a_name);
            const auto value = setting ? setting->GetInt() : a_fallback;
            return static_cast<std::uint32_t>(std::clamp(value, 0, 255));
        };

        return (readChannel("iHUDColorR:Interface", 18) << 16) |
               (readChannel("iHUDColorG:Interface", 255) << 8) |
               readChannel("iHUDColorB:Interface", 21);
    }

    std::string BuildJuryStatsUiConfigString() {
        std::ostringstream ss;
        ss << g_mcmSettings.juryStatsOffsetX.load() << ';'
           << g_mcmSettings.juryStatsOffsetY.load() << ';'
           << std::clamp(g_mcmSettings.juryStatsRowSpacing.load(), 84.0f, 110.0f) << ';'
           << std::clamp(g_mcmSettings.juryStatsColorMode.load(), 0, 1);
        return ss.str();
    }

    std::string GenerateLedgerDataString();

    void QueueJuryRiggingMenuRefresh(bool a_isRefresh) {
        std::thread([a_isRefresh]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (auto task = F4SE::GetTaskInterface()) {
                task->AddTask([a_isRefresh]() {
                    auto ui = RE::UI::GetSingleton(); auto pip = ui ? ui->GetMenu("PipboyMenu") : nullptr;
                    if (pip && pip->uiMovie) {
                        std::string newData = GenerateLedgerDataString();
                        std::string uiConfig = BuildJuryStatsUiConfigString();
                        Scaleform::GFx::Value args[5]; args[0] = newData.c_str(); args[1] = a_isRefresh; args[2] = static_cast<double>(GetPipboyColor()); args[3] = ""; args[4] = uiConfig.c_str();
                        pip->uiMovie->Invoke("root.ReceiveJuryRiggingData", nullptr, args, 5);
                    }
                    });
            }
            }).detach();
    }

    std::string GenerateLedgerDataString() {
        if (!ConditionSystem::g_mcmSettings.enablePipboyJuryRepair.load()) return "";

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return "";

        RE::BSAutoReadLock invLock{ player->inventoryList->rwLock };

        RE::TESBoundObject* targetObj = g_lockedTargetObject.load();
        RE::BGSInventoryItem::Stack* targetStack = g_lockedTargetStack.load();
        if (!targetObj || !targetStack) return "";

        RE::BGSInventoryItem* targetItem = nullptr;
        for (auto& item : player->inventoryList->data) { if (item.object == targetObj) { targetItem = &item; break; } }
        if (!targetItem) return "";

        bool stackStillValid = false; RE::BGSInventoryItem::Stack* fallbackStack = nullptr;
        for (auto s = targetItem->stackData.get(); s; s = s->nextStack.get()) {
            if (!fallbackStack) fallbackStack = s;
            if (s == targetStack) { stackStillValid = true; break; }
        }
        if (!stackStillValid) {
            if (fallbackStack) { targetStack = fallbackStack; g_lockedTargetStack.store(targetStack); }
            else return "";
        }

        bool isW = targetObj->Is(RE::ENUM_FORM_ID::kWEAP); // 修正为新版枚举
        { std::unique_lock<std::shared_mutex> lock(g_ledgerMutex); g_sessionLedger.clear(); g_ledgerCounter = 1; g_sessionLedger[0] = { targetObj->GetFormID(), GetStackIndex(targetItem, targetStack), isW, false, 0.0f, 1.0f }; }

        float targetMax = ::ConditionSystem::GetEffectiveMaxDurability(
            targetObj, targetStack->extra ? targetStack->extra.get() : nullptr);
        if (!std::isfinite(targetMax) || targetMax <= 0.0f) return "";

        float targetTotalPct = 1.0f;
        if (targetStack->extra) {
            auto hExt = targetStack->extra->GetByType<RE::ExtraHealth>();
            if (hExt && hExt->health < ConditionSystem::ENGINE_DEFAULT_HEALTH_THRESHOLD) targetTotalPct = ConditionSystem::GetDecompressedPct(targetObj, hExt->health);
        }

        float playerSkillLimit = ConditionSystem::GetJuryRepairLimit(targetObj, targetStack->extra.get());
        std::string jrSkillStr = ConditionSystem::GetJuryRiggingSkillString(targetObj, targetStack->extra.get());
        auto [thresholdPct, thresholdPct2] = GetRepairThresholdPcts(targetObj);

        std::string res = "0:::" + GetSafeName(targetObj, targetStack) + ":::" + std::to_string(targetTotalPct) + ":::" + std::to_string(playerSkillLimit) + ":::" + jrSkillStr + ":::" + std::to_string(thresholdPct) + ":::" + std::to_string(thresholdPct2) + ":::" + BuildStackUiMeta(targetStack) + "@@@";

        std::string kitStr = "", matStr = "";
        for (auto& item : player->inventoryList->data) {
            if (!item.object) continue;

            auto kitProf = ConditionSystem::g_mcmSettings.enableRepairKits.load() ? GetRepairKitProfile(item.object) : ConditionSystem::RepairKitProfile();
            if (kitProf.isValid && ((isW && kitProf.canRepairWeapon) || (!isW && kitProf.canRepairArmor))) {
                int pendingKitCount = 0;
                {
                    std::lock_guard<std::mutex> dLock(g_deletionMutex);
                    for (const auto& pending : g_pendingDeletions) {
                        if (pending.objectFormID == item.object->GetFormID() && pending.stackIndex == 0xFFFFFFFFu) pendingKitCount++;
                    }
                }

                std::uint32_t count = 0;
                for (auto s = item.stackData.get(); s; s = s->nextStack.get()) count += s->count;

                if (count > static_cast<std::uint32_t>(pendingKitCount)) {
                    std::uint32_t realCount = count - pendingKitCount;
                    RepairAmountData amtData = CalculateRepairAmount(kitProf);

                    float amtPct = amtData.value;
                    if (amtData.isFlatPoints) {
                        amtPct = targetMax > 0.0f ? (amtData.value / targetMax) : 0.0f;
                    }

                    float finalMaxLimit = ConditionSystem::GetRepairKitLimit(kitProf.maxConditionLimit);

                    std::uint16_t sID;
                    { std::unique_lock<std::shared_mutex> lock(g_ledgerMutex); sID = g_ledgerCounter++; g_sessionLedger[sID] = { item.object->GetFormID(), 0xFFFFFFFFu, isW, true, amtPct, kitProf.maxConditionLimit }; }

                    std::string displayKitName = GetSafeName(item.object, item.stackData.get());

                    float previewPct = std::min(finalMaxLimit, targetTotalPct + amtPct);
                    std::string previewStats = BuildRepairPreviewStats(targetItem, targetStack, isW, targetTotalPct, previewPct);
                    if (!previewStats.empty()) previewStats += ",";
                    previewStats += BuildStackUiMeta(item.stackData.get());
                    kitStr += std::to_string(sID) + ":::" + displayKitName + " x" + std::to_string(realCount) + ":::" + std::to_string(amtPct) + ":::" + std::to_string(amtPct) + ":::" + std::to_string(finalMaxLimit) + ":::" + previewStats + ":::0|||";
                }
                continue;
            }

            if (!item.object->Is(RE::ENUM_FORM_ID::kWEAP) && !item.object->Is(RE::ENUM_FORM_ID::kARMO)) continue; // 修正为新版枚举
            if (item.object->Is(RE::ENUM_FORM_ID::kWEAP) != isW) continue; // 修正为新版枚举
            if (!ConditionSystem::ShouldShowDurability(item.object)) continue;

            auto matFlags = ConditionSystem::GetItemFlags(item.object);
            if (!matFlags.enableDurabilitySystem) continue;

            int pendingKitCount = 0; std::vector<std::uint32_t> pendingStackIndices;
            {
                std::lock_guard<std::mutex> dLock(g_deletionMutex);
                for (const auto& pending : g_pendingDeletions) {
                    if (pending.objectFormID == item.object->GetFormID()) {
                        if (pending.stackIndex == 0xFFFFFFFFu) pendingKitCount++;
                        else pendingStackIndices.push_back(pending.stackIndex);
                    }
                }
            }

            for (auto s = item.stackData.get(); s; s = s->nextStack.get()) {
                if (item.object == targetObj && s == targetStack) continue;
                const auto currentStackIndex = GetStackIndex(&item, s);
                bool isPending = false; for (auto ps : pendingStackIndices) { if (ps == currentStackIndex) { isPending = true; break; } }
                if (isPending) continue;

                auto juryResult = ConditionSystem::EvaluateJuryRigging(targetObj, targetStack->extra.get(), item.object, s->extra.get());

                if (juryResult.allowed) {
                    float mTotalPct = 1.0f;
                    if (s->extra) {
                        auto mExt = s->extra->GetByType<RE::ExtraHealth>();
                        if (mExt && mExt->health < ConditionSystem::ENGINE_DEFAULT_HEALTH_THRESHOLD) mTotalPct = ConditionSystem::GetDecompressedPct(item.object, mExt->health);
                    }
                    if (mTotalPct <= 0.001f) continue;

                    std::string materialName = GetSafeName(item.object, s);
                    float matMax = ::ConditionSystem::GetEffectiveMaxDurability(
                        item.object, s->extra ? s->extra.get() : nullptr);

                    float efficiency = juryResult.efficiencyMult;
                    float add = (matMax * mTotalPct * efficiency) / targetMax;
                    if (add < 0.15f) add = 0.15f;

                    float juryLimit = ConditionSystem::GetJuryRepairLimit(targetObj, targetStack->extra.get());
                    std::uint16_t sID; { std::unique_lock<std::shared_mutex> lock(g_ledgerMutex); sID = g_ledgerCounter++; g_sessionLedger[sID] = { item.object->GetFormID(), GetStackIndex(&item, s), isW, false, 0.0f, juryLimit }; }
                    float previewPct = std::min(juryLimit, targetTotalPct + add);
                    std::string previewStats = BuildRepairPreviewStats(targetItem, targetStack, isW, targetTotalPct, previewPct);
                    if (!previewStats.empty()) previewStats += ",";
                    previewStats += BuildStackUiMeta(s);
                    matStr += std::to_string(sID) + ":::" + materialName + ":::" + std::to_string(mTotalPct) + ":::" + std::to_string(add) + ":::" + std::to_string(juryLimit) + ":::" + previewStats + ":::" + (s->IsEquipped() ? "1" : "0") + "|||";
                }
            }
        }
        if (!kitStr.empty()) kitStr.erase(kitStr.size() - 3); if (!matStr.empty()) matStr.erase(matStr.size() - 3);
        return res + kitStr + "@@@" + matStr;
    }

    std::string GetJuryRiggingDataString(std::uint16_t) { return GenerateLedgerDataString(); }
    std::string GetJuryRiggingDataStringImpl(std::uint16_t, std::uint16_t) { return GenerateLedgerDataString(); }

    void ExecuteJuryRiggingAsync(std::uint16_t a_m) {
        LedgerEntry targetEntry, matEntry;
        { std::unique_lock<std::shared_mutex> lock(g_ledgerMutex); if (g_sessionLedger.count(0) == 0 || g_sessionLedger.count(a_m) == 0) return; targetEntry = g_sessionLedger[0]; matEntry = g_sessionLedger[a_m]; g_sessionLedger.clear(); }
        RE::BGSInventoryItem* targetItem = nullptr;
        RE::BGSInventoryItem::Stack* tStack = nullptr;
        if (!ResolveLedgerEntry(targetEntry, targetItem, tStack) || !targetItem || !tStack) return;

        RE::BGSInventoryItem* materialItem = nullptr;
        RE::BGSInventoryItem::Stack* mStack = nullptr;
        if (!ResolveLedgerEntry(matEntry, materialItem, mStack) || !materialItem) return;
        if (!matEntry.isKit && !mStack) return;

        float tTotalPct = 1.0f;
        if (tStack->extra) {
            auto h = tStack->extra->GetByType<RE::ExtraHealth>();
            if (h && h->health < ConditionSystem::ENGINE_DEFAULT_HEALTH_THRESHOLD) tTotalPct = ConditionSystem::GetDecompressedPct(targetItem->object, h->health);
        }
        
        float added = matEntry.isKit ? matEntry.kitRepairAmount : 0.25f;
        float tMax = ::ConditionSystem::GetEffectiveMaxDurability(
            targetItem->object, tStack->extra ? tStack->extra.get() : nullptr);
        if (!std::isfinite(tMax) || tMax <= 0.0f) return;

        if (!matEntry.isKit) {
            float mMax = ::ConditionSystem::GetEffectiveMaxDurability(
                materialItem->object, mStack->extra ? mStack->extra.get() : nullptr);
            float mTotalPct = 1.0f;
            if (mStack->extra) {
                auto hExtra = mStack->extra->GetByType<RE::ExtraHealth>();
                if (hExtra && hExtra->health < ConditionSystem::ENGINE_DEFAULT_HEALTH_THRESHOLD) mTotalPct = ConditionSystem::GetDecompressedPct(materialItem->object, hExtra->health);
            }

            auto juryResult = ConditionSystem::EvaluateJuryRigging(targetItem->object, tStack->extra.get(), materialItem->object, mStack->extra.get());
            float efficiency = juryResult.efficiencyMult;

            added = (mMax * mTotalPct * efficiency) / tMax;
            if (added < 0.15f) added = 0.15f;
        }

        float finalMaxLimit = matEntry.isKit
            ? ConditionSystem::GetRepairKitLimit(matEntry.kitMaxLimit)
            : ConditionSystem::GetJuryRepairLimit(targetItem->object, tStack->extra.get());

        float newTotal = (tTotalPct + added > finalMaxLimit) ? finalMaxLimit : tTotalPct + added;

        float currentPts = std::round(newTotal * tMax);
        float alignedPercent = currentPts / tMax;

        if (!tStack->extra) tStack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());
        tStack->extra->SetHealthPerc(ConditionSystem::GetCompressedPct(targetItem->object, alignedPercent)); // 修正为 SetHealthPerc

        if (targetEntry.isWeapon) {
            float jamThreshold = ConditionSystem::g_mcmSettings.jamThreshold.load();
            if (alignedPercent > jamThreshold) {
                const auto identity = ConditionSystem::BuildWeaponInstanceIdentity(targetItem->object->As<RE::TESObjectWEAP>(), tStack);
                if (ConditionSystem::ClearWeaponFault(identity)) {
                    ConditionSystem::ConditionUI::ShowUnjammingUI(false);
                }
            }
        }
        { std::lock_guard<std::mutex> dLock(g_deletionMutex); g_pendingDeletions.push_back({ matEntry.objectFormID, matEntry.isKit ? 0xFFFFFFFFu : matEntry.stackIndex }); }

        QueueJuryRiggingMenuRefresh(true);
    }

    static bool IsProtectedFavoriteMaterial(const LedgerEntry& a_entry)
    {
        RE::BGSInventoryItem* item = nullptr;
        RE::BGSInventoryItem::Stack* stack = nullptr;
        return !a_entry.isKit && ResolveLedgerEntry(a_entry, item, stack) && stack && stack->extra && stack->extra->IsFavorite();
    }

    static bool IsProtectedLegendaryMaterial(const LedgerEntry& a_entry)
    {
        RE::BGSInventoryItem* item = nullptr;
        RE::BGSInventoryItem::Stack* stack = nullptr;
        return !a_entry.isKit && ResolveLedgerEntry(a_entry, item, stack) && stack && stack->extra && stack->extra->GetLegendaryMod() != nullptr;
    }

    static bool ShouldConfirmProtectedMaterial(const LedgerEntry& a_entry)
    {
        const bool warnFavorite = g_mcmSettings.confirmConsumeFavoriteMaterial.load() && IsProtectedFavoriteMaterial(a_entry);
        const bool warnLegendary = g_mcmSettings.confirmConsumeLegendaryMaterial.load() && IsProtectedLegendaryMaterial(a_entry);
        return warnFavorite || warnLegendary;
    }

    class JuryRiggingProtectedMaterialConfirmCallback : public RE::IMessageBoxCallback {
    public:
        explicit JuryRiggingProtectedMaterialConfirmCallback(std::uint16_t a_materialUID) :
            materialUID(a_materialUID)
        {}

        virtual void operator()(std::uint8_t a_buttonIdx) override {
            if (a_buttonIdx == 0) {
                ExecuteJuryRiggingAsync(materialUID);
            } else {
                QueueJuryRiggingMenuRefresh(false);
            }
        }

    private:
        std::uint16_t materialUID;
    };

    static bool ShowProtectedMaterialConfirmBox(std::uint16_t a_materialUID, const LedgerEntry& a_entry)
    {
        auto msgMgr = RE::MessageMenuManager::GetSingleton();
        if (!msgMgr) return false;

        const bool favorite = IsProtectedFavoriteMaterial(a_entry);
        const bool legendary = IsProtectedLegendaryMaterial(a_entry);

        std::string bodyText;
        if (favorite && legendary) {
            bodyText = LOC("$CSF_JuryRiggingProtectedFavoriteLegendaryWarning");
        } else if (favorite) {
            bodyText = LOC("$CSF_JuryRiggingProtectedFavoriteWarning");
        } else {
            bodyText = LOC("$CSF_JuryRiggingProtectedLegendaryWarning");
        }

        RE::BGSInventoryItem* materialItem = nullptr;
        RE::BGSInventoryItem::Stack* materialStack = nullptr;
        if (ResolveLedgerEntry(a_entry, materialItem, materialStack) && materialItem && materialItem->object) {
            bodyText += "\n\n";
            bodyText += GetSafeName(materialItem->object, materialStack);
        }

        auto callback = new JuryRiggingProtectedMaterialConfirmCallback(a_materialUID);
        auto titleText = LOC("$CSF_JuryRiggingProtectedMaterialTitle");
        auto btn1Text = LOC("$Confirm");
        auto btn2Text = LOC("$Cancel");
        msgMgr->Create(titleText.c_str(), bodyText.c_str(), callback, static_cast<RE::WARNING_TYPES>(0), btn1Text.c_str(), btn2Text.c_str());
        return true;
    }

    bool ExecuteJuryRigging(std::uint16_t, std::uint16_t a_m)
    {
        LedgerEntry matEntry;
        {
            std::shared_lock<std::shared_mutex> lock(g_ledgerMutex);
            auto it = g_sessionLedger.find(a_m);
            if (it == g_sessionLedger.end()) return false;
            matEntry = it->second;
        }

        if (ShouldConfirmProtectedMaterial(matEntry) && ShowProtectedMaterialConfirmBox(a_m, matEntry)) {
            return true;
        }

        ExecuteJuryRiggingAsync(a_m);
        return true;
    }

    static bool TryGetScaleformNumber(const Scaleform::GFx::FunctionHandler::Params& a_params, std::uint32_t a_index, double& a_value)
    {
        if (a_index >= a_params.argCount) return false;

        const auto& value = a_params.args[a_index];
        if (value.IsNumber()) {
            a_value = value.GetNumber();
            return std::isfinite(a_value);
        }
        if (value.IsInt()) {
            a_value = static_cast<double>(value.GetInt());
            return true;
        }
        if (value.IsUInt()) {
            a_value = static_cast<double>(value.GetUInt());
            return true;
        }
        return false;
    }

    class ConditionSystemCallback : public Scaleform::GFx::FunctionHandler {
    public:
        virtual void Call(const Params& a_params) override {
            if (a_params.argCount < 1 || !a_params.movie || !a_params.args[0].IsString()) return;
            std::string cmd = a_params.args[0].GetString();

            auto numberArg = [&](std::uint32_t a_index, double a_default = 0.0) {
                double value = a_default;
                if (!TryGetScaleformNumber(a_params, a_index, value)) {
                    REX::WARN("[DEBUG-WB-b673] Ignoring non-numeric Scaleform argument {} for command '{}'.", a_index, cmd);
                    return a_default;
                }
                return value;
            };

            if (cmd == "RequestData") {
                std::string ledger = GenerateLedgerDataString();
                if (!ledger.empty()) {
                    std::string uiConfig = BuildJuryStatsUiConfigString();
                    Scaleform::GFx::Value args[5];
                    args[0] = ledger.c_str();
                    args[1] = false;
                    args[2] = static_cast<double>(GetPipboyColor());
                    args[3] = "";
                    args[4] = uiConfig.c_str();
                    a_params.movie->Invoke("root.ReceiveJuryRiggingData", nullptr, args, 5);
                }
            }
            else if (cmd == "RequestRepairCost") {
                ConditionSystem::Workbench::SendRepairCostToUI();
            }
            else if (cmd == "SetWorkbenchContext") {
                const bool isRobotWorkbench = numberArg(1) > 0.0;
                ConditionSystem::Workbench::SetRobotWorkbenchContext(isRobotWorkbench);
            }
            else if (cmd == "ExecuteWorkbenchRepair") {
                ConditionSystem::Workbench::ExecuteWorkbenchRepairFromUI();
            }
            else if (cmd == "TagRepairComponents") {
                ConditionSystem::Workbench::ToggleRepairComponentsTag();
            }
            else if (cmd == "ShowRepairConfirmBox") {
                ConditionSystem::Workbench::ShowRepairConfirmBox();
            }
            else if (cmd == "ShowRepairKitConfirmBox") {
                if (!ConditionSystem::g_mcmSettings.enableRepairKits.load()) return;
                std::uint16_t kitID = static_cast<std::uint16_t>(numberArg(1));
                ConditionSystem::Workbench::ShowRepairKitConfirmBox(kitID);
            }
            else if (cmd == "ShowRepairKitListBox") {
                if (!ConditionSystem::g_mcmSettings.enableRepairKits.load()) return;
                std::uint32_t fID = static_cast<std::uint32_t>(numberArg(1));
                bool eq = numberArg(2) > 0;
                std::uint32_t exactStackIndex = static_cast<std::uint32_t>(numberArg(3));
                ConditionSystem::Workbench::ShowRepairKitListBox(0, fID, eq, exactStackIndex);
            }
            else if (cmd == "RequestRepairKitButtonState") {
                std::uint32_t fID = static_cast<std::uint32_t>(numberArg(1));
                bool eq = numberArg(2) > 0;
                std::uint32_t exactStackIndex = static_cast<std::uint32_t>(numberArg(3));
                REX::INFO("[CSF-Workbench] Repair-button state request received formID={:08X} equipped={} stack={}", fID, eq ? 1 : 0, exactStackIndex);
                ConditionSystem::Workbench::SendRepairKitButtonStateToUI(fID, eq, exactStackIndex);
            }
            else if (cmd == "ExecuteJuryRigging") {
                if (a_params.argCount < 3) {
                    REX::WARN("[DEBUG-WB-b673] ExecuteJuryRigging request is missing its material UID.");
                    return;
                }
                ExecuteJuryRigging(0, static_cast<std::uint16_t>(numberArg(2)));
            }
            else if (cmd == "CloseRepairMenu") {
                ::ConditionSystem::g_isRepairMenuOpen.store(false);
                RE::TESBoundObject* targetObj = g_lockedTargetObject.load();
                g_lockedTargetObject.store(nullptr);
                g_lockedTargetStack.store(nullptr);
                bool didRepair = false;
                { std::lock_guard<std::mutex> dLock(g_deletionMutex); didRepair = !g_pendingDeletions.empty(); }
                if (didRepair) {
                    std::thread([targetObj]() {
                        ConditionSystem::Repair::ProcessPendingDeletions();
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        if (targetObj) {
                            if (auto task = F4SE::GetTaskInterface()) {
                                task->AddTask([targetObj]() {
                                    auto player = RE::PlayerCharacter::GetSingleton();
                                    if (player && player->inventoryList) {
                                        for (auto& item : player->inventoryList->data) {
                                            if (item.object == targetObj) {
                                                ConditionSystem::Workbench::NotifyInventoryItemChanged(&item);
                                                break;
                                            }
                                        }
                                    }
                                    });
                            }
                        }
                        }).detach();
                }
            }
            else if (cmd == "OpenRepairMenuFromMouse") {
                if (!ConditionSystem::g_mcmSettings.enablePipboyJuryRepair.load()) return;
                std::uint32_t fID = static_cast<std::uint32_t>(numberArg(1));
                bool eq = numberArg(3) > 0;
                std::uint32_t exactStackIndex = static_cast<std::uint32_t>(numberArg(4));
                bool stackIndexMode = numberArg(5) > 0;
                if (exactStackIndex == 0) {
                    exactStackIndex = ConditionSystem::g_hoveredStackIndex.load();
                    stackIndexMode = true;
                }
                if (LockHoveredTarget(fID, eq, exactStackIndex, stackIndexMode)) OpenJuryRiggingMenu(1);
            }
            else if (cmd == "CheckRepairable") {
                std::uint32_t fID = static_cast<std::uint32_t>(numberArg(1));
                std::uint32_t exactStackIndex = static_cast<std::uint32_t>(numberArg(2));
                bool isEquipped = numberArg(3) > 0;
                bool stackIndexMode = numberArg(4) > 0;
                std::uint32_t requestId = static_cast<std::uint32_t>(numberArg(5));
                bool isSupported = false;
                bool needsRepair = false;
                if (ConditionSystem::g_mcmSettings.enablePipboyJuryRepair.load() && fID != 0) {
                    auto form = RE::TESForm::GetFormByID(fID);
                    auto boundObj = form ? form->As<RE::TESBoundObject>() : nullptr;
                    if (boundObj && (boundObj->Is(RE::ENUM_FORM_ID::kWEAP) || boundObj->Is(RE::ENUM_FORM_ID::kARMO))) { // 修正为新版枚举
                        if (::ConditionSystem::ShouldShowDurability(boundObj)) {
                            auto flags = ::ConditionSystem::GetItemFlags(boundObj);
                            if (flags.enableDurabilitySystem) {
                                isSupported = true;
                                auto player = RE::PlayerCharacter::GetSingleton();
                                if (player && player->inventoryList) {
                                    for (auto& item : player->inventoryList->data) {
                                        if (item.object != boundObj) continue;

                                        auto targetStack = ResolveSelectedStack(item, isEquipped, exactStackIndex, stackIndexMode);
                                        if (targetStack) {
                                            if (!targetStack->extra) {
                                                targetStack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());
                                            }
                                            auto hExtra = targetStack->extra->GetByType<RE::ExtraHealth>();
                                            if (!hExtra || hExtra->health > ConditionSystem::ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                                                ConditionSystem::WakeUpAndRandomizeSingleStack(boundObj, targetStack, false);
                                            }
                                            float currentPct = ConditionSystem::GetVisualDurabilityPercent(boundObj, targetStack);
                                            float repairLimit = ConditionSystem::GetJuryRepairLimit(boundObj, targetStack->extra.get());
                                            needsRepair = currentPct < repairLimit - 0.001f;
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                Scaleform::GFx::Value args[3]; // 净化去除了旧版 RE::
                args[0] = isSupported;
                args[1] = needsRepair;
                args[2] = static_cast<double>(requestId);
                a_params.movie->Invoke("root.SetRepairButtonEnabled_Call", nullptr, args, 3);
            }
        }
    };

    // 修正智能指针与初始化写法
    static Scaleform::Ptr<ConditionSystemCallback> g_csCallback;

    void InjectConditionSystemCallback(Scaleform::GFx::Movie* movie) {
        if (!movie) return;
        if (!g_csCallback) g_csCallback.reset(new ConditionSystemCallback());
        Scaleform::GFx::Value fn;
        movie->CreateFunction(&fn, g_csCallback.get());
        movie->SetVariable("root.ConditionSystem_Call", fn);
    }

    void RefreshExamineMenuButtons(Scaleform::GFx::Movie* movie) {
        if (!movie) return;
        const bool updateInvoked = movie->Invoke("root.UpdateButtons", nullptr, nullptr, 0);

        // UpdateButtons only schedules the state query in inventory mode while
        // _allowEquip is false.  During ExamineMenu construction those values
        // can still be transient, so explicitly use the SWF's existing public
        // delayed request entry point after the callback has been installed.
        Scaleform::GFx::Value delayArg;
        delayArg = static_cast<double>(150.0);
        const bool requestInvoked = movie->Invoke("root.RequestRepairKitButtonStateWithDelay", nullptr, &delayArg, 1);
        REX::INFO("[CSF-Workbench] ExamineMenu refresh: UpdateButtons={} RequestState={}.", updateInvoked ? 1 : 0, requestInvoked ? 1 : 0);
    }

    void OpenJuryRiggingMenu(std::uint32_t) {
        if (!ConditionSystem::g_mcmSettings.enablePipboyJuryRepair.load()) return;
        auto ui = RE::UI::GetSingleton(); auto pip = ui ? ui->GetMenu("PipboyMenu") : nullptr;
        if (pip && pip->uiMovie) {
            InjectConditionSystemCallback(pip->uiMovie.get());
            ConditionSystem::g_isRepairMenuOpen.store(true);
            Scaleform::GFx::Value arg(true); // 净化去除了旧版 RE::
            pip->uiMovie->Invoke("root.ToggleRepairLayout_Call", nullptr, &arg, 1);
        }
    }

    void Papyrus_UseRepairKit(std::monostate, RE::Actor* akActor, RE::TESBoundObject* akKit) {
        if (!ConditionSystem::g_mcmSettings.enableRepairKits.load()) return;
        if (!akActor || !akKit) return;
        auto profile = GetRepairKitProfile(akKit);
        if (!profile.isValid) return;
        RepairAmountData amount = CalculateRepairAmount(profile);
        if (RepairEquippedWeaponLogic(akActor, amount, profile.maxConditionLimit)) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_KitUsed", "UIPipBoySDCardEquip", true, true);
            // 显式移除已使用的修理工具（Papyrus 路径不会触发引擎自动消耗）
            if (auto task = F4SE::GetTaskInterface()) {
                task->AddTask([akActor, akKit]() {
                    if (akActor && akKit) {
                        RE::TESObjectREFR::RemoveItemData rmData(akKit, 1);
                        akActor->RemoveItem(rmData);
                    }
                });
            }
        }
    }

    void Papyrus_OpenRepairMenu(std::monostate) {
        if (!ConditionSystem::g_mcmSettings.enablePipboyJuryRepair.load()) return;
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return;

        // 尝试锁定当前装备的武器并打开修理菜单
        for (auto& item : player->inventoryList->data) {
            if (!item.object || !item.object->Is(RE::ENUM_FORM_ID::kWEAP)) continue;
            if (!::ConditionSystem::ShouldShowDurability(item.object)) continue;
            auto flags = ::ConditionSystem::GetItemFlags(item.object);
            if (!flags.enableDurabilitySystem) continue;

            for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (stack->IsEquipped()) {
                    if (LockHoveredTarget(item.object->GetFormID(), true, 0)) {
                        OpenJuryRiggingMenu(1);
                    }
                    return;
                }
            }
        }

        // 退一步：尝试锁定背包中的第一把可用武器
        for (auto& item : player->inventoryList->data) {
            if (!item.object || !item.object->Is(RE::ENUM_FORM_ID::kWEAP)) continue;
            if (!::ConditionSystem::ShouldShowDurability(item.object)) continue;
            auto flags = ::ConditionSystem::GetItemFlags(item.object);
            if (!flags.enableDurabilitySystem) continue;

            if (LockHoveredTarget(item.object->GetFormID(), false, 0)) {
                OpenJuryRiggingMenu(1);
            }
            return;
        }
    }

    namespace
    {
        struct EquippedWeaponStack
        {
            RE::TESBoundObject* object{ nullptr };
            RE::BGSInventoryItem::Stack* stack{ nullptr };
        };

        EquippedWeaponStack FindEquippedManagedWeapon(RE::Actor* a_actor)
        {
            if (!a_actor || !a_actor->inventoryList) return {};

            for (auto& item : a_actor->inventoryList->data) {
                if (!item.object || !item.object->Is(RE::ENUM_FORM_ID::kWEAP)) continue;
                if (!ConditionSystem::GetItemFlags(item.object).enableDurabilitySystem) continue;

                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack->IsEquipped()) return { item.object, stack };
                }
            }
            return {};
        }

        void RefreshPlayerWeaponCondition(RE::Actor* a_actor, const EquippedWeaponStack& a_weapon)
        {
            if (a_actor != RE::PlayerCharacter::GetSingleton() || !a_weapon.object || !a_weapon.stack) return;

            const float condition = ConditionSystem::GetStackDurabilityPercent(a_weapon.object, a_weapon.stack);
            float points = 0.0f;
            if (auto weapon = a_weapon.object->As<RE::TESObjectWEAP>()) {
                points = std::round(condition * ConditionSystem::GetEffectiveMaxDurability(
                    weapon, a_weapon.stack->extra ? a_weapon.stack->extra.get() : nullptr));
            }
            ConditionSystem::UpdateHUDDurabilityWidget(condition, points, true);
        }
    }

    bool Papyrus_IsConditionManaged(std::monostate, RE::TESBoundObject* a_item)
    {
        return a_item && ConditionSystem::GetItemFlags(a_item).enableDurabilitySystem;
    }

    float Papyrus_GetEquippedWeaponConditionPct(std::monostate, RE::Actor* a_actor)
    {
        const auto weapon = FindEquippedManagedWeapon(a_actor);
        if (!weapon.object || !weapon.stack) return -1.0f;
        return ConditionSystem::GetStackDurabilityPercent(weapon.object, weapon.stack) * 100.0f;
    }

    bool Papyrus_SetEquippedWeaponConditionPct(std::monostate, RE::Actor* a_actor, float a_percent)
    {
        if (!std::isfinite(a_percent)) return false;
        const auto weapon = FindEquippedManagedWeapon(a_actor);
        if (!weapon.object || !weapon.stack) return false;

        if (!weapon.stack->extra) {
            weapon.stack->extra = RE::BSTSmartPointer<RE::ExtraDataList>(new RE::ExtraDataList());
        }

        const float normalized = (std::clamp)(a_percent, 0.0f, 100.0f) / 100.0f;
        const float compressed = ConditionSystem::GetCompressedPct(weapon.object, normalized);
        weapon.stack->extra->SetHealthPerc(compressed);
        if (auto health = weapon.stack->extra->GetByType<RE::ExtraHealth>()) {
            health->health = compressed;
        }

        const auto identity = ConditionSystem::BuildWeaponInstanceIdentity(weapon.object->As<RE::TESObjectWEAP>(), weapon.stack);
        if (normalized > ConditionSystem::g_mcmSettings.jamThreshold.load() &&
            ConditionSystem::ClearWeaponFault(identity)) {
            ConditionSystem::ConditionUI::ShowUnjammingUI(false);
        }

        RefreshPlayerWeaponCondition(a_actor, weapon);
        return true;
    }

    bool Papyrus_ModEquippedWeaponConditionPct(std::monostate, RE::Actor* a_actor, float a_deltaPercent)
    {
        const float current = Papyrus_GetEquippedWeaponConditionPct({}, a_actor);
        return current >= 0.0f && Papyrus_SetEquippedWeaponConditionPct({}, a_actor, current + a_deltaPercent);
    }

    float Papyrus_GetEquippedArmorConditionPct(std::monostate, RE::Actor* a_actor)
    {
        if (!a_actor || !a_actor->inventoryList) return -1.0f;

        float total = 0.0f;
        std::uint32_t count = 0;
        for (auto& item : a_actor->inventoryList->data) {
            if (!item.object || !item.object->Is(RE::ENUM_FORM_ID::kARMO)) continue;
            if (!ConditionSystem::GetItemFlags(item.object).enableDurabilitySystem) continue;

            for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (!stack->IsEquipped()) continue;
                total += ConditionSystem::GetStackDurabilityPercent(item.object, stack);
                ++count;
                break;
            }
        }
        return count > 0 ? (total / static_cast<float>(count)) * 100.0f : -1.0f;
    }

    bool Papyrus_CanJuryRig(std::monostate, RE::TESBoundObject* a_target, RE::TESBoundObject* a_material)
    {
        return ConditionSystem::EvaluateJuryRigging(a_target, nullptr, a_material, nullptr).allowed;
    }

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) return false;
        a_vm->BindNativeMethod("WDF_NativeFunctions", "UseRepairKit", Papyrus_UseRepairKit);
        a_vm->BindNativeMethod("ConditionFramework", "OpenRepairMenu", Papyrus_OpenRepairMenu);
        a_vm->BindNativeMethod("ConditionFramework", "IsConditionManaged", Papyrus_IsConditionManaged);
        a_vm->BindNativeMethod("ConditionFramework", "GetEquippedWeaponConditionPct", Papyrus_GetEquippedWeaponConditionPct);
        a_vm->BindNativeMethod("ConditionFramework", "SetEquippedWeaponConditionPct", Papyrus_SetEquippedWeaponConditionPct);
        a_vm->BindNativeMethod("ConditionFramework", "ModEquippedWeaponConditionPct", Papyrus_ModEquippedWeaponConditionPct);
        a_vm->BindNativeMethod("ConditionFramework", "GetEquippedArmorConditionPct", Papyrus_GetEquippedArmorConditionPct);
        a_vm->BindNativeMethod("ConditionFramework", "CanJuryRig", Papyrus_CanJuryRig);
        return true;
    }
}

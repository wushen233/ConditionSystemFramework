#include "pch.h"
#include "ConditionCore.h"
#include "ConditionUI.h"
#include "ClassificationManager.h"
#include "ProfileManager.h"

#include <Windows.h>
#include <random>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <cctype>
#include <cstdio>
#include <shared_mutex>
#include <sstream>

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
    std::atomic<FaultType> g_faultType{ FaultType::None };
    std::atomic<float> g_weaponHeat{ 0.0f };
    std::atomic<std::uintptr_t> g_heatWeaponUniqueID{ 0 };
    std::atomic<float> g_energyFaultRemaining{ 0.0f };
    std::atomic<std::uintptr_t> g_jammedWeaponUniqueID{ 0 };
    std::atomic<bool> g_isWeaponDrawn{ false };
    std::atomic<bool> g_isUnjamming{ false };
    std::atomic<float> g_unjamProgress{ 0.0f };
    std::atomic<bool> g_isRepairMenuOpen{ false };
    std::atomic<RE::TESBoundObject*> g_hoveredObject{ nullptr };
    std::atomic<std::uint32_t> g_hoveredStackIndex{ 0 };
    std::atomic<std::uint32_t> g_hoveredSelectedIndex{ 0 };

    namespace {
        std::atomic<bool> g_armorRatingRefreshQueued{ false };
        std::atomic<std::int64_t> g_lastArmorRatingRefreshMs{ 0 };

        void RefreshPlayerArmorRatingImpl()
        {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player || !player->inventoryList) return;

            for (auto& item : player->inventoryList->data) {
                if (!item.object || !item.object->Is(RE::ENUM_FORM_ID::kARMO)) continue;

                std::uint32_t stackIndex = 0;
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex) {
                    if (!stack->IsEquipped()) continue;

                    // This is the native path used when the engine applies a
                    // worn armor instance. It rebuilds the actor armor cache,
                    // allowing IIF to observe current durability and settings.
                    RE::BGSObjectInstance object(item.object, item.GetInstanceData(stackIndex));
                    player->WornArmorChanged(object);
                }
            }
        }
    }

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



    const char* GetGameSettingText(const char* a_editorID, const char* a_fallback)
    {
        if (!a_editorID || !a_fallback) return a_fallback;

        auto settings = RE::GameSettingCollection::GetSingleton();
        auto setting = settings ? settings->GetSetting(a_editorID) : nullptr;
        const char* text = setting ? setting->GetString().data() : nullptr;
        return text && text[0] != '\0' ? text : a_fallback;
    }

    float GetWeaponFaultChance(float durability, WeaponMechanism a_mechanism) {
        float jamThreshold = g_mcmSettings.jamThreshold.load();
        if (jamThreshold <= 0.0f || durability > jamThreshold || durability < 0.0f) return 0.0f;

        if (a_mechanism == WeaponMechanism::Auto) {
            a_mechanism = GetEquippedWeaponMechanism();
        }
        // Overheat is a deterministic thermal state, never a random durability jam.
        if (a_mechanism == WeaponMechanism::Overheat || a_mechanism == WeaponMechanism::None) return 0.0f;

        float maxFaultChance = g_mcmSettings.maxJamChance.load();
        if (a_mechanism == WeaponMechanism::EnergyFault) {
            if (!g_mcmSettings.enableEnergyFault.load()) return 0.0f;
            const float multiplier = g_mcmSettings.iJammingPhase.load() == 1
                ? g_mcmSettings.energyReloadFaultChanceMultiplier.load()
                : g_mcmSettings.energyFaultChanceMultiplier.load();
            maxFaultChance *= std::clamp(multiplier, 0.0f, 5.0f);
        }
        return std::clamp(((jamThreshold - durability) / jamThreshold) * maxFaultChance, 0.0f, 1.0f);
    }

    bool RollForJam(float durability, WeaponMechanism a_mechanism) {
        const float chance = GetWeaponFaultChance(durability, a_mechanism);
        if (chance <= 0.0f) return false;

        static thread_local std::mt19937 randEngine(std::random_device{}());
        std::uniform_real_distribution<float> randDist(0.0f, 1.0f);
        return randDist(randEngine) < chance;
    }

    WeaponMechanism GetWeaponMechanism(RE::TESObjectWEAP* a_weapon, RE::TESAmmo* a_ammo, const WeaponProfile* a_profile)
    {
        if (!a_weapon) return WeaponMechanism::None;

        const WeaponProfile profile = a_profile ? *a_profile : GetProfileForWeapon(a_weapon);
        if (profile.mechanism != WeaponMechanism::Auto) return profile.mechanism;
        if (!profile.canJam) return WeaponMechanism::None;
        if (IsHeatManagedWeapon(a_weapon, a_ammo)) return WeaponMechanism::Overheat;
        if (IsEnergyWeapon(a_weapon, a_ammo)) return WeaponMechanism::EnergyFault;
        return WeaponMechanism::BallisticJam;
    }

    namespace
    {
        std::string JoinCategories(const Classification::Result& a_result)
        {
            std::string output;
            for (const auto& category : a_result.categories) {
                if (!output.empty()) output += ",";
                output += category;
            }
            return output.empty() ? "<none>" : output;
        }

        bool IsWeaponEnergyIdentity(RE::TESObjectWEAP* a_weapon)
        {
            if (!a_weapon) return false;

            auto containsEnergyTerm = [](std::string a_value) {
                std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                constexpr std::string_view terms[] = {
                    "laser", "plasma", "gamma", "cryo", "alienblaster", "fusioncell", "electronchargepack", "microfusioncell", "energy"
                };
                return std::any_of(std::begin(terms), std::end(terms), [&a_value](std::string_view term) { return a_value.find(term) != std::string::npos; });
            };

            for (const auto& keyword : ExtractKeywords(a_weapon)) {
                if (containsEnergyTerm(keyword)) return true;
            }

            return Classification::HasCategory(a_weapon, nullptr, "Weapon.Family.Energy");
        }
    }

    bool IsEnergyWeapon(RE::TESObjectWEAP* a_weapon, RE::TESAmmo* a_ammo)
    {
        if (!a_weapon) return false;

        auto containsEnergyTerm = [](std::string a_value) {
            std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            constexpr std::string_view terms[] = {
                "laser", "plasma", "gamma", "cryo", "alienblaster", "fusioncell", "electronchargepack", "microfusioncell", "energy"
            };
            return std::any_of(std::begin(terms), std::end(terms), [&a_value](std::string_view term) { return a_value.find(term) != std::string::npos; });
        };

        for (const auto& keyword : ExtractKeywords(a_weapon)) {
            if (containsEnergyTerm(keyword)) return true;
        }

        if (!a_ammo) a_ammo = a_weapon->weaponData.ammo;
        if (a_ammo) {
            if (containsEnergyTerm(a_ammo->GetFormEditorID())) return true;
            for (const auto& keyword : ExtractKeywords(a_ammo)) {
                if (containsEnergyTerm(keyword)) return true;
            }
        }
        return false;
    }

    bool IsEquippedWeaponEnergy()
    {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->inventoryList) return false;

        for (auto& item : player->inventoryList->data) {
            auto weapon = item.object ? item.object->As<RE::TESObjectWEAP>() : nullptr;
            if (!weapon) continue;
            for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (!stack->IsEquipped()) continue;
                RE::TESAmmo* ammo = weapon->weaponData.ammo;
                if (stack->extra) {
                    auto instExtra = stack->extra->GetByType<RE::ExtraInstanceData>();
                    if (instExtra && instExtra->data) {
                        auto instance = static_cast<RE::TESObjectWEAP::InstanceData*>(instExtra->data.get());
                        if (instance && instance->ammo) ammo = instance->ammo;
                    }
                }
                return IsEnergyWeapon(weapon, ammo);
            }
        }
        return false;
    }

    void StartEnergyFault()
    {
        const auto owner = GetEquippedWeaponInstanceIdentity();
        if (g_weaponFaultController.StartEnergyFault(owner, g_mcmSettings.energyFaultDuration.load())) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_EnergyFault", "WPNPistol10mmFireDry", true, true);
        }
    }

    void UpdateEnergyFault()
    {
        const auto now = std::chrono::steady_clock::now();
        static auto lastUpdate = now;
        constexpr auto kUpdateInterval = std::chrono::milliseconds(50);
        if (now - lastUpdate < kUpdateInterval) {
            return;
        }
        const float deltaSeconds = std::clamp(std::chrono::duration<float>(now - lastUpdate).count(), 0.0f, 0.10f);
        lastUpdate = now;

        const bool wasEnergyFault = IsCurrentWeaponEnergyFault();
        const auto snapshot = g_weaponFaultController.TickEnergyFault(deltaSeconds);
        if (wasEnergyFault && !snapshot.IsEnergyFault()) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_EnergyRecovered", nullptr, true, true);
        }
    }

    bool IsHeatManagedWeapon(RE::TESObjectWEAP* a_weapon, RE::TESAmmo* a_ammo)
    {
        if (!a_weapon) return false;
        // Ammo can change energy-fault behavior, but it must not turn a
        // ballistic weapon such as the Minigun into a heat-managed weapon.
        (void)a_ammo;

        // Heavy energy weapons such as the Gatling Laser commonly expose only
        // a heavy-weapon keyword, so combine that with the energy classifier.
        const bool heavy = Classification::HasCategory(a_weapon, nullptr, "Weapon.Handling.Heavy");
        return heavy && IsWeaponEnergyIdentity(a_weapon);
    }

    namespace
    {
        struct EquippedHeatWeapon
        {
            RE::TESObjectWEAP* weapon{ nullptr };
            RE::TESAmmo* ammo{ nullptr };
            RE::BGSInventoryItem::Stack* stack{ nullptr };
        };

        EquippedHeatWeapon GetEquippedHeatWeapon()
        {
            EquippedHeatWeapon result;
            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player || !player->inventoryList) return result;

            auto isThrownWeapon = [](RE::TESObjectWEAP* a_weapon) {
                if (!a_weapon) return false;
                const auto type = a_weapon->weaponData.type.get();
                return type == RE::WEAPON_TYPE::kGrenade || type == RE::WEAPON_TYPE::kMine;
            };

            auto findEquippedStack = [&](RE::TESObjectWEAP* a_weapon) {
                if (!a_weapon || isThrownWeapon(a_weapon)) return false;
                for (auto& item : player->inventoryList->data) {
                    if (item.object != a_weapon) continue;
                    for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                        if (!stack->IsEquipped()) continue;
                        result.weapon = a_weapon;
                        result.stack = stack;
                        result.ammo = a_weapon->weaponData.ammo;
                        if (stack->extra) {
                            auto instExtra = stack->extra->GetByType<RE::ExtraInstanceData>();
                            if (instExtra && instExtra->data) {
                                auto instance = static_cast<RE::TESObjectWEAP::InstanceData*>(instExtra->data.get());
                                if (instance && instance->ammo) result.ammo = instance->ammo;
                            }
                        }
                        return true;
                    }
                }
                return false;
            };

            // GetEquippedItem represents the weapon currently used by the active
            // hand. This is important when a grenade slot and a firearm are both
            // marked equipped in the inventory list.
            RE::BGSObjectInstance equipped{ nullptr, nullptr };
            RE::BGSEquipIndex equipIndex{ 0 };
            if (auto* current = player->GetEquippedItem(&equipped, equipIndex); current && current->object) {
                auto currentWeapon = current->object->As<RE::TESObjectWEAP>();
                if (!currentWeapon || isThrownWeapon(currentWeapon)) return result;
                findEquippedStack(currentWeapon);
                return result;
            }

            for (auto& item : player->inventoryList->data) {
                auto weapon = item.object ? item.object->As<RE::TESObjectWEAP>() : nullptr;
                if (!weapon) continue;
                if (isThrownWeapon(weapon)) continue;
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (!stack->IsEquipped()) continue;
                    result.weapon = weapon;
                    result.stack = stack;
                    result.ammo = weapon->weaponData.ammo;
                    if (stack->extra) {
                        auto instExtra = stack->extra->GetByType<RE::ExtraInstanceData>();
                        if (instExtra && instExtra->data) {
                            auto instance = static_cast<RE::TESObjectWEAP::InstanceData*>(instExtra->data.get());
                            if (instance && instance->ammo) result.ammo = instance->ammo;
                        }
                    }
                    return result;
                }
            }
            return result;
        }
    }

    bool IsEquippedWeaponHeatManaged()
    {
        auto equipped = GetEquippedHeatWeapon();
        return equipped.weapon && GetEquippedWeaponMechanism() == WeaponMechanism::Overheat;
    }

    WeaponInstanceIdentity BuildWeaponInstanceIdentity(RE::TESObjectWEAP* a_weapon, RE::BGSInventoryItem::Stack* a_stack)
    {
        if (!a_weapon || !a_stack) return {};
        return { a_weapon->GetFormID(), reinterpret_cast<std::uintptr_t>(a_stack) };
    }

    WeaponInstanceIdentity GetEquippedWeaponInstanceIdentity()
    {
        const auto equipped = GetEquippedHeatWeapon();
        return BuildWeaponInstanceIdentity(equipped.weapon, equipped.stack);
    }

    void SetActiveWeaponInstance(const WeaponInstanceIdentity& a_weapon)
    {
        g_weaponFaultController.SetActiveWeapon(a_weapon);
    }

    WeaponFaultSnapshot GetWeaponFaultSnapshot()
    {
        return g_weaponFaultController.Snapshot();
    }

    bool IsCurrentWeaponJammed()
    {
        return GetWeaponFaultSnapshot().IsWeaponJammed();
    }

    bool IsCurrentWeaponEnergyFault()
    {
        return GetWeaponFaultSnapshot().IsEnergyFault();
    }

    bool IsCurrentWeaponOverheated()
    {
        return GetWeaponFaultSnapshot().IsOverheated();
    }

    bool IsCurrentWeaponBlocked()
    {
        return GetWeaponFaultSnapshot().IsOperationallyBlocked();
    }

    bool StartBallisticJam()
    {
        const auto owner = GetEquippedWeaponInstanceIdentity();
        return g_weaponFaultController.StartBallisticJam(owner);
    }

    bool ClearCurrentWeaponFault()
    {
        return g_weaponFaultController.ClearCurrentFault();
    }

    bool ClearWeaponFault(const WeaponInstanceIdentity& a_weapon)
    {
        return g_weaponFaultController.ClearFaultForWeapon(a_weapon);
    }

    void ResetWeaponFaultState()
    {
        g_weaponFaultController.Reset();
    }

    WeaponMechanism GetEquippedWeaponMechanism()
    {
        auto equipped = GetEquippedHeatWeapon();
        if (!equipped.weapon) return WeaponMechanism::None;
        const WeaponProfile profile = GetProfileForWeapon(
            equipped.weapon, equipped.stack && equipped.stack->extra ? equipped.stack->extra.get() : nullptr);
        return GetWeaponMechanism(equipped.weapon, equipped.ammo, &profile);
    }

    void UpdateWeaponHeat(bool a_force)
    {
        const auto now = std::chrono::steady_clock::now();
        static auto lastUpdate = now;
        constexpr auto kUpdateInterval = std::chrono::milliseconds(50);
        if (!a_force && now - lastUpdate < kUpdateInterval) {
            return;
        }
        const float deltaSeconds = std::clamp(std::chrono::duration<float>(now - lastUpdate).count(), 0.0f, 0.10f);
        lastUpdate = now;

        auto equipped = GetEquippedHeatWeapon();
        const auto currentIdentity = BuildWeaponInstanceIdentity(equipped.weapon, equipped.stack);
        const auto currentID = currentIdentity.stackAddress;
        SetActiveWeaponInstance(currentIdentity);
        const WeaponProfile profile = equipped.weapon
            ? GetProfileForWeapon(equipped.weapon, equipped.stack && equipped.stack->extra ? equipped.stack->extra.get() : nullptr)
            : WeaponProfile{};
        const bool managed = equipped.weapon && GetWeaponMechanism(equipped.weapon, equipped.ammo, &profile) == WeaponMechanism::Overheat;
        static std::uintptr_t lastLoggedWeapon = 0;
        static std::uint32_t lastLoggedAmmo = 0;
        static bool lastManaged = false;
        static std::string lastLoggedProfile;
        const auto ammoID = equipped.ammo ? equipped.ammo->GetFormID() : 0;
        if (currentID != lastLoggedWeapon || ammoID != lastLoggedAmmo ||
            managed != lastManaged || profile.name != lastLoggedProfile) {
            lastLoggedWeapon = currentID;
            lastLoggedAmmo = ammoID;
            lastManaged = managed;
            lastLoggedProfile = profile.name;
            REX::INFO("[CSF-Heat] weapon={} mechanism={} enabled={} managed={}",
                equipped.weapon ? equipped.weapon->GetFormEditorID() : "<none>",
                equipped.weapon ? static_cast<int>(GetWeaponMechanism(equipped.weapon, equipped.ammo, &profile)) : static_cast<int>(WeaponMechanism::None),
                g_mcmSettings.enableWeaponOverheat.load() ? 1 : 0,
                managed ? 1 : 0);

            if (equipped.weapon) {
                const auto baseCategories = Classification::Evaluate(equipped.weapon, nullptr);
                const auto instanceCategories = Classification::Evaluate(
                    equipped.weapon, equipped.stack && equipped.stack->extra ? equipped.stack->extra.get() : nullptr);
                REX::INFO(
                    "[DEBUG-HEAT-9f2c] weapon={:08X}/{} ammo={:08X}/{} profile={} canJam={} profileMechanism={} "
                    "baseCategories=[{}] instanceCategories=[{}] finalMechanism={} managed={}",
                    equipped.weapon->GetFormID(), equipped.weapon->GetFormEditorID(),
                    ammoID, equipped.ammo ? equipped.ammo->GetFormEditorID() : "<none>",
                    profile.name, profile.canJam ? 1 : 0, static_cast<int>(profile.mechanism),
                    JoinCategories(baseCategories), JoinCategories(instanceCategories),
                    static_cast<int>(GetWeaponMechanism(equipped.weapon, equipped.ammo, &profile)), managed ? 1 : 0);
            }
        }

        g_weaponFaultController.PrepareHeatOwner(currentIdentity);

        if (!managed || !g_mcmSettings.enableWeaponOverheat.load()) {
            g_weaponFaultController.ResetHeatForWeapon(currentIdentity);
            ConditionUI::UpdateHeat(0.0f, false, false);
            return;
        }

        const float coolRate = std::clamp(g_mcmSettings.overheatCooldownRate.load(), 0.0f, 5.0f);
        const auto before = GetWeaponFaultSnapshot();
        const float heat = std::max(0.0f, before.weaponHeat - deltaSeconds * coolRate);
        const auto snapshot = g_weaponFaultController.ApplyHeat(
            currentIdentity,
            heat,
            g_mcmSettings.overheatRecoveryThreshold.load());

        if (before.IsOverheated() && !snapshot.IsOverheated()) {
            RE::SendHUDMessage::ShowHUDMessage("$CSF_OverheatRecovered", nullptr, true, true);
        }
        // Keep an empty slot visible while an overheat-capable weapon is equipped.
        // This makes the feature discoverable and confirms that the weapon was classified correctly.
        ConditionUI::UpdateHeat(heat, snapshot.IsOverheated(), true);
    }

    void ProcessWeaponHeat(RE::Actor* a_actor)
    {
        if (!a_actor || a_actor != RE::PlayerCharacter::GetSingleton() || !g_mcmSettings.enableWeaponOverheat.load()) return;

        UpdateWeaponHeat(true);
        auto equipped = GetEquippedHeatWeapon();
        if (!equipped.weapon || !equipped.stack) return;
        const WeaponProfile profile = GetProfileForWeapon(
            equipped.weapon, equipped.stack->extra ? equipped.stack->extra.get() : nullptr);
        if (GetWeaponMechanism(equipped.weapon, equipped.ammo, &profile) != WeaponMechanism::Overheat) return;
        const auto currentIdentity = BuildWeaponInstanceIdentity(equipped.weapon, equipped.stack);
        SetActiveWeaponInstance(currentIdentity);
        g_weaponFaultController.PrepareHeatOwner(currentIdentity);
        const auto before = GetWeaponFaultSnapshot();
        const float currentHeat = std::clamp(before.weaponHeat, 0.0f, 1.0f);
        const float baseHeatPerShot = std::clamp(g_mcmSettings.overheatHeatPerShot.load(), 0.0f, 1.0f);
        // Diminishing returns make the last part of the heat bar less abrupt while
        // still allowing sustained fire to reach the overheat threshold.
        constexpr float kHeatDamping = 0.35f;
        const float heatPerShot = baseHeatPerShot * (1.0f - currentHeat * kHeatDamping);
        const float heat = std::clamp(currentHeat + heatPerShot, 0.0f, 1.0f);
        const auto snapshot = g_weaponFaultController.ApplyHeat(
            currentIdentity,
            heat,
            g_mcmSettings.overheatRecoveryThreshold.load());
        // Preserve the logical overheat state while the weapon cools. The UI
        // derives its visual phase from both this flag and the heat value, so
        // passing false here would interrupt the red/yellow alert cycle.
        ConditionUI::UpdateHeat(heat, snapshot.IsOverheated(), true);
        if (!before.IsOverheated() && snapshot.IsOverheated()) {
            ConditionUI::UpdateHeat(1.0f, true, true);
            RE::SendHUDMessage::ShowHUDMessage("$CSF_Overheated", "WPNPistol10mmFireDry", true, true);
        }
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
                        if (weap && !GetProfileForWeapon(weap, stack->extra ? stack->extra.get() : nullptr).isExcluded &&
                            GetItemFlags(weap).enableDurabilitySystem) {
                            auto wType = weap->weaponData.type.get();
                            if (wType != RE::WEAPON_TYPE::kHandToHand &&
                                wType != RE::WEAPON_TYPE::kGrenade &&
                                wType != RE::WEAPON_TYPE::kMine) {
                                return true;
                            }
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

    float GetExtraDataDurabilityPercent(RE::TESBoundObject* a_obj, const RE::ExtraDataList* a_extra) {
        if (!a_extra) return 1.0f;

        auto healthExtra = a_extra->GetByType<RE::ExtraHealth>();
        if (healthExtra && healthExtra->health < ENGINE_DEFAULT_HEALTH_THRESHOLD) {
            float result = GetDecompressedPct(a_obj, healthExtra->health);
            if (result < 0.0f) result = 0.0f;
            return result;
        }

        auto chargeExtra = a_extra->GetByType<RE::ExtraCharge>();
        if (chargeExtra) {
            float c = chargeExtra->charge;
            if (c > 1.0f) c /= 100.0f;
            return c > 1.0f ? 1.0f : c;
        }
        return 1.0f;
    }

    static float ExtractConditionPercent(RE::TESBoundObject* a_obj, RE::BGSInventoryItem::Stack* a_stack) {
        return a_stack && a_stack->extra ? GetExtraDataDurabilityPercent(a_obj, a_stack->extra.get()) : 1.0f;
    }

    float GetVisualDurabilityPercent(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack) {
        return ExtractConditionPercent(a_object, a_stack);
    }

    float GetItemDurabilityPercent(RE::BGSInventoryItem* a_item, std::uint32_t a_stackID) {
        if (!a_item || !a_item->object) return 1.0f;

        // Pip-Boy's value query may omit the stack index. Prefer the instance
        // currently selected by the item-card callback before falling back to
        // the first stack, so same-FormID weapons keep their own price.
        std::uint32_t targetStack = a_stackID;
        if (targetStack == 0xFFFFFFFF) {
            const auto hoveredObject = g_hoveredObject.load();
            const auto hoveredStack = g_hoveredStackIndex.load();
            targetStack = (hoveredObject == a_item->object && hoveredStack != 0xFFFFFFFF) ? hoveredStack : 0;
        }

        RE::BGSInventoryItem::Stack* iter = a_item->stackData.get();
        std::uint32_t count = 0;
        while (iter && count < targetStack) {
            iter = iter->nextStack.get();
            count++;
        }

        return ExtractConditionPercent(a_item->object, iter);
    }

    float GetStackDurabilityPercent(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack) {
        return ExtractConditionPercent(a_object, a_stack);
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

    void QueuePlayerArmorRatingRefresh(bool a_force)
    {
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const auto lastMs = g_lastArmorRatingRefreshMs.load(std::memory_order_acquire);
        if (!a_force && lastMs != 0 && nowMs - lastMs < 250) return;

        bool expected = false;
        if (!g_armorRatingRefreshQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
        g_lastArmorRatingRefreshMs.store(nowMs, std::memory_order_release);

        auto task = F4SE::GetTaskInterface();
        if (!task) {
            g_armorRatingRefreshQueued.store(false, std::memory_order_release);
            return;
        }

        task->AddTask([]() {
            RefreshPlayerArmorRatingImpl();
            g_armorRatingRefreshQueued.store(false, std::memory_order_release);
        });
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
                            float realPct = GetDecompressedPct(item.object, healthExtra->health);
                            return std::round(GetEffectiveMaxDurability(item.object, stack->extra.get()) * realPct);
                        }
                    }
                }
            }
        }
        return 1000.0f;
    }

}

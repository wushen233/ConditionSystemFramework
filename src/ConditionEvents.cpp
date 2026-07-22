#include "pch.h"
#include "ConditionCore.h"
#include "ConditionUI.h"
#include "ConditionWorkbench.h"
#include "ProfileManager.h"

#include <Windows.h>
#include <random>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <string>

#include <F4SE/Trampoline.h>
#include <RE/M/Main.h>

namespace ConditionSystem
{
    namespace
    {
        [[nodiscard]] bool IsMenuTransitionActive()
        {
            auto ui = RE::UI::GetSingleton();
            if (ui) {
                if (ui->menuMode != 0) {
                    return true;
                }

                constexpr std::string_view menuNames[] = {
                    "ContainerMenu",
                    "BarterMenu",
                    "PipboyMenu",
                    "Pipboy3DMenu",
                    "PauseMenu",
                    "LooksMenu",
                    "LockpickingMenu",
                    "TerminalMenu",
                    "DialogueMenu",
                    "WorkshopMenu",
                    "ExamineMenu"
                };

                for (auto menuName : menuNames) {
                    if (ui->GetMenuOpen(RE::BSFixedString(menuName))) {
                        return true;
                    }
                }
            }

            if (auto main = RE::Main::GetSingleton(); main && main->inMenuMode) {
                return true;
            }

            return false;
        }

        struct EquippedAmmoSnapshot
        {
            bool hasWeapon{ false };
            bool usesAmmo{ false };
            std::uint32_t ammoFormID{ 0 };
            std::uint32_t count{ 0 };
        };

        struct WeaponFireGateState
        {
            bool hasBaseline{ false };
            std::uint32_t ammoFormID{ 0 };
            std::uint32_t count{ 0 };
        };

        std::mutex s_weaponFireGateMutex;
        WeaponFireGateState s_weaponFireGate;

        [[nodiscard]] EquippedAmmoSnapshot GetEquippedAmmoSnapshot(RE::Actor* a_actor)
        {
            EquippedAmmoSnapshot snapshot{};
            if (!a_actor || !a_actor->inventoryList) return snapshot;

            for (auto& item : a_actor->inventoryList->data) {
                if (!item.object || !item.object->Is(RE::ENUM_FORM_ID::kWEAP)) continue;

                auto weapon = item.object->As<RE::TESObjectWEAP>();
                if (!weapon) continue;

                auto wType = weapon->weaponData.type.get();
                if (wType == RE::WEAPON_TYPE::kGrenade || wType == RE::WEAPON_TYPE::kMine) continue;

                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (!stack->IsEquipped()) continue;

                    snapshot.hasWeapon = true;

                    RE::TESAmmo* ammo = nullptr;
                    if (stack->extra) {
                        auto instExtra = stack->extra->GetByType<RE::ExtraInstanceData>();
                        if (instExtra && instExtra->data) {
                            auto instData = static_cast<RE::TESObjectWEAP::InstanceData*>(instExtra->data.get());
                            ammo = instData->ammo;
                        }
                    }
                    if (!ammo) ammo = weapon->weaponData.ammo;
                    if (!ammo) return snapshot;

                    snapshot.usesAmmo = true;
                    snapshot.ammoFormID = ammo->GetFormID();
                    a_actor->GetItemCount(snapshot.count, ammo, false);
                    return snapshot;
                }
            }

            return snapshot;
        }

        void RefreshWeaponFireBaseline(RE::Actor* a_actor)
        {
            auto snapshot = GetEquippedAmmoSnapshot(a_actor);
            std::lock_guard<std::mutex> lock(s_weaponFireGateMutex);

            if (!snapshot.hasWeapon || !snapshot.usesAmmo) {
                s_weaponFireGate = {};
                return;
            }

            s_weaponFireGate.hasBaseline = true;
            s_weaponFireGate.ammoFormID = snapshot.ammoFormID;
            s_weaponFireGate.count = snapshot.count;
        }

        [[nodiscard]] bool ShouldAcceptWeaponFireEvent(RE::Actor* a_actor)
        {
            auto snapshot = GetEquippedAmmoSnapshot(a_actor);
            std::lock_guard<std::mutex> lock(s_weaponFireGateMutex);

            if (!snapshot.hasWeapon || !snapshot.usesAmmo) {
                s_weaponFireGate = {};
                return false;
            }

            if (!s_weaponFireGate.hasBaseline || s_weaponFireGate.ammoFormID != snapshot.ammoFormID) {
                s_weaponFireGate.hasBaseline = true;
                s_weaponFireGate.ammoFormID = snapshot.ammoFormID;
                s_weaponFireGate.count = snapshot.count;
                return false;
            }

            if (snapshot.count < s_weaponFireGate.count) {
                s_weaponFireGate.count = snapshot.count;
                return true;
            }

            s_weaponFireGate.count = snapshot.count;
            return false;
        }
    }

    // =========================================================================
    // TESObjectLoadedEvent 源修复
    // =========================================================================

    RE::BSTEventSource<RE::TESObjectLoadedEvent>* GetLoadedEventSourceFix()
    {
        return RE::TESObjectLoadedEvent::GetEventSource();
    }

    // =========================================================================
    // 动画事件钩子 (vtable 劫持)
    // =========================================================================

    using ProcessAnimEvent_t = RE::BSEventNotifyControl(RE::BSTEventSink<RE::BSAnimationGraphEvent>*, const RE::BSAnimationGraphEvent&, RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
    static ProcessAnimEvent_t* _ProcessAnimEvent_Original = nullptr;

    RE::BSEventNotifyControl ProcessAnimEvent_Hook(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this, const RE::BSAnimationGraphEvent& a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
    {
        static const RE::BSFixedString tag_weaponDraw("weaponDraw");
        static const RE::BSFixedString tag_heavyWeaponDraw("heavyWeaponDraw");
        static const RE::BSFixedString tag_weaponSheathe("weaponSheathe");
        static const RE::BSFixedString tag_heavyWeaponSheathe("heavyWeaponSheathe");
        static const RE::BSFixedString tag_weaponFire("weaponFire");
        static const RE::BSFixedString tag_AimStart("AimStart");
        static const RE::BSFixedString tag_IronSightsEnter("IronSightsEnter");
        static const RE::BSFixedString tag_reloadStart("reloadStart");
        static const RE::BSFixedString tag_reloadComplete("reloadComplete");
        static const RE::BSFixedString tag_weaponReloadComplete("weaponReloadComplete");

        // 状态初始化逻辑
        if (!g_initialUISyncDone.load()) {
            float currentPercent = GetEquippedWeaponDurabilityPercent();
            float currentPoints = GetEquippedWeaponDurabilityPoints();
            UpdateHUDDurabilityWidget(currentPercent, currentPoints, true);
            g_initialUISyncDone.store(true);
        }

        // 1. 处理拔枪/收枪 (UI 刷新)
        if (a_event.tag == tag_weaponDraw || a_event.tag == tag_heavyWeaponDraw) {
            if (auto player = RE::PlayerCharacter::GetSingleton(); ConditionSystem::g_mcmSettings.enableLogging.load()) {
                REX::INFO("[CSF-WidgetDiag] AnimEvent tag={} menuTransition={} cachedBefore={} magicDrawn={} weaponState={}",
                    a_event.tag.c_str(),
                    IsMenuTransitionActive() ? 1 : 0,
                    g_isWeaponDrawn.load() ? 1 : 0,
                    (player && player->GetWeaponMagicDrawn()) ? 1 : 0,
                    player ? static_cast<int>(player->weaponState) : -1);
            }
            g_isWeaponDrawn.store(true);
            ConditionUI::EvaluateVisibility(true);
            auto player = RE::PlayerCharacter::GetSingleton();
            InitializeEquippedWeapon(player);
            RefreshWeaponFireBaseline(player);
            return _ProcessAnimEvent_Original(a_this, a_event, a_source);
        }
        else if (a_event.tag == tag_weaponSheathe || a_event.tag == tag_heavyWeaponSheathe) {
            const bool menuTransition = IsMenuTransitionActive();
            if (auto player = RE::PlayerCharacter::GetSingleton(); ConditionSystem::g_mcmSettings.enableLogging.load()) {
                REX::INFO("[CSF-WidgetDiag] AnimEvent tag={} menuTransition={} cachedBefore={} magicDrawn={} weaponState={} willClearCached={}",
                    a_event.tag.c_str(),
                    menuTransition ? 1 : 0,
                    g_isWeaponDrawn.load() ? 1 : 0,
                    (player && player->GetWeaponMagicDrawn()) ? 1 : 0,
                    player ? static_cast<int>(player->weaponState) : -1,
                    menuTransition ? 0 : 1);
            }
            if (!menuTransition) {
                g_isWeaponDrawn.store(false);
            }
            ConditionUI::EvaluateVisibility(true);
            return _ProcessAnimEvent_Original(a_this, a_event, a_source);
        }

        // 2. 处理开火 (WeaponFire)
        if (a_event.tag == tag_weaponFire) {
            if (ConditionSystem::g_mcmSettings.enableLogging.load()) {
                REX::INFO("[DEBUG] 动画事件触发: {}", a_event.tag.c_str());
            }

            auto player = RE::PlayerCharacter::GetSingleton();
            if (!ShouldAcceptWeaponFireEvent(player)) {
                if (ConditionSystem::g_mcmSettings.enableLogging.load()) {
                    REX::INFO("[CSF] Ignored weaponFire without ammo consumption: payload={}", a_event.payload.c_str() ? a_event.payload.c_str() : "");
                }
                return _ProcessAnimEvent_Original(a_this, a_event, a_source);
            }

            if (ConditionSystem::g_mcmSettings.iJammingPhase.load() == 0) {
                std::string payloadStr = a_event.payload.c_str() ? a_event.payload.c_str() : "";
                bool isBashing = false;
                if (player) player->GetGraphVariableImplBool("bIsBashing", isBashing);

                if (payloadStr != "2" && !isBashing) {
                    DeductEquippedWeaponDurability(player, false);
                }
            }
            else {
                // Phase 1 下开火只扣耐久，不卡壳
                DeductEquippedWeaponDurability(player, false);
            }
        }

        // 3. 处理换弹完成 (ReloadComplete) - 仅限 Phase 1
        else if (a_event.tag == tag_reloadComplete || a_event.tag == tag_weaponReloadComplete) {
            RefreshWeaponFireBaseline(RE::PlayerCharacter::GetSingleton());
            if (g_isWeaponJammed.load()) {
                g_isWeaponJammed.store(false);
                g_jammedWeaponUniqueID.store(0);
                RE::SendHUDMessage::ShowHUDMessage("$CSF_UnjamSuccessAlt", nullptr, true, true);
            }
            else if (ConditionSystem::g_mcmSettings.iJammingPhase.load() == 1) {
                float condition = GetEquippedWeaponDurabilityPercent();
                if (RollForJam(condition)) {
                    g_isWeaponJammed.store(true);

                    if (g_mcmSettings.quickUnjam.load()) {
                        RE::SendHUDMessage::ShowHUDMessage("$CSF_ReloadJamFailed", "UIActionDeny", true, true);
                    }
                    else {
                        auto player = RE::PlayerCharacter::GetSingleton();
                        if (player) TriggerFullUnjam(static_cast<RE::Actor*>(player));
                    }
                }
            }
        }
        else if (a_event.tag == tag_AimStart || a_event.tag == tag_IronSightsEnter) {
            RefreshWeaponFireBaseline(RE::PlayerCharacter::GetSingleton());
        }

        return _ProcessAnimEvent_Original(a_this, a_event, a_source);
    }

    // =========================================================================
    // HitEventHandler: 物理击中事件
    // =========================================================================

    class HitEventHandler : public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        static HitEventHandler* GetSingleton() {
            static HitEventHandler singleton;
            return &singleton;
        }

        virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent& a_event, RE::BSTEventSource<RE::TESHitEvent>*) override {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player) return RE::BSEventNotifyControl::kContinue;

            RE::TESObjectREFR* target = GetRawPtr(a_event.target);
            RE::TESObjectREFR* cause = GetRawPtr(a_event.cause);

            if (cause == player && target != player) {
                bool isActualMeleeHit = true;
                if (a_event.projectileFormID != 0) isActualMeleeHit = false;
                if (a_event.usesHitData && a_event.hitData.flags.any(RE::HitData::Flag::kExplosion)) isActualMeleeHit = false;

                auto sourceForm = RE::TESForm::GetFormByID(a_event.sourceFormID);
                if (!sourceForm && a_event.usesHitData && a_event.hitData.sourceRef) {
                    auto ref = a_event.hitData.sourceRef.get();
                    if (ref) sourceForm = ref->GetObjectReference();
                }

                if (sourceForm) {
                    if (auto weap = sourceForm->As<RE::TESObjectWEAP>()) {
                        auto wType = weap->weaponData.type.get();
                        if (wType == RE::WEAPON_TYPE::kGrenade || wType == RE::WEAPON_TYPE::kMine) isActualMeleeHit = false;
                    }
                    else if (sourceForm->Is(RE::ENUM_FORM_ID::kHAZD) || sourceForm->Is(RE::ENUM_FORM_ID::kSPEL) || sourceForm->Is(RE::ENUM_FORM_ID::kENCH)) {
                        isActualMeleeHit = false;
                    }
                    else if (sourceForm->Is(RE::ENUM_FORM_ID::kPROJ)) {
                        isActualMeleeHit = false;
                    }
                }

                if (isActualMeleeHit) {
                    DeductEquippedWeaponDurability(player, true);
                }
            }

            if (target == player) {
                static std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> s_hitThrottleMap;
                static std::mutex s_throttleMutex;

                std::uint32_t attackerID = cause ? cause->GetFormID() : 0;
                auto now = std::chrono::steady_clock::now();

                long long throttleLimitMs = 500;
                std::string attackTypeStr = "未知来源";

                bool isContinuous = false;
                bool isBash = false;
                bool isExplosion = false;
                bool isEnvironmental = (!cause || !cause->Is(RE::ENUM_FORM_ID::kACHR));

                if (a_event.usesHitData) {
                    if (a_event.hitData.flags.any(RE::HitData::Flag::kExplosion)) isExplosion = true;
                    auto rawAtkData = GetRawPtr(a_event.hitData.attackData);
                    if (rawAtkData) {
                        if ((rawAtkData->data.flags & 0x40) != 0) isContinuous = true;
                        if ((rawAtkData->data.flags & 0x04) != 0) isBash = true;
                    }
                }

                auto sourceForm = RE::TESForm::GetFormByID(a_event.sourceFormID);
                if (!sourceForm && a_event.usesHitData && a_event.hitData.sourceRef) {
                    auto ref = a_event.hitData.sourceRef.get();
                    if (ref) sourceForm = ref->GetObjectReference();
                }

                if (sourceForm && sourceForm->Is(RE::ENUM_FORM_ID::kEXPL)) {
                    isExplosion = true;
                }

                bool isAutomaticGun = false;
                bool isGunOrProjectile = false;
                bool isMeleeWeapon = false;

                if (sourceForm) {
                    if (auto weap = sourceForm->As<RE::TESObjectWEAP>()) {
                        auto wType = weap->weaponData.type.get();
                        if (wType == RE::WEAPON_TYPE::kHandToHand ||
                            wType == RE::WEAPON_TYPE::kOneHandSword || wType == RE::WEAPON_TYPE::kOneHandDagger ||
                            wType == RE::WEAPON_TYPE::kOneHandAxe || wType == RE::WEAPON_TYPE::kOneHandMace ||
                            wType == RE::WEAPON_TYPE::kTwoHandSword || wType == RE::WEAPON_TYPE::kTwoHandAxe) {
                            isMeleeWeapon = true;
                        }
                        else {
                            isGunOrProjectile = true;
                        }
                        if (weap->weaponData.flags.any(RE::WEAPON_FLAGS::kAutomatic)) {
                            isAutomaticGun = true;
                        }
                    }
                    else if (sourceForm->Is(RE::ENUM_FORM_ID::kPROJ)) {
                        isGunOrProjectile = true;
                    }
                    else if (sourceForm->Is(RE::ENUM_FORM_ID::kHAZD) || sourceForm->Is(RE::ENUM_FORM_ID::kSPEL) || sourceForm->Is(RE::ENUM_FORM_ID::kENCH)) {
                        isContinuous = true;
                    }
                }

                if (!sourceForm && a_event.projectileFormID == 0 && !isExplosion && !isContinuous) {
                    isMeleeWeapon = true;
                }

                if (!a_event.usesHitData && (isGunOrProjectile || isMeleeWeapon) && !isExplosion) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (isExplosion) {
                    throttleLimitMs = 0;
                    attackTypeStr = isEnvironmental ? "环境爆炸/汽车/油桶" : "敌人爆炸物/手雷/导弹";
                }
                else if (isAutomaticGun || isContinuous) {
                    throttleLimitMs = 120;
                    attackTypeStr = "全自动火力/持续/附魔燃烧";
                }
                else if (isGunOrProjectile && !isBash) {
                    throttleLimitMs = 40;
                    attackTypeStr = "远程单发枪械/散弹/弹射物";
                }
                else if (isBash || isMeleeWeapon) {
                    throttleLimitMs = 500;
                    attackTypeStr = isEnvironmental ? "陷阱物理撞击" : "近战武器/怪物生物撕咬/枪托";
                }
                else {
                    throttleLimitMs = 250;
                    attackTypeStr = "附加特效/未知物理判定";
                }

                if (throttleLimitMs > 0) {
                    std::lock_guard<std::mutex> lock(s_throttleMutex);
                    if (s_hitThrottleMap.find(attackerID) != s_hitThrottleMap.end()) {
                        auto timeSinceLastHit = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_hitThrottleMap[attackerID]).count();
                        if (timeSinceLastHit < throttleLimitMs) {
                            return RE::BSEventNotifyControl::kContinue;
                        }
                    }
                    s_hitThrottleMap[attackerID] = now;
                }

                static auto s_lastCleanupTime = now;
                if (std::chrono::duration_cast<std::chrono::seconds>(now - s_lastCleanupTime).count() > 60) {
                    std::lock_guard<std::mutex> lock(s_throttleMutex);
                    for (auto it = s_hitThrottleMap.begin(); it != s_hitThrottleMap.end();) {
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count() > 10000) {
                            it = s_hitThrottleMap.erase(it);
                        }
                        else {
                            ++it;
                        }
                    }
                    s_lastCleanupTime = now;
                }

                if (g_mcmSettings.enableLogging.load()) {
                    REX::INFO("[事件拦截] 遭遇 [{}]！节流保护:{}ms。开始结算护甲磨损...", attackTypeStr, throttleLimitMs);
                }
                DeductEquippedArmorDurability(player, a_event);
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // =========================================================================
    // MagicEffectApplyEventHandler: 魔法/环境侵蚀事件
    // =========================================================================

    class MagicEffectApplyEventHandler : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
    {
    public:
        static MagicEffectApplyEventHandler* GetSingleton() {
            static MagicEffectApplyEventHandler singleton;
            return &singleton;
        }

        virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent& a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player) return RE::BSEventNotifyControl::kContinue;

            RE::TESObjectREFR* target = GetRawPtr(a_event.target);
            if (target != player) return RE::BSEventNotifyControl::kContinue;

            RE::TESObjectREFR* caster = GetRawPtr(a_event.caster);

            if (caster == player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            bool isEnvironmental = (!caster || !caster->Is(RE::ENUM_FORM_ID::kACHR));

            if (g_mcmSettings.enableLogging.load()) {
                std::string sourceStr = isEnvironmental ? "环境陷阱/辐射源/地雷机关" : "敌人魔法/附魔/毒气";
                REX::INFO("[魔法事件] 遭到 [{}] 的能量侵蚀，开始交由元素/环境磨损模块结算...", sourceStr);
            }

            DeductEquippedArmorDurabilityFromMagic(player, a_event);

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // =========================================================================
    // PickupEventHandler: 拾取物品事件
    // =========================================================================

    class PickupEventHandler : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
    public:
        static PickupEventHandler* GetSingleton() {
            static PickupEventHandler singleton;
            return &singleton;
        }
        virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent& a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override {
            if (ConditionSystem::g_mcmSettings.randomLootDurability.load()) {
                auto player = RE::PlayerCharacter::GetSingleton();
                if (player && a_event.newContainerFormID == player->GetFormID()) {
                    if (auto task = F4SE::GetTaskInterface()) {
                        task->AddTask([]() {
                            if (g_mcmSettings.enableLogging.load()) REX::INFO("[PickupEvent] 玩家捡起物品，触发背包耐久初始化任务...");
                            ConditionSystem::RandomizeNewPlayerItems();
                            });
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // =========================================================================
    // MenuEventHandler: 菜单开关事件
    // =========================================================================

    class MenuEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuEventHandler* GetSingleton() {
            static MenuEventHandler singleton;
            return &singleton;
        }
        virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            auto runSyncWakeUp = []() {
                if (ConditionSystem::g_mcmSettings.randomLootDurability.load()) {
                    auto invIntfc = RE::BGSInventoryInterface::GetSingleton();
                    if (invIntfc) {
                        for (auto& agent : invIntfc->agentArray) {
                            if (agent.itemOwner) {
                                auto targetRef = agent.itemOwner.get().get();
                                if (targetRef && targetRef != RE::PlayerCharacter::GetSingleton() && targetRef->inventoryList) {
                                    if (g_mcmSettings.enableLogging.load()) REX::INFO("[MenuEvent] 菜单打开，同步强制唤醒容器 {:X} 内所有白板物品！", targetRef->GetFormID());
                                    for (auto& item : targetRef->inventoryList->data) {
                                        if (item.object && ConditionSystem::GetItemFlagsRaw(item.object).enableDurabilitySystem) {
                                            for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                                                auto healthExtra = stack->extra ? stack->extra->GetByType<RE::ExtraHealth>() : nullptr;
                                                if (!stack->extra || !healthExtra || healthExtra->health > ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                                                    ConditionSystem::WakeUpAndRandomizeSingleStack(item.object, stack, true);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                };

            if (a_event.opening) {
                if (a_event.menuName == "ContainerMenu" || a_event.menuName == "BarterMenu") {
                    runSyncWakeUp();
                }
            }
            else {
                if (a_event.menuName == "LockpickingMenu" || a_event.menuName == "TerminalMenu") {
                    runSyncWakeUp();
                }
                if (a_event.menuName == "ArmorMenu" || a_event.menuName == "WeaponMenu" ||
                    a_event.menuName == "WorkshopMenu" || a_event.menuName == "CookingMenu" ||
                    a_event.menuName == "ExamineMenu" || a_event.menuName == "ConsoleMenu" ||
                    (a_event.menuName == "DialogueMenu" && ConditionSystem::g_mcmSettings.dialogueFullDurability.load())) {

                    if (auto task = F4SE::GetTaskInterface()) {
                        task->AddTask([]() { ConditionSystem::GrandfatherPlayerInventory(); });
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // =========================================================================
    // ObjectLoadedEventHandler: 物品加载事件
    // =========================================================================

    class ObjectLoadedEventHandler : public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
    public:
        static ObjectLoadedEventHandler* GetSingleton() {
            static ObjectLoadedEventHandler singleton;
            return &singleton;
        }

        virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent& a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override {
            if (a_event.loaded && ConditionSystem::g_mcmSettings.randomLootDurability.load()) {
                auto form = RE::TESForm::GetFormByID(a_event.formID);
                if (form) {
                    auto refr = form->As<RE::TESObjectREFR>();
                    if (refr) {
                        bool shouldProcess = false;
                        if (refr->Is(RE::ENUM_FORM_ID::kACHR) || refr->Is(RE::ENUM_FORM_ID::kCONT)) {
                            if (refr != RE::PlayerCharacter::GetSingleton()) {
                                shouldProcess = true;
                            }
                        }
                        else {
                            auto baseObj = refr->GetObjectReference();
                            if (baseObj && ConditionSystem::GetItemFlagsRaw(baseObj).enableDurabilitySystem) {
                                shouldProcess = true;
                            }
                        }

                        if (shouldProcess) {
                            if (auto task = F4SE::GetTaskInterface()) {
                                std::uint32_t refrID = a_event.formID;
                                task->AddTask([refrID]() {
                                    auto r = RE::TESForm::GetFormByID(refrID);
                                    if (r) {
                                        if (auto targetRef = r->As<RE::TESObjectREFR>()) {
                                            if (targetRef->Is(RE::ENUM_FORM_ID::kACHR) || targetRef->Is(RE::ENUM_FORM_ID::kCONT)) {
                                                ConditionSystem::RandomizeInventoryDurability(targetRef);
                                            }
                                            else {
                                                ConditionSystem::RandomizeWorldObject(targetRef, targetRef->GetObjectReference());
                                            }
                                        }
                                    }
                                    });
                            }
                        }
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // =========================================================================
    // 事件注册入口
    // =========================================================================

    void RegisterEvents()
    {
        auto hitEventSource = RE::TESHitEvent::GetEventSource();
        if (hitEventSource) {
            hitEventSource->RegisterSink(HitEventHandler::GetSingleton());
            if (g_mcmSettings.enableLogging.load()) REX::INFO("[ConditionSystem] 物理击中 (TESHitEvent) 监听器原生注册成功！");
        }

        auto magicEventSource = RE::TESMagicEffectApplyEvent::GetEventSource();
        if (magicEventSource) {
            magicEventSource->RegisterSink(MagicEffectApplyEventHandler::GetSingleton());
            if (g_mcmSettings.enableLogging.load()) REX::INFO("[ConditionSystem] 魔法/环境侵蚀 (TESMagicEffectApplyEvent) 监听器原生注册成功！");
        }

        auto loadedEventSource = GetLoadedEventSourceFix();
        if (loadedEventSource) {
            loadedEventSource->RegisterSink(ObjectLoadedEventHandler::GetSingleton());
        }

        auto containerSource = RE::TESContainerChangedEvent::GetEventSource();
        if (containerSource) {
            containerSource->RegisterSink(PickupEventHandler::GetSingleton());
        }

        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(MenuEventHandler::GetSingleton());
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto sink = static_cast<RE::BSTEventSink<RE::BSAnimationGraphEvent>*>(player);
            uintptr_t* vtable = *(uintptr_t**)sink;
            if (reinterpret_cast<ProcessAnimEvent_t*>(vtable[1]) != ProcessAnimEvent_Hook) {
                _ProcessAnimEvent_Original = reinterpret_cast<ProcessAnimEvent_t*>(vtable[1]);
                DWORD oldProtect;
                VirtualProtect(&vtable[1], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
                vtable[1] = reinterpret_cast<uintptr_t>(ProcessAnimEvent_Hook);
                VirtualProtect(&vtable[1], sizeof(void*), oldProtect, &oldProtect);
            }
        }
    }
}

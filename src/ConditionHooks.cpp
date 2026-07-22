#include "pch.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <Windows.h>

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>

#include "ConditionHooks.h"
#include "ConditionCore.h"
#include "ConditionUI.h"
#include "Repair/RepairSystem.h"
#include "ConditionWorkbench.h"
#include "IIF_API.h" 

#include "RE/B/BGSInventoryItem.h"
#include "RE/P/PipboyInventoryData.h"
#include "RE/P/PipboyObject.h"
#include "RE/P/PipboyPrimitiveValue.h"

#include <MinHook.h>

namespace ConditionSystem::Repair {
    extern std::atomic<RE::TESBoundObject*> g_lockedTargetObject;
    extern std::atomic<RE::BGSInventoryItem::Stack*> g_lockedTargetStack;
    void OpenJuryRiggingMenu(std::uint32_t a_itemIndex);
    void ProcessPendingDeletions();
    void InjectConditionSystemCallback(Scaleform::GFx::Movie* movie);
}

namespace ConditionSystem::Hooks
{
    using namespace RE;

    using EventFunc_t = void(*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
    static EventFunc_t _PerformInputProcessing_Original = nullptr;

    // =========================================================================
    // EquipObject 钩子：拦截非辅助品类型的修理工具
    // 当点击杂项/垃圾等不可装备的物品时，直接触发修理逻辑，
    // 取代原版的 "无法装备" 提示。
    // =========================================================================
    using EquipObjectFunc_t = bool(*)(RE::ActorEquipManager*, RE::Actor*, const RE::BGSObjectInstance&, std::uint32_t, std::uint32_t, const RE::BGSEquipSlot*, bool, bool, bool, bool, bool);
    static EquipObjectFunc_t _EquipObject_Original = nullptr;

    bool EquipObject_Hook(
        RE::ActorEquipManager* a_this,
        RE::Actor* a_actor,
        const RE::BGSObjectInstance& a_object,
        std::uint32_t a_stackID,
        std::uint32_t a_number,
        const RE::BGSEquipSlot* a_slot,
        bool a_queueEquip,
        bool a_forceEquip,
        bool a_playSounds,
        bool a_applyNow,
        bool a_locked)
    {
        // 仅拦截玩家的操作
        if (a_actor && a_object.object) {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (a_actor == player) {
                RE::TESBoundObject* boundObj = a_object.object->As<RE::TESBoundObject>();
                // 跳过辅助品（kALCH），由现有的 ActorEquipManagerEvent 处理
                if (boundObj && !boundObj->Is(RE::ENUM_FORM_ID::kALCH)) {
                    auto profile = ConditionSystem::Repair::GetRepairKitProfile(boundObj);
                    if (profile.isValid) {
                        ConditionSystem::Repair::RepairAmountData amount = ConditionSystem::Repair::CalculateRepairAmount(profile);
                        bool didRepair = false;

                        if (profile.canRepairWeapon)
                            didRepair |= ConditionSystem::Repair::RepairEquippedWeaponLogic(player, amount, profile.maxConditionLimit);
                        if (profile.canRepairArmor)
                            didRepair |= ConditionSystem::Repair::SmartCascadeArmorRepair(player, amount, profile.maxConditionLimit);

                        if (didRepair) {
                            RE::SendHUDMessage::ShowHUDMessage("$CSF_ToolRepairDone", "UIPipBoySDCardEquip", true, true);
                            // 非辅助品不会被引擎自动消耗，手动移除
                            if (auto task = F4SE::GetTaskInterface()) {
                                task->AddTask([player, boundObj]() {
                                    if (player && boundObj) {
                                        RE::TESObjectREFR::RemoveItemData rmData(boundObj, 1);
                                        player->RemoveItem(rmData);
                                    }
                                });
                            }
                        }
                        else {
                            RE::SendHUDMessage::ShowHUDMessage("$CSF_ToolRepairNotNeeded", nullptr, true, true);
                        }

                        return true;  // 跳过原始 EquipObject，阻止 "无法装备" 提示
                    }
                }
            }
        }
        return _EquipObject_Original(a_this, a_actor, a_object, a_stackID, a_number, a_slot, a_queueEquip, a_forceEquip, a_playSounds, a_applyNow, a_locked);
    }

    static RE::BSTSmartPointer<RE::BSInputEnableLayer> g_jamVatsLayer;

    // =========================================================================
    // 现代事件驱动优化：取代老旧高频输入线程里的 ExamineMenu 轮询
    // =========================================================================
    class ExamineMenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static ExamineMenuSink* GetSingleton() { static ExamineMenuSink singleton; return &singleton; }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            if (a_event.menuName == "ExamineMenu") {
                ConditionSystem::Workbench::ClearRuntimeSelection();
            }
            if (a_event.menuName == "ExamineMenu" && a_event.opening) {
                auto ui = RE::UI::GetSingleton();
                auto menu = ui ? ui->GetMenu("ExamineMenu") : nullptr;
                if (menu && menu->uiMovie) {
                    ConditionSystem::Repair::InjectConditionSystemCallback(menu->uiMovie.get());
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // =========================================================================
    // 核心输入顶级拦截（保留卡壳、排障与拔枪状态硬控制）
    // =========================================================================
    static void PerformInputProcessing_Hook(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead) {
        // 1. VATS 限制层动态管理
        if (ConditionSystem::g_isWeaponJammed.load()) {
            if (!g_jamVatsLayer) {
                auto inputMgr = RE::BSInputEnableManager::GetSingleton();
                if (inputMgr) {
                    inputMgr->AllocateNewLayer(g_jamVatsLayer, "ConditionSystem_JamLayer");
                    if (g_jamVatsLayer) {
                        inputMgr->EnableOtherEvent(g_jamVatsLayer->layerID, RE::OtherInputEvents::OTHER_EVENT_FLAG::kVATS, false, static_cast<RE::UserEvents::SENDER_ID>(1));
                    }
                }
            }
        }
        else if (g_jamVatsLayer) {
            g_jamVatsLayer.reset();
        }

        // 2. Pipboy 渲染消隐计时器评估
        int timer = ConditionSystem::g_pipboyTransitionTimer.load();
        if (timer > 0) {
            ConditionSystem::g_pipboyTransitionTimer.fetch_sub(1);
            if (timer == 15) {
                auto player = RE::PlayerCharacter::GetSingleton();
                if (player && player->currentProcess) {
                    player->currentProcess->SetWeaponBonesCulled(*player, true, RE::WEAPON_CULL_TYPE::kGeneral);
                }
            }
            if (timer == 1) {
                ConditionSystem::g_pipboyTransitionTimer.store(-1);
            }
        }

        // 3. 游戏真实按键控制流
        auto ui = RE::UI::GetSingleton();
        bool inMenu = ui && (ui->GetMenuOpen("Pipboy3DMenu") || ui->GetMenuOpen("PipboyMenu") || ui->GetMenuOpen("PauseMenu"));

        if (!inMenu) {
            if (auto player = RE::PlayerCharacter::GetSingleton()) {
                ConditionSystem::Repair::ProcessPendingDeletions();

                for (auto event = a_queueHead; event; event = event->next) {
                    if (event->eventType == RE::INPUT_EVENT_TYPE::kButton) {
                        auto buttonEvent = const_cast<RE::ButtonEvent*>(static_cast<const RE::ButtonEvent*>(event));
                        if (buttonEvent) {
                            auto userEvent = buttonEvent->strUserEvent;

                            // 收枪长按检测
                            if (buttonEvent->value > 0.0f) {
                                if ((userEvent == "ReadyWeapon" || userEvent == "Reload") && buttonEvent->heldDownSecs > 0.4f) {
                                    if (ConditionSystem::g_isWeaponDrawn.load()) {
                                        ConditionSystem::g_isWeaponDrawn.store(false);
                                        ConditionSystem::ConditionUI::EvaluateVisibility();
                                    }
                                }
                            }

                            // 卡壳阻止 VATS
                            if (userEvent == "VATS" && ConditionSystem::g_isWeaponJammed.load()) {
                                if (buttonEvent->value > 0.0f && buttonEvent->heldDownSecs == 0.0f) {
                                    RE::SendHUDMessage::ShowHUDMessage("$CSF_VATSJamMessage", "UIActionDeny", true, true);
                                }
                            }

                            // ✅ 核心修改：排障逻辑 (彻底删除了 weaponIdle，恢复无缝静默拦截)
                            if (userEvent == "PrimaryAttack" && ConditionSystem::g_isWeaponJammed.load()) {
                                if (buttonEvent->heldDownSecs == 0.0f && !ConditionSystem::g_isUnjamming.load()) {

                                    if (ConditionSystem::g_mcmSettings.enableLogging.load()) {
                                        REX::INFO("[DEBUG] 触发排障！模式: {}", ConditionSystem::g_mcmSettings.quickUnjam.load() ? "快速" : "完整");
                                    }

                                    if (ConditionSystem::g_mcmSettings.quickUnjam.load()) {
                                        // 快速排障逻辑
                                        ConditionSystem::g_isWeaponJammed.store(false);
                                        ConditionSystem::g_jammedWeaponUniqueID.store(0);
                                        ConditionSystem::ConditionUI::ShowUnjammingUI(false);
                                        RE::SendHUDMessage::ShowHUDMessage("$CSF_QuickUnjamSuccess", "WPNPistol10mmFireDry", true, true);
                                    }
                                    else {
                                        // 完整排障逻辑 (调用外部统一接口)
                                        if (ConditionSystem::g_mcmSettings.enableLogging.load()) {
                                            REX::INFO("[DEBUG] 执行开火键手动完整排障流程...");
                                        }
                                        ConditionSystem::TriggerFullUnjam(static_cast<RE::Actor*>(player));
                                    }
                                }
                                // 💡 就是这行代码：静默吞掉开火信号！不会有任何动画抽搐，枪就是开不了火！
                                buttonEvent->value = 0.0f;
                            }
                        }
                    }
                }
            }
        }

        // 4. L3 (左摇杆按下) 在 Pip-Boy 中触发维修菜单
        if (ui && ui->GetMenuOpen("PipboyMenu")) {
            for (auto event = a_queueHead; event; event = event->next) {
                if (event->eventType == RE::INPUT_EVENT_TYPE::kButton) {
                    auto buttonEvent = const_cast<RE::ButtonEvent*>(static_cast<const RE::ButtonEvent*>(event));
                    if (buttonEvent && buttonEvent->GetBSButtonCode() == RE::BS_BUTTON_CODE::kLStick && buttonEvent->value > 0.0f && buttonEvent->heldDownSecs == 0.0f) {
                        auto pipboyMenu = ui->GetMenu("PipboyMenu");
                        if (pipboyMenu && pipboyMenu->uiMovie) {
                            ConditionSystem::Repair::InjectConditionSystemCallback(pipboyMenu->uiMovie.get());
                            pipboyMenu->uiMovie->Invoke("root.OnL3Press_Call", nullptr, nullptr, 0);
                            buttonEvent->value = 0.0f;
                        }
                    }
                }
            }
        }

        // 5. X/Square 按钮在 Pip-Boy 维修模式中转发到 JuryRiggingMenu
        if (ui && ui->GetMenuOpen("PipboyMenu")) {
            for (auto event = a_queueHead; event; event = event->next) {
                if (event->eventType == RE::INPUT_EVENT_TYPE::kButton) {
                    auto buttonEvent = const_cast<RE::ButtonEvent*>(static_cast<const RE::ButtonEvent*>(event));
                    if (buttonEvent && buttonEvent->value > 0.0f && buttonEvent->heldDownSecs == 0.0f) {
                        auto userEvent = buttonEvent->strUserEvent;
                        if (userEvent == "XButton") {
                            auto pipboyMenu = ui->GetMenu("PipboyMenu");
                            if (pipboyMenu && pipboyMenu->uiMovie) {
                                Scaleform::GFx::Value isRepairMode;
                                pipboyMenu->uiMovie->GetVariable(&isRepairMode, "root.Menu_mc.BottomBar_mc.modIsRepairMode");
                                if (isRepairMode.IsBoolean() && isRepairMode.GetBoolean()) {
                                    pipboyMenu->uiMovie->Invoke("root.OnControllerXButton_Call", nullptr, nullptr, 0);
                                    buttonEvent->value = 0.0f;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (_PerformInputProcessing_Original) {
            _PerformInputProcessing_Original(a_this, a_queueHead);
        }
    }

    // =========================================================================
    // Render condition and overflow segments through the IIF item-card API.
    // =========================================================================
    void OnIIFMessage_CND(IIF_API::UpdateMessage* msg) {
        if (!msg || !msg->inventoryItem) return;

        auto a_item = static_cast<RE::BGSInventoryItem*>(msg->inventoryItem);
        if (!a_item || !a_item->object) return;

        std::uint32_t a_stackID = msg->stackIndex;
        bool isRepairable = ConditionSystem::ShouldShowDurability(a_item->object);

        if (msg->movie) {
            auto movie = static_cast<Scaleform::GFx::Movie*>(msg->movie);
            Scaleform::GFx::Value isRepVal(isRepairable);
            movie->SetVariable("root.Menu_mc.BottomBar_mc.savedRepairSupported", isRepVal);
            ConditionSystem::Repair::InjectConditionSystemCallback(movie);

            if (isRepairable) {
                ConditionSystem::g_hoveredHandleID.store(msg->itemHandleID);
                ConditionSystem::g_hoveredStackIndex.store(a_stackID);
                ConditionSystem::g_hoveredObject.store(a_item->object);
            }
            else {
                ConditionSystem::g_hoveredHandleID.store(0);
                ConditionSystem::g_hoveredStackIndex.store(0);
                ConditionSystem::g_hoveredObject.store(nullptr);
            }
        }

        if (!isRepairable) return;

        RE::BGSInventoryItem::Stack* currentStack = nullptr;
        if (msg->movie) {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (player && player->inventoryList) {
                for (auto& item : player->inventoryList->data) {
                    if (item.object == a_item->object) {
                        std::uint32_t cIdx = 0;
                        for (auto s = item.stackData.get(); s; s = s->nextStack.get()) {
                            if (cIdx == a_stackID) { currentStack = s; break; }
                            cIdx++;
                        }
                        if (!currentStack) currentStack = item.stackData.get();
                        break;
                    }
                }
            }
        }
        else {
            std::uint32_t cIdx = 0;
            for (auto s = a_item->stackData.get(); s; s = s->nextStack.get()) {
                if (cIdx == a_stackID) { currentStack = s; break; }
                cIdx++;
            }
            if (!currentStack) currentStack = a_item->stackData.get();
        }

        bool isArmor = a_item->object->Is(RE::ENUM_FORM_ID::kARMO);
        bool isWeapon = a_item->object->Is(RE::ENUM_FORM_ID::kWEAP);
        bool weaponDamageAffected = ConditionSystem::g_mcmSettings.weaponConditionAffectsDamage.load();
        bool weaponValueAffected = ConditionSystem::g_mcmSettings.weaponConditionAffectsValue.load();
        bool armorResistanceAffected = ConditionSystem::g_mcmSettings.armorConditionAffectsResistance.load();
        bool armorValueAffected = ConditionSystem::g_mcmSettings.armorConditionAffectsValue.load();
        bool isWeapOrArmo = isWeapon || isArmor;
        if (isWeapOrArmo) {
            ConditionSystem::Workbench::g_hoveredObject.store(a_item->object);
            ConditionSystem::Workbench::g_hoveredStack.store(static_cast<void*>(currentStack));
            if (currentStack) {
                bool workbenchRepairMode = false;
                if (msg->movie) {
                    auto movie = static_cast<Scaleform::GFx::Movie*>(msg->movie);
                    Scaleform::GFx::Value repairModeValue;
                    movie->GetVariable(&repairModeValue, "root.BaseInstance.isRepairModeActive");
                    workbenchRepairMode = repairModeValue.IsBoolean() && repairModeValue.GetBoolean();
                }
                if (workbenchRepairMode) {
                    ConditionSystem::Workbench::SendRepairCostToUI();
                } else {
                    ConditionSystem::Workbench::SendRepairKitButtonStateToUI(a_item->object->GetFormID(), currentStack->IsEquipped(), a_stackID);
                }
            }
        }

        if (currentStack && ConditionSystem::GetItemFlags(a_item->object).enableDurabilitySystem) {
            auto healthExtra = currentStack->extra ? currentStack->extra->GetByType<RE::ExtraHealth>() : nullptr;
            if (!currentStack->extra || !healthExtra || healthExtra->health > ConditionSystem::ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                ConditionSystem::WakeUpAndRandomizeSingleStack(a_item->object, currentStack, true);
            }
        }

        if (msg->movie) {
            auto movie = static_cast<Scaleform::GFx::Movie*>(msg->movie);
            bool needsRepair = false;
            if (currentStack && currentStack->extra) {
                float currentPct = ConditionSystem::GetVisualDurabilityPercent(a_item->object, currentStack);
                float repairLimit = ConditionSystem::GetPlayerOverRepairLimit(a_item->object, currentStack->extra.get());
                needsRepair = currentPct < repairLimit - 0.001f;
            }

            Scaleform::GFx::Value repairArgs[2];
            repairArgs[0] = isRepairable && ConditionSystem::g_mcmSettings.enablePipboyJuryRepair.load();
            repairArgs[1] = needsRepair;
            movie->Invoke("root.SetRepairButtonEnabled_Call", nullptr, repairArgs, 2);
        }

        bool showCndCard = ConditionSystem::g_mcmSettings.showItemCardCND.load() && msg->gfxArray;
        if (!showCndCard && !msg->modifyCard) return;

        // 【关键修复 3】：提取耐久度（范围 0.0 ~ 1.0）
        float durabilityPercent = 1.0f;
        if (currentStack) {
            durabilityPercent = ConditionSystem::GetVisualDurabilityPercent(a_item->object, currentStack);
        }
        float currentHealth = durabilityPercent * 100.0f; // 用于显示文字的 0-100 值

        if (showCndCard) {
            IIF_API::CardRequest card{};
            card.text = "CND";
            card.label = "$condition";
            card.highlightLabel = false;
            card.displayType = 1;
            card.hasBackground = true;

            card.hideDifference = true;
            card.showBar = true;
            card.valueAlign = "right";
            card.valueGood = true;

            static char valBuf[32]; snprintf(valBuf, sizeof(valBuf), "%.2f", currentHealth);
            card.value = valBuf;

            // [修复] 信息卡百分比文字受 bShowWidgetText 控制
            bool showPercentText = ConditionSystem::g_mcmSettings.showWidgetText.load();
            card.showValue = showPercentText;
            if (showPercentText) {
                static char textBuf[32]; snprintf(textBuf, sizeof(textBuf), "%.0f%%", currentHealth);
                card.valueText = textBuf;
            }

            card.fillPct = (std::min)(1.0f, durabilityPercent);
            card.shieldPct = (std::max)(0.0f, durabilityPercent - 1.0f);
            card.fillColor = 0;

            // 缺口门槛: 传递衰减门槛数据
            // 护甲: 50% 抗性衰减门槛; 武器: penaltyThreshold 伤害衰减门槛
            // 明确初始化 thresholdPct2 = -1（不发送），确保不会有两个门槛
            // 注意：只有 CSF 系统管理的物品（enableDurabilitySystem=true）才显示门槛缺口
            // 动力甲甲片(vanilla 耐久) 和融合核心(充能) 不应显示
            card.thresholdPct = -1.0f;
            card.thresholdPct2 = -1.0f;
            auto cndFlags = ConditionSystem::GetItemFlags(a_item->object);
            if (cndFlags.enableDurabilitySystem) {
                if (isArmor && armorResistanceAffected) {
                    card.thresholdPct = 0.5f; // 护甲抗性衰减门槛 (硬编码)
                } else if (isWeapon && weaponDamageAffected) {
                    card.thresholdPct = ConditionSystem::g_mcmSettings.penaltyThreshold.load();  // 武器伤害衰减门槛 (默认0.75)
                }
            }

            msg->addCard(msg->context, &card);
        }

        // ========================================================
        // Apply the display-value adjustment only in the IIF panel context.
        // ========================================================
        if (msg->modifyCard) {
            if (isArmor || isWeapon) {
                
                // 核心侦测：判断当前处于什么 UI 环境
                auto ui = RE::UI::GetSingleton();
                bool inPipboy = ui && ui->GetMenuOpen("PipboyMenu");
                bool inExamine = ui && ui->GetMenuOpen("ExamineMenu");
                bool inBarter = ui && ui->GetMenuOpen("BarterMenu");
                bool inContainer = ui && ui->GetMenuOpen("ContainerMenu");

                // 价值同步：只有在纯粹的 Pip-Boy 列表里，我们才手动打折（因为 Pipboy 读的是死缓存）。
                // 一旦进入检视、交易、容器，底层 MinHook 已经算好了精确价格，我们绝不二次打折！
                bool valueAffected = (isWeapon && weaponValueAffected) || (isArmor && armorValueAffected);
                if (valueAffected && inPipboy && !inExamine && !inBarter && !inContainer) {
                    double valMult = static_cast<double>(durabilityPercent);
                    
                    // FO76 过量维修红利联动：涨价 (最高涨20%)
                    if (durabilityPercent > 1.0f) {
                        valMult = 1.0 + 0.2 * (static_cast<double>(durabilityPercent) - 1.0);
                    }
                    
                    msg->modifyCard(msg->context, "$val", valMult);
                }

                // ----------------------------------------------------
                // 下面是你已经恢复的护甲($dr)和武器($dmg)显示打折...
                // ----------------------------------------------------
                if (isArmor && armorResistanceAffected && durabilityPercent < 0.5f) {
                    double penaltyMult = static_cast<double>(durabilityPercent * 2.0f);
                    if (penaltyMult < 0.0) penaltyMult = 0.0;
                    msg->modifyCard(msg->context, "$dr", penaltyMult);
                    msg->modifyCard(msg->context, "$Armor", penaltyMult);
                    msg->modifyCard(msg->context, "$er", penaltyMult);
                    msg->modifyCard(msg->context, "$rr", penaltyMult);
                    msg->modifyCard(msg->context, "$pr", penaltyMult);
                }
                else if (isArmor && armorResistanceAffected && durabilityPercent > 1.0f) {
                    // FO76 护甲过量维修面板红利
                    double bonusMult = 1.0 + 0.2 * (static_cast<double>(durabilityPercent) - 1.0);
                    msg->modifyCard(msg->context, "$dr", bonusMult);
                    msg->modifyCard(msg->context, "$Armor", bonusMult);
                    msg->modifyCard(msg->context, "$er", bonusMult);
                    msg->modifyCard(msg->context, "$rr", bonusMult);
                    msg->modifyCard(msg->context, "$pr", bonusMult);
                }

                if (isWeapon && weaponDamageAffected && durabilityPercent < 0.75f) {
                    double penaltyMult = 0.5 + 0.5 * (static_cast<double>(durabilityPercent) / 0.75);
                    if (penaltyMult < 0.5) penaltyMult = 0.5;
                    msg->modifyCard(msg->context, "$dmg", penaltyMult);
                }
                else if (isWeapon && weaponDamageAffected && durabilityPercent > 1.0f) {
                    // FO76 武器过量维修面板红利
                    double bonusMult = 1.0 + 0.2 * (static_cast<double>(durabilityPercent) - 1.0);
                    msg->modifyCard(msg->context, "$dmg", bonusMult);
                }
            }
        }
    }

    // =========================================================================
    // Adjust Pip-Boy value display and Fallout 76-style over-repair pricing.
    // =========================================================================
    using GetInventoryValue_t = std::int32_t(*)(const RE::BGSInventoryItem*, std::uint32_t, bool);
    static GetInventoryValue_t _GetInventoryValue_Original = nullptr;

    std::int32_t GetInventoryValue_Hook(const RE::BGSInventoryItem* a_this, std::uint32_t a_stackID, bool a_scale) {
        std::int32_t originalValue = _GetInventoryValue_Original(a_this, a_stackID, a_scale);
        if (!a_this || !a_this->object) return originalValue;

        bool isWeapon = a_this->object->Is(RE::ENUM_FORM_ID::kWEAP);
        bool isArmor = a_this->object->Is(RE::ENUM_FORM_ID::kARMO);
        bool valueAffected =
            (isWeapon && ConditionSystem::g_mcmSettings.weaponConditionAffectsValue.load()) ||
            (isArmor && ConditionSystem::g_mcmSettings.armorConditionAffectsValue.load());

        if (valueAffected) {
            float durability = ConditionSystem::GetItemDurabilityPercent(const_cast<RE::BGSInventoryItem*>(a_this), a_stackID);

            if (durability != 1.0f) {
                std::int32_t newValue = static_cast<std::int32_t>(originalValue * durability);
                return newValue > 0 ? newValue : (originalValue > 0 ? 1 : 0);
            }
        }
        return originalValue;
    }

    // =========================================================================
    // 2. 挂载给 IIF 的武器伤害计算回调
    // =========================================================================
    void OnCombatDamageCalculate(RE::Actor* /*attacker*/, RE::TESObjectWEAP* weapon, float* damagePtr) {
        // IIF 已经帮我们排除了 UI 环境，并且确保了 attacker 是玩家
        if (!weapon || !damagePtr) return;
        if (!ConditionSystem::g_mcmSettings.weaponConditionAffectsDamage.load()) return;

        auto wType = weapon->weaponData.type.get();
        if (wType == RE::WEAPON_TYPE::kGrenade || wType == RE::WEAPON_TYPE::kMine) return;

        float durability = ConditionSystem::GetEquippedWeaponDurabilityPercent();

        // FNV 惩罚机制
        if (durability < 0.75f) {
            float penaltyMult = 0.5f + 0.5f * (durability / 0.75f);
            if (penaltyMult < 0.5f) penaltyMult = 0.5f;
            *damagePtr = (*damagePtr) * penaltyMult;
        }
        // FO76 奖励机制
        else if (durability > 1.0f) {
            float bonusMult = 1.0f + 0.2f * ((durability - 1.0f) / 1.0f);
            *damagePtr = (*damagePtr) * bonusMult;
        }
    }

    // =========================================================================
    // 3. 挂载给 IIF 的护甲防御计算回调
    // =========================================================================
    void OnCombatArmorCalculate(RE::Actor* /*wearer*/, float* ratingPtr) {
        if (!ratingPtr) return;
        if (!ConditionSystem::g_mcmSettings.armorConditionAffectsResistance.load()) return;

        float avgDurability = ConditionSystem::GetAverageEquippedArmorDurability();

        // FNV 惩罚机制
        if (avgDurability < 0.5f) {
            float penaltyMult = avgDurability * 2.0f;
            if (penaltyMult < 0.0f) penaltyMult = 0.0f;
            *ratingPtr = (*ratingPtr) * penaltyMult;
        }
        // FO76 奖励机制
        else if (avgDurability > 1.0f) {
            float bonusMult = 1.0f + 0.2f * ((avgDurability - 1.0f) / 1.0f);
            *ratingPtr = (*ratingPtr) * bonusMult;
        }
    }

    // =========================================================================
    // 安装与初始化
    // =========================================================================
    void InstallAll() {
        // 初始化 MinHook，允许 ALREADY_INITIALIZED 状态
        if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
            REX::ERROR("MinHook 初始化失败！");
            return;
        }

        REL::Relocation<std::uintptr_t> getInvValueAddr{ RE::ID::BGSInventoryItem::GetInventoryValue };
        MH_CreateHook((void*)getInvValueAddr.address(), (void*)&GetInventoryValue_Hook, (void**)&_GetInventoryValue_Original);

        // 挂钩 EquipObject：让杂项/垃圾类型的修理工具也能被点击使用
        REL::Relocation<std::uintptr_t> equipObjAddr{ RE::ID::ActorEquipManager::EquipObject };
        MH_CreateHook((void*)equipObjAddr.address(), (void*)&EquipObject_Hook, (void**)&_EquipObject_Original);

        MH_EnableHook(MH_ALL_HOOKS);
        REX::INFO("[MinHook] CSF 交易价值拦截 + 修理工具 EquipObject 钩子已接管！");
    }

    void InstallSingletons() {
        DWORD oldProtect;

        // 1. 输入 Hook (索引 0，没问题)
        auto pc = RE::PlayerControls::GetSingleton();
        if (pc) {
            auto receiver = (RE::BSInputEventReceiver*)pc;
            auto vtable = *reinterpret_cast<uintptr_t**>(receiver);
            _PerformInputProcessing_Original = reinterpret_cast<EventFunc_t>(vtable[0]);
            VirtualProtect(&vtable[0], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
            vtable[0] = reinterpret_cast<uintptr_t>(PerformInputProcessing_Hook);
            VirtualProtect(&vtable[0], sizeof(void*), oldProtect, &oldProtect);
        }

        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(ExamineMenuSink::GetSingleton());
        }
    }
}

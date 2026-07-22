#include "pch.h"
#include "ConditionUI.h"
#include "ConditionCore.h" 
#include "ProfileManager.h" // 确保能拿到最新的声明
#include "Repair/RepairSystem.h" // 获取 GetPipboyColor()
#include "Translation.h"
#include "PrismaUI_F4_API.h"

#include <RE/M/Main.h>

#include <nlohmann/json.hpp>

namespace ConditionSystem {
    namespace {
        constexpr auto kPrismaViewPath = "csf_condition_widget.html";

        // Prisma 2.0 API pointers — request the highest stable interface (V4) for
        // reliable vtable compatibility. The API header includes V5-V9 definitions
        // for future use, but we request V4 to avoid any vtable layout mismatches
        // between this plugin and the installed PrismaUI_F4.dll.
        PRISMA_UI_API::IVPrismaUI4* g_prismaUIv4 = nullptr;
        PRISMA_UI_API::IVPrismaUI2* g_prismaUIv2 = nullptr;
        PRISMA_UI_API::IVPrismaUI1* g_prismaUI = nullptr;
        PrismaView g_prismaView = 0;
        bool g_prismaDomReady = false;
        bool g_prismaApiMissingLogged = false;
        bool g_prismaViewShown = false;
        bool g_widgetVisible = false;
        bool g_unjamVisible = false;
        bool g_widgetDomVisible = false;
        bool g_unjamDomVisible = false;
        std::chrono::steady_clock::time_point g_hudRestoreGraceUntil{};
        std::string g_hudRestoreClosingMenu;
        bool g_loadingFadeRestorePending = false;
        std::chrono::steady_clock::time_point g_loadingFadeRestoreArmUntil{};
        std::chrono::steady_clock::time_point g_loadingFadeRestoreExpireUntil{};
        int g_pendingWidgetShowAnimationMs = 0;

        [[nodiscard]] int BoolFlag(bool a_value)
        {
            return a_value ? 1 : 0;
        }

        [[nodiscard]] long long GetRestoreGraceRemainingMs()
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= g_hudRestoreGraceUntil) {
                return 0;
            }
            return std::chrono::duration_cast<std::chrono::milliseconds>(g_hudRestoreGraceUntil - now).count();
        }

        [[nodiscard]] bool IsPrismaViewReady()
        {
            return g_prismaUI && g_prismaView != 0 && g_prismaUI->IsValid(g_prismaView) && g_prismaDomReady;
        }

        [[nodiscard]] bool IsRestoringFromMenu(std::string_view a_menuName)
        {
            return !g_hudRestoreClosingMenu.empty() && g_hudRestoreClosingMenu == std::string(a_menuName);
        }

        [[nodiscard]] bool IsAuxiliaryMenuEvent(std::string_view a_menuName)
        {
            constexpr std::string_view auxiliaryMenus[] = {
                "HUDMenu",
                "CursorMenu",
                "MSFwidget",
                "ConditionWidgetMenu",
                "FloatingDamageMenu",
                "CapsWidget",
                "FaderMenu",
                "VignetteMenu"
            };

            for (auto menuName : auxiliaryMenus) {
                if (a_menuName == menuName) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool CanRestoreBeforeHudMenu()
        {
            return IsRestoringFromMenu("ContainerMenu") || IsRestoringFromMenu("BarterMenu");
        }

        [[nodiscard]] bool IsRestoreGraceActive(bool a_allowRestoreGrace)
        {
            return a_allowRestoreGrace && std::chrono::steady_clock::now() < g_hudRestoreGraceUntil;
        }

        [[nodiscard]] bool IsMenuOpen(RE::UI* a_ui, const char* a_menuName)
        {
            return a_ui && a_ui->GetMenuOpen(RE::BSFixedString(a_menuName));
        }

        [[nodiscard]] std::string GetGameplayHudBlockReason(bool a_allowRestoreGrace)
        {
            auto ui = RE::UI::GetSingleton();
            if (!ui) {
                return "no_ui";
            }

            const bool restoreGraceActive = IsRestoreGraceActive(a_allowRestoreGrace);

            if (!ui->GetMenuOpen("HUDMenu") && !(restoreGraceActive && CanRestoreBeforeHudMenu())) {
                return "hudmenu_closed";
            }

            if (ui->menuMode != 0 && !restoreGraceActive) {
                return "ui_menu_mode";
            }

            if (auto main = RE::Main::GetSingleton(); main && main->inMenuMode && !restoreGraceActive) {
                return "main_menu_mode";
            }

            if (auto player = RE::PlayerCharacter::GetSingleton();
                player && player->inLooksMenu && !(restoreGraceActive && IsRestoringFromMenu("LooksMenu"))) {
                return "player_looks_menu";
            }

            if (g_loadingFadeRestorePending) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= g_loadingFadeRestoreExpireUntil) {
                    g_loadingFadeRestorePending = false;
                    g_loadingFadeRestoreArmUntil = {};
                    g_loadingFadeRestoreExpireUntil = {};
                } else if (now < g_loadingFadeRestoreArmUntil || IsMenuOpen(ui, "FaderMenu")) {
                    return "loading_fader";
                } else {
                    g_loadingFadeRestorePending = false;
                    g_loadingFadeRestoreArmUntil = {};
                    g_loadingFadeRestoreExpireUntil = {};
                }
            }

            constexpr std::string_view blockedMenus[] = {
                "LooksMenu",
                "ContainerMenu",
                "BarterMenu",
                "LockpickingMenu",
                "TerminalMenu",
                "TerminalMenuButtons",
                "PipboyMenu",
                "Pipboy3DMenu",
                "PipboyHolotapeMenu",
                "PauseMenu",
                "LoadingMenu",
                "DialogueMenu",
                "VATSMenu",
                "WorkshopMenu",
                "ExamineMenu",
                "PowerArmorModMenu",
                "Console",
                "ConsoleMenu",
                "MessageBoxMenu",
                "SitWaitMenu",
                "SPECIALMenu",
                "HolotapeMenu"
            };

            for (auto menuName : blockedMenus) {
                if (restoreGraceActive && IsRestoringFromMenu(menuName)) {
                    continue;
                }
                if (ui->GetMenuOpen(RE::BSFixedString(menuName))) {
                    return std::string("menu_open:") + std::string(menuName);
                }
            }

            return "ok";
        }

        void LogWidgetDiag(std::string_view a_event, std::string_view a_reason = {})
        {
            if (!ConditionSystem::g_mcmSettings.enableLogging.load()) {
                return;
            }

            auto ui = RE::UI::GetSingleton();
            auto main = RE::Main::GetSingleton();
            auto player = RE::PlayerCharacter::GetSingleton();
            const auto weaponState = player ? static_cast<int>(player->weaponState) : -1;

            CS_LOG(
                "[CSF-WidgetDiag] event={} reason={} hud={} menuMode={} mainMenu={} container={} barter={} pipboy={} pipboy3d={} pause={} looks={} lockpick={} terminal={} fader={} loadingFadePending={} graceMs={} closingMenu={} cachedDrawn={} magicDrawn={} weaponState={} realWeapon={} unjam={} widgetLogic={} widgetDom={} unjamLogic={} unjamDom={} prismaReady={} viewShown={}",
                a_event,
                a_reason,
                BoolFlag(IsMenuOpen(ui, "HUDMenu")),
                ui ? static_cast<int>(ui->menuMode) : -1,
                BoolFlag(main && main->inMenuMode),
                BoolFlag(IsMenuOpen(ui, "ContainerMenu")),
                BoolFlag(IsMenuOpen(ui, "BarterMenu")),
                BoolFlag(IsMenuOpen(ui, "PipboyMenu")),
                BoolFlag(IsMenuOpen(ui, "Pipboy3DMenu")),
                BoolFlag(IsMenuOpen(ui, "PauseMenu")),
                BoolFlag(IsMenuOpen(ui, "LooksMenu")),
                BoolFlag(IsMenuOpen(ui, "LockpickingMenu")),
                BoolFlag(IsMenuOpen(ui, "TerminalMenu")),
                BoolFlag(IsMenuOpen(ui, "FaderMenu")),
                BoolFlag(g_loadingFadeRestorePending),
                GetRestoreGraceRemainingMs(),
                g_hudRestoreClosingMenu,
                BoolFlag(ConditionSystem::g_isWeaponDrawn.load()),
                BoolFlag(player && player->GetWeaponMagicDrawn()),
                weaponState,
                BoolFlag(ConditionSystem::IsRealWeaponEquipped()),
                BoolFlag(ConditionSystem::g_isUnjamming.load()),
                BoolFlag(g_widgetVisible),
                BoolFlag(g_widgetDomVisible),
                BoolFlag(g_unjamVisible),
                BoolFlag(g_unjamDomVisible),
                BoolFlag(IsPrismaViewReady()),
                BoolFlag(g_prismaViewShown));
        }

        [[nodiscard]] bool IsGameplayHUDOnly(bool a_allowRestoreGrace = false)
        {
            return GetGameplayHudBlockReason(a_allowRestoreGrace) == "ok";
        }

        [[nodiscard]] bool IsWeaponDrawnForWidget()
        {
            const bool cachedDrawn = ConditionSystem::g_isWeaponDrawn.load();

            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return cachedDrawn;
            }

            const bool actualDrawn =
                player->GetWeaponMagicDrawn() ||
                player->weaponState == RE::WEAPON_STATE::kDrawn ||
                player->weaponState == RE::WEAPON_STATE::kDrawing ||
                player->weaponState == RE::WEAPON_STATE::kWantToDraw;

            if (actualDrawn) {
                ConditionSystem::g_isWeaponDrawn.store(true);
            }

            return cachedDrawn || actualDrawn;
        }

        void PollWeaponDrawStart()
        {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player ||
                (player->weaponState != RE::WEAPON_STATE::kWantToDraw &&
                 player->weaponState != RE::WEAPON_STATE::kDrawing)) {
                return;
            }

            if (ConditionSystem::g_isWeaponDrawn.load()) {
                return;
            }

            ConditionSystem::g_isWeaponDrawn.store(true);
            ConditionUI::EvaluateVisibility(true);
        }

        void SendPrisma(const char* a_functionName, const nlohmann::json& a_payload)
        {
            if (!IsPrismaViewReady()) {
                return;
            }
            const auto payload = a_payload.dump();
            g_prismaUI->InteropCall(g_prismaView, a_functionName, payload.c_str());
        }

        void RefreshPrismaViewVisibility()
        {
            if (!g_prismaUI || g_prismaView == 0 || !g_prismaUI->IsValid(g_prismaView)) {
                return;
            }

            if (g_prismaViewShown && !g_prismaUI->IsHidden(g_prismaView)) {
                return;
            }

            g_prismaUI->Show(g_prismaView);
            g_prismaViewShown = true;
        }

        void ApplyPrismaHudVisibility(bool a_animateWidget = false, int a_animationMs = 0)
        {
            RefreshPrismaViewVisibility();
            if (!IsPrismaViewReady()) {
                LogWidgetDiag("ApplyPrismaHudVisibility:skip", "prisma_not_ready");
                if (a_animateWidget && a_animationMs > 0 && g_widgetVisible) {
                    g_pendingWidgetShowAnimationMs = a_animationMs;
                }
                return;
            }

            const bool inHud = IsGameplayHUDOnly(true);
            const bool widgetDomVisible = inHud && g_widgetVisible;
            const bool unjamDomVisible = inHud && g_unjamVisible;

            if (widgetDomVisible != g_widgetDomVisible) {
                if (widgetDomVisible && !a_animateWidget && g_pendingWidgetShowAnimationMs > 0) {
                    a_animateWidget = true;
                    a_animationMs = g_pendingWidgetShowAnimationMs;
                }
                if (widgetDomVisible) {
                    g_pendingWidgetShowAnimationMs = 0;
                }
                LogWidgetDiag("ApplyPrismaHudVisibility:widget", widgetDomVisible ? "dom_show" : "dom_hide");
                SendPrisma("setWidgetVisible", {
                    { "visible", widgetDomVisible },
                    { "animate", a_animateWidget && inHud },
                    { "durationMs", a_animationMs }
                });
                g_widgetDomVisible = widgetDomVisible;
            }

            if (unjamDomVisible != g_unjamDomVisible) {
                LogWidgetDiag("ApplyPrismaHudVisibility:unjam", unjamDomVisible ? "dom_show" : "dom_hide");
                SendPrisma("setUnjamVisible", { { "visible", unjamDomVisible } });
                g_unjamDomVisible = unjamDomVisible;
            }
        }

        void PushPrismaState()
        {
            ConditionUI::UpdateVisuals(
                ConditionSystem::g_mcmSettings.x.load(),
                ConditionSystem::g_mcmSettings.y.load(),
                ConditionSystem::g_mcmSettings.scale.load());
            ConditionUI::UpdateUnjamVisuals(
                ConditionSystem::g_mcmSettings.unjamX.load(),
                ConditionSystem::g_mcmSettings.unjamY.load(),
                ConditionSystem::g_mcmSettings.unjamScale.load());
            ConditionUI::UpdatePenaltyThreshold(ConditionSystem::g_mcmSettings.penaltyThreshold.load());
            ConditionUI::UpdateTextVisibility(ConditionSystem::g_mcmSettings.showWidgetText.load());
            ConditionUI::UpdateQuickLootColor();
            ConditionUI::UpdateDurability(
                ConditionSystem::GetEquippedWeaponDurabilityPercent(),
                ConditionSystem::GetEquippedWeaponDurabilityPoints());
            g_widgetDomVisible = false;
            g_unjamDomVisible = false;
            ApplyPrismaHudVisibility(g_pendingWidgetShowAnimationMs > 0, g_pendingWidgetShowAnimationMs);
        }

        void OnPrismaDomReady(PrismaView a_view)
        {
            if (a_view != g_prismaView) {
                return;
            }
            g_prismaDomReady = true;
            PushPrismaState();
            // Notify the widget that PrismaUI bridge is fully initialized.
            // window.init() is the standard PrismaUI lifecycle callback that
            // signals DOM ready + window.prisma availability to the view.
            SendPrisma("init", {});
            SendPrisma("warmupWidget", {});
            REX::INFO("[CSF-PrismaUI] Condition widget DOM ready, init() called.");
        }

        void QueuePostMenuCloseVisibilityRefresh(bool a_animateWidget = false, int a_animationMs = 0)
        {
            if (auto task = F4SE::GetTaskInterface()) {
                task->AddTask([a_animateWidget, a_animationMs]() {
                    LogWidgetDiag("PostMenuCloseRefresh", "immediate_task");
                    ConditionUI::EvaluateVisibility(a_animateWidget, a_animationMs);
                });
            }

            std::thread([a_animateWidget, a_animationMs]() {
                int elapsedMs = 0;
                for (const int delayMs : { 100, 350, 800 }) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs - elapsedMs));
                    elapsedMs = delayMs;

                    if (auto task = F4SE::GetTaskInterface()) {
                        task->AddTask([delayMs, a_animateWidget, a_animationMs]() {
                            CS_LOG("[CSF-WidgetDiag] post-close delayed refresh delayMs={}", delayMs);
                            LogWidgetDiag("PostMenuCloseRefresh", "delayed_task");
                            ConditionUI::EvaluateVisibility(a_animateWidget, a_animationMs);
                        });
                    }
                }
            }).detach();
        }

        void BeginHudRestoreGrace(const RE::BSFixedString& a_menuName)
        {
            g_hudRestoreClosingMenu = a_menuName.c_str() ? a_menuName.c_str() : "";
            g_hudRestoreGraceUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
            LogWidgetDiag("BeginHudRestoreGrace", g_hudRestoreClosingMenu);
        }

        void ClearHudRestoreGrace()
        {
            LogWidgetDiag("ClearHudRestoreGrace", g_hudRestoreClosingMenu);
            g_hudRestoreClosingMenu.clear();
            g_hudRestoreGraceUntil = {};
        }

        void BeginLoadingFadeRestorePending()
        {
            g_loadingFadeRestorePending = true;
            const auto now = std::chrono::steady_clock::now();
            g_loadingFadeRestoreArmUntil = now + std::chrono::milliseconds(250);
            g_loadingFadeRestoreExpireUntil = now + std::chrono::seconds(6);
            g_pendingWidgetShowAnimationMs = 700;
            LogWidgetDiag("BeginLoadingFadeRestorePending", "LoadingMenu");
        }

        void ClearLoadingFadeRestorePending(std::string_view a_reason)
        {
            if (!g_loadingFadeRestorePending) {
                return;
            }

            LogWidgetDiag("ClearLoadingFadeRestorePending", a_reason);
            g_loadingFadeRestorePending = false;
            g_loadingFadeRestoreArmUntil = {};
            g_loadingFadeRestoreExpireUntil = {};
        }
    }

    // =========================================================================
    // 模块 1：核心 UI 循环 (Frame Update)
    // =========================================================================

    void ConditionUI::AdvanceMovie(float a_interval, std::uint64_t a_currentTime) {
        RE::GameMenuBase::AdvanceMovie(a_interval, a_currentTime);
        PollWeaponDrawStart();

        // 保留 3 帧节流阀：防止高频刷爆 Scaleform 接口
        static int frameCounter = 0;
        if (frameCounter++ % 3 != 0) return; 

        auto ui = RE::UI::GetSingleton();
        if (!ui) return;
        auto invIntfc = RE::BGSInventoryInterface::GetSingleton();
        if (!invIntfc) return;

        static std::uint32_t lastScannedRefID = 0;
        bool isLookingAtContainer = false;
        RE::TESObjectREFR* currentContainer = nullptr;

        // 【合并遍历】：只扫一次 agentArray 找到当前正在看的容器
        for (auto& agent : invIntfc->agentArray) {
            if (agent.itemOwner) {
                auto targetRef = agent.itemOwner.get().get();
                if (targetRef && targetRef != RE::PlayerCharacter::GetSingleton() && targetRef->inventoryList) {
                    isLookingAtContainer = true;
                    currentContainer = targetRef;
                    break; // 准星下通常只有一个容器，找到就立刻跳出
                }
            }
        }

        if (isLookingAtContainer && currentContainer) {
            std::uint32_t currentRefID = currentContainer->GetFormID();

            // 1. 【核心优化：初始化】：只有看向一个“新箱子”时，才进行重度计算！
            if (currentRefID != lastScannedRefID) {
                if (ConditionSystem::g_mcmSettings.randomLootDurability.load()) {
                    // 💡 【核心修复】：跨线程安全委托！
                    // AdvanceMovie 运行在 UI 线程，绝对不能直接修改 ExtraData。
                    // 必须提取 FormID，包装成 Task 扔给主线程去安全执行！
                    std::uint32_t containerID = currentRefID;
                    if (auto task = F4SE::GetTaskInterface()) {
                        task->AddTask([containerID]() {
                            auto form = RE::TESForm::GetFormByID(containerID);
                            if (form) {
                                if (auto containerRef = form->As<RE::TESObjectREFR>()) {
                                    ConditionSystem::RandomizeInventoryDurability(containerRef);
                                }
                            }
                        });
                    }
                }
                lastScannedRefID = currentRefID; // 记录 ID，后续只需读取不再计算！
            }

            // 2. 【UI 更新】：向 HUD 发送已经绝对真实的耐久度
            auto menu = ui->GetMenu("HUDMenu");
            if (menu && menu->uiMovie) {
                Scaleform::GFx::Value cndArray;
                menu->uiMovie->CreateArray(&cndArray);
                int validItems = 0;

                for (auto& item : currentContainer->inventoryList->data) {
                    if (item.object && ConditionSystem::ShouldShowDurability(item.object)) {
                        std::string baseName = LOC("$CSF_Unknown");
                        auto fullNameForm = item.object->As<RE::TESFullName>();
                        if (fullNameForm && fullNameForm->GetFullName()) {
                            baseName = fullNameForm->GetFullName();
                        }
                        if (baseName.empty()) continue;

                        for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                            // 此时获取的数据，已经被底层 GetVisualDurabilityPercent 解压！绝对是真数据
                            float cndVal = ConditionSystem::GetVisualDurabilityPercent(item.object, stack) * 100.0f;
                            
                            Scaleform::GFx::Value obj;
                            menu->uiMovie->CreateObject(&obj);
                            obj.SetMember("name", Scaleform::GFx::Value(baseName.c_str()));
                            
                            // 💡 [核心数据驱动]：不再写死判断动力甲。
                            // 只要是 UI 显示（上面过了ShouldShow）但系统禁用的物品，一律打上 isPA 标志禁止维修！
                            bool isPA = false;
                            auto flags = ConditionSystem::GetItemFlags(item.object);
                            if (!flags.enableDurabilitySystem) {
                                isPA = true;
                            }
                            
                            obj.SetMember("isPA", Scaleform::GFx::Value(isPA));
                            
                            obj.SetMember("formID", Scaleform::GFx::Value(static_cast<double>(item.object->GetFormID())));
                            obj.SetMember("count", Scaleform::GFx::Value(static_cast<double>(stack->count)));
                            obj.SetMember("cnd", Scaleform::GFx::Value(static_cast<double>(cndVal)));

                            // 传递衰减门槛缺口数据（与 ItemCard 同步）
                            // 注意：只有 CSF 系统管理的物品（enableDurabilitySystem=true）才显示门槛缺口
                            // 动力甲甲片(vanilla) 和融合核心(充能) 不应显示
                            float thresholdPct = -1.0f;
                            float thresholdPct2 = -1.0f;
                            if (flags.enableDurabilitySystem) {
                                if (item.object->Is(RE::ENUM_FORM_ID::kARMO)) {
                                    thresholdPct = 0.5f; // 护甲抗性衰减门槛
                                } else if (item.object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                                    thresholdPct = ConditionSystem::g_mcmSettings.penaltyThreshold.load(); // 武器伤害衰减门槛
                                }
                            }
                            if (thresholdPct >= 0.0f) {
                                obj.SetMember("thresholdPct", Scaleform::GFx::Value(static_cast<double>(thresholdPct)));
                            }
                            if (thresholdPct2 >= 0.0f) {
                                obj.SetMember("thresholdPct2", Scaleform::GFx::Value(static_cast<double>(thresholdPct2)));
                            }
                            
                            cndArray.PushBack(obj);
                            validItems++;
                        }
                    }
                }

                if (validItems > 0) {
                    ConditionUI::UpdateQuickLootColor();
                    Scaleform::GFx::Value args[1];
                    args[0] = cndArray;
                    menu->uiMovie->Invoke("root.CenterGroup_mc.QuickContainerWidget_mc.ReceiveCNDData", nullptr, args, 1);
                }
            }
        } else {
            // 玩家移开了准星（面前没有容器了），重置缓存
            // 这样玩家下次再看向同一个箱子时，还能再做一次兜底的初始化检查
            lastScannedRefID = 0;
        }
    }


    // =========================================================================
    // 模块 2：Scaleform UI 菜单生命周期管理
    // =========================================================================

    ConditionUI::ConditionUI() : RE::GameMenuBase() {
        menuFlags.set(
            RE::UI_MENU_FLAGS::kAllowSaving,
            RE::UI_MENU_FLAGS::kDontHideCursorWhenTopmost,
            RE::UI_MENU_FLAGS::kAlwaysOpen
        );
        depthPriority = RE::UI_DEPTH_PRIORITY::kSWFLoader;
    }

    RE::IMenu* ConditionUI::CreateMenu(const RE::UIMessage&) {
        auto menu = new ConditionUI();
        auto scaleformMgr = RE::BSScaleformManager::GetSingleton();
        
        if (scaleformMgr) {
            auto MoviePath = "Interface\\ConditionWidget.swf"sv;
            bool success = scaleformMgr->LoadMovieEx(*menu, MoviePath, "root"); 
            
            if (success && menu->uiMovie) {
                Scaleform::GFx::Value widgetVal;
                if (menu->uiMovie->GetVariable(&widgetVal, "root.Widget_mc") && widgetVal.IsDisplayObject()) {
                    widgetVal.SetMember("x", ConditionSystem::g_mcmSettings.x.load());
                    widgetVal.SetMember("y", ConditionSystem::g_mcmSettings.y.load());
                    widgetVal.SetMember("scaleX", ConditionSystem::g_mcmSettings.scale.load());
                    widgetVal.SetMember("scaleY", ConditionSystem::g_mcmSettings.scale.load());
                }

                Scaleform::GFx::Value root;
                menu->uiMovie->GetVariable(&root, "root");
                if (root.IsObject()) {
                    Scaleform::GFx::Value colorArgs[1];
                    colorArgs[0] = ConditionSystem::g_mcmSettings.useCustomColor.load() ? static_cast<double>(ConditionSystem::g_mcmSettings.customColor.load()) : 0x18FF00;
                    root.Invoke("SetCustomColor", nullptr, colorArgs, 1);

                    Scaleform::GFx::Value args[3];
                    args[0] = static_cast<double>(ConditionSystem::g_mcmSettings.unjamX.load());
                    args[1] = static_cast<double>(ConditionSystem::g_mcmSettings.unjamY.load());
                    args[2] = static_cast<double>(ConditionSystem::g_mcmSettings.unjamScale.load());
                    root.Invoke("SetUnjamTransform", nullptr, args, 3);
                    
                    Scaleform::GFx::Value threshArgs[1];
                    threshArgs[0] = static_cast<double>(ConditionSystem::g_mcmSettings.jamThreshold.load());
                    root.Invoke("SetJamThreshold", nullptr, threshArgs, 1);

                    Scaleform::GFx::Value textArgs[1];
                    textArgs[0] = ConditionSystem::g_mcmSettings.showWidgetText.load();
                    root.Invoke("SetTextVisible", nullptr, textArgs, 1);
                }
            }
        }
        return menu;
    }

    void ConditionUI::RegisterMenu() {
        auto ui = RE::UI::GetSingleton();
        if (ui) ui->RegisterMenu(MENU_NAME, CreateMenu);
    }

    void ConditionUI::InitializePrisma() {
        if (g_prismaUI) {
            return;
        }

        // Request the highest stable interface version (IVPrismaUI4).
        // The API header includes V5-V9 definitions for future expansion,
        // but we deliberately request V4 to guarantee vtable compatibility
        // with both Prisma 1.0 and 2.0 installations.
        g_prismaUIv4 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>();
        if (g_prismaUIv4) {
            g_prismaUIv2 = static_cast<PRISMA_UI_API::IVPrismaUI2*>(g_prismaUIv4);
            g_prismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1*>(g_prismaUIv4);
            REX::INFO("[CSF-PrismaUI] PrismaUI_F4 V4 API found. HUD overlay is enabled.");
            return;
        }

        // Fallback: V2 (console callback)
        g_prismaUIv2 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
        if (g_prismaUIv2) {
            g_prismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1*>(g_prismaUIv2);
            REX::INFO("[CSF-PrismaUI] PrismaUI_F4 V2 API found. HUD overlay enabled with console callbacks.");
            return;
        }

        // Fallback: V1 (core operations only — no console callbacks, no translations)
        g_prismaUI = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();
        if (g_prismaUI) {
            REX::INFO("[CSF-PrismaUI] PrismaUI_F4 V1 API found. JS console callback and translations are unavailable.");
            return;
        }

        // No PrismaUI installed
        if (!g_prismaApiMissingLogged) {
            g_prismaApiMissingLogged = true;
            REX::INFO("[CSF-PrismaUI] PrismaUI_F4 API not found. Condition HUD overlay will stay disabled.");
        }
    }

    void ConditionUI::CreatePrismaView() {
        InitializePrisma();
        if (!g_prismaUI) {
            return;
        }

        if (g_prismaView != 0 && g_prismaUI->IsValid(g_prismaView)) {
            return;
        }

        g_prismaDomReady = false;
        g_prismaView = g_prismaUI->CreateView(kPrismaViewPath, OnPrismaDomReady);
        if (g_prismaView == 0 || !g_prismaUI->IsValid(g_prismaView)) {
            REX::INFO("[CSF-PrismaUI] Failed to create condition widget view: {}", kPrismaViewPath);
            g_prismaView = 0;
            return;
        }

        if (g_prismaUIv4) {
            g_prismaUIv4->RegisterTranslations(g_prismaView, "Condition System Framework");
        }
        g_prismaUI->SetOrder(g_prismaView, 5);
        if (g_prismaUIv2) {
            g_prismaUIv2->RegisterConsoleCallback(
                g_prismaView,
                [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel a_level, const char* a_message) {
                    const char* level =
                        a_level == PRISMA_UI_API::ConsoleMessageLevel::Error ? "ERR" :
                        a_level == PRISMA_UI_API::ConsoleMessageLevel::Warning ? "WARN" :
                        a_level == PRISMA_UI_API::ConsoleMessageLevel::Debug ? "DBG" :
                        a_level == PRISMA_UI_API::ConsoleMessageLevel::Info ? "INFO" :
                        "LOG";
                    REX::INFO("[CSF-PrismaUI][JS:{}] {}", level, a_message ? a_message : "");
                });
        }
        g_prismaUI->Hide(g_prismaView);
        g_prismaViewShown = false;
        g_widgetDomVisible = false;
        g_unjamDomVisible = false;
        REX::INFO("[CSF-PrismaUI] Created condition widget view: {}", kPrismaViewPath);
    }

    void ConditionUI::OpenMenu() {
        auto queue = RE::UIMessageQueue::GetSingleton();
        if (queue) queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
    }

    void ConditionUI::CloseMenu() {
        auto queue = RE::UIMessageQueue::GetSingleton();
        if (queue) queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
        g_widgetVisible = false;
        g_unjamVisible = false;
        ApplyPrismaHudVisibility();
    }


    // =========================================================================
    // 模块 3：UI 通信与状态更新接口
    // =========================================================================

    void ConditionUI::UpdateVisuals(float a_x, float a_y, float a_scale) {
        SendPrisma("setWidgetTransform", {
            { "x", a_x },
            { "y", a_y },
            { "scale", a_scale }
        });
    }

    void ConditionUI::UpdateUnjamVisuals(float a_offsetX, float a_offsetY, float a_scale) {
        SendPrisma("setUnjamTransform", {
            { "x", a_offsetX },
            { "y", a_offsetY },
            { "scale", a_scale }
        });
    }

    void ConditionUI::EvaluateVisibility(bool a_animateWidget, int a_animationMs) {
        auto ui = RE::UI::GetSingleton();
        if (!ui || !ConditionSystem::g_mcmSettings.enable.load()) {
            LogWidgetDiag("EvaluateVisibility:block", !ui ? "no_ui" : "mcm_disabled");
            SetWidgetVisible(false, a_animateWidget, a_animationMs);
            ShowUnjammingUI(false);
            return;
        }

        const auto hudReason = GetGameplayHudBlockReason(true);
        if (hudReason != "ok") {
            LogWidgetDiag("EvaluateVisibility:block", hudReason);
            ApplyPrismaHudVisibility();
            return;
        }

        // 🐛 修复：OpenMenu() 退避逻辑
        // 如果菜单创建失败（SWF 缺失等），每次 EvaluateVisibility 都会重复发 kShow 消息
        // 而此函数被动画事件/菜单事件/MCM热更等多处高频调用，形成无限重试循环
        // 增加 2 秒退避窗口，防止刷爆 UI 消息队列
        if (!ui->GetMenuOpen(MENU_NAME)) {
            static std::chrono::steady_clock::time_point s_lastOpenAttempt{};
            auto now = std::chrono::steady_clock::now();
            constexpr auto BACKOFF_INTERVAL = std::chrono::milliseconds(2000);
            if (now - s_lastOpenAttempt >= BACKOFF_INTERVAL) {
                s_lastOpenAttempt = now;
                OpenMenu();
            }
        }

        const bool shouldBeVisible =
            (IsWeaponDrawnForWidget() || ConditionSystem::g_isUnjamming.load()) &&
            ConditionSystem::IsRealWeaponEquipped();

        LogWidgetDiag("EvaluateVisibility:decision", shouldBeVisible ? "show" : "hide");
        SetWidgetVisible(shouldBeVisible, a_animateWidget, a_animationMs);
    }

    void ConditionUI::SetWidgetVisible(bool a_visible, bool a_animate, int a_animationMs) {
        if (a_visible != g_widgetVisible) {
            LogWidgetDiag("SetWidgetVisible", a_visible ? "logic_show" : "logic_hide");
        }
        g_widgetVisible = a_visible;
        ApplyPrismaHudVisibility(a_animate, a_animationMs);
    }

    void ConditionUI::ForceColorRefresh() {
        UpdateQuickLootColor();
    }

    void ConditionUI::UpdateDurability(float /*a_percent*/, float /*a_points*/) {
        float realPct = ConditionSystem::GetEquippedWeaponDurabilityPercent();
        float realPts = ConditionSystem::GetEquippedWeaponDurabilityPoints();
        SendPrisma("setDurability", {
            { "percent", realPct },
            { "points", realPts }
        });
    }

    void ConditionUI::UpdatePenaltyThreshold(float a_percent) {
        SendPrisma("setPenaltyThreshold", { { "percent", a_percent } });
    }

    void ConditionUI::UpdateTextVisibility(bool a_show) {
        SendPrisma("setTextVisible", { { "visible", a_show } });
    }

    void ConditionUI::UpdateQuickLootColor() {
        double qlColor = -1.0; 
        double myColor = 0x18FF00; 
        
        if (ConditionSystem::g_mcmSettings.useCustomColor.load()) {
            qlColor = static_cast<double>(ConditionSystem::g_mcmSettings.customColor.load());
            myColor = qlColor; 
        } else {
            // [修复] 使用真实的 Pipboy 主题色代替硬编码白色
            std::uint32_t pipboyColor = ConditionSystem::Repair::GetPipboyColor();
            qlColor = static_cast<double>(pipboyColor);
            myColor = qlColor;
        }

        auto ui = RE::UI::GetSingleton();
        if (ui) {
            auto hudMenu = ui->GetMenu("HUDMenu");
            if (hudMenu && hudMenu->uiMovie) {
                Scaleform::GFx::Value args[1];
                args[0] = qlColor;
                hudMenu->uiMovie->Invoke("root.CenterGroup_mc.QuickContainerWidget_mc.SetCNDColor", nullptr, args, 1);
            }
        }

        SendPrisma("setCustomColor", { { "color", myColor } });
    }

    void ConditionUI::ShowUnjammingUI(bool a_show) {
        g_unjamVisible = a_show;
        ApplyPrismaHudVisibility();
    }

    void ConditionUI::UpdateUnjammingProgress(float a_percent) {
        SendPrisma("setUnjamProgress", { { "percent", a_percent } });
    }


    // =========================================================================
    // 模块 4：MCM 设置管理器与自动热更
    // =========================================================================

    SettingsManager* SettingsManager::GetSingleton() {
        static SettingsManager singleton;
        return &singleton;
    }

    void SettingsManager::InstallHook() {
        static bool bRegistered = false;
        if (bRegistered) return;
        if (auto UI = RE::UI::GetSingleton()) {
            UI->RegisterSink<RE::MenuOpenCloseEvent>(this);
            bRegistered = true;
        }
    }

    RE::BSEventNotifyControl SettingsManager::ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        LogWidgetDiag(a_event.opening ? "MenuOpenClose:open" : "MenuOpenClose:close",
            a_event.menuName.c_str() ? a_event.menuName.c_str() : "");

        if (a_event.menuName == "PauseMenu" && !a_event.opening) {
            Load();
        }

        // 🐛 修复：为工作台(ExamineMenu)注入 ConditionSystem_Call 回调，
        // 使得手柄/键盘的维修确认按钮能够调用 C++ 端的函数。
        // 此前这个回调只注入了 PipboyMenu（混修菜单），导致工作台维修模式下按钮无效。
        if (a_event.menuName == "ExamineMenu" && a_event.opening) {
            if (auto* examineUi = RE::UI::GetSingleton()) {
                if (auto menu = examineUi->GetMenu("ExamineMenu")) {
                    if (menu && menu->uiMovie) {
                        REX::INFO("[CSF-Workbench] Injecting ConditionSystem_Call into ExamineMenu.");
                        ConditionSystem::Repair::InjectConditionSystemCallback(menu->uiMovie.get());
                    }
                }
            }
        }
        
        if (a_event.menuName == "PipboyHolotapeMenu" && !a_event.opening) {
            auto renderer = RE::Interface3D::Renderer::GetByName("Pipboy3D");
            if (renderer) renderer->Enable(true); 
        }

        const auto menuName = std::string_view(a_event.menuName.c_str() ? a_event.menuName.c_str() : "");
        const bool isAuxiliaryMenu = IsAuxiliaryMenuEvent(menuName);

        if (a_event.opening && !isAuxiliaryMenu) {
            ClearLoadingFadeRestorePending(menuName);
            ClearHudRestoreGrace();
        } else if (!a_event.opening && !isAuxiliaryMenu) {
            BeginHudRestoreGrace(a_event.menuName);
        }

        bool animateWidgetRestore = false;
        int widgetAnimationMs = 0;
        if (!a_event.opening && menuName == "LoadingMenu") {
            BeginLoadingFadeRestorePending();
            animateWidgetRestore = true;
            widgetAnimationMs = 700;
        } else if (!a_event.opening && menuName == "FaderMenu" && g_loadingFadeRestorePending) {
            ClearLoadingFadeRestorePending("FaderMenu");
            animateWidgetRestore = true;
            widgetAnimationMs = 700;
        }

        ConditionUI::EvaluateVisibility(animateWidgetRestore, widgetAnimationMs);
        if (!a_event.opening) {
            QueuePostMenuCloseVisibilityRefresh(animateWidgetRestore, widgetAnimationMs);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    void SettingsManager::Load() {
        ConditionSystem::LoadAllProfiles();
        
        std::filesystem::path defaultPath = "Data\\MCM\\Config\\ConditionSystemFramework\\settings.ini";
        std::filesystem::path userPath = "Data\\MCM\\Settings\\ConditionSystemFramework.ini";
        
        CSimpleIniA defaultIni;
        defaultIni.SetUnicode();
        defaultIni.SetSpaces(false); 
        defaultIni.LoadFile(defaultPath.string().c_str());

        CSimpleIniA userIni;
        userIni.SetUnicode();
        userIni.SetSpaces(false); 
        userIni.LoadFile(userPath.string().c_str());

        bool bSaveDefaultNeeded = false; 

        #define GET_BOOL(section, key, defaultVal) \
            [&]() { \
                if (userIni.GetValue(section, key)) return (bool)userIni.GetBoolValue(section, key); \
                if (defaultIni.GetValue(section, key)) return (bool)defaultIni.GetBoolValue(section, key); \
                defaultIni.SetLongValue(section, key, defaultVal ? 1 : 0); \
                bSaveDefaultNeeded = true; \
                return (bool)defaultVal; \
            }()

        #define GET_FLOAT(section, key, defaultVal) \
            [&]() { \
                if (userIni.GetValue(section, key)) return static_cast<float>(userIni.GetDoubleValue(section, key)); \
                if (defaultIni.GetValue(section, key)) return static_cast<float>(defaultIni.GetDoubleValue(section, key)); \
                defaultIni.SetDoubleValue(section, key, defaultVal); \
                bSaveDefaultNeeded = true; \
                return static_cast<float>(defaultVal); \
            }()

        #define GET_INT(section, key, defaultVal) \
            [&]() { \
                if (userIni.GetValue(section, key)) return (int)userIni.GetLongValue(section, key); \
                if (defaultIni.GetValue(section, key)) return (int)defaultIni.GetLongValue(section, key); \
                defaultIni.SetLongValue(section, key, defaultVal); \
                bSaveDefaultNeeded = true; \
                return (int)defaultVal; \
            }()

        bool bEnable = GET_BOOL("General", "bEnable", 1);
        float fX     = GET_FLOAT("General", "fWidgetX", 1460.0f);
        float fY     = GET_FLOAT("General", "fWidgetY", 1000.0f);
        float fScale = GET_FLOAT("General", "fWidgetScale", 1.5f);

        bool bShowWidgetText = GET_BOOL("UI", "bShowWidgetText", 1);
        bool bShowCND        = GET_BOOL("UI", "bShowItemCardCND", 1);
        int iCNDPos          = GET_INT("UI", "iItemCardCNDPosition", 2);
        float fUnjamX        = GET_FLOAT("UI", "fUnjamX", 0.0f);
        float fUnjamY        = GET_FLOAT("UI", "fUnjamY", 0.0f);
        float fUnjamScale    = GET_FLOAT("UI", "fUnjamScale", 1.0f);
        float fJuryStatsOffsetX = GET_FLOAT("UI", "fJuryStatsOffsetX", 0.0f);
        float fJuryStatsOffsetY = GET_FLOAT("UI", "fJuryStatsOffsetY", 0.0f);
        float fJuryStatsRowSpacing = GET_FLOAT("UI", "fJuryStatsRowSpacing", 92.0f);
        int iJuryStatsColorMode = GET_INT("UI", "iJuryStatsColorMode", 0);
        
        bool bUseCustomColor = GET_BOOL("UI", "bUseCustomColor", 0);
        int parsedCustomColor = 16777215; 
        const char* colorVal = userIni.GetValue("UI", "sCustomColor");
        if (!colorVal || strlen(colorVal) == 0) colorVal = defaultIni.GetValue("UI", "sCustomColor");
        
        if (colorVal && strlen(colorVal) > 0) {
            std::string sColor = colorVal;
            sColor.erase(std::remove(sColor.begin(), sColor.end(), '\"'), sColor.end());
            if (!sColor.empty() && sColor[0] == '#') sColor.erase(0, 1);
            if (!sColor.empty()) {
                char* pEnd;
                unsigned long hexValue = std::strtoul(sColor.c_str(), &pEnd, 16);
                if (pEnd != sColor.c_str()) parsedCustomColor = static_cast<int>(hexValue);
            }
        } else {
            defaultIni.SetValue("UI", "sCustomColor", "FFFFFF");
            bSaveDefaultNeeded = true;
        }

        bool bRandomLoot   = GET_BOOL("Mechanics", "bRandomLootDurability", 1);
        bool bEnableWeaponCondition = GET_BOOL("Mechanics", "bEnableWeaponCondition", 1);
        bool bEnableArmorCondition = GET_BOOL("Mechanics", "bEnableArmorCondition", 1);
        bool bEnableOverCondition = GET_BOOL("Mechanics", "bEnableOverCondition", 1);
        bool bDialogueFull = GET_BOOL("Mechanics", "bDialogueFullDurability", 1);
        float fWeapLootMin = GET_FLOAT("Mechanics", "fWeaponLootDurabilityMin", 0.25f);
        float fWeapLootMax = GET_FLOAT("Mechanics", "fWeaponLootDurabilityMax", 0.80f);
        float fArmorLootMin= GET_FLOAT("Mechanics", "fArmorLootDurabilityMin", 0.35f);
        float fArmorLootMax= GET_FLOAT("Mechanics", "fArmorLootDurabilityMax", 0.90f);
        float fJamThreshold= GET_FLOAT("Mechanics", "fJamThreshold", 0.50f);
        float fMaxJamChance= GET_FLOAT("Mechanics", "fMaxJamChance", 0.15f);
        int iJammingPhase = GET_INT("Mechanics", "iJammingPhase", 0);
        bool bQuickUnjam   = GET_BOOL("Mechanics", "bQuickUnjam", 0);
        int iDegradationMode = GET_INT("Mechanics", "iDegradationMode", 0);
        float fPenaltyThreshold = GET_FLOAT("Mechanics", "fPenaltyThreshold", 0.75f);
        float fPenaltyMultMid   = GET_FLOAT("Mechanics", "fPenaltyMultMid", 0.75f);
        float fPenaltyMultLow   = GET_FLOAT("Mechanics", "fPenaltyMultLow", 0.50f);
        float fLinearMinMult    = GET_FLOAT("Mechanics", "fLinearMinMult", 0.50f);
        bool bWeaponConditionAffectsDamage = GET_BOOL("Mechanics", "bWeaponConditionAffectsDamage", 1);
        bool bWeaponConditionAffectsValue = GET_BOOL("Mechanics", "bWeaponConditionAffectsValue", 1);
        bool bArmorConditionAffectsResistance = GET_BOOL("Mechanics", "bArmorConditionAffectsResistance", 1);
        bool bArmorConditionAffectsValue = GET_BOOL("Mechanics", "bArmorConditionAffectsValue", 1);
        int iRepairMaterialMode = GET_INT("Mechanics", "iRepairMaterialMode", 0);
        bool bDynamicRepairMaterialCost = GET_BOOL("Mechanics", "bDynamicRepairMaterialCost", 1);
        bool bEnablePipboyJuryRepair = GET_BOOL("Mechanics", "bEnablePipboyJuryRepair", 1);
        bool bEnableWorkbenchMaterialRepair = GET_BOOL("Mechanics", "bEnableWorkbenchMaterialRepair", 1);
        bool bEnableRepairKits = GET_BOOL("Mechanics", "bEnableRepairKits", 1);
        bool bConfirmConsumeFavoriteMaterial = GET_BOOL("Mechanics", "bConfirmConsumeFavoriteMaterial", 1);
        bool bConfirmConsumeLegendaryMaterial = GET_BOOL("Mechanics", "bConfirmConsumeLegendaryMaterial", 1);
        
        bool bEnableLog    = GET_BOOL("Debug", "bEnableLogging", 0);

        #undef GET_BOOL
        #undef GET_FLOAT
        #undef GET_INT

        if (bSaveDefaultNeeded) {
            std::error_code ec;
            std::filesystem::create_directories(defaultPath.parent_path(), ec);
            defaultIni.SaveFile(defaultPath.string().c_str(), false);
        }

        ConditionSystem::g_mcmSettings.enable.store(bEnable);
        ConditionSystem::g_mcmSettings.x.store(fX);
        ConditionSystem::g_mcmSettings.y.store(fY);
        ConditionSystem::g_mcmSettings.scale.store(fScale);
        
        ConditionSystem::g_mcmSettings.showWidgetText.store(bShowWidgetText);
        ConditionSystem::g_mcmSettings.showItemCardCND.store(bShowCND);
        ConditionSystem::g_mcmSettings.itemCardCNDPosition.store(iCNDPos);
        ConditionSystem::g_mcmSettings.useCustomColor.store(bUseCustomColor);
        ConditionSystem::g_mcmSettings.customColor.store(parsedCustomColor);
        ConditionSystem::g_mcmSettings.unjamX.store(fUnjamX);
        ConditionSystem::g_mcmSettings.unjamY.store(fUnjamY);
        ConditionSystem::g_mcmSettings.unjamScale.store(fUnjamScale);
        ConditionSystem::g_mcmSettings.juryStatsOffsetX.store(fJuryStatsOffsetX);
        ConditionSystem::g_mcmSettings.juryStatsOffsetY.store(fJuryStatsOffsetY);
        ConditionSystem::g_mcmSettings.juryStatsRowSpacing.store(std::clamp(fJuryStatsRowSpacing, 84.0f, 110.0f));
        ConditionSystem::g_mcmSettings.juryStatsColorMode.store(std::clamp(iJuryStatsColorMode, 0, 1));

        ConditionSystem::g_mcmSettings.randomLootDurability.store(bRandomLoot);
        ConditionSystem::g_mcmSettings.enableWeaponCondition.store(bEnableWeaponCondition);
        ConditionSystem::g_mcmSettings.enableArmorCondition.store(bEnableArmorCondition);
        ConditionSystem::g_mcmSettings.enableOverCondition.store(bEnableOverCondition);
        ConditionSystem::g_mcmSettings.dialogueFullDurability.store(bDialogueFull);
        ConditionSystem::g_mcmSettings.weaponLootDurabilityMin.store(fWeapLootMin);
        ConditionSystem::g_mcmSettings.weaponLootDurabilityMax.store(fWeapLootMax);
        ConditionSystem::g_mcmSettings.armorLootDurabilityMin.store(fArmorLootMin);
        ConditionSystem::g_mcmSettings.armorLootDurabilityMax.store(fArmorLootMax);
        ConditionSystem::g_mcmSettings.jamThreshold.store(fJamThreshold);
        ConditionSystem::g_mcmSettings.maxJamChance.store(fMaxJamChance);
        ConditionSystem::g_mcmSettings.iJammingPhase.store(iJammingPhase);
        ConditionSystem::g_mcmSettings.quickUnjam.store(bQuickUnjam);
        ConditionSystem::g_mcmSettings.degradationMode.store(iDegradationMode);
        ConditionSystem::g_mcmSettings.penaltyThreshold.store(fPenaltyThreshold);
        ConditionSystem::g_mcmSettings.penaltyMultMid.store(fPenaltyMultMid);
        ConditionSystem::g_mcmSettings.penaltyMultLow.store(fPenaltyMultLow);
        ConditionSystem::g_mcmSettings.linearMinMult.store(fLinearMinMult);
        ConditionSystem::g_mcmSettings.weaponConditionAffectsDamage.store(bWeaponConditionAffectsDamage);
        ConditionSystem::g_mcmSettings.weaponConditionAffectsValue.store(bWeaponConditionAffectsValue);
        ConditionSystem::g_mcmSettings.armorConditionAffectsResistance.store(bArmorConditionAffectsResistance);
        ConditionSystem::g_mcmSettings.armorConditionAffectsValue.store(bArmorConditionAffectsValue);
        ConditionSystem::g_mcmSettings.repairMaterialMode.store(std::clamp(iRepairMaterialMode, 0, 1));
        ConditionSystem::g_mcmSettings.dynamicRepairMaterialCost.store(bDynamicRepairMaterialCost);
        ConditionSystem::g_mcmSettings.enablePipboyJuryRepair.store(bEnablePipboyJuryRepair);
        ConditionSystem::g_mcmSettings.enableWorkbenchMaterialRepair.store(bEnableWorkbenchMaterialRepair);
        ConditionSystem::g_mcmSettings.enableRepairKits.store(bEnableRepairKits);
        ConditionSystem::g_mcmSettings.confirmConsumeFavoriteMaterial.store(bConfirmConsumeFavoriteMaterial);
        ConditionSystem::g_mcmSettings.confirmConsumeLegendaryMaterial.store(bConfirmConsumeLegendaryMaterial);
        ConditionSystem::g_mcmSettings.enableLogging.store(bEnableLog);

        ConditionUI::UpdateVisuals(fX, fY, fScale);
        ConditionUI::UpdateUnjamVisuals(fUnjamX, fUnjamY, fUnjamScale); 
        ConditionUI::UpdatePenaltyThreshold(fPenaltyThreshold);
        ConditionUI::UpdateTextVisibility(bShowWidgetText); 
        ConditionUI::UpdateQuickLootColor();
        if (bEnableWeaponCondition || bEnableArmorCondition) {
            ConditionSystem::GrandfatherPlayerInventory();
        }
        ConditionUI::EvaluateVisibility();
    }
}

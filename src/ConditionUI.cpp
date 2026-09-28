#include "pch.h"
#include "ConditionUI.h"
#include "ConditionCore.h" 
#include "ProfileManager.h" // 确保能拿到最新的声明
#include "Repair/RepairSystem.h" // 获取 HUD 颜色
#include "Translation.h"
#include "PrismaUI_F4_API.h"

#include <RE/M/Main.h>
#include <RE/A/ACTOR_LIFE_STATE.h>

#include <array>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>

namespace ConditionSystem {
    namespace {
        constexpr auto kPrismaViewPath = "csf_condition_widget.html";

        // CSF targets the PrismaUI API shipped in the active MO2 overlay.
        // The current installed contract is V12; there is intentionally no
        // fallback to older PrismaUI interfaces.
        PRISMA_UI_API::IVPrismaUI12* g_prismaUI = nullptr;
        PrismaView g_prismaView = 0;
        bool g_prismaDomReady = false;
        bool g_prismaApiMissingLogged = false;
        bool g_prismaViewShown = false;
        std::atomic_bool g_widgetVisible{ false };
        std::atomic_bool g_unjamVisible{ false };
        std::atomic_bool g_widgetDomVisible{ false };
        std::atomic_bool g_heatWidgetHostVisible{ false };
        std::atomic_bool g_unjamDomVisible{ false };
        std::atomic<std::uint64_t> g_widgetVisibilityGeneration{ 0 };
        std::atomic<std::uint64_t> g_heatVisibilityGeneration{ 0 };
        std::atomic<std::uint64_t> g_unjamVisibilityGeneration{ 0 };
        bool g_prismaWidgetDomVisible = false;
        bool g_prismaUnjamDomVisible = false;
        std::chrono::steady_clock::time_point g_hudRestoreGraceUntil{};
        std::string g_hudRestoreClosingMenu;
        bool g_loadingFadeRestorePending = false;
        std::chrono::steady_clock::time_point g_loadingFadeRestoreArmUntil{};
        std::chrono::steady_clock::time_point g_loadingFadeRestoreExpireUntil{};
        int g_pendingWidgetShowAnimationMs = 0;
        std::atomic<std::uint64_t> g_visibilityRefreshGeneration{ 0 };
        constexpr std::array<int, 3> kPostMenuCloseRefreshDelays{ 100, 350, 800 };
        constexpr auto kScaleformTransitionGrace = std::chrono::milliseconds(1200);
        constexpr float kHeatWidgetLegacyCenterOffsetX = 84.0f - 640.0f;
        constexpr float kHeatWidgetLegacyCenterOffsetY = 19.0f - 360.0f;
        std::atomic_bool g_scaleformTransitionActive{ false };
        std::atomic<std::int64_t> g_scaleformTransitionGraceUntilNs{ 0 };
        std::atomic_bool g_deathHudRecoveryPending{ false };

        struct PostMenuCloseRefreshState
        {
            bool pending{ false };
            std::uint64_t generation{ 0 };
            bool animateWidget{ false };
            int animationMs{ 0 };
            std::size_t nextDelay{ 0 };
            std::chrono::steady_clock::time_point startedAt{};
        };

        PostMenuCloseRefreshState g_postMenuCloseRefresh{};

        struct HeatDispatchState
        {
            bool initialized{ false };
            float heat{ 0.0f };
            bool overheated{ false };
            bool visible{ false };
            std::uint8_t backend{ 0 };
            std::uint64_t target{ 0 };
        };

        HeatDispatchState g_heatDispatchState{};
        std::mutex g_heatDispatchMutex;
        std::atomic<std::uint64_t> g_heatDispatchGeneration{ 0 };

        void InvalidateHeatDispatchState()
        {
            std::lock_guard lock(g_heatDispatchMutex);
            g_heatDispatchGeneration.fetch_add(1, std::memory_order_relaxed);
            g_heatDispatchState = {};
        }

        void FailHeatDispatch(std::uint64_t a_generation)
        {
            if (g_heatDispatchGeneration.load(std::memory_order_relaxed) != a_generation) {
                return;
            }
            std::lock_guard lock(g_heatDispatchMutex);
            if (g_heatDispatchGeneration.load(std::memory_order_relaxed) == a_generation) {
                g_heatDispatchState.initialized = false;
            }
        }

        std::uint64_t GetContainerInventoryRevision(RE::TESObjectREFR* a_container)
        {
            if (!a_container || !a_container->inventoryList) {
                return 0;
            }

            std::uint64_t revision = 1469598103934665603ull;
            auto mix = [&revision](std::uint64_t value) {
                revision ^= value;
                revision *= 1099511628211ull;
            };

            for (const auto& item : a_container->inventoryList->data) {
                mix(item.object ? item.object->GetFormID() : 0);
                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    mix(static_cast<std::uint64_t>(stack->count));
                    mix(reinterpret_cast<std::uintptr_t>(stack));
                }
            }
            return revision;
        }

        [[nodiscard]] bool UsePrismaUI()
        {
            return ConditionSystem::g_mcmSettings.widgetBackend.load() == 0;
        }

        [[nodiscard]] bool UsePrismaUnjamUI()
        {
            return ConditionSystem::g_mcmSettings.unjamWidgetBackend.load() == 0;
        }

        [[nodiscard]] bool UsePrismaHeatUI()
        {
            return ConditionSystem::g_mcmSettings.heatWidgetBackend.load() == 0;
        }

        [[nodiscard]] bool UseAnyPrismaUI()
        {
            return UsePrismaUI() || UsePrismaUnjamUI() || UsePrismaHeatUI();
        }

        [[nodiscard]] std::uint32_t GetHudColor()
        {
            return ConditionSystem::Repair::GetHudColor() & 0x00FFFFFFu;
        }

        void QueueColorRefreshAfterMenuCreate()
        {
            if (auto task = F4SE::GetTaskInterface()) {
                task->AddTask([]() {
                    ConditionUI::ForceColorRefresh();
                });
            }
        }

        [[nodiscard]] std::uint32_t GetConfiguredWidgetColor(const std::atomic<int>& a_mode)
        {
            if (a_mode.load() == 1) {
                return static_cast<std::uint32_t>(ConditionSystem::g_mcmSettings.customColor.load()) & 0x00FFFFFFu;
            }
            return GetHudColor();
        }

        [[nodiscard]] std::int64_t GetSteadyClockNanoseconds()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        [[nodiscard]] bool IsPlayerDeathTransitionActive()
        {
            const auto player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return false;
            }

            const auto lifeState = static_cast<RE::ACTOR_LIFE_STATE>(player->lifeState);
            return lifeState == RE::ACTOR_LIFE_STATE::kDying ||
                   lifeState == RE::ACTOR_LIFE_STATE::kDead;
        }

        [[nodiscard]] bool IsScaleformTransitionActive()
        {
            if (IsPlayerDeathTransitionActive()) {
                return true;
            }

            if (g_scaleformTransitionActive.load(std::memory_order_relaxed)) {
                return true;
            }

            if (GetSteadyClockNanoseconds() <
                g_scaleformTransitionGraceUntilNs.load(std::memory_order_relaxed)) {
                return true;
            }

            auto ui = RE::UI::GetSingleton();
            return ui && (ui->GetMenuOpen("LoadingMenu") || ui->GetMenuOpen("FaderMenu"));
        }

        void MarkScaleformTransition(std::string_view a_menuName, bool a_opening)
        {
            if (a_menuName != "LoadingMenu" && a_menuName != "FaderMenu") {
                return;
            }

            const auto graceUntil = GetSteadyClockNanoseconds() +
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        kScaleformTransitionGrace)
                                        .count();
            g_scaleformTransitionGraceUntilNs.store(graceUntil, std::memory_order_relaxed);
            g_scaleformTransitionActive.store(a_opening, std::memory_order_relaxed);
        }

        struct SwfArgument
        {
            enum class Type
            {
                Boolean,
                Int,
                UInt,
                Number,
                String
            } type{ Type::Number };
            bool boolean{ false };
            std::int32_t intValue{ 0 };
            std::uint32_t uintValue{ 0 };
            double number{ 0.0 };
            std::string string;
        };

        using SwfCompletion = std::function<void(bool)>;

        [[nodiscard]] Scaleform::Ptr<RE::IMenu> GetWidgetMenu(const char* a_menuName)
        {
            auto ui = RE::UI::GetSingleton();
            return ui ? ui->GetMenu(a_menuName) : nullptr;
        }

        bool CopySwfArguments(
            Scaleform::GFx::Value* a_args,
            std::uint32_t a_argCount,
            std::vector<SwfArgument>& a_copiedArgs)
        {
            if (a_argCount > 0 && !a_args) {
                return false;
            }

            a_copiedArgs.reserve(a_argCount);
            for (std::uint32_t i = 0; i < a_argCount; ++i) {
                SwfArgument copied;
                switch (a_args[i].GetType()) {
                case Scaleform::GFx::Value::ValueType::kBoolean:
                    copied.type = SwfArgument::Type::Boolean;
                    copied.boolean = a_args[i].GetBoolean();
                    break;
                case Scaleform::GFx::Value::ValueType::kInt:
                    copied.type = SwfArgument::Type::Int;
                    copied.intValue = a_args[i].GetInt();
                    break;
                case Scaleform::GFx::Value::ValueType::kUInt:
                    copied.type = SwfArgument::Type::UInt;
                    copied.uintValue = a_args[i].GetUInt();
                    break;
                case Scaleform::GFx::Value::ValueType::kNumber:
                    copied.type = SwfArgument::Type::Number;
                    copied.number = a_args[i].GetNumber();
                    break;
                case Scaleform::GFx::Value::ValueType::kString:
                    copied.type = SwfArgument::Type::String;
                    copied.string = a_args[i].GetString() ? a_args[i].GetString() : "";
                    break;
                default:
                    return false;
                }
                a_copiedArgs.push_back(std::move(copied));
            }
            return true;
        }

        void BuildSwfArguments(const std::vector<SwfArgument>& a_source, std::vector<Scaleform::GFx::Value>& a_target)
        {
            a_target.resize(a_source.size());
            for (std::size_t i = 0; i < a_source.size(); ++i) {
                switch (a_source[i].type) {
                case SwfArgument::Type::Boolean:
                    a_target[i] = a_source[i].boolean;
                    break;
                case SwfArgument::Type::Int:
                    a_target[i] = a_source[i].intValue;
                    break;
                case SwfArgument::Type::UInt:
                    a_target[i] = a_source[i].uintValue;
                    break;
                case SwfArgument::Type::Number:
                    a_target[i] = a_source[i].number;
                    break;
                case SwfArgument::Type::String:
                    a_target[i] = a_source[i].string.c_str();
                    break;
                }
            }
        }

        bool QueueMenuMovieOperation(
            Scaleform::Ptr<RE::IMenu> a_menu,
            std::function<bool(Scaleform::GFx::Movie&)> a_operation,
            SwfCompletion a_completion = {},
            const std::atomic<std::uint64_t>* a_generation = nullptr,
            std::uint64_t a_expectedGeneration = 0)
        {
            auto complete = [&a_completion](bool a_succeeded) {
                if (a_completion) {
                    a_completion(a_succeeded);
                }
            };

            if (IsScaleformTransitionActive() || !a_menu || !a_menu->uiMovie) {
                complete(false);
                return false;
            }

            auto task = F4SE::GetTaskInterface();
            if (!task) {
                complete(false);
                return false;
            }

            task->AddUITask([
                menu = std::move(a_menu),
                operation = std::move(a_operation),
                completion = std::move(a_completion),
                generation = a_generation,
                expectedGeneration = a_expectedGeneration]() mutable {
                bool succeeded = false;
                const bool generationIsCurrent = !generation ||
                    generation->load(std::memory_order_relaxed) == expectedGeneration;
                if (generationIsCurrent && !IsScaleformTransitionActive() && menu->uiMovie) {
                    succeeded = operation(*menu->uiMovie);
                }
                if (completion) {
                    completion(succeeded);
                }
            });
            return true;
        }

        bool QueueWidgetSwfInvoke(
            Scaleform::Ptr<RE::IMenu> a_menu,
            const char* a_functionName,
            Scaleform::GFx::Value* a_args = nullptr,
            std::uint32_t a_argCount = 0,
            SwfCompletion a_completion = {},
            const std::atomic<std::uint64_t>* a_generation = nullptr,
            std::uint64_t a_expectedGeneration = 0)
        {
            std::vector<SwfArgument> copiedArgs;
            if (!a_functionName || !CopySwfArguments(a_args, a_argCount, copiedArgs)) {
                if (a_completion) {
                    a_completion(false);
                }
                return false;
            }

            std::string functionName(a_functionName);
            auto operation = [functionName = std::move(functionName), copiedArgs = std::move(copiedArgs)](
                                 Scaleform::GFx::Movie& a_movie) {
                Scaleform::GFx::Value root;
                if (!a_movie.GetVariable(&root, "root") || !root.IsObject()) {
                    return false;
                }
                std::vector<Scaleform::GFx::Value> args;
                BuildSwfArguments(copiedArgs, args);
                root.Invoke(
                    functionName.c_str(),
                    nullptr,
                    args.empty() ? nullptr : args.data(),
                    static_cast<std::uint32_t>(args.size()));
                return true;
            };
            return QueueMenuMovieOperation(
                std::move(a_menu),
                std::move(operation),
                std::move(a_completion),
                a_generation,
                a_expectedGeneration);
        }

        bool InvokeHeatSwf(
            const char* a_functionName,
            Scaleform::GFx::Value* a_args = nullptr,
            std::uint32_t a_argCount = 0,
            SwfCompletion a_completion = {},
            const std::atomic<std::uint64_t>* a_generation = nullptr,
            std::uint64_t a_expectedGeneration = 0)
        {
            return QueueWidgetSwfInvoke(
                GetWidgetMenu(HeatWidgetUI::MENU_NAME),
                a_functionName,
                a_args,
                a_argCount,
                std::move(a_completion),
                a_generation,
                a_expectedGeneration);
        }

        bool InvokeCndSwf(
            const char* a_functionName,
            Scaleform::GFx::Value* a_args = nullptr,
            std::uint32_t a_argCount = 0,
            SwfCompletion a_completion = {},
            const std::atomic<std::uint64_t>* a_generation = nullptr,
            std::uint64_t a_expectedGeneration = 0)
        {
            return QueueWidgetSwfInvoke(
                GetWidgetMenu(CndWidgetUI::MENU_NAME),
                a_functionName,
                a_args,
                a_argCount,
                std::move(a_completion),
                a_generation,
                a_expectedGeneration);
        }

        bool InvokeUnjamSwf(
            const char* a_functionName,
            Scaleform::GFx::Value* a_args = nullptr,
            std::uint32_t a_argCount = 0,
            SwfCompletion a_completion = {},
            const std::atomic<std::uint64_t>* a_generation = nullptr,
            std::uint64_t a_expectedGeneration = 0)
        {
            return QueueWidgetSwfInvoke(
                GetWidgetMenu(UnjamWidgetUI::MENU_NAME),
                a_functionName,
                a_args,
                a_argCount,
                std::move(a_completion),
                a_generation,
                a_expectedGeneration);
        }

        void HidePrismaView()
        {
            if (g_prismaUI && g_prismaView != 0 && g_prismaUI->IsValid(g_prismaView)) {
                g_prismaUI->Hide(g_prismaView);
            }
            g_prismaViewShown = false;
            g_widgetDomVisible = false;
            g_unjamDomVisible = false;
            g_prismaWidgetDomVisible = false;
            g_prismaUnjamDomVisible = false;
        }

        enum class AmmoHudVisibility : std::uint8_t
        {
            kUnknown,
            kVisible,
            kHidden
        };

        struct AmmoHudSyncState
        {
            bool applies{ false };
            AmmoHudVisibility visibility{ AmmoHudVisibility::kUnknown };
        };

        AmmoHudSyncState g_ammoHudSync{};

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
                "ConditionCndWidgetMenu",
                "ConditionUnjamWidgetMenu",
                "ConditionHeatWidgetMenu",
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

            if (IsPlayerDeathTransitionActive()) {
                return "player_death";
            }

            if (IsScaleformTransitionActive()) {
                return "loading_transition";
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

            const auto weaponState = player->weaponState;
            const bool sheathing =
                weaponState == RE::WEAPON_STATE::kSheathed ||
                weaponState == RE::WEAPON_STATE::kWantToSheathe ||
                weaponState == RE::WEAPON_STATE::kSheathing;
            if (sheathing) {
                ConditionSystem::g_isWeaponDrawn.store(false);
                return false;
            }

            const bool actualDrawn =
                player->GetWeaponMagicDrawn() ||
                weaponState == RE::WEAPON_STATE::kDrawn ||
                weaponState == RE::WEAPON_STATE::kDrawing ||
                weaponState == RE::WEAPON_STATE::kWantToDraw;

            if (actualDrawn) {
                // The weaponSheathe animation event can arrive before the
                // engine advances weaponState away from kDrawn. The event
                // cache is authoritative during that transition; do not
                // revive the widget from the stale engine state.
                return cachedDrawn;
            }

            // Only the explicit draw states above can keep the widget visible.
            // Clearing unknown states prevents a missed sheathe event from
            // leaving the last drawn state latched on-screen.
            ConditionSystem::g_isWeaponDrawn.store(false);
            return false;
        }

        [[nodiscard]] bool EquippedWeaponUsesAmmo()
        {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player || !player->inventoryList) {
                return false;
            }

            for (auto& item : player->inventoryList->data) {
                auto weapon = item.object ? item.object->As<RE::TESObjectWEAP>() : nullptr;
                if (!weapon) {
                    continue;
                }

                for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (!stack->IsEquipped()) {
                        continue;
                    }

                    RE::TESAmmo* ammo = nullptr;
                    if (stack->extra) {
                        if (auto instanceExtra = stack->extra->GetByType<RE::ExtraInstanceData>();
                            instanceExtra && instanceExtra->data) {
                            auto instanceData = static_cast<RE::TESObjectWEAP::InstanceData*>(instanceExtra->data.get());
                            ammo = instanceData->ammo;
                        }
                    }

                    return ammo != nullptr || weapon->weaponData.ammo != nullptr;
                }
            }

            return false;
        }

        [[nodiscard]] bool ReadMovieBoolean(Scaleform::GFx::Movie* a_movie, const char* a_path, bool& a_result)
        {
            if (!a_movie || !a_movie->asMovieRoot) {
                return false;
            }

            Scaleform::GFx::Value value;
            if (!a_movie->asMovieRoot->GetVariable(&value, a_path) || !value.IsBoolean()) {
                return false;
            }

            a_result = value.GetBoolean();
            return true;
        }

        [[nodiscard]] bool ReadMovieAlpha(Scaleform::GFx::Movie* a_movie, const char* a_path, float& a_result)
        {
            if (!a_movie || !a_movie->asMovieRoot) {
                return false;
            }

            Scaleform::GFx::Value value;
            if (!a_movie->asMovieRoot->GetVariable(&value, a_path)) {
                return false;
            }

            if (value.IsNumber()) {
                a_result = static_cast<float>(value.GetNumber());
                return true;
            }
            if (value.IsInt()) {
                a_result = static_cast<float>(value.GetInt());
                return true;
            }
            if (value.IsUInt()) {
                a_result = static_cast<float>(value.GetUInt());
                return true;
            }
            return false;
        }

        [[nodiscard]] AmmoHudVisibility QueryAmmoHudVisibility()
        {
            if (IsPlayerDeathTransitionActive() || IsScaleformTransitionActive()) {
                return AmmoHudVisibility::kUnknown;
            }

            auto ui = RE::UI::GetSingleton();
            auto hudMenu = ui ? ui->GetMenu("HUDMenu") : nullptr;
            if (!hudMenu || !hudMenu->uiMovie) {
                return AmmoHudVisibility::kUnknown;
            }

            auto movie = hudMenu->uiMovie.get();
            constexpr std::array<const char*, 2> visibilityPaths{
                "root.RightMeters_mc.visible",
                "root.RightMeters_mc.AmmoCount_mc.visible"
            };
            constexpr std::array<const char*, 2> alphaPaths{
                "root.RightMeters_mc.alpha",
                "root.RightMeters_mc.AmmoCount_mc.alpha"
            };

            for (auto path : visibilityPaths) {
                bool visible = true;
                if (!ReadMovieBoolean(movie, path, visible)) {
                    return AmmoHudVisibility::kUnknown;
                }
                if (!visible) {
                    return AmmoHudVisibility::kHidden;
                }
            }

            for (auto path : alphaPaths) {
                float alpha = 1.0f;
                if (!ReadMovieAlpha(movie, path, alpha)) {
                    return AmmoHudVisibility::kUnknown;
                }
                if (alpha <= 0.01f) {
                    return AmmoHudVisibility::kHidden;
                }
            }

            return AmmoHudVisibility::kVisible;
        }

        [[nodiscard]] bool ShouldShowForAmmoHud()
        {
            return !g_ammoHudSync.applies || g_ammoHudSync.visibility != AmmoHudVisibility::kHidden;
        }

        void PollAmmoHudVisibility()
        {
            if (IsPlayerDeathTransitionActive()) {
                g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
                return;
            }
            if (IsScaleformTransitionActive()) {
                return;
            }

            static auto lastPoll = std::chrono::steady_clock::time_point{};
            const auto now = std::chrono::steady_clock::now();
            constexpr auto kPollInterval = std::chrono::milliseconds(250);
            if (lastPoll != std::chrono::steady_clock::time_point{} && now - lastPoll < kPollInterval) {
                return;
            }
            lastPoll = now;

            const bool applies = EquippedWeaponUsesAmmo();
            const AmmoHudVisibility visibility = applies ? QueryAmmoHudVisibility() : AmmoHudVisibility::kUnknown;
            const bool changed =
                g_ammoHudSync.applies != applies ||
                g_ammoHudSync.visibility != visibility;

            if (!changed) {
                return;
            }

            g_ammoHudSync = { applies, visibility };
            if (ConditionSystem::g_mcmSettings.enableLogging.load()) {
                const char* state = visibility == AmmoHudVisibility::kVisible ? "visible" :
                                    visibility == AmmoHudVisibility::kHidden ? "hidden" : "unknown";
                CS_LOG("[CSF-WidgetDiag] AmmoHudSync applies={} state={}", BoolFlag(applies), state);
            }

            ConditionUI::EvaluateVisibility();
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

        bool SendPrisma(const char* a_functionName, const nlohmann::json& a_payload)
        {
            if (IsPlayerDeathTransitionActive()) {
                g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
                return false;
            }
            if (IsScaleformTransitionActive()) {
                return false;
            }
            if (!IsPrismaViewReady()) {
                return false;
            }
            const auto payload = a_payload.dump();
            g_prismaUI->InteropCall(g_prismaView, a_functionName, payload.c_str());
            return true;
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
            if (IsPlayerDeathTransitionActive()) {
                g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
                return;
            }
            if (IsScaleformTransitionActive()) {
                return;
            }

            if (!UseAnyPrismaUI()) {
                HidePrismaView();
                return;
            }
            RefreshPrismaViewVisibility();
            if (!IsPrismaViewReady()) {
                LogWidgetDiag("ApplyPrismaHudVisibility:skip", "prisma_not_ready");
                if (a_animateWidget && a_animationMs > 0 && g_widgetVisible) {
                    g_pendingWidgetShowAnimationMs = a_animationMs;
                }
                return;
            }

            const bool inHud = IsGameplayHUDOnly(true);
            const bool widgetDomVisible = inHud && UsePrismaUI() && g_widgetVisible;
            const bool unjamDomVisible = inHud && UsePrismaUnjamUI() && g_unjamVisible;

            if (widgetDomVisible != g_prismaWidgetDomVisible) {
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
                g_prismaWidgetDomVisible = widgetDomVisible;
            }

            if (unjamDomVisible != g_prismaUnjamDomVisible) {
                LogWidgetDiag("ApplyPrismaHudVisibility:unjam", unjamDomVisible ? "dom_show" : "dom_hide");
                SendPrisma("setUnjamVisible", { { "visible", unjamDomVisible } });
                g_prismaUnjamDomVisible = unjamDomVisible;
            }
        }

        void ApplySwfHudVisibility()
        {
            if (IsScaleformTransitionActive()) {
                return;
            }

            const bool inHud = IsGameplayHUDOnly(true);
            const bool widgetSwfVisible = inHud && !UsePrismaUI() && g_widgetVisible;
            const bool unjamSwfVisible = inHud && !UsePrismaUnjamUI() && g_unjamVisible;

            if (widgetSwfVisible != g_widgetDomVisible.load(std::memory_order_relaxed)) {
                Scaleform::GFx::Value args[1];
                args[0] = widgetSwfVisible;
                const auto generation = g_widgetVisibilityGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
                g_widgetDomVisible.store(widgetSwfVisible, std::memory_order_relaxed);
                InvokeCndSwf(
                    "SetWidgetVisible",
                    args,
                    1,
                    [widgetSwfVisible, generation](bool a_invoked) {
                        if (!a_invoked && g_widgetVisibilityGeneration.load(std::memory_order_relaxed) == generation) {
                            g_widgetDomVisible.store(!widgetSwfVisible, std::memory_order_relaxed);
                        }
                    },
                    &g_widgetVisibilityGeneration,
                    generation);
                LogWidgetDiag("ApplySwfHudVisibility:widget", widgetSwfVisible ? "swf_show" : "swf_hide");
            }

            // The heat bar is a separate always-open SWF. Only send visibility
            // changes; repeatedly re-rendering the root during draw transitions
            // can expose the FLA preview frame for one or more frames.
            const bool heatWidgetVisible = inHud && !UsePrismaHeatUI() && g_widgetVisible;
            if (heatWidgetVisible != g_heatWidgetHostVisible.load(std::memory_order_relaxed)) {
                Scaleform::GFx::Value heatArgs[1];
                heatArgs[0] = heatWidgetVisible;
                const auto generation = g_heatVisibilityGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
                g_heatWidgetHostVisible.store(heatWidgetVisible, std::memory_order_relaxed);
                InvokeHeatSwf(
                    "SetWidgetVisible",
                    heatArgs,
                    1,
                    [heatWidgetVisible, generation](bool a_invoked) {
                        if (!a_invoked && g_heatVisibilityGeneration.load(std::memory_order_relaxed) == generation) {
                            g_heatWidgetHostVisible.store(!heatWidgetVisible, std::memory_order_relaxed);
                        }
                    },
                    &g_heatVisibilityGeneration,
                    generation);
                LogWidgetDiag("ApplySwfHudVisibility:heat", heatWidgetVisible ? "swf_show" : "swf_hide");
            }

            if (unjamSwfVisible != g_unjamDomVisible.load(std::memory_order_relaxed)) {
                Scaleform::GFx::Value args[1];
                args[0] = unjamSwfVisible;
                const auto generation = g_unjamVisibilityGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
                g_unjamDomVisible.store(unjamSwfVisible, std::memory_order_relaxed);
                InvokeUnjamSwf(
                    "SetWidgetVisible",
                    args,
                    1,
                    [unjamSwfVisible, generation](bool a_invoked) {
                        if (!a_invoked && g_unjamVisibilityGeneration.load(std::memory_order_relaxed) == generation) {
                            g_unjamDomVisible.store(!unjamSwfVisible, std::memory_order_relaxed);
                        }
                    },
                    &g_unjamVisibilityGeneration,
                    generation);
                LogWidgetDiag("ApplySwfHudVisibility:unjam", unjamSwfVisible ? "swf_show" : "swf_hide");
            }
        }

        void ApplyHudVisibility(bool a_animateWidget = false, int a_animationMs = 0)
        {
            if (UseAnyPrismaUI()) {
                ApplyPrismaHudVisibility(a_animateWidget, a_animationMs);
            }
            ApplySwfHudVisibility();
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
            ConditionUI::UpdateHeatVisuals(
                ConditionSystem::g_mcmSettings.heatWidgetX.load(),
                ConditionSystem::g_mcmSettings.heatWidgetY.load(),
                ConditionSystem::g_mcmSettings.heatWidgetScale.load());
            ConditionUI::UpdatePenaltyThreshold(ConditionSystem::g_mcmSettings.penaltyThreshold.load());
            ConditionUI::UpdateTextVisibility(ConditionSystem::g_mcmSettings.showWidgetText.load());
            ConditionUI::UpdateQuickLootColor();
            ConditionUI::UpdateDurability(
                ConditionSystem::GetEquippedWeaponDurabilityPercent(),
                ConditionSystem::GetEquippedWeaponDurabilityPoints());
            const auto fault = ConditionSystem::GetWeaponFaultSnapshot();
            ConditionUI::UpdateHeat(
                fault.weaponHeat,
                fault.IsOverheated(),
                fault.weaponHeat > 0.001f || fault.IsOverheated());
            g_widgetDomVisible = false;
            g_heatWidgetHostVisible = false;
            g_unjamDomVisible = false;
            g_prismaWidgetDomVisible = false;
            g_prismaUnjamDomVisible = false;
            ApplyPrismaHudVisibility(g_pendingWidgetShowAnimationMs > 0, g_pendingWidgetShowAnimationMs);
        }

        void OnPrismaDomReady(PrismaView a_view)
        {
            if (a_view != g_prismaView) {
                return;
            }
            g_prismaDomReady = true;
            InvalidateHeatDispatchState();
            // Notify the widget that PrismaUI bridge is fully initialized.
            // window.init() is the standard PrismaUI lifecycle callback that
            // signals DOM ready + window.prisma availability to the view.
            SendPrisma("init", {});
            SendPrisma("warmupWidget", {});
            PushPrismaState();
            REX::INFO("[CSF-PrismaUI] Condition widget DOM ready, init() called.");
        }

        void QueuePostMenuCloseVisibilityRefresh(bool a_animateWidget = false, int a_animationMs = 0)
        {
            const auto generation = g_visibilityRefreshGeneration.fetch_add(1) + 1;
            g_postMenuCloseRefresh = {
                true,
                generation,
                a_animateWidget,
                a_animationMs,
                0,
                std::chrono::steady_clock::now()
            };

            if (auto task = F4SE::GetTaskInterface()) {
                task->AddTask([a_animateWidget, a_animationMs]() {
                    LogWidgetDiag("PostMenuCloseRefresh", "immediate_task");
                    if (IsScaleformTransitionActive()) {
                        return;
                    }
                    ConditionUI::EvaluateVisibility(a_animateWidget, a_animationMs);
                });
            }
        }

        void ProcessPostMenuCloseVisibilityRefresh()
        {
            if (!g_postMenuCloseRefresh.pending) {
                return;
            }

            if (g_postMenuCloseRefresh.generation != g_visibilityRefreshGeneration.load()) {
                g_postMenuCloseRefresh.pending = false;
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (IsScaleformTransitionActive()) {
                // Keep the refresh pending, but restart its relative schedule so
                // the first callback runs only after the loading/fader boundary
                // has settled instead of calling into a tearing-down SWF menu.
                g_postMenuCloseRefresh.startedAt = now;
                return;
            }

            while (g_postMenuCloseRefresh.nextDelay < kPostMenuCloseRefreshDelays.size()) {
                const auto delayMs = kPostMenuCloseRefreshDelays[g_postMenuCloseRefresh.nextDelay];
                const auto deadline = g_postMenuCloseRefresh.startedAt + std::chrono::milliseconds(delayMs);
                if (now < deadline) {
                    break;
                }

                ++g_postMenuCloseRefresh.nextDelay;
                CS_LOG("[CSF-WidgetDiag] post-close delayed refresh delayMs={}", delayMs);
                LogWidgetDiag("PostMenuCloseRefresh", "delayed_task");
                ConditionUI::EvaluateVisibility(
                    g_postMenuCloseRefresh.animateWidget,
                    g_postMenuCloseRefresh.animationMs);
            }

            if (g_postMenuCloseRefresh.nextDelay >= kPostMenuCloseRefreshDelays.size()) {
                g_postMenuCloseRefresh.pending = false;
            }
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
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return;
        }
        if (IsScaleformTransitionActive()) {
            return;
        }

        if (g_deathHudRecoveryPending.exchange(false, std::memory_order_relaxed)) {
            REX::INFO("[CSF-WidgetDiag] Recovering HUD widgets after player death transition.");
            OpenMenu();
            EvaluateVisibility();
            return;
        }

        RE::GameMenuBase::AdvanceMovie(a_interval, a_currentTime);

        // Delayed menu-close refreshes are driven by the always-open UI menu.
        // Keeping this scheduler on the UI thread avoids one detached timer
        // thread per menu event and makes rapid menu transitions cancellable.
        ProcessPostMenuCloseVisibilityRefresh();

        // 保留 3 帧节流阀：防止高频刷爆 Scaleform 接口
        static int frameCounter = 0;
        if (frameCounter++ % 3 != 0) return; 

        // 动画事件负责正常的拔枪状态同步；这里仅保留低频兜底，避免每帧
        // 读取玩家状态成为静止游戏时的固定开销。
        PollWeaponDrawStart();

        // 对远程武器同步 FallUI 的弹药 HUD 显隐；近战武器不受弹药组件影响。
        PollAmmoHudVisibility();

        static std::uint32_t lastScannedRefID = 0;
        static std::uint64_t lastInventoryRevision = 0;

        auto ui = RE::UI::GetSingleton();
        if (!ui) {
            lastScannedRefID = 0;
            lastInventoryRevision = 0;
            return;
        }
        auto invIntfc = RE::BGSInventoryInterface::GetSingleton();
        if (!invIntfc || invIntfc->agentArray.size() == 0) {
            // A closed container leaves no inventory agents. Invalidate the
            // focused-container cache so reopening the same reference sends a
            // fresh CND payload instead of being mistaken for an unchanged UI.
            lastScannedRefID = 0;
            lastInventoryRevision = 0;
            return;
        }
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
            const auto inventoryRevision = GetContainerInventoryRevision(currentContainer);

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

            // 2. Only rebuild the QuickLoot payload when the focused container or
            // its inventory contents changed. The menu advances every frame, but
            // the container payload does not need to be recreated every 3 frames.
            if (currentRefID == lastScannedRefID && inventoryRevision == lastInventoryRevision) {
                return;
            }
            lastInventoryRevision = inventoryRevision;

            // 3. 【UI 更新】：向 HUD 发送已经绝对真实的耐久度
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
                }
                // Always send the array, including [] for containers without
                // managed items, so QuickLoot cannot retain the previous
                // container's cached condition data.
                Scaleform::GFx::Value args[1];
                args[0] = cndArray;
                menu->uiMovie->Invoke("root.CenterGroup_mc.QuickContainerWidget_mc.ReceiveCNDData", nullptr, args, 1);
            }
        } else {
            // 玩家移开了准星（面前没有容器了），重置缓存
            // 这样玩家下次再看向同一个箱子时，还能再做一次兜底的初始化检查
            lastScannedRefID = 0;
            lastInventoryRevision = 0;
        }
    }


    // =========================================================================
    // 模块 2：Scaleform UI 菜单生命周期管理
    // =========================================================================

    CndWidgetUI::CndWidgetUI() : RE::GameMenuBase() {
        menuFlags.set(RE::UI_MENU_FLAGS::kAllowSaving, RE::UI_MENU_FLAGS::kAlwaysOpen);
        depthPriority = RE::UI_DEPTH_PRIORITY::kSWFLoader;
    }

    CndWidgetUI::~CndWidgetUI() {
        // IMenu destroys uiMovie before member values. Release menuObj while
        // its Scaleform movie/object interface is still alive.
        menuObj = nullptr;
    }

    RE::IMenu* CndWidgetUI::CreateMenu(const RE::UIMessage&) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return nullptr;
        }
        if (IsScaleformTransitionActive()) {
            return nullptr;
        }

        g_widgetVisibilityGeneration.fetch_add(1, std::memory_order_relaxed);
        g_widgetDomVisible.store(false, std::memory_order_relaxed);
        auto menu = new CndWidgetUI();
        auto scaleformMgr = RE::BSScaleformManager::GetSingleton();
        if (!scaleformMgr) return menu;
        const auto moviePath = ConditionSystem::g_mcmSettings.swfCndStyle.load() == 1
            ? "Interface\\ConditionCndWidgetFNV.swf"sv
            : "Interface\\ConditionCndWidgetFO4.swf"sv;
        if (!scaleformMgr->LoadMovieEx(*menu, moviePath, "root") || !menu->uiMovie) {
            return menu;
        }
        Scaleform::GFx::Value root;
        if (menu->uiMovie->GetVariable(&root, "root") && root.IsObject()) {
            Scaleform::GFx::Value transform[3];
            transform[0] = static_cast<double>(ConditionSystem::g_mcmSettings.x.load());
            transform[1] = static_cast<double>(ConditionSystem::g_mcmSettings.y.load());
            transform[2] = static_cast<double>(ConditionSystem::g_mcmSettings.scale.load());
            root.Invoke("SetWidgetTransform", nullptr, transform, 3);
            Scaleform::GFx::Value style[1];
            style[0] = ConditionSystem::g_mcmSettings.swfCndStyle.load() == 1;
            root.Invoke("SetCndStyle", nullptr, style, 1);
            Scaleform::GFx::Value color[1];
            color[0] = static_cast<double>(GetConfiguredWidgetColor(ConditionSystem::g_mcmSettings.cndColorMode));
            root.Invoke("SetCustomColor", nullptr, color, 1);
            Scaleform::GFx::Value visible[1];
            visible[0] = false;
            root.Invoke("SetWidgetVisible", nullptr, visible, 1);
        }
        QueueColorRefreshAfterMenuCreate();
        return menu;
    }

    void CndWidgetUI::RegisterMenu() {
        if (auto ui = RE::UI::GetSingleton()) ui->RegisterMenu(MENU_NAME, CreateMenu);
    }

    UnjamWidgetUI::UnjamWidgetUI() : RE::GameMenuBase() {
        menuFlags.set(RE::UI_MENU_FLAGS::kAllowSaving, RE::UI_MENU_FLAGS::kAlwaysOpen);
        depthPriority = RE::UI_DEPTH_PRIORITY::kSWFLoader;
    }

    UnjamWidgetUI::~UnjamWidgetUI() {
        menuObj = nullptr;
    }

    RE::IMenu* UnjamWidgetUI::CreateMenu(const RE::UIMessage&) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return nullptr;
        }
        if (IsScaleformTransitionActive()) {
            return nullptr;
        }

        g_unjamVisibilityGeneration.fetch_add(1, std::memory_order_relaxed);
        g_unjamDomVisible.store(false, std::memory_order_relaxed);
        auto menu = new UnjamWidgetUI();
        auto scaleformMgr = RE::BSScaleformManager::GetSingleton();
        if (!scaleformMgr) return menu;
        if (!scaleformMgr->LoadMovieEx(*menu, "Interface\\ConditionUnjamWidget.swf"sv, "root") || !menu->uiMovie) {
            return menu;
        }
        Scaleform::GFx::Value root;
        if (menu->uiMovie->GetVariable(&root, "root") && root.IsObject()) {
            Scaleform::GFx::Value transform[3];
            transform[0] = static_cast<double>(ConditionSystem::g_mcmSettings.unjamX.load());
            transform[1] = static_cast<double>(ConditionSystem::g_mcmSettings.unjamY.load());
            transform[2] = static_cast<double>(ConditionSystem::g_mcmSettings.unjamScale.load());
            root.Invoke("SetWidgetTransform", nullptr, transform, 3);
            Scaleform::GFx::Value color[1];
            color[0] = static_cast<double>(GetConfiguredWidgetColor(ConditionSystem::g_mcmSettings.unjamColorMode));
            root.Invoke("SetUnjamColor", nullptr, color, 1);
            Scaleform::GFx::Value visible[1];
            visible[0] = false;
            root.Invoke("SetWidgetVisible", nullptr, visible, 1);
        }
        QueueColorRefreshAfterMenuCreate();
        return menu;
    }

    void UnjamWidgetUI::RegisterMenu() {
        if (auto ui = RE::UI::GetSingleton()) ui->RegisterMenu(MENU_NAME, CreateMenu);
    }

    HeatWidgetUI::HeatWidgetUI() : RE::GameMenuBase() {
        menuFlags.set(
            RE::UI_MENU_FLAGS::kAllowSaving,
            RE::UI_MENU_FLAGS::kAlwaysOpen
        );
        depthPriority = RE::UI_DEPTH_PRIORITY::kSWFLoader;
    }

    HeatWidgetUI::~HeatWidgetUI() {
        menuObj = nullptr;
    }

    RE::IMenu* HeatWidgetUI::CreateMenu(const RE::UIMessage&) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return nullptr;
        }
        if (IsScaleformTransitionActive()) {
            return nullptr;
        }

        g_heatVisibilityGeneration.fetch_add(1, std::memory_order_relaxed);
        g_heatWidgetHostVisible.store(false, std::memory_order_relaxed);
        auto menu = new HeatWidgetUI();
        auto scaleformMgr = RE::BSScaleformManager::GetSingleton();
        if (!scaleformMgr) return menu;

        const auto moviePath = "Interface\\ConditionHeatWidget.swf"sv;
        if (!scaleformMgr->LoadMovieEx(*menu, moviePath, "root") || !menu->uiMovie) {
            return menu;
        }

        Scaleform::GFx::Value root;
        if (menu->uiMovie->GetVariable(&root, "root") && root.IsObject()) {
            // Hide the FLA preview sample before applying live state. The
            // heat clip is always-open, so its first frame can otherwise flash
            // while the draw state is still being synchronized.
            Scaleform::GFx::Value hidden[1];
            hidden[0] = false;
            root.Invoke("SetWidgetVisible", nullptr, hidden, 1);
            g_heatWidgetHostVisible = false;

            Scaleform::GFx::Value args[3];
            args[0] = static_cast<double>(ConditionSystem::g_mcmSettings.heatWidgetX.load());
            args[1] = static_cast<double>(ConditionSystem::g_mcmSettings.heatWidgetY.load());
            args[2] = static_cast<double>(ConditionSystem::g_mcmSettings.heatWidgetScale.load());
            root.Invoke("SetHeatTransform", nullptr, args, 3);

            Scaleform::GFx::Value heatColor[1];
            heatColor[0] = static_cast<double>(GetConfiguredWidgetColor(ConditionSystem::g_mcmSettings.heatColorMode));
            root.Invoke("SetHeatColor", nullptr, heatColor, 1);

            const auto fault = ConditionSystem::GetWeaponFaultSnapshot();
            Scaleform::GFx::Value heat[3];
            heat[0] = static_cast<double>(fault.weaponHeat);
            heat[1] = fault.IsOverheated();
            heat[2] = fault.weaponHeat > 0.001f || fault.IsOverheated();
            root.Invoke("SetHeat", nullptr, heat, 3);

            const bool initialVisible = !UsePrismaUI() && g_widgetVisible && IsGameplayHUDOnly(true);
            Scaleform::GFx::Value visible[1];
            visible[0] = initialVisible;
            root.Invoke("SetWidgetVisible", nullptr, visible, 1);
            g_heatWidgetHostVisible = initialVisible;
        }
        QueueColorRefreshAfterMenuCreate();
        return menu;
    }

    void HeatWidgetUI::RegisterMenu() {
        if (auto ui = RE::UI::GetSingleton()) {
            ui->RegisterMenu(MENU_NAME, CreateMenu);
        }
    }

    void HeatWidgetUI::OpenMenu() {
        if (auto queue = RE::UIMessageQueue::GetSingleton()) {
            queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
        }
    }

    void HeatWidgetUI::CloseMenu() {
        if (auto queue = RE::UIMessageQueue::GetSingleton()) {
            queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
        }
    }

    ConditionUI::ConditionUI() : RE::GameMenuBase() {
        menuFlags.set(
            RE::UI_MENU_FLAGS::kAllowSaving,
            RE::UI_MENU_FLAGS::kDontHideCursorWhenTopmost,
            RE::UI_MENU_FLAGS::kAlwaysOpen
        );
        depthPriority = RE::UI_DEPTH_PRIORITY::kSWFLoader;
    }

    ConditionUI::~ConditionUI() {
        menuObj = nullptr;
    }

    RE::IMenu* ConditionUI::CreateMenu(const RE::UIMessage&) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return nullptr;
        }
        if (IsScaleformTransitionActive()) {
            return nullptr;
        }

        // This menu remains registered so AdvanceMovie can drive QuickLoot
        // polling and HUD visibility refreshes. It deliberately has no
        // Scaleform movie; only the four standalone widget menus load SWFs.
        return new ConditionUI();
    }

    void ConditionUI::RegisterMenu() {
        auto ui = RE::UI::GetSingleton();
        if (ui) ui->RegisterMenu(MENU_NAME, CreateMenu);
        CndWidgetUI::RegisterMenu();
        UnjamWidgetUI::RegisterMenu();
        HeatWidgetUI::RegisterMenu();
    }

    void ConditionUI::InitializePrisma() {
        if (!UseAnyPrismaUI()) {
            return;
        }
        if (g_prismaUI) {
            return;
        }

        g_prismaUI = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI12>();
        if (g_prismaUI) {
            REX::INFO("[CSF-PrismaUI] PrismaUI_F4 V12 API found. HUD overlay is enabled.");
            return;
        }

        if (!g_prismaApiMissingLogged) {
            g_prismaApiMissingLogged = true;
            REX::INFO("[CSF-PrismaUI] PrismaUI_F4 V12 API not found. "
                      "The active MO2 PrismaUI installation is required.");
        }
    }

    void ConditionUI::CreatePrismaView() {
        if (!UseAnyPrismaUI()) {
            return;
        }
        InitializePrisma();
        if (!g_prismaUI) {
            return;
        }

        if (g_prismaView != 0 && g_prismaUI->IsValid(g_prismaView)) {
            return;
        }

        g_prismaDomReady = false;
        InvalidateHeatDispatchState();
        g_prismaView = g_prismaUI->CreateView(kPrismaViewPath, OnPrismaDomReady);
        if (g_prismaView == 0 || !g_prismaUI->IsValid(g_prismaView)) {
            REX::INFO("[CSF-PrismaUI] Failed to create condition widget view: {}", kPrismaViewPath);
            g_prismaView = 0;
            return;
        }

        g_prismaUI->RegisterTranslations(g_prismaView, "Condition System Framework");
        g_prismaUI->SetOrder(g_prismaView, 5);
        g_prismaUI->RegisterConsoleCallback(
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
        g_prismaUI->Hide(g_prismaView);
        g_prismaViewShown = false;
        g_widgetDomVisible = false;
        g_unjamDomVisible = false;
        g_prismaWidgetDomVisible = false;
        g_prismaUnjamDomVisible = false;
        REX::INFO("[CSF-PrismaUI] Created condition widget view: {}", kPrismaViewPath);
    }

    void ConditionUI::OpenMenu() {
        if (IsScaleformTransitionActive()) {
            if (IsPlayerDeathTransitionActive()) {
                g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            }
            return;
        }

        auto queue = RE::UIMessageQueue::GetSingleton();
        if (queue) {
            queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
            if (!UsePrismaUI()) {
                queue->AddMessage(CndWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
            }
            if (!UsePrismaUnjamUI()) {
                queue->AddMessage(UnjamWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
            }
            if (!UsePrismaHeatUI()) {
                queue->AddMessage(HeatWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
            }
        }
    }

    void ConditionUI::CloseMenu() {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            g_widgetVisible = false;
            g_unjamVisible = false;
            return;
        }

        auto queue = RE::UIMessageQueue::GetSingleton();
        if (queue) {
            queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
            queue->AddMessage(CndWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
            queue->AddMessage(UnjamWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
            queue->AddMessage(HeatWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
        }
        g_widgetVisible = false;
        g_unjamVisible = false;
        Scaleform::GFx::Value args[1];
        args[0] = false;
        ApplyHudVisibility();
        if (UsePrismaHeatUI()) {
            InvalidateHeatDispatchState();
            const auto fault = ConditionSystem::GetWeaponFaultSnapshot();
            SendPrisma("setHeat", {
                { "heat", fault.weaponHeat },
                { "overheated", fault.IsOverheated() },
                { "visible", false }
            });
        }
    }

    void ConditionUI::RefreshBackend()
    {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return;
        }
        if (IsScaleformTransitionActive()) {
            return;
        }

        InvalidateHeatDispatchState();
        Scaleform::GFx::Value hidden[1];
        hidden[0] = false;

        if (!UsePrismaUI()) {
            const bool useFNVStyle = ConditionSystem::g_mcmSettings.swfCndStyle.load() == 1;
            // The split CND widget is the only Scaleform CND root.
            Scaleform::GFx::Value styleArgs[1];
            styleArgs[0] = useFNVStyle;
            InvokeCndSwf("SetCndStyle", styleArgs, 1);
        }

        if (UseAnyPrismaUI()) {
            if (UsePrismaUI()) {
                InvokeCndSwf("SetWidgetVisible", hidden, 1);
            }
            if (UsePrismaUnjamUI()) {
                InvokeUnjamSwf("SetWidgetVisible", hidden, 1);
            }
            if (UsePrismaHeatUI()) {
                InvokeHeatSwf("SetWidgetVisible", hidden, 1);
            }
            CreatePrismaView();
            if (!UsePrismaUI()) {
                if (auto queue = RE::UIMessageQueue::GetSingleton()) {
                    queue->AddMessage(CndWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
                }
            }
            if (!UsePrismaUnjamUI()) {
                if (auto queue = RE::UIMessageQueue::GetSingleton()) {
                    queue->AddMessage(UnjamWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
                }
            }
            if (!UsePrismaHeatUI()) {
                if (auto queue = RE::UIMessageQueue::GetSingleton()) {
                    queue->AddMessage(HeatWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
                }
            }
            g_widgetDomVisible = false;
            g_heatWidgetHostVisible = false;
            g_unjamDomVisible = false;
            g_prismaWidgetDomVisible = false;
            g_prismaUnjamDomVisible = false;
        } else {
            HidePrismaView();
            if (auto queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(CndWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
                queue->AddMessage(UnjamWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
                queue->AddMessage(HeatWidgetUI::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
            }
            g_widgetDomVisible = false;
            g_heatWidgetHostVisible = false;
            g_unjamDomVisible = false;
            g_prismaWidgetDomVisible = false;
            g_prismaUnjamDomVisible = false;
        }
    }


    // =========================================================================
    // 模块 3：UI 通信与状态更新接口
    // =========================================================================

    void ConditionUI::UpdateVisuals(float a_x, float a_y, float a_scale) {
        if (UsePrismaUI()) {
            SendPrisma("setWidgetTransform", {
                { "x", a_x },
                { "y", a_y },
                { "scale", a_scale }
            });
        } else {
            Scaleform::GFx::Value args[3];
            args[0] = static_cast<double>(a_x);
            args[1] = static_cast<double>(a_y);
            args[2] = static_cast<double>(a_scale);
            InvokeCndSwf("SetWidgetTransform", args, 3);
        }
    }

    void ConditionUI::UpdateUnjamVisuals(float a_offsetX, float a_offsetY, float a_scale) {
        if (UsePrismaUnjamUI()) {
            SendPrisma("setUnjamTransform", {
                { "x", a_offsetX },
                { "y", a_offsetY },
                { "scale", a_scale }
            });
        } else {
            Scaleform::GFx::Value args[3];
            args[0] = static_cast<double>(a_offsetX);
            args[1] = static_cast<double>(a_offsetY);
            args[2] = static_cast<double>(a_scale);
            InvokeUnjamSwf("SetWidgetTransform", args, 3);
        }
    }

    void ConditionUI::EvaluateVisibility(bool a_animateWidget, int a_animationMs) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return;
        }

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
            // Keep the logical state in sync with the HUD gate. Calling only
            // ApplyHudVisibility() leaves g_widgetVisible=true, so a newly
            // created CND movie (whose MCM preview starts visible) can remain
            // on-screen while WorkshopMenu/ExamineMenu is active.
            SetWidgetVisible(false, a_animateWidget, a_animationMs);
            ShowUnjammingUI(false);
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
            ConditionSystem::IsRealWeaponEquipped() &&
            ShouldShowForAmmoHud();

        LogWidgetDiag("EvaluateVisibility:decision", shouldBeVisible ? "show" : "hide");
        SetWidgetVisible(shouldBeVisible, a_animateWidget, a_animationMs);
    }

    void ConditionUI::SetWidgetVisible(bool a_visible, bool a_animate, int a_animationMs) {
        if (a_visible != g_widgetVisible) {
            LogWidgetDiag("SetWidgetVisible", a_visible ? "logic_show" : "logic_hide");
        }
        g_widgetVisible = a_visible;
        ApplyHudVisibility(a_animate, a_animationMs);
        // Heat uses the same HUD gate as CND, with its own weapon/heat
        // predicate applied by UpdateHeat below.
        const auto fault = ConditionSystem::GetWeaponFaultSnapshot();
        UpdateHeat(
            fault.weaponHeat,
            fault.IsOverheated(),
            fault.weaponHeat > 0.001f || fault.IsOverheated());
    }

    void ConditionUI::ForceColorRefresh() {
        UpdateQuickLootColor();
    }

    void ConditionUI::UpdateDurability(float a_percent, float a_points) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return;
        }

        if (UsePrismaUI()) {
            SendPrisma("setDurability", {
                { "percent", a_percent },
                { "points", a_points }
            });
        } else {
            Scaleform::GFx::Value args[2];
            args[0] = static_cast<double>(a_percent);
            args[1] = static_cast<double>(a_points);
            InvokeCndSwf("SetDurability", args, 2);
        }
    }

    void ConditionUI::UpdateHeatVisuals(float a_x, float a_y, float a_scale) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return;
        }

        if (UsePrismaHeatUI()) {
            SendPrisma("setHeatTransform", {
                { "x", a_x },
                { "y", a_y },
                { "scale", a_scale }
            });
        } else {
            Scaleform::GFx::Value args[3];
            args[0] = static_cast<double>(a_x);
            args[1] = static_cast<double>(a_y);
            args[2] = static_cast<double>(a_scale);
            InvokeHeatSwf("SetHeatTransform", args, 3);
        }
    }

    void ConditionUI::UpdateHeat(float a_heat, bool a_overheated, bool a_visible) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return;
        }

        a_heat = std::clamp(a_heat, 0.0f, 1.0f);
        const bool hudVisible = IsGameplayHUDOnly(true) && g_widgetVisible;
        const bool effectiveVisible = a_visible && hudVisible;
        const bool usePrisma = UsePrismaHeatUI();

        // The SWF backend owns a separate always-open IMenu. During a loading
        // or fader transition Fallout can remove that menu while dispatching
        // the same MenuOpenCloseEvent that reaches this function. Queue the
        // Scaleform call on the UI task queue and retain the menu until it runs.
        if (IsScaleformTransitionActive()) {
            return;
        }

        auto heatMenu = usePrisma ? Scaleform::Ptr<RE::IMenu>{} : GetWidgetMenu(HeatWidgetUI::MENU_NAME);
        const auto target = usePrisma
            ? static_cast<std::uint64_t>(g_prismaView)
            : static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(heatMenu.get()));
        const auto backend = static_cast<std::uint8_t>(usePrisma ? 0 : 1);

        std::uint64_t generation = 0;
        {
            std::lock_guard lock(g_heatDispatchMutex);
            if (g_heatDispatchState.initialized &&
                std::abs(a_heat - g_heatDispatchState.heat) < 0.002f &&
                a_overheated == g_heatDispatchState.overheated &&
                effectiveVisible == g_heatDispatchState.visible &&
                backend == g_heatDispatchState.backend &&
                target == g_heatDispatchState.target) {
                return;
            }
            generation = g_heatDispatchGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
            g_heatDispatchState = {
                true,
                a_heat,
                a_overheated,
                effectiveVisible,
                backend,
                target
            };
        }

        if (usePrisma) {
            const bool delivered = SendPrisma("setHeat", {
                { "heat", a_heat },
                { "overheated", a_overheated },
                { "visible", effectiveVisible }
            });
            if (!delivered) {
                FailHeatDispatch(generation);
            }
            return;
        }

        Scaleform::GFx::Value args[3];
        args[0] = static_cast<double>(a_heat);
        args[1] = a_overheated;
        args[2] = effectiveVisible;
        QueueWidgetSwfInvoke(
            std::move(heatMenu),
            "SetHeat",
            args,
            3,
            [generation](bool a_invoked) {
                if (!a_invoked) {
                    FailHeatDispatch(generation);
                }
            },
            &g_heatDispatchGeneration,
            generation);
    }

    void ConditionUI::UpdatePenaltyThreshold(float a_percent) {
        if (UsePrismaUI()) {
            SendPrisma("setPenaltyThreshold", { { "percent", a_percent } });
        } else {
            Scaleform::GFx::Value args[1];
            args[0] = static_cast<double>(a_percent);
            InvokeCndSwf("SetFNVThreshold", args, 1);
        }
    }

    void ConditionUI::UpdateTextVisibility(bool a_show) {
        if (UsePrismaUI()) {
            SendPrisma("setTextVisible", { { "visible", a_show } });
        } else {
            Scaleform::GFx::Value args[1];
            args[0] = a_show;
            InvokeCndSwf("SetTextVisible", args, 1);
        }
    }

    void ConditionUI::UpdateQuickLootColor() {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return;
        }
        if (IsScaleformTransitionActive()) {
            return;
        }

        const auto hudColor = GetHudColor();
        const auto cndColor = GetConfiguredWidgetColor(ConditionSystem::g_mcmSettings.cndColorMode);
        const auto unjamColor = GetConfiguredWidgetColor(ConditionSystem::g_mcmSettings.unjamColorMode);
        const auto heatColor = GetConfiguredWidgetColor(ConditionSystem::g_mcmSettings.heatColorMode);
        const auto quickLootColor = ConditionSystem::g_mcmSettings.useCustomColor.load()
            ? static_cast<std::uint32_t>(ConditionSystem::g_mcmSettings.customColor.load()) & 0x00FFFFFFu
            : hudColor;

        auto hudMenu = GetWidgetMenu("HUDMenu");
        if (hudMenu && hudMenu->uiMovie) {
            QueueMenuMovieOperation(
                std::move(hudMenu),
                [quickLootColor](Scaleform::GFx::Movie& a_movie) {
                    Scaleform::GFx::Value args[1];
                    args[0] = static_cast<double>(quickLootColor);
                    return a_movie.Invoke(
                        "root.CenterGroup_mc.QuickContainerWidget_mc.SetCNDColor",
                        nullptr,
                        args,
                        1);
                });
        }

        if (UsePrismaUI()) {
            SendPrisma("setCustomColor", { { "color", cndColor } });
        } else {
            Scaleform::GFx::Value args[1];
            args[0] = static_cast<double>(cndColor);
            InvokeCndSwf("SetCustomColor", args, 1);
        }

        if (UsePrismaUnjamUI()) {
            SendPrisma("setUnjamColor", { { "color", unjamColor } });
        } else {
            Scaleform::GFx::Value args[1];
            args[0] = static_cast<double>(unjamColor);
            InvokeUnjamSwf("SetUnjamColor", args, 1);
        }

        if (UsePrismaHeatUI()) {
            SendPrisma("setHeatColor", { { "color", heatColor } });
        } else {
            Scaleform::GFx::Value heatArgs[1];
            heatArgs[0] = static_cast<double>(heatColor);
            InvokeHeatSwf("SetHeatColor", heatArgs, 1);
        }
    }

    void ConditionUI::ShowUnjammingUI(bool a_show) {
        g_unjamVisible = a_show;
        ApplyHudVisibility();
    }

    void ConditionUI::UpdateUnjammingProgress(float a_percent) {
        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return;
        }

        if (UsePrismaUnjamUI()) {
            SendPrisma("setUnjamProgress", { { "percent", a_percent } });
        } else {
            Scaleform::GFx::Value args[1];
            args[0] = static_cast<double>(a_percent);
            InvokeUnjamSwf("SetUnjamProgress", args, 1);
        }
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
                        ConditionSystem::Repair::RefreshExamineMenuButtons(menu->uiMovie.get());
                    }
                }
            }
        }
        
        if (a_event.menuName == "PipboyHolotapeMenu" && !a_event.opening) {
            auto renderer = RE::Interface3D::Renderer::GetByName("Pipboy3D");
            if (renderer) renderer->Enable(true); 
        }

        if (IsPlayerDeathTransitionActive()) {
            g_deathHudRecoveryPending.store(true, std::memory_order_relaxed);
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto menuName = std::string_view(a_event.menuName.c_str() ? a_event.menuName.c_str() : "");
        const bool isScaleformTransitionEvent =
            menuName == "LoadingMenu" || menuName == "FaderMenu";
        MarkScaleformTransition(menuName, a_event.opening);
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

        if (isScaleformTransitionEvent) {
            // LoadingMenu/FaderMenu events are emitted while Fallout is still
            // tearing down or rebuilding Scaleform menus. Queue the normal
            // post-close refresh, but never synchronously resolve a widget
            // Movie from this event.
            if (!a_event.opening) {
                QueuePostMenuCloseVisibilityRefresh(animateWidgetRestore, widgetAnimationMs);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        ConditionUI::EvaluateVisibility(animateWidgetRestore, widgetAnimationMs);
        if (!a_event.opening) {
            QueuePostMenuCloseVisibilityRefresh(animateWidgetRestore, widgetAnimationMs);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    void SettingsManager::Load() {
        
        
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
        int iWidgetType = GET_INT("General", "iWidgetType", 1);
        if (!userIni.GetValue("General", "iWidgetType") && userIni.GetValue("General", "bUsePrismaUI")) {
            iWidgetType = userIni.GetBoolValue("General", "bUsePrismaUI") ? 0 : 1;
        }
        int iSwfCndStyle = GET_INT("General", "iSwfCndStyle", 0);
        float fX     = GET_FLOAT("General", "fWidgetX", 975.0f);
        float fY     = GET_FLOAT("General", "fWidgetY", 667.0f);
        float fScale = GET_FLOAT("General", "fWidgetScale", 1.0f);

        bool bShowWidgetText = GET_BOOL("UI", "bShowWidgetText", 1);
        bool bShowCND        = GET_BOOL("UI", "bShowItemCardCND", 1);
        int iCNDPos          = GET_INT("UI", "iItemCardCNDPosition", 2);
        int iCndColorMode    = GET_INT("UI", "iCndColorMode", 0);
        int iUnjamColorMode  = GET_INT("UI", "iUnjamColorMode", 0);
        int iHeatColorMode   = GET_INT("UI", "iHeatColorMode", 0);
        float fUnjamX        = GET_FLOAT("UI", "fUnjamX", 0.0f);
        float fUnjamY        = GET_FLOAT("UI", "fUnjamY", 0.0f);
        float fUnjamScale    = GET_FLOAT("UI", "fUnjamScale", 1.0f);
        float fHeatWidgetX   = GET_FLOAT("UI", "fHeatWidgetX", 0.0f);
        float fHeatWidgetY   = GET_FLOAT("UI", "fHeatWidgetY", 0.0f);
        float fHeatWidgetScale = GET_FLOAT("UI", "fHeatWidgetScale", 1.0f);

        // The first heat-widget release stored the top-left corner on the
        // 1280x720 logical stage. New code uses the same center-relative
        // contract as the unjam widget. Migrate an existing user profile
        // once, while leaving a fresh 0,0 profile centered by default.
        const bool hasUserHeatX = userIni.GetValue("UI", "fHeatWidgetX") != nullptr;
        const bool hasUserHeatY = userIni.GetValue("UI", "fHeatWidgetY") != nullptr;
        const bool hasHeatPositionVersion = userIni.GetValue("UI", "iHeatWidgetPositionVersion") != nullptr;
        if (hasUserHeatX && hasUserHeatY && !hasHeatPositionVersion) {
            const bool wasLegacyDefault =
                std::abs(fHeatWidgetX - 70.0f) < 0.001f &&
                std::abs(fHeatWidgetY - 35.0f) < 0.001f;
            if (!wasLegacyDefault &&
                (std::abs(fHeatWidgetX) > 0.001f || std::abs(fHeatWidgetY) > 0.001f)) {
                fHeatWidgetX += kHeatWidgetLegacyCenterOffsetX;
                fHeatWidgetY += kHeatWidgetLegacyCenterOffsetY;
            } else {
                fHeatWidgetX = 0.0f;
                fHeatWidgetY = 0.0f;
            }

            userIni.SetDoubleValue("UI", "fHeatWidgetX", fHeatWidgetX);
            userIni.SetDoubleValue("UI", "fHeatWidgetY", fHeatWidgetY);
            userIni.SetLongValue("UI", "iHeatWidgetPositionVersion", 1);
            std::error_code userEc;
            std::filesystem::create_directories(userPath.parent_path(), userEc);
            userIni.SaveFile(userPath.string().c_str(), false);
        }
        int iUnjamWidgetType = GET_INT("UI", "iUnjamWidgetType", iWidgetType);
        int iHeatWidgetType = GET_INT("UI", "iHeatWidgetType", iWidgetType);
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
        bool bEnableEnergyFault = GET_BOOL("Mechanics", "bEnableEnergyFault", 1);
        float fEnergyFaultChanceMultiplier = GET_FLOAT("Mechanics", "fEnergyFaultChanceMultiplier", 0.50f);
        float fEnergyReloadFaultChanceMultiplier = GET_FLOAT("Mechanics", "fEnergyReloadFaultChanceMultiplier", 0.25f);
        float fEnergyFaultDuration = GET_FLOAT("Mechanics", "fEnergyFaultDuration", 0.80f);
        bool bEnableWeaponOverheat = GET_BOOL("Mechanics", "bEnableWeaponOverheat", 1);
        float fOverheatHeatPerShot = GET_FLOAT("Mechanics", "fOverheatHeatPerShot", 0.02f);
        float fOverheatCooldownRate = GET_FLOAT("Mechanics", "fOverheatCooldownRate", 0.30f);
        float fOverheatRecoveryThreshold = GET_FLOAT("Mechanics", "fOverheatRecoveryThreshold", 0.15f);
        float fOverheatWearMultiplier = GET_FLOAT("Mechanics", "fOverheatWearMultiplier", 2.0f);
        float fWeaponWearMultiplier = GET_FLOAT("Mechanics", "fWeaponWearMultiplier", 1.0f);
        float fArmorWearMultiplier = GET_FLOAT("Mechanics", "fArmorWearMultiplier", 1.0f);
        float fRepairMaterialCostMultiplier = GET_FLOAT("Mechanics", "fRepairMaterialCostMultiplier", 1.0f);
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
        float fJuryRepairCap = GET_FLOAT("Mechanics", "fJuryRepairCap", 1.0f);
        float fWorkbenchRepairCap = GET_FLOAT("Mechanics", "fWorkbenchRepairCap", 2.0f);

        bool bEnableLog    = GET_BOOL("Debug", "bEnableLogging", 0);

        #undef GET_BOOL
        #undef GET_FLOAT
        #undef GET_INT

        const bool armorConditionSettingChanged =
            ConditionSystem::g_mcmSettings.enableArmorCondition.load() != bEnableArmorCondition;
        const bool armorResistanceSettingChanged =
            ConditionSystem::g_mcmSettings.armorConditionAffectsResistance.load() != bArmorConditionAffectsResistance;

        if (bSaveDefaultNeeded) {
            std::error_code ec;
            std::filesystem::create_directories(defaultPath.parent_path(), ec);
            defaultIni.SaveFile(defaultPath.string().c_str(), false);
        }

        ConditionSystem::g_mcmSettings.enable.store(bEnable);
        iWidgetType = std::clamp(iWidgetType, 0, 1);
        ConditionSystem::g_mcmSettings.widgetBackend.store(iWidgetType);
        ConditionSystem::g_mcmSettings.usePrismaUI.store(
            iWidgetType == 0 || iUnjamWidgetType == 0 || iHeatWidgetType == 0);
        ConditionSystem::g_mcmSettings.unjamWidgetBackend.store(std::clamp(iUnjamWidgetType, 0, 1));
        ConditionSystem::g_mcmSettings.heatWidgetBackend.store(std::clamp(iHeatWidgetType, 0, 1));
        ConditionSystem::g_mcmSettings.swfCndStyle.store(std::clamp(iSwfCndStyle, 0, 1));
        ConditionSystem::g_mcmSettings.x.store(fX);
        ConditionSystem::g_mcmSettings.y.store(fY);
        ConditionSystem::g_mcmSettings.scale.store(fScale);
        
        ConditionSystem::g_mcmSettings.showWidgetText.store(bShowWidgetText);
        ConditionSystem::g_mcmSettings.showItemCardCND.store(bShowCND);
        ConditionSystem::g_mcmSettings.itemCardCNDPosition.store(iCNDPos);
        ConditionSystem::g_mcmSettings.cndColorMode.store(std::clamp(iCndColorMode, 0, 1));
        ConditionSystem::g_mcmSettings.unjamColorMode.store(std::clamp(iUnjamColorMode, 0, 1));
        ConditionSystem::g_mcmSettings.heatColorMode.store(std::clamp(iHeatColorMode, 0, 1));
        ConditionSystem::g_mcmSettings.useCustomColor.store(bUseCustomColor);
        ConditionSystem::g_mcmSettings.customColor.store(parsedCustomColor);
        ConditionSystem::g_mcmSettings.unjamX.store(fUnjamX);
        ConditionSystem::g_mcmSettings.unjamY.store(fUnjamY);
        ConditionSystem::g_mcmSettings.unjamScale.store(fUnjamScale);
        ConditionSystem::g_mcmSettings.heatWidgetX.store(fHeatWidgetX);
        ConditionSystem::g_mcmSettings.heatWidgetY.store(fHeatWidgetY);
        ConditionSystem::g_mcmSettings.heatWidgetScale.store(std::clamp(fHeatWidgetScale, 0.25f, 3.0f));
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
        ConditionSystem::g_mcmSettings.enableEnergyFault.store(bEnableEnergyFault);
        ConditionSystem::g_mcmSettings.energyFaultChanceMultiplier.store(
            std::clamp(std::isfinite(fEnergyFaultChanceMultiplier) ? fEnergyFaultChanceMultiplier : 0.50f, 0.0f, 5.0f));
        ConditionSystem::g_mcmSettings.energyReloadFaultChanceMultiplier.store(
            std::clamp(std::isfinite(fEnergyReloadFaultChanceMultiplier) ? fEnergyReloadFaultChanceMultiplier : 0.25f, 0.0f, 5.0f));
        ConditionSystem::g_mcmSettings.energyFaultDuration.store(
            std::clamp(std::isfinite(fEnergyFaultDuration) ? fEnergyFaultDuration : 0.80f, 0.1f, 5.0f));
        ConditionSystem::g_mcmSettings.enableWeaponOverheat.store(bEnableWeaponOverheat);
        ConditionSystem::g_mcmSettings.overheatHeatPerShot.store(
            std::clamp(std::isfinite(fOverheatHeatPerShot) ? fOverheatHeatPerShot : 0.02f, 0.0f, 1.0f));
        ConditionSystem::g_mcmSettings.overheatCooldownRate.store(
            std::clamp(std::isfinite(fOverheatCooldownRate) ? fOverheatCooldownRate : 0.30f, 0.0f, 5.0f));
        ConditionSystem::g_mcmSettings.overheatRecoveryThreshold.store(
            std::clamp(std::isfinite(fOverheatRecoveryThreshold) ? fOverheatRecoveryThreshold : 0.15f, 0.0f, 1.0f));
        ConditionSystem::g_mcmSettings.overheatWearMultiplier.store(
            std::clamp(std::isfinite(fOverheatWearMultiplier) ? fOverheatWearMultiplier : 2.0f, 1.0f, 10.0f));
        ConditionSystem::g_mcmSettings.weaponWearMultiplier.store(
            std::clamp(std::isfinite(fWeaponWearMultiplier) ? fWeaponWearMultiplier : 1.0f, 0.0f, 5.0f));
        ConditionSystem::g_mcmSettings.armorWearMultiplier.store(
            std::clamp(std::isfinite(fArmorWearMultiplier) ? fArmorWearMultiplier : 1.0f, 0.0f, 5.0f));
        ConditionSystem::g_mcmSettings.repairMaterialCostMultiplier.store(
            std::clamp(std::isfinite(fRepairMaterialCostMultiplier) ? fRepairMaterialCostMultiplier : 1.0f, 0.0f, 5.0f));
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

        if (armorConditionSettingChanged || armorResistanceSettingChanged) {
            ConditionSystem::QueuePlayerArmorRatingRefresh(true);
        }
        ConditionSystem::g_mcmSettings.repairMaterialMode.store(std::clamp(iRepairMaterialMode, 0, 1));
        ConditionSystem::g_mcmSettings.dynamicRepairMaterialCost.store(bDynamicRepairMaterialCost);
        ConditionSystem::g_mcmSettings.enablePipboyJuryRepair.store(bEnablePipboyJuryRepair);
        ConditionSystem::g_mcmSettings.enableWorkbenchMaterialRepair.store(bEnableWorkbenchMaterialRepair);
        ConditionSystem::g_mcmSettings.enableRepairKits.store(bEnableRepairKits);
        ConditionSystem::g_mcmSettings.confirmConsumeFavoriteMaterial.store(bConfirmConsumeFavoriteMaterial);
        ConditionSystem::g_mcmSettings.confirmConsumeLegendaryMaterial.store(bConfirmConsumeLegendaryMaterial);
        if (!std::isfinite(fJuryRepairCap)) {
            REX::WARN("[DEBUG-LIMIT-b673] Invalid MCM fJuryRepairCap; using 1.0.");
            fJuryRepairCap = 1.0f;
        }
        if (!std::isfinite(fWorkbenchRepairCap)) {
            REX::WARN("[DEBUG-LIMIT-b673] Invalid MCM fWorkbenchRepairCap; using 2.0.");
            fWorkbenchRepairCap = 2.0f;
        }
        ConditionSystem::g_mcmSettings.juryRepairCap.store(std::clamp(fJuryRepairCap, 0.0f, 8.9f));
        ConditionSystem::g_mcmSettings.workbenchRepairCap.store(std::clamp(fWorkbenchRepairCap, 0.0f, 8.9f));
        ConditionSystem::g_mcmSettings.enableLogging.store(bEnableLog);

        ConditionUI::RefreshBackend();

        ConditionUI::UpdateVisuals(fX, fY, fScale);
        ConditionUI::UpdateUnjamVisuals(fUnjamX, fUnjamY, fUnjamScale); 
        ConditionUI::UpdateHeatVisuals(fHeatWidgetX, fHeatWidgetY, std::clamp(fHeatWidgetScale, 0.25f, 3.0f));
        ConditionUI::UpdatePenaltyThreshold(fPenaltyThreshold);
        ConditionUI::UpdateTextVisibility(bShowWidgetText); 
        ConditionUI::UpdateQuickLootColor();
        if (bEnableWeaponCondition || bEnableArmorCondition) {
            ConditionSystem::GrandfatherPlayerInventory();
        }
        ConditionUI::EvaluateVisibility();
    }
}

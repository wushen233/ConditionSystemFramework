#include "pch.h"
#include "ConditionCore.h"
#include "ConditionUI.h"
#include "ConditionHooks.h"
#include "Repair/RepairSystem.h"   
#include "ConditionWorkbench.h"
#include "IIF_API.h" 
#include "ProfileManager.h" 
#include "Translation.h" 
#include "DurabilityAPI.h" 
#include <fstream>
#include <nlohmann/json.hpp>

#define PLUGIN_NAME "ConditionSystemFramework"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 0
#define PLUGIN_VERSION_PATCH 8

void OnGameExit() { ConditionSystem::g_isGameRunning.store(false); }

static bool g_hooksInstalled = false;

namespace {
    struct ItemUICardLayout
    {
        int priority{ 0 };
        std::string anchorTarget{ "$ammo" };
        std::string anchorMode{ "after" };
        std::string fallbackAnchorTarget{ "TOP" };
        std::string fallbackAnchorMode{ "after" };
    };

    ItemUICardLayout LoadItemUICardLayout()
    {
        ItemUICardLayout layout;
        constexpr const char* path = "Data\\F4SE\\Plugins\\ConditionSystemFramework\\ItemUICards.json";
        try {
            std::ifstream file(path);
            if (!file.is_open()) return layout;

            auto root = nlohmann::json::parse(file, nullptr, true, true);
            if (!root.is_object()) return layout;

            const nlohmann::json* card = nullptr;
            if (root.contains("ItemUICards") && root["ItemUICards"].is_object()) {
                auto& cards = root["ItemUICards"];
                if (cards.contains("CND") && cards["CND"].is_object()) card = &cards["CND"];
            }
            if (!card && root.contains("CND") && root["CND"].is_object()) {
                card = &root["CND"];
            }
            if (!card) return layout;

            layout.priority = card->value("priority", layout.priority);
            layout.anchorTarget = card->value("anchorTarget", layout.anchorTarget);
            layout.anchorMode = card->value("anchorMode", layout.anchorMode);
            layout.fallbackAnchorTarget = card->value("fallbackAnchorTarget", layout.fallbackAnchorTarget);
            layout.fallbackAnchorMode = card->value("fallbackAnchorMode", layout.fallbackAnchorMode);
        }
        catch (const std::exception& e) {
            REX::WARN("[CSF-IIF] Failed to load ItemUICards.json: {}", e.what());
        }
        catch (...) {
            REX::WARN("[CSF-IIF] Failed to load ItemUICards.json.");
        }
        return layout;
    }
}

static const char* F4SEMessageName(std::uint32_t a_type)
{
    switch (a_type) {
    case F4SE::MessagingInterface::kPostLoad:
        return "PostLoad";
    case F4SE::MessagingInterface::kPostPostLoad:
        return "PostPostLoad";
    case F4SE::MessagingInterface::kInputLoaded:
        return "InputLoaded";
    case F4SE::MessagingInterface::kNewGame:
        return "NewGame";
    case F4SE::MessagingInterface::kPreLoadGame:
        return "PreLoadGame";
    case F4SE::MessagingInterface::kPostLoadGame:
        return "PostLoadGame";
    case F4SE::MessagingInterface::kPreSaveGame:
        return "PreSaveGame";
    case F4SE::MessagingInterface::kPostSaveGame:
        return "PostSaveGame";
    case F4SE::MessagingInterface::kDeleteGame:
        return "DeleteGame";
    case F4SE::MessagingInterface::kGameLoaded:
        return "GameLoaded";
    case F4SE::MessagingInterface::kGameDataReady:
        return "GameDataReady";
    default:
        return "Unknown";
    }
}

void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg) {
    if (!a_msg) return;

    REX::INFO("[CSF-BootDiag] Message begin: {} ({})", F4SEMessageName(a_msg->type), a_msg->type);

    if (a_msg->type == F4SE::MessagingInterface::kPostLoad) {
        REX::INFO("[CSF-BootDiag] IIF provider registration begin");
        static ItemUICardLayout layout = LoadItemUICardLayout();
        IIF_API::CPPCardRegistration reg;
        reg.id = "CND";
        reg.defaultPriority = layout.priority;
        reg.defaultAnchorTarget = layout.anchorTarget.c_str();
        reg.defaultAnchorMode = layout.anchorMode.c_str();
        reg.fallbackAnchorTarget = layout.fallbackAnchorTarget.c_str();
        reg.fallbackAnchorMode = layout.fallbackAnchorMode.c_str();

        IIF_API::ProviderConfig cfg;
        cfg.cards = { reg };
        cfg.damageModifier = ConditionSystem::Hooks::OnCombatDamageCalculate;
        cfg.armorModifier = ConditionSystem::Hooks::OnCombatArmorCalculate;

        IIF_API::RegisterCardProvider(F4SE::GetMessagingInterface(), ConditionSystem::Hooks::OnIIFMessage_CND, cfg);
        REX::INFO("[CSF-BootDiag] IIF provider registration end");
        REX::INFO("[IIF-Link] CND and combat interceptors registered safely at kPostLoad.");
    }
    else if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
        REX::INFO("[CSF-BootDiag] GameLoaded setup begin");
        ConditionSystem::LoadAllProfiles();
        REX::INFO("[CSF-BootDiag] InstallSingletons begin");
        ConditionSystem::Hooks::InstallSingletons();
        REX::INFO("[CSF-BootDiag] GameLoaded setup end");
    }
    else if (a_msg->type == ConditionSystem::DurabilityAPI::kMessage_RequestInterface) {
        static ConditionSystem::DurabilityAPI::Interface s_apiInterface = {
            1,
            [](RE::TESBoundObject* obj, RE::BGSInventoryItem::Stack* stack) -> float {
                return ConditionSystem::DurabilityAPI::GetStackHealthPercent(obj, stack);
            }
        };
        if (auto msg = F4SE::GetMessagingInterface()) {
            msg->Dispatch(
                ConditionSystem::DurabilityAPI::kMessage_ProvideInterface,
                &s_apiInterface,
                sizeof(s_apiInterface),
                "ConditionSystemFramework");
            REX::INFO("[DurabilityAPI] Provided Durability interface to requester (v{})", s_apiInterface.version);
        }
    }
    else if (a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
        REX::INFO("[CSF-BootDiag] Prisma init begin");
        ConditionSystem::ConditionUI::InitializePrisma();
        REX::INFO("[CSF-BootDiag] Prisma init end");
    }
    else if (a_msg->type == F4SE::MessagingInterface::kPostLoadGame || a_msg->type == F4SE::MessagingInterface::kNewGame) {
        REX::INFO("[CSF-BootDiag] Save/NewGame setup begin hooksInstalled={}", g_hooksInstalled ? 1 : 0);
        if (!g_hooksInstalled) {
            REX::INFO("[CSF-BootDiag] RegisterEvents begin");
            ConditionSystem::RegisterEvents();
            REX::INFO("[CSF-BootDiag] Workbench cache begin");
            ConditionSystem::Workbench::InitializeRecipeCache();
            REX::INFO("[CSF-BootDiag] RegisterMenu begin");
            ConditionSystem::ConditionUI::RegisterMenu();
            REX::INFO("[CSF-BootDiag] CreatePrismaView initial begin");
            ConditionSystem::ConditionUI::CreatePrismaView();
            REX::INFO("[CSF-BootDiag] MenuOpenClose sink begin");
            ConditionSystem::SettingsManager::GetSingleton()->InstallHook();
            REX::INFO("[CSF-BootDiag] Settings load begin");
            ConditionSystem::SettingsManager::GetSingleton()->Load();
            REX::INFO("[CSF-BootDiag] Repair equip sink begin");
            ConditionSystem::Repair::RegisterEquipEventSink();
            g_hooksInstalled = true;
        }
        REX::INFO("[CSF-BootDiag] CreatePrismaView refresh begin");
        ConditionSystem::ConditionUI::CreatePrismaView();
        REX::INFO("[CSF-BootDiag] OpenMenu begin");
        ConditionSystem::ConditionUI::OpenMenu();

        if (auto task = F4SE::GetTaskInterface()) {
            task->AddTask([]() {
                REX::INFO("[CSF-BootDiag] SyncAfterGameLoad task begin");
                ConditionSystem::SyncAfterGameLoad();
                REX::INFO("[CSF-BootDiag] SyncAfterGameLoad task end");
            });
        }
        REX::INFO("[CSF-BootDiag] Save/NewGame setup end");
    }

    REX::INFO("[CSF-BootDiag] Message end: {} ({})", F4SEMessageName(a_msg->type), a_msg->type);
}

namespace OGSupport {
    static F4SE::Impl::F4SEInterface RestoreLoadInterface;
    [[nodiscard]] inline static const char* F4SEAPI F4SEGetSaveFolderName() noexcept { return "Fallout4"; }
    void Init(const F4SE::LoadInterface* a_f4se) {
        if (a_f4se->RuntimeVersion() <= F4SE::RUNTIME_1_10_163) {
            memcpy(&RestoreLoadInterface, a_f4se, 48);
            (((F4SE::Impl::F4SEInterface*)(&RestoreLoadInterface))->GetSaveFolderName) = F4SEGetSaveFolderName;
            F4SE::Init((const F4SE::LoadInterface*)(&RestoreLoadInterface));
        }
        else F4SE::Init(a_f4se);
    }
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info) {
    if (!a_f4se || !a_info) return false;
    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name = PLUGIN_NAME;
    a_info->version = PLUGIN_VERSION_MAJOR;
    if (a_f4se->IsEditor()) return false;
    return true;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se) {
    static std::once_flag once;	std::call_once(once, [&]() {
        OGSupport::Init(a_f4se);
        REL::GetTrampoline().create(1024);
        ConditionSystem::Hooks::InstallAll();

        std::atexit(OnGameExit);

        auto papyrus = F4SE::GetPapyrusInterface();
        if (papyrus) papyrus->Register(ConditionSystem::Repair::RegisterPapyrusFunctions);

        auto serialization = F4SE::GetSerializationInterface();
        if (serialization) {
            serialization->SetUniqueID('CNDS');
            serialization->SetSaveCallback(ConditionSystem::OnSave);
            serialization->SetLoadCallback(ConditionSystem::OnLoad);
        }

        if (auto msg = F4SE::GetMessagingInterface()) {
            msg->RegisterListener(OnF4SEMessage);
        }
        });
    return true;
}

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <shared_mutex>
#include <F4SE/Interfaces.h>

namespace RE {
    class Actor;
    class TESBoundObject;
    namespace BSScript { class IVirtualMachine; }
}
namespace Scaleform::GFx { class Movie; }

namespace ConditionSystem { struct RepairKitProfile; }

namespace ConditionSystem::Repair
{
    // =========================================================================
    // 模块 1：修理包数据结构 (从 ProfileManager.h 引用)
    // =========================================================================
    
    struct RepairAmountData {
        float value;
        bool isFlatPoints;
    };


    // =========================================================================
    // 模块 2：修理包配置加载与解析
    // =========================================================================
    
    void LoadRepairKitProfiles();
    ConditionSystem::RepairKitProfile GetRepairKitProfile(RE::TESBoundObject* a_kit);
    RepairAmountData CalculateRepairAmount(const ConditionSystem::RepairKitProfile& a_profile);


    // =========================================================================
    // 模块 3：核心修理应用逻辑
    // =========================================================================
    
    bool SmartCascadeArmorRepair(RE::Actor* a_actor, RepairAmountData a_amount, float a_materialLimit);
    bool RepairEquippedWeaponLogic(RE::Actor* a_actor, RepairAmountData a_amount, float a_materialLimit);


    // =========================================================================
    // 模块 4：内存锁定与安全销毁 (防止引擎 GC 误删)
    // =========================================================================
    
    bool LockHoveredTarget(std::uint32_t a_formID, bool a_isEquipped, std::uint32_t a_exactStackIndex, bool a_stackIndexMode = false);
    void ProcessPendingDeletions();


    // =========================================================================
    // 模块 5：Pipboy 界面交互与 Jury Rigging (同类互修)
    // =========================================================================
    
    uint32_t GetPipboyColor(); 

    void OpenJuryRiggingMenu(std::uint32_t a_itemIndex); 
    std::string GetJuryRiggingDataString(std::uint16_t a_targetUID);
    std::string GetJuryRiggingDataStringImpl(std::uint16_t a_targetUID, std::uint16_t a_excludeUID);
    bool ExecuteJuryRigging(std::uint16_t a_targetUID, std::uint16_t a_materialUID);

    void InjectConditionSystemCallback(Scaleform::GFx::Movie* movie);
    

    // =========================================================================
    // 模块 6：事件拦截与 Papyrus 脚本桥接
    // =========================================================================
    
    void RegisterEquipEventSink();
    
    void Papyrus_UseRepairKit(std::monostate, RE::Actor* akActor, RE::TESBoundObject* akKit);
    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm);
}

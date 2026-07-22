#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace RE { 
    class TESBoundObject; 
}

namespace ConditionSystem::Workbench
{
    // 用于接收从 ItemCard 传来的高亮物品实体
    extern std::atomic<RE::TESBoundObject*> g_hoveredObject;
    extern std::atomic<void*> g_hoveredStack; 

    struct RepairMaterial {
        RE::TESBoundObject* componentForm; 
        std::uint32_t requiredCount;       
        std::string componentName;         
    };

    void InitializeRecipeCache();
    void ResetRecipeCache();  // 清空配方缓存，供读档时重新构建
    void ClearRuntimeSelection();
    
    // 💡 核心修复：声明我们新写的两个服务端通信接口
    void SendRepairCostToUI(); 
    void ExecuteWorkbenchRepairFromUI(); 
    void ExecuteWorkbenchRepairKitFromUI(std::uint16_t a_kitID);
	// 新增：向全工程声明我们刚刚写好的标记翻转函数
    void ToggleRepairComponentsTag();
    void ShowRepairConfirmBox();
    void ShowRepairKitConfirmBox(std::uint16_t a_kitID);
    void ShowRepairKitListBox(std::uint16_t a_page = 0, std::uint32_t a_formID = 0, bool a_isEquipped = false, std::uint32_t a_exactStackIndex = 0);
    void SendRepairKitButtonStateToUI(std::uint32_t a_formID = 0, bool a_isEquipped = false, std::uint32_t a_exactStackIndex = 0);

    std::vector<RepairMaterial> CalculateRepairCost(RE::TESBoundObject* a_weapon, RE::BGSInventoryItem::Stack* a_stack, float a_damagePercent);
}

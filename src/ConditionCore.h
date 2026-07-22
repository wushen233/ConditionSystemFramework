#pragma once
#include "ProfileManager.h" // 引入独立的配置管理器

#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <atomic>
#include <cstdint>
#include <string>
#include <cstring> 
#include <F4SE/Interfaces.h>
#include <new>

// 修正警告：将 struct TESMagicEffectApplyEvent 改为 class TESMagicEffectApplyEvent
namespace RE { class Actor; class TESObjectREFR; class TESHitEvent; class TESMagicEffectApplyEvent; class BGSInventoryItem; class TESBoundObject; class TESObjectWEAP; class TESObjectARMO; class TESForm; class ExtraDataList; }

namespace ConditionSystem
{
    // =========================================================================
    // 模块 1：全局耐久度阈值常量 (绝对安全引擎交互设定)
    // =========================================================================

    // 10x 耐久编码方案：0.1 healthExtra = 100% 耐久
    // 0.0=0%, 0.1=100%, 0.5=500%, 0.89=890%(上限)
    // 引擎默认健康值为 ~1.0（未初始化物品），通过阈值区分
    constexpr float ENGINE_DEFAULT_HEALTH_THRESHOLD = 0.9f;

    // 护甲最低安全耐久值（压缩后 ExtraHealth）
    // 当护甲耐久归零时，钳制在此值，使其刚好不触发引擎的禁止装备机制
    constexpr float ARMOR_MIN_ENGINE_HEALTH = 0.0001f;

    // =========================================================================
    // 模块 2：MCM 动态配置设置
    // =========================================================================

    // 将所有 MCM 设置打包进这个结构体
    struct MCMSettings {
        std::atomic<bool> enableLogging{ false };

        std::atomic<bool> enable{ true };
        std::atomic<float> x{ 1460.0f };
        std::atomic<float> y{ 1000.0f };
        std::atomic<float> scale{ 1.0f };

        std::atomic<bool> showWidgetText{ true };
        std::atomic<bool> showItemCardCND{ true };
        std::atomic<int>  itemCardCNDPosition{ 2 };
        std::atomic<bool> useCustomColor{ false };
        std::atomic<int>  customColor{ 16777215 };
        std::atomic<float> unjamX{ 0.0f };
        std::atomic<float> unjamY{ 0.0f };
        std::atomic<float> unjamScale{ 1.0f };
        std::atomic<float> juryStatsOffsetX{ 0.0f };
        std::atomic<float> juryStatsOffsetY{ 0.0f };
        std::atomic<float> juryStatsRowSpacing{ 92.0f };
        std::atomic<int> juryStatsColorMode{ 0 }; // 0=标题/顶部维修项颜色, 1=材料列表文字颜色
        std::atomic<bool> quickUnjam{ false };

        std::atomic<bool> randomLootDurability{ true };
        std::atomic<bool> enableWeaponCondition{ true };
        std::atomic<bool> enableArmorCondition{ true };
        std::atomic<bool> enableOverCondition{ true };
        std::atomic<bool> dialogueFullDurability{ true };
        std::atomic<float> weaponLootDurabilityMin{ 0.25f };
        std::atomic<float> weaponLootDurabilityMax{ 0.80f };
        std::atomic<float> armorLootDurabilityMin{ 0.35f };
        std::atomic<float> armorLootDurabilityMax{ 0.90f };
        std::atomic<float> jamThreshold{ 0.50f };
        std::atomic<float> maxJamChance{ 0.15f };

        std::atomic<int> iJammingPhase{ 0 }; // 0: 射击卡壳, 1: 换弹卡壳

        // 新增：衰减模式系统
        std::atomic<int> degradationMode{ 0 };        // 0=NV阶梯式, 1=FO76线性式
        std::atomic<float> penaltyThreshold{ 0.75f }; // NV模式惩罚阈值
        std::atomic<float> penaltyMultMid{ 0.75f };   // NV中等耐久惩罚倍率
        std::atomic<float> penaltyMultLow{ 0.50f };   // NV低耐久惩罚倍率
        std::atomic<float> linearMinMult{ 0.50f };    // FO76线性模式最低倍率

        std::atomic<bool> weaponConditionAffectsDamage{ true };
        std::atomic<bool> weaponConditionAffectsValue{ true };
        std::atomic<bool> armorConditionAffectsResistance{ true };
        std::atomic<bool> armorConditionAffectsValue{ true };
        std::atomic<int> repairMaterialMode{ 0 }; // 0=拆解材料, 1=改装制作材料
        std::atomic<bool> dynamicRepairMaterialCost{ true };
        std::atomic<bool> enablePipboyJuryRepair{ true };
        std::atomic<bool> enableWorkbenchMaterialRepair{ true };
        std::atomic<bool> enableRepairKits{ true };
        std::atomic<bool> confirmConsumeFavoriteMaterial{ true };
        std::atomic<bool> confirmConsumeLegendaryMaterial{ true };
    };

    bool RollForJam(float durability);

    // 声明唯一的全局配置实例
    extern MCMSettings g_mcmSettings;

    // 更新日志宏，指向新的结构体
#define CS_LOG(...) do { if (::ConditionSystem::g_mcmSettings.enableLogging.load()) { REX::INFO(__VA_ARGS__); } } while(0)


// =========================================================================
// 模块 3：底层内存分配工具
// =========================================================================

    template <typename T, typename... Args>
    T* SafeAllocate(Args&&... a_args) {
        auto& memMgr = RE::MemoryManager::GetSingleton();
        void* mem = memMgr.Allocate(sizeof(T), 0, false);
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(a_args)...);
    }

    template <typename T, typename... Args>
    T* SafeCreateExtraData(RE::EXTRA_DATA_TYPE a_type, Args&&... a_args) {
        auto& memMgr = RE::MemoryManager::GetSingleton();
        void* mem = memMgr.Allocate(sizeof(T), 0, false);
        if (!mem) return nullptr;
        std::memset(mem, 0, sizeof(T));
        T* obj = new (mem) T(std::forward<Args>(a_args)...);
        if (obj) {
            obj->type = a_type;
            REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
            *(std::uintptr_t*)obj = vtbl.address();
        }
        return obj;
    }


    // =========================================================================
    // 模块 4：全局运行时状态与缓存变量
    // =========================================================================

    extern std::atomic<bool> g_isGameRunning;
    extern std::atomic<bool> g_isWeaponJammed;
    extern std::atomic<std::uintptr_t> g_jammedWeaponUniqueID;
    extern std::atomic<bool> g_isWeaponDrawn;
    extern std::atomic<bool> g_isUnjamming;
    extern std::atomic<bool> g_isRepairMenuOpen;

    extern std::atomic<RE::TESBoundObject*> g_hoveredObject;
    extern std::atomic<std::uint32_t> g_hoveredStackIndex;
    extern std::atomic<std::uint32_t> g_hoveredSelectedIndex;

    extern std::shared_mutex g_uiNameMutex;
    extern std::unordered_map<RE::TESBoundObject*, std::string> g_uiNameCache;

    struct DurabilityRecord { float currentPoints; float maxPoints; };
    extern std::shared_mutex g_cacheMutex;

    extern std::unordered_map<std::uint32_t, std::uint16_t> g_baseDamageCache;
    extern std::atomic<std::uint16_t> g_customUIDCounter;
    extern std::atomic<std::uint16_t> g_pipboyHighlightedUID;

    extern std::atomic<bool> g_initialUISyncDone;

    extern std::atomic<RE::BGSInventoryItem*> g_highlightedItem;
    extern std::atomic<RE::BGSInventoryItem::Stack*> g_highlightedStack;

    extern std::atomic<int> g_pipboyTransitionTimer;
    extern std::atomic<std::uint32_t> g_hoveredHandleID;

    template <typename T>
    auto GetRawPtr(const T& ptr) {
        if constexpr (std::is_pointer_v<T>) return ptr;
        else return ptr.get();
    }

    // 10x 倍率映射：engineHealth * 10 = 真实百分比 (0-890%)
    float GetDecompressedPct(RE::TESBoundObject* obj, float engineHealth);
    float GetCompressedPct(RE::TESBoundObject* obj, float realPct);


    // =========================================================================
    // 模块 5：核心业务函数声明
    // =========================================================================

    // 核心初始化与事件注册
    void RegisterEvents();
    void RegisterSerialization(const F4SE::SerializationInterface* a_intfc);

    // 补全 F4SE 读写存盘的关键服务端通信接口，供 main.cpp 安全调用
    void OnSave(const F4SE::SerializationInterface* a_intfc);
    void OnLoad(const F4SE::SerializationInterface* a_intfc);

    // 基础物品检查与信息获取
    bool ShouldShowDurability(RE::TESBoundObject* a_obj);
    bool IsRealWeaponEquipped();
    std::uint16_t GetPlayerEquippedWeaponUniqueID();
    bool IsStackInPlayerInventory(RE::BGSInventoryItem::Stack* a_targetStack);

    // 获取绝对真实耐久（解压后）百分比与点数
    float GetEquippedWeaponDurabilityPercent();
    float GetEquippedWeaponDurabilityPoints();
    float GetAverageEquippedArmorDurability();
    float GetItemDurabilityPercent(RE::BGSInventoryItem* a_item, std::uint32_t a_stackID);
    float GetStackDurabilityPercent(RE::BGSInventoryItem::Stack* a_stack);
    float GetVisualDurabilityPercent(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack);

    // 初始化、刷新与同步功能
    void InitializeEquippedWeapon(RE::Actor* a_actor);
    void GrandfatherPlayerInventory();
    void RandomizeNewPlayerItems();
    void WakeUpAndRandomizeSingleStack(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack, bool a_isLoot = false);
    void RandomizeInventoryDurability(RE::TESObjectREFR* a_refr);
    void RandomizeWorldObject(RE::TESObjectREFR* a_refr, RE::TESBoundObject* a_obj);
    void UpdateHUDDurabilityWidget(float a_percent, float a_points, bool a_forceUpdate = false);

    // 获取武器性能衰减倍率（基于当前耐久度）
    // mode 0: NV阶梯式 - 高于阈值满效, 低于阈值阶梯递减
    // mode 1: FO76线性式 - 从100%线性衰减到linearMinMult
    float GetWeaponDegradationMultiplier(float durabilityPercent);

    // 跨模块工具函数
    std::string GameUTF8ToLocal(const char* utf8Str);
    std::vector<std::string> ExtractKeywords(RE::TESForm* form);

    // 读档后同步武器拔出状态 & UI
    void SyncAfterGameLoad();

    // 扣血（磨损）核心分发逻辑
    void DeductEquippedWeaponDurability(RE::Actor* a_actor, bool a_requireMelee);
    void DeductEquippedArmorDurability(RE::Actor* a_actor, const RE::TESHitEvent& a_event);
    void DeductEquippedArmorDurabilityFromMagic(RE::Actor* a_actor, const RE::TESMagicEffectApplyEvent& a_event);

    void TriggerFullUnjam(RE::Actor* a_actor);
}

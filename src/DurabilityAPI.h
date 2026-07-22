#pragma once
#include <cstdint>

// =========================================================================
// ConditionSystemFramework - 公开耐久度 API
// =========================================================================
// 其他模组通过包含此头文件，即可调用 CSF 的耐久度翻译层，
// 无需了解底层 10x 压缩编码即可获取真实耐久百分比。
//
// 使用方法：
//   1. 添加包含路径到 ConditionSystemFramework/src
//   2. 在你的 pch.h 中包含 <RE/Fallout.h> (或等效 RE 类型头文件)
//   3. #include "DurabilityAPI.h"
//   4. 调用 ConditionSystem::DurabilityAPI::GetItemHealthPercent()
//
// ⚠重要：必须先包含 RE/Fallout.h 等完整 RE 类型定义头文件，
//    因为内联函数需要 RE::ExtraHealth、RE::ExtraCharge 等完整类型。
// =========================================================================

namespace ConditionSystem::DurabilityAPI
{
    // =========================================================================
    // 1. 常量定义
    // =========================================================================

    // 引擎默认健康值阈值
    // ExtraHealth > 0.9 表示物品未经 CSF 初始化（未分配耐久）
    constexpr float ENGINE_DEFAULT_HEALTH_THRESHOLD = 0.9f;

    // 护甲最低安全耐久值（压缩后 ExtraHealth）
    // 当护甲耐久归零时，钳制在此值，使其刚好不触发引擎的禁止装备机制
    constexpr float ARMOR_MIN_ENGINE_HEALTH = 0.0001f;

    // 护甲最低真实耐久百分比
    constexpr float ARMOR_MIN_REAL_PERCENT = 0.001f; // 0.1%

    // =========================================================================
    // 2. 核心压缩/解压工具函数 (Header-only inline)
    // =========================================================================

    // 解压：将引擎 ExtraHealth 转换为真实百分比
    // engineHealth: ExtraHealth->health (引擎原始值，如 0.1 = 100%)
    // 返回: 真实耐久百分比 (1.0 = 100%)
    inline float DecompressHealth(float engineHealth)
    {
        return engineHealth * 10.0f;
    }

    // 压缩：将真实百分比转换为引擎 ExtraHealth 值
    // realPercent: 真实耐久百分比 (1.0 = 100%)
    // 返回: 引擎 ExtraHealth 值
    inline float CompressHealth(float realPercent)
    {
        float result = realPercent * 0.1f;
        if (result >= ENGINE_DEFAULT_HEALTH_THRESHOLD) {
            result = ENGINE_DEFAULT_HEALTH_THRESHOLD - 0.0001f;
        }
        return result;
    }

    // =========================================================================
    // 3. 从游戏对象提取耐久百分比的便捷函数
    //
    // ⚠这些内联函数需要完整的 RE 类型定义！
    //    请确保在包含此文件前已包含 <RE/Fallout.h> 或等效头文件。
    // =========================================================================

    // 从任意 ExtraDataList 中提取真实耐久百分比
    // 返回: 0.0 ~ ∞ 的真实耐久百分比，-1.0 表示无耐久系统支持
    inline float GetItemHealthPercent(RE::TESBoundObject* /*obj*/, RE::ExtraDataList* extra)
    {
        if (!extra) return -1.0f;

        auto healthExtra = extra->GetByType<RE::ExtraHealth>();
        if (healthExtra) {
            // 跳过未初始化的物品 (ExtraHealth > 0.9 是引擎默认值)
            if (healthExtra->health >= ENGINE_DEFAULT_HEALTH_THRESHOLD) {
                return -1.0f;
            }
            return DecompressHealth(healthExtra->health);
        }

        // 检查电量 (融合核心等)
        auto chargeExtra = extra->GetByType<RE::ExtraCharge>();
        if (chargeExtra) {
            float c = chargeExtra->charge;
            return (c > 1.0f) ? (c / 100.0f) : c;
        }

        return -1.0f;
    }

    // 从 BGSInventoryItem::Stack 中提取真实耐久百分比
    inline float GetStackHealthPercent(RE::TESBoundObject* obj, RE::BGSInventoryItem::Stack* stack)
    {
        if (!stack) return -1.0f;
        return GetItemHealthPercent(obj, stack->extra.get());
    }

    // =========================================================================
    // 4. F4SE 消息接口 (用于运行时跨 DLL 函数指针交换)
    // =========================================================================

    // 函数指针类型
    using GetDurabilityPercentFn = float (*)(RE::TESBoundObject* obj, RE::BGSInventoryItem::Stack* stack);

    // 运行时接口结构体
    struct Interface
    {
        std::uint32_t version;
        GetDurabilityPercentFn GetDurabilityPercent;
    };

    // F4SE 消息类型
    constexpr std::uint32_t kMessage_RequestInterface = 'CDRI'; // "CDRI" - Condition Durability Request Interface
    constexpr std::uint32_t kMessage_ProvideInterface = 'CDRO'; // "CDRO" - Condition Durability Provide Interface
}

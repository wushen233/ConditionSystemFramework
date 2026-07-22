#include "pch.h"
#include "ConditionCore.h"
#include "ConditionUI.h"
#include "ConditionWorkbench.h"
#include "ProfileManager.h"

#include <shared_mutex>

namespace ConditionSystem
{
    // =========================================================================
    // 回档回调
    // =========================================================================

    void OnRevert(const F4SE::SerializationInterface*)
    {
        std::unique_lock<std::shared_mutex> lock(g_cacheMutex);
        g_baseDamageCache.clear();

        g_customUIDCounter.store(0x8000);
        g_initialUISyncDone.store(false);

        // 清空配方缓存，读档后自动重新构建
        Workbench::ResetRecipeCache();

        REX::INFO("Serialization: 回档清空缓存。");
    }

    // =========================================================================
    // 存档回调
    // =========================================================================

    void OnSave(const F4SE::SerializationInterface* a_intfc)
    {
        std::shared_lock<std::shared_mutex> lock(g_cacheMutex);

        if (a_intfc->OpenRecord('BDMG', 1)) {
            std::size_t size = g_baseDamageCache.size();
            a_intfc->WriteRecordData(&size, sizeof(size));
            for (const auto& [uid, dmg] : g_baseDamageCache) {
                a_intfc->WriteRecordData(&uid, sizeof(uid));
                a_intfc->WriteRecordData(&dmg, sizeof(dmg));
            }
        }

        // 序列化 g_damageFormula
        if (a_intfc->OpenRecord('DMGF', 1)) {
            auto& formula = g_damageFormula;
            a_intfc->WriteRecordData(&formula.enabled, sizeof(formula.enabled));
            a_intfc->WriteRecordData(&formula.baseHardness, sizeof(formula.baseHardness));
            a_intfc->WriteRecordData(&formula.glanceThreshold, sizeof(formula.glanceThreshold));
            a_intfc->WriteRecordData(&formula.glanceMultiplier, sizeof(formula.glanceMultiplier));
            a_intfc->WriteRecordData(&formula.scratchThreshold, sizeof(formula.scratchThreshold));
            a_intfc->WriteRecordData(&formula.scratchMultiplier, sizeof(formula.scratchMultiplier));
            a_intfc->WriteRecordData(&formula.durabilityDamageConstant, sizeof(formula.durabilityDamageConstant));
        }
    }

    // =========================================================================
    // 读档回调
    // =========================================================================

    void OnLoad(const F4SE::SerializationInterface* a_intfc)
    {
        std::unique_lock<std::shared_mutex> lock(g_cacheMutex);
        std::uint32_t type, version, length;

        // 先重置到默认值，再逐个读取记录
        g_damageFormula = DamageFormulaConfig();

        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type == 'BDMG') {
                std::size_t size;
                a_intfc->ReadRecordData(&size, sizeof(size));
                for (std::size_t i = 0; i < size; ++i) {
                    std::uint32_t uid;
                    std::uint16_t dmg;
                    a_intfc->ReadRecordData(&uid, sizeof(uid));
                    a_intfc->ReadRecordData(&dmg, sizeof(dmg));
                    g_baseDamageCache[uid] = dmg;
                }
            } else if (type == 'DMGF' && version >= 1) {
                auto& formula = g_damageFormula;
                a_intfc->ReadRecordData(&formula.enabled, sizeof(formula.enabled));
                a_intfc->ReadRecordData(&formula.baseHardness, sizeof(formula.baseHardness));
                a_intfc->ReadRecordData(&formula.glanceThreshold, sizeof(formula.glanceThreshold));
                a_intfc->ReadRecordData(&formula.glanceMultiplier, sizeof(formula.glanceMultiplier));
                a_intfc->ReadRecordData(&formula.scratchThreshold, sizeof(formula.scratchThreshold));
                a_intfc->ReadRecordData(&formula.scratchMultiplier, sizeof(formula.scratchMultiplier));
                a_intfc->ReadRecordData(&formula.durabilityDamageConstant, sizeof(formula.durabilityDamageConstant));
            }
        }
        g_initialUISyncDone.store(false);
    }

    // =========================================================================
    // 注册序列化回调
    // =========================================================================

    void RegisterSerialization(const F4SE::SerializationInterface* a_intfc)
    {
        a_intfc->SetUniqueID('CSF1');
        a_intfc->SetRevertCallback(OnRevert);
        a_intfc->SetSaveCallback(OnSave);
        a_intfc->SetLoadCallback(OnLoad);
    }

    // =========================================================================
    // 读档后同步武器拔出状态 & UI
    // =========================================================================

    void SyncAfterGameLoad()
    {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        // 1. 通过 ActorState 直接读取武器拔出状态
        bool isDrawn = player->GetWeaponMagicDrawn();
        g_isWeaponDrawn.store(isDrawn);
        g_initialUISyncDone.store(false);

        // 2. 通过 TaskInterface 延迟一帧同步 UI 刷新
        if (isDrawn) {
            if (auto task = F4SE::GetTaskInterface()) {
                task->AddTask([]() {
                    auto p = RE::PlayerCharacter::GetSingleton();
                    if (p) {
                        float currentPercent = GetEquippedWeaponDurabilityPercent();
                        float currentPoints = GetEquippedWeaponDurabilityPoints();
                        UpdateHUDDurabilityWidget(currentPercent, currentPoints, true);
                        ConditionUI::EvaluateVisibility();
                    }
                    });
            }
        }
    }
}

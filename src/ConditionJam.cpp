#include "pch.h"
#include "ConditionCore.h"
#include "ConditionUI.h"

#include <thread>
#include <chrono>

namespace ConditionSystem
{
    // =========================================================================
    // 完整排障流程 (异步)
    // =========================================================================

    void TriggerFullUnjam(RE::Actor* a_actor)
    {
        if (!a_actor) return;

        ConditionSystem::g_isUnjamming.store(true);
        RE::SendHUDMessage::ShowHUDMessage("$CSF_Unjamming", "WPNPistol10mmFireDry", true, true);

        // 使用 ActorHandle 替代裸指针，防止分离线程中 use-after-free
        auto actorHandle = a_actor->GetHandle();

        std::thread([actorHandle]() {
            // 给引擎 0.25 秒的缓冲期
            // 防止在 reloadComplete 的瞬间放下武器被引擎的 Idle 动画覆盖吞没
            std::this_thread::sleep_for(std::chrono::milliseconds(250));

            if (!ConditionSystem::g_isGameRunning.load()) return;

            // 强制放下武器并显示 UI (放入Task确保在主线程执行)
            if (auto task = F4SE::GetTaskInterface()) {
                task->AddTask([actorHandle]() {
                    auto ref = actorHandle.get();
                    auto actor = ref ? ref->As<RE::Actor>() : nullptr;
                    if (actor) actor->DrawWeaponMagicHands(false);
                    ConditionSystem::ConditionUI::ShowUnjammingUI(true);
                    });
            }

            // 播放进度条
            for (int i = 1; i <= 90; ++i) {
                if (!ConditionSystem::g_isGameRunning.load()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                float progress = i / 90.0f;
                if (auto task = F4SE::GetTaskInterface()) {
                    task->AddTask([progress]() {
                        ConditionSystem::ConditionUI::UpdateUnjammingProgress(progress);
                        });
                }
            }

            // 恢复武器并解除锁定
            if (auto task = F4SE::GetTaskInterface()) {
                task->AddTask([actorHandle]() {
                    auto ref = actorHandle.get();
                    auto actor = ref ? ref->As<RE::Actor>() : nullptr;
                    if (actor) actor->DrawWeaponMagicHands(true);
                    ConditionSystem::ConditionUI::ShowUnjammingUI(false);
                    ConditionSystem::g_isWeaponJammed.store(false);
                    ConditionSystem::g_jammedWeaponUniqueID.store(0);
                    ConditionSystem::g_isUnjamming.store(false);
                    RE::SendHUDMessage::ShowHUDMessage("$CSF_UnjamSuccess", nullptr, true, true);
                    });
            }
            }).detach();
    }
}

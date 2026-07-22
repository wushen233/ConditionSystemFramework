#pragma once

#include <F4SE/Interfaces.h>
#include "IIF_API.h" 

// 前置声明，防止编译找不到类型
namespace RE { class Actor; class TESObjectWEAP; }

namespace ConditionSystem::Hooks
{
    void InstallAll();
    void InstallSingletons();
    void OnIIFMessage_CND(IIF_API::UpdateMessage* msg);

    // 必须补上的战斗总线回调函数声明
    void OnCombatDamageCalculate(RE::Actor* attacker, RE::TESObjectWEAP* weapon, float* damagePtr);
    void OnCombatArmorCalculate(RE::Actor* wearer, float* ratingPtr);
}
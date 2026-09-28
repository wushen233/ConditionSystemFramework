#pragma once

#include <cstdint>

namespace ConditionSystem::AnimationAPI
{
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::uint32_t kMessage_RequestInterface = 'CSAI';
    inline constexpr std::uint32_t kMessage_ProvideInterface = 'CSAO';

    struct State
    {
        std::uint32_t version{ kVersion };
        bool weaponJammed{ false };
        bool unjamming{ false };
        bool weaponDrawn{ false };
        bool realWeaponEquipped{ false };
        bool energyFault{ false };
        bool overheated{ false };
        float weaponHeat{ 0.0f };
        float weaponDurability{ 1.0f };
        float unjamProgress{ 0.0f };
    };

    using GetStateFn = State (*)();

    struct Interface
    {
        std::uint32_t version{ kVersion };
        GetStateFn GetState{ nullptr };
    };
}

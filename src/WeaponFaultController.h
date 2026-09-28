#pragma once

#include <cstdint>
#include <mutex>

namespace ConditionSystem
{
    enum class FaultType : std::uint8_t
    {
        None = 0,
        BallisticJam = 1,
        EnergySystemFault = 2,
        Overheat = 3
    };

    // Runtime identity for one equipped inventory instance. The stack address
    // is intentionally kept behind this type so callers do not use raw pointer
    // values as business state.
    struct WeaponInstanceIdentity
    {
        std::uint32_t weaponFormID{ 0 };
        std::uintptr_t stackAddress{ 0 };

        [[nodiscard]] bool IsValid() const noexcept
        {
            return weaponFormID != 0 && stackAddress != 0;
        }

        friend bool operator==(const WeaponInstanceIdentity& a_lhs, const WeaponInstanceIdentity& a_rhs) noexcept
        {
            return a_lhs.weaponFormID == a_rhs.weaponFormID && a_lhs.stackAddress == a_rhs.stackAddress;
        }

        friend bool operator!=(const WeaponInstanceIdentity& a_lhs, const WeaponInstanceIdentity& a_rhs) noexcept
        {
            return !(a_lhs == a_rhs);
        }
    };

    struct WeaponFaultSnapshot
    {
        WeaponInstanceIdentity currentWeapon{};
        WeaponInstanceIdentity faultOwner{};
        WeaponInstanceIdentity heatOwner{};
        FaultType faultType{ FaultType::None };
        float energyFaultRemaining{ 0.0f };
        float weaponHeat{ 0.0f };

        [[nodiscard]] bool IsCurrentFaultOwner() const noexcept
        {
            return currentWeapon.IsValid() && currentWeapon == faultOwner;
        }

        [[nodiscard]] bool IsCurrentHeatOwner() const noexcept
        {
            return currentWeapon.IsValid() && currentWeapon == heatOwner;
        }

        [[nodiscard]] bool IsWeaponJammed() const noexcept
        {
            return IsCurrentFaultOwner() && faultType == FaultType::BallisticJam;
        }

        [[nodiscard]] bool IsEnergyFault() const noexcept
        {
            return IsCurrentFaultOwner() && faultType == FaultType::EnergySystemFault;
        }

        [[nodiscard]] bool IsOverheated() const noexcept
        {
            return IsCurrentFaultOwner() && faultType == FaultType::Overheat;
        }

        [[nodiscard]] bool IsOperationallyBlocked() const noexcept
        {
            return IsWeaponJammed() || IsEnergyFault() || IsOverheated();
        }
    };

    // Deep Module for weapon fault ownership and state transitions. Legacy
    // atomic globals are mirrored by the implementation for existing UI/API
    // callers, while all new state changes pass through this Interface.
    class WeaponFaultController
    {
    public:
        [[nodiscard]] WeaponFaultSnapshot Snapshot() const;

        void Reset();
        void SetActiveWeapon(WeaponInstanceIdentity a_weapon);

        [[nodiscard]] bool StartBallisticJam(WeaponInstanceIdentity a_owner);
        [[nodiscard]] bool StartEnergyFault(WeaponInstanceIdentity a_owner, float a_duration);
        [[nodiscard]] bool ClearFaultForWeapon(WeaponInstanceIdentity a_owner);
        [[nodiscard]] bool ClearCurrentFault();

        // Heat is a continuous state owned by a weapon instance. It has its
        // own lifecycle, but can promote/demote the visible Overheat fault.
        WeaponFaultSnapshot PrepareHeatOwner(WeaponInstanceIdentity a_owner);
        WeaponFaultSnapshot ResetHeatForWeapon(WeaponInstanceIdentity a_owner);
        WeaponFaultSnapshot ApplyHeat(WeaponInstanceIdentity a_owner, float a_heat, float a_recoveryThreshold);
        WeaponFaultSnapshot TickEnergyFault(float a_deltaSeconds);

    private:
        void PublishLegacyStateLocked() const;
        void RestoreOverheatIfNeededLocked(WeaponInstanceIdentity a_owner);

        mutable std::mutex _mutex;
        WeaponFaultSnapshot _state{};
    };

    extern WeaponFaultController g_weaponFaultController;
}

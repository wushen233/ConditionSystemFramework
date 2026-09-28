#include "pch.h"
#include "WeaponFaultController.h"
#include "ConditionCore.h"

#include <algorithm>

namespace ConditionSystem
{
    WeaponFaultController g_weaponFaultController;

    namespace
    {
        [[nodiscard]] WeaponFaultSnapshot MakeVisibleSnapshot(const WeaponFaultSnapshot& a_state)
        {
            auto visible = a_state;
            if (!visible.IsCurrentFaultOwner()) {
                visible.faultType = FaultType::None;
                visible.energyFaultRemaining = 0.0f;
            }
            if (!visible.IsCurrentHeatOwner()) {
                visible.weaponHeat = 0.0f;
            }
            return visible;
        }
    }

    WeaponFaultSnapshot WeaponFaultController::Snapshot() const
    {
        std::lock_guard lock(_mutex);
        auto visible = MakeVisibleSnapshot(_state);
        PublishLegacyStateLocked();
        return visible;
    }

    void WeaponFaultController::PublishLegacyStateLocked() const
    {
        const auto visible = MakeVisibleSnapshot(_state);
        g_isWeaponJammed.store(visible.IsWeaponJammed(), std::memory_order_release);
        g_faultType.store(visible.faultType, std::memory_order_release);
        g_weaponHeat.store(visible.weaponHeat, std::memory_order_release);
        g_heatWeaponUniqueID.store(_state.heatOwner.stackAddress, std::memory_order_release);
        g_energyFaultRemaining.store(visible.energyFaultRemaining, std::memory_order_release);
        g_jammedWeaponUniqueID.store(_state.faultOwner.stackAddress, std::memory_order_release);
    }

    void WeaponFaultController::Reset()
    {
        std::lock_guard lock(_mutex);
        _state = {};
        PublishLegacyStateLocked();
    }

    void WeaponFaultController::SetActiveWeapon(WeaponInstanceIdentity a_weapon)
    {
        std::lock_guard lock(_mutex);
        _state.currentWeapon = a_weapon;
        PublishLegacyStateLocked();
    }

    bool WeaponFaultController::StartBallisticJam(WeaponInstanceIdentity a_owner)
    {
        if (!a_owner.IsValid()) return false;

        std::lock_guard lock(_mutex);
        _state.currentWeapon = a_owner;
        _state.faultOwner = a_owner;
        _state.faultType = FaultType::BallisticJam;
        _state.energyFaultRemaining = 0.0f;
        PublishLegacyStateLocked();
        return true;
    }

    bool WeaponFaultController::StartEnergyFault(WeaponInstanceIdentity a_owner, float a_duration)
    {
        if (!a_owner.IsValid()) return false;

        std::lock_guard lock(_mutex);
        _state.currentWeapon = a_owner;
        if (_state.faultType == FaultType::BallisticJam && _state.faultOwner == a_owner) {
            return false;
        }

        _state.faultOwner = a_owner;
        _state.faultType = FaultType::EnergySystemFault;
        _state.energyFaultRemaining = std::clamp(a_duration, 0.1f, 5.0f);
        PublishLegacyStateLocked();
        return true;
    }

    void WeaponFaultController::RestoreOverheatIfNeededLocked(WeaponInstanceIdentity a_owner)
    {
        if (_state.heatOwner == a_owner && _state.weaponHeat >= 1.0f) {
            _state.faultOwner = a_owner;
            _state.faultType = FaultType::Overheat;
        } else {
            _state.faultOwner = {};
            _state.faultType = FaultType::None;
        }
    }

    bool WeaponFaultController::ClearFaultForWeapon(WeaponInstanceIdentity a_owner)
    {
        if (!a_owner.IsValid()) return false;

        std::lock_guard lock(_mutex);
        if (_state.faultOwner != a_owner ||
            (_state.faultType != FaultType::BallisticJam && _state.faultType != FaultType::EnergySystemFault)) {
            return false;
        }

        _state.energyFaultRemaining = 0.0f;
        RestoreOverheatIfNeededLocked(a_owner);
        PublishLegacyStateLocked();
        return true;
    }

    bool WeaponFaultController::ClearCurrentFault()
    {
        std::lock_guard lock(_mutex);
        if (!_state.currentWeapon.IsValid() || _state.faultOwner != _state.currentWeapon) return false;
        if (_state.faultType == FaultType::None || _state.faultType == FaultType::Overheat) return false;

        const auto owner = _state.currentWeapon;
        _state.energyFaultRemaining = 0.0f;
        RestoreOverheatIfNeededLocked(owner);
        PublishLegacyStateLocked();
        return true;
    }

    WeaponFaultSnapshot WeaponFaultController::PrepareHeatOwner(WeaponInstanceIdentity a_owner)
    {
        std::lock_guard lock(_mutex);
        if (_state.heatOwner != a_owner) {
            _state.heatOwner = a_owner;
            _state.weaponHeat = 0.0f;
            if (_state.faultType == FaultType::Overheat) {
                _state.faultOwner = {};
                _state.faultType = FaultType::None;
            }
        }
        if (!a_owner.IsValid()) {
            _state.heatOwner = {};
            _state.weaponHeat = 0.0f;
        }
        PublishLegacyStateLocked();
        return MakeVisibleSnapshot(_state);
    }

    WeaponFaultSnapshot WeaponFaultController::ResetHeatForWeapon(WeaponInstanceIdentity a_owner)
    {
        std::lock_guard lock(_mutex);
        if (_state.heatOwner == a_owner) {
            _state.weaponHeat = 0.0f;
            if (_state.faultType == FaultType::Overheat && _state.faultOwner == a_owner) {
                _state.faultOwner = {};
                _state.faultType = FaultType::None;
            }
        }
        PublishLegacyStateLocked();
        return MakeVisibleSnapshot(_state);
    }

    WeaponFaultSnapshot WeaponFaultController::ApplyHeat(WeaponInstanceIdentity a_owner, float a_heat, float a_recoveryThreshold)
    {
        std::lock_guard lock(_mutex);
        if (_state.heatOwner != a_owner) {
            _state.heatOwner = a_owner;
            _state.weaponHeat = 0.0f;
        }

        _state.weaponHeat = std::clamp(a_heat, 0.0f, 1.0f);
        if (_state.faultType == FaultType::Overheat && _state.faultOwner == a_owner &&
            _state.weaponHeat <= std::clamp(a_recoveryThreshold, 0.0f, 1.0f)) {
            _state.faultOwner = {};
            _state.faultType = FaultType::None;
        } else if (_state.faultType == FaultType::None && _state.weaponHeat >= 1.0f) {
            _state.faultOwner = a_owner;
            _state.faultType = FaultType::Overheat;
        }

        PublishLegacyStateLocked();
        return MakeVisibleSnapshot(_state);
    }

    WeaponFaultSnapshot WeaponFaultController::TickEnergyFault(float a_deltaSeconds)
    {
        std::lock_guard lock(_mutex);
        if (_state.faultType == FaultType::EnergySystemFault) {
            _state.energyFaultRemaining = std::max(0.0f, _state.energyFaultRemaining - std::max(0.0f, a_deltaSeconds));
            if (_state.energyFaultRemaining <= 0.0f) {
                const auto owner = _state.faultOwner;
                _state.energyFaultRemaining = 0.0f;
                RestoreOverheatIfNeededLocked(owner);
            }
        } else {
            _state.energyFaultRemaining = 0.0f;
        }

        PublishLegacyStateLocked();
        return MakeVisibleSnapshot(_state);
    }
}

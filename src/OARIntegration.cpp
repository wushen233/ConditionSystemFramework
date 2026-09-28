#include "pch.h"
#include "OARIntegration.h"

#include "ConditionCore.h"
#include "OAR/OpenAnimationReplacerAPI-Conditions.h"

#include <algorithm>

namespace ConditionSystem::OARIntegration
{
    namespace
    {
        enum class ConditionKind
        {
            kWeaponJammed,
            kUnjamming,
            kWeaponDrawn,
            kWeaponDurability,
            kEnergyFault,
            kOverheated,
            kWeaponHeat
        };

        class CSFCondition final : public OAR::ConditionBase
        {
        public:
            explicit CSFCondition(ConditionKind a_kind) : kind(a_kind) {}

            std::string GetName() const override
            {
                switch (kind) {
                case ConditionKind::kWeaponJammed: return "CSF_IsWeaponJammed";
                case ConditionKind::kUnjamming: return "CSF_IsUnjamming";
                case ConditionKind::kWeaponDrawn: return "CSF_IsWeaponDrawn";
                case ConditionKind::kWeaponDurability: return "CSF_WeaponDurability";
                case ConditionKind::kEnergyFault: return "CSF_IsEnergyFault";
                case ConditionKind::kOverheated: return "CSF_IsOverheated";
                case ConditionKind::kWeaponHeat: return "CSF_WeaponHeat";
                default: return "CSF_Condition";
                }
            }

            std::string GetDescription() const override
            {
                switch (kind) {
                case ConditionKind::kWeaponJammed: return "The player weapon is currently jammed.";
                case ConditionKind::kUnjamming: return "The CSF full unjamming sequence is active.";
                case ConditionKind::kWeaponDrawn: return "The player weapon is drawn.";
                case ConditionKind::kWeaponDurability: return "The equipped CSF weapon durability passes a comparison.";
                case ConditionKind::kEnergyFault: return "The player energy weapon has an energy system fault.";
                case ConditionKind::kOverheated: return "The player weapon is currently overheated.";
                case ConditionKind::kWeaponHeat: return "The player weapon heat passes a comparison.";
                default: return "Condition System Framework state.";
                }
            }

            std::string GetParameterString() const override
            {
                if (kind != ConditionKind::kWeaponDurability && kind != ConditionKind::kWeaponHeat) return {};
                return std::format("{} {}", OAR::ComparisonOperatorToString(comparison), value);
            }

        protected:
            bool EvaluateImpl(RE::TESObjectREFR* a_refr, RE::hkbClipGenerator*, const OAR::SubMod*) const override
            {
                auto player = RE::PlayerCharacter::GetSingleton();
                if (!player || !a_refr || a_refr->GetFormID() != player->GetFormID()) return false;

                switch (kind) {
                case ConditionKind::kWeaponJammed: return IsCurrentWeaponJammed();
                case ConditionKind::kUnjamming: return g_isUnjamming.load();
                case ConditionKind::kWeaponDrawn: return g_isWeaponDrawn.load() || player->GetWeaponMagicDrawn();
                case ConditionKind::kWeaponDurability:
                    return IsRealWeaponEquipped() && OAR::CompareValues(GetEquippedWeaponDurabilityPercent(), comparison, value);
                case ConditionKind::kEnergyFault:
                    return IsCurrentWeaponEnergyFault();
                case ConditionKind::kOverheated:
                    return IsCurrentWeaponOverheated();
                case ConditionKind::kWeaponHeat:
                    return IsEquippedWeaponHeatManaged() && OAR::CompareValues(GetWeaponFaultSnapshot().weaponHeat, comparison, value);
                default: return false;
                }
            }

            void InitializeImpl(const nlohmann::json& a_json) override
            {
                if (kind != ConditionKind::kWeaponDurability && kind != ConditionKind::kWeaponHeat) return;

                const auto op = a_json.value("comparison", static_cast<int>(OAR::ComparisonOperator::kLessEqual));
                if (op >= static_cast<int>(OAR::ComparisonOperator::kEqual) && op <= static_cast<int>(OAR::ComparisonOperator::kLessEqual)) {
                    comparison = static_cast<OAR::ComparisonOperator>(op);
                }
                value = std::clamp(a_json.value("value", 0.5f), 0.0f, 10.0f);
            }

            void SerializeImpl(nlohmann::json& a_json) const override
            {
                if (kind == ConditionKind::kWeaponDurability || kind == ConditionKind::kWeaponHeat) {
                    a_json["comparison"] = static_cast<int>(comparison);
                    a_json["value"] = value;
                }
            }

        private:
            ConditionKind kind;
            OAR::ComparisonOperator comparison{ OAR::ComparisonOperator::kLessEqual };
            float value{ 0.5f };
        };

        template <ConditionKind Kind>
        std::unique_ptr<OAR::ICondition> MakeCondition()
        {
            return std::make_unique<CSFCondition>(Kind);
        }

        bool s_registered = false;
    }

    void TryRegister()
    {
        if (s_registered) return;

        auto api = OAR::Conditions::GetAPI();
        if (!api) return;

        const auto registerOne = [api](const char* a_name, OAR::Conditions::ConditionFactoryFn a_factory) {
            const auto result = api->RegisterCondition(a_name, a_factory);
            return result == OAR::Conditions::APIResult::OK || result == OAR::Conditions::APIResult::AlreadyRegistered;
        };

        const bool ok =
            registerOne("CSF_IsWeaponJammed", &MakeCondition<ConditionKind::kWeaponJammed>) &&
            registerOne("CSF_IsUnjamming", &MakeCondition<ConditionKind::kUnjamming>) &&
            registerOne("CSF_IsWeaponDrawn", &MakeCondition<ConditionKind::kWeaponDrawn>) &&
            registerOne("CSF_WeaponDurability", &MakeCondition<ConditionKind::kWeaponDurability>) &&
            registerOne("CSF_IsEnergyFault", &MakeCondition<ConditionKind::kEnergyFault>) &&
            registerOne("CSF_IsOverheated", &MakeCondition<ConditionKind::kOverheated>) &&
            registerOne("CSF_WeaponHeat", &MakeCondition<ConditionKind::kWeaponHeat>);

        if (ok) {
            s_registered = true;
            REX::INFO("[CSF-OAR] Registered CSF animation conditions (OAR API v{})", api->GetAPIVersion());
        }
    }
}

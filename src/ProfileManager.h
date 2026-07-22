#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <cstdint>

namespace RE {
    class TESForm;
    class TESBoundObject;
    class TESObjectWEAP;
    class TESObjectARMO;
    class TESAmmo;
    class BGSPerk;
    class ActorValueInfo;
    class ExtraDataList;
    namespace BGSMod::Attachment { class Mod; }
}

namespace ConditionSystem {

    // =========================================================================
    // 基础数据结构
    // =========================================================================

    struct WeaponProfile {
        std::string name;
        int priority;
        float maxDurability;
        float degradeRate;
        bool isExcluded;
        bool canJam;
        std::vector<std::uint32_t> formIDs;
        std::vector<std::vector<std::string>> keywords;
    };

    struct ArmorProfile {
        std::string name;
        int priority;
        float maxDurability;
        bool isExcluded;

        bool useFlatDegrade{ false };
        float degradeRate{ 0.0f };

        std::vector<std::uint32_t> formIDs;
        std::vector<std::vector<std::string>> keywords;
    };

    struct AmmoProfile {
        std::string name;
        float weaponWearMult;
        float armorDamageMult;
        std::vector<std::uint32_t> formIDs;
        std::vector<std::vector<std::string>> keywords;
    };

    struct ModifierProfile {
        std::string name;
        int priority;
        float degradeMult;
        bool immuneToJam;
        std::vector<std::uint32_t> formIDs;
        std::vector<std::vector<std::string>> keywords;
    };

    // =========================================================================
    // Omod (Object Modification) 改装件配置：武器/护甲的改装部件（机匣、枪管、内衬等）
    // 每个改装件可独立调整耐久度相关参数。
    // =========================================================================

    struct OmodProfile {
        std::string name;
        int priority;
        float degradeMult;          // 磨损倍率乘数 (累乘)：1.3 = 磨损加快30%，0.7 = 磨损降低30%
        float degradeRateFlat;      // 固定磨损加成 (累加)：每次射击额外增加/减少的磨损点数
        float maxDurabilityMult;    // 最大耐久倍率 (累乘)：1.2 = 增加20%最大耐久
        bool immuneToJam;           // 该改装件是否免疫卡壳
        std::vector<std::uint32_t> formIDs;   // 按具体 omod FormID 匹配
        std::vector<std::vector<std::string>> keywords;  // 按关键词匹配
    };

    struct RepairKitProfile {
        bool isValid{ false };
        std::string name;
        bool canRepairWeapon{ false };
        bool canRepairArmor{ false };
        bool useFlatPoints{ false };
        float baseRepairPoints{ 0.0f };
        float baseRepairPercent{ 0.0f };
        float maxConditionLimit{ 1.0f };
        std::vector<std::uint32_t> formIDs;
        std::vector<std::string> keywords;
    };

    struct ItemFlags {
        bool enableDurabilitySystem{ true };
        bool showDurabilityUI{ true };
    };

    struct OverrideRule {
        std::string name;
        std::vector<std::string> keywords;
        std::vector<std::uint32_t> formIDs;
        std::vector<std::string> plugins;

        bool enableDurabilitySystem{ false };
        bool showDurabilityUI{ false };
    };

    // =========================================================================
    // 高级规则数据结构 (公式与特性)
    // =========================================================================

    struct DamageFormulaConfig {
        bool enabled{ true };
        float baseHardness{ 1.0f };
        float glanceThreshold{ 0.3f };
        float glanceMultiplier{ 0.1f };
        float scratchThreshold{ 0.7f };
        float scratchMultiplier{ 0.4f };
        float durabilityDamageConstant{ 0.15f };
    };

    struct SkillRequirement {
        std::string type;
        std::string pluginName;
        std::uint32_t localFormID{ 0 };
        std::uint32_t runtimeFormID{ 0 };
        float minRank{ 0.0f };
    };

    struct OverRepairRule {
        std::string ruleName;
        std::vector<std::string> targetKeywords;
        SkillRequirement requiredSkill;
        float avStep{ 25.0f };
        std::vector<float> rankLimits;
    };

    struct JuryRigResult {
        bool allowed{ false };
        float efficiencyMult{ 1.0f };
    };

    struct JuryRiggingRule {
        std::string ruleName;
        std::string description;
        bool allowCrossCategory{ false };
        float materialConditionMultiplier{ 1.0f };
        float maxConditionCap{ 1.0f };
        std::vector<std::string> targetKeywords;
        std::vector<std::string> matchCategories;
        SkillRequirement requiredSkill;
    };

    struct LootBonusTier {
        float minAVLevel;
        float hitChance;
        float bonusMinPct;
        float bonusMaxPct;
    };

    struct LootBonusRule {
        bool enabled{ true };
        float globalCap{ 2.0f };
        SkillRequirement requiredAV;
        std::vector<LootBonusTier> tiers;
    };

    // =========================================================================
    // 全局变量与接口声明
    // =========================================================================

    extern std::shared_mutex g_profileMutex;
    extern std::vector<WeaponProfile> g_weaponProfiles;
    extern std::vector<ArmorProfile> g_armorProfiles;
    extern std::vector<AmmoProfile> g_ammoProfiles;
    extern std::vector<ModifierProfile> g_modifierProfiles;
    extern std::vector<OmodProfile> g_omodProfiles;

    extern std::vector<OverrideRule> g_overrideRules;
    extern std::vector<OverRepairRule> g_overRepairRules;
    extern std::vector<JuryRiggingRule> g_juryRiggingRules;
    extern LootBonusRule g_lootBonusRule;

    extern DamageFormulaConfig g_damageFormula;

    // Forwarder that calls LoadProfilesFromDirectory<RepairKitProfile> with the correct lambda.
    // Defined in ProfileManager.cpp; used by Repair::LoadRepairKitProfiles().
    void LoadRepairKitProfilesFromTemplate(std::vector<RepairKitProfile>& a_out);

    namespace Repair {
        void LoadRepairKitProfiles();
    }

    void LoadAllProfiles();

    bool HasKeywordString(RE::TESForm* a_form, RE::ExtraDataList* a_extraList, const std::string& a_keyword);
    inline bool HasKeywordString(RE::TESForm* a_form, const std::string& a_keyword) {
        return HasKeywordString(a_form, nullptr, a_keyword);
    }

    WeaponProfile GetProfileForWeapon(RE::TESObjectWEAP* weap);
    ArmorProfile GetProfileForArmor(RE::TESObjectARMO* armor);
    AmmoProfile GetProfileForAmmo(RE::TESAmmo* a_ammo);
    ModifierProfile GetProfileForModifier(RE::TESForm* omodForm);
    OmodProfile GetProfileForOmod(RE::BGSMod::Attachment::Mod* omod);
    RepairKitProfile GetRepairKitProfile(RE::TESBoundObject* a_kit);

    std::uint32_t ParseFormID(const std::string& a_identifier);

    ItemFlags GetItemFlags(RE::TESForm* a_form);
    ItemFlags GetItemFlagsRaw(RE::TESForm* a_form);
    float GetPlayerOverRepairLimit(RE::TESBoundObject* targetObj, RE::ExtraDataList* a_targetExtra);

    JuryRigResult EvaluateJuryRigging(RE::TESBoundObject* a_target, RE::ExtraDataList* a_targetExtra, RE::TESBoundObject* a_material, RE::ExtraDataList* a_materialExtra);
    std::string GetJuryRiggingSkillString(RE::TESBoundObject* a_target, RE::ExtraDataList* a_targetExtra);
}

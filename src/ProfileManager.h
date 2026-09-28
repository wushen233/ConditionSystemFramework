#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <cstdint>
#include <string_view>
namespace RE {
    class TESForm;
    class TESFile;
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
    struct CategoryPredicate { std::vector<std::string> all; std::vector<std::string> any; std::vector<std::string> none; };
    enum class WeaponMechanism : std::uint8_t { Auto = 0, BallisticJam = 1, EnergyFault = 2, Overheat = 3, None = 4 };
    struct WeaponProfile { std::string name; int priority; float maxDurability; float degradeRate; bool isExcluded; bool canJam; WeaponMechanism mechanism{ WeaponMechanism::Auto }; CategoryPredicate appliesTo; std::vector<std::uint32_t> formIDs; std::vector<std::vector<std::string>> keywords; };
    struct ArmorProfile { std::string name; int priority; float maxDurability; bool isExcluded; bool useFlatDegrade{ false }; float degradeRate{ 0.0f }; CategoryPredicate appliesTo; std::vector<std::uint32_t> formIDs; std::vector<std::vector<std::string>> keywords; };
    struct DurabilityCategoryModifier { std::string name; std::string domain; int priority{ 0 }; CategoryPredicate appliesTo; float maxDurabilityMult{ 1.0f }; float degradeMult{ 1.0f }; float degradeRateFlat{ 0.0f }; };
    struct AmmoProfile { std::string name; float weaponWearMult; float armorDamageMult; std::vector<std::uint32_t> formIDs; std::vector<std::vector<std::string>> keywords; };
    struct ModifierProfile { std::string name; int priority; float degradeMult; bool immuneToJam; std::vector<std::uint32_t> formIDs; std::vector<std::vector<std::string>> keywords; };
    struct OmodProfile { std::string name; int priority; float degradeMult; float degradeRateFlat; float maxDurabilityMult; bool immuneToJam; std::vector<std::uint32_t> formIDs; std::vector<std::vector<std::string>> keywords; };
    struct RepairKitProfile { bool isValid{ false }; std::string name; bool canRepairWeapon{ false }; bool canRepairArmor{ false }; bool useFlatPoints{ false }; float baseRepairPoints{ 0.0f }; float baseRepairPercent{ 0.0f }; float maxConditionLimit{ 1.0f }; std::vector<std::uint32_t> formIDs; std::vector<std::string> keywords; };
    struct ItemFlags { bool enableDurabilitySystem{ true }; bool showDurabilityUI{ true }; };
    struct DamageFormulaConfig { bool enabled{ true }; float baseHardness{ 1.0f }; float glanceThreshold{ 0.7f }; float glanceMultiplier{ 0.1f }; float scratchThreshold{ 0.3f }; float scratchMultiplier{ 0.4f }; float durabilityDamageConstant{ 0.15f }; };
    struct SkillRequirement { bool configured{ false }; std::string type; std::string pluginName; std::uint32_t localFormID{ 0 }; std::uint32_t runtimeFormID{ 0 }; float minRank{ 0.0f }; float scaleFactor{ 1.0f }; };
    struct SkillWearModifier {
        struct Threshold { float threshold{ 0.0f }; float wearMultiplier{ 1.0f }; float wearRateFlat{ 0.0f }; };
        std::string name;
        std::string domain;
        int priority{ 0 };
        CategoryPredicate appliesTo;
        SkillRequirement requiredSkill;
        std::vector<Threshold> thresholds;
    };
    struct OverRepairRule { std::string ruleName; std::vector<std::string> targetCategories; SkillRequirement requiredSkill; struct SkillThreshold { float threshold{ 0.0f }; float maxValue{ 1.0f }; }; std::vector<SkillThreshold> skillThresholds; };
    enum class JuryCompatibilityType : std::uint8_t { Always, SameBase, SameGroup, SameOptionalGroup, SharesAnyCategory, GroupPair };
    struct JuryCompatibilityCondition { JuryCompatibilityType type{ JuryCompatibilityType::SameBase }; std::string group; bool symmetric{ false }; std::vector<std::pair<std::string, std::string>> pairs; };
    struct JuryEfficiencyModifier { std::string group; float same{ 1.0f }; float different{ 1.0f }; float oneMissing{ 1.0f }; };
    struct JuryRigResult { bool allowed{ false }; float efficiencyMult{ 1.0f }; int priority{ 0 }; std::string matchedRule; };
    struct JuryRiggingRule { std::string ruleName; std::string description; int priority{ 0 }; float materialConditionMultiplier{ 1.0f }; CategoryPredicate targetPredicate; CategoryPredicate materialPredicate; std::vector<JuryCompatibilityCondition> compatibility; std::vector<JuryEfficiencyModifier> efficiencyModifiers; SkillRequirement requiredSkill; struct SkillThreshold { float threshold{ 0.0f }; float maxValue{ 1.0f }; }; std::vector<SkillThreshold> skillThresholds; };
    struct LootBonusTier { float minLevel; float hitChance; float bonusMin; float bonusMax; };
    struct LootBonusRule { float globalCap{ 2.0f }; SkillRequirement requiredAV; std::vector<LootBonusTier> tiers; };
    extern std::shared_mutex g_profileMutex;
    extern std::vector<WeaponProfile> g_weaponProfiles;
    extern std::vector<ArmorProfile> g_armorProfiles;
    extern std::vector<AmmoProfile> g_ammoProfiles;
    extern std::vector<ModifierProfile> g_modifierProfiles;
    extern std::vector<OmodProfile> g_omodProfiles;
    extern std::vector<DurabilityCategoryModifier> g_durabilityCategoryModifiers;
    extern std::vector<SkillWearModifier> g_skillWearModifiers;
    extern std::vector<OverRepairRule> g_overRepairRules;
    extern std::vector<JuryRiggingRule> g_juryRiggingRules;
    extern LootBonusRule g_lootBonusRule;
    extern DamageFormulaConfig g_damageFormula;
    void LoadRepairKitProfilesFromTemplate(std::vector<RepairKitProfile>& a_out);
    namespace Repair { void LoadRepairKitProfiles(); }
    void LoadAllProfiles();
    bool HasKeywordString(RE::TESForm* a_form, RE::ExtraDataList* a_extraList, const std::string& a_keyword);
    RE::BGSKeywordForm* SafeGetKeywordForm(RE::TESForm* a_form);
    inline bool HasKeywordString(RE::TESForm* a_form, const std::string& a_keyword) { return HasKeywordString(a_form, nullptr, a_keyword); }
    WeaponProfile GetProfileForWeapon(RE::TESObjectWEAP* weap, RE::ExtraDataList* a_extraList = nullptr);
    ArmorProfile GetProfileForArmor(RE::TESObjectARMO* armor, RE::ExtraDataList* a_extraList = nullptr);
    float GetEffectiveMaxDurability(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList);
    void GetSkillWearEffects(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList, bool a_weapon, float& a_multiplier, float& a_flat);
    AmmoProfile GetProfileForAmmo(RE::TESAmmo* a_ammo);
    ModifierProfile GetProfileForModifier(RE::TESForm* omodForm);
    OmodProfile GetProfileForOmod(RE::BGSMod::Attachment::Mod* omod);
    RepairKitProfile GetRepairKitProfile(RE::TESBoundObject* a_kit);
    std::uint32_t ParseFormID(const std::string& a_identifier);
    const RE::TESFile* FindLoadedPlugin(std::string_view a_pluginName);
    ItemFlags GetItemFlags(RE::TESForm* a_form);
    ItemFlags GetItemFlagsRaw(RE::TESForm* a_form);
    bool TryGetSkillRequirementValue(const SkillRequirement& a_requirement, RE::PlayerCharacter* a_player, float& a_value);
    float GetPlayerOverRepairLimit(RE::TESBoundObject* targetObj, RE::ExtraDataList* a_targetExtra);
    float GetJuryRepairLimit(RE::TESBoundObject* targetObj, RE::ExtraDataList* a_targetExtra);
    float GetWorkbenchRepairLimit(RE::TESBoundObject* targetObj, RE::ExtraDataList* a_targetExtra);
    float GetRepairKitLimit(float a_kitMaxConditionLimit);
    JuryRigResult EvaluateJuryRigging(RE::TESBoundObject* a_target, RE::ExtraDataList* a_targetExtra, RE::TESBoundObject* a_material, RE::ExtraDataList* a_materialExtra);
    std::string GetJuryRiggingSkillString(RE::TESBoundObject* a_target, RE::ExtraDataList* a_targetExtra);
}

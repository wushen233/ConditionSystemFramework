#include "pch.h"
#include "ConditionCore.h"
#include "ProfileManager.h"
#include "Repair/RepairSystem.h"
#include "Translation.h" 

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace ConditionSystem
{
    std::shared_mutex g_profileMutex;

    std::vector<WeaponProfile> g_weaponProfiles;
    std::vector<ArmorProfile> g_armorProfiles;
    std::vector<AmmoProfile> g_ammoProfiles;
    std::vector<ModifierProfile> g_modifierProfiles;
    std::vector<OmodProfile> g_omodProfiles;
    std::vector<OverrideRule> g_overrideRules;

    std::vector<OverRepairRule> g_overRepairRules;
    std::vector<JuryRiggingRule> g_juryRiggingRules;
    LootBonusRule g_lootBonusRule;

    // 新增：公式配置实例初始化
    DamageFormulaConfig g_damageFormula;

    WeaponProfile g_defaultWeaponProfile{ "DefaultWeapon", 0, 1000.0f, 1.0f, false, true, {}, {} };
    ArmorProfile g_defaultArmorProfile{ "DefaultArmor", 0, 500.0f, false, false, 0.0f, {}, {} };
    AmmoProfile g_defaultAmmoProfile{ "DefaultAmmo", 1.0f, 1.0f, {}, {} };
    ModifierProfile g_defaultModifierProfile{ "DefaultModifier", 0, 1.0f, false, {}, {} };
    OmodProfile g_defaultOmodProfile{ "DefaultOmod", 0, 1.0f, 0.0f, 1.0f, false, {}, {} };

    std::uint32_t ParseFormID(const std::string& a_identifier) {
        auto pos = a_identifier.find('|');
        if (pos != std::string::npos) {
            std::string plugin = a_identifier.substr(0, pos);
            std::string hexStr = a_identifier.substr(pos + 1);
            try {
                std::uint32_t localId = std::stoul(hexStr, nullptr, 16);
                auto dataHandler = RE::TESDataHandler::GetSingleton();
                if (dataHandler) {
                    auto form = dataHandler->LookupForm(localId, plugin);
                    if (form) return form->GetFormID();
                }
            }
            catch (...) {}
        }
        else {
            try { return std::stoul(a_identifier, nullptr, 16); }
            catch (...) {}
        }
        return 0;
    }

    RE::BGSKeywordForm* SafeGetKeywordForm(RE::TESForm* a_form) {
        if (!a_form) return nullptr;
        if (auto kf = a_form->As<RE::BGSKeywordForm>()) return kf;

        if (a_form->GetFormType() == RE::ENUM_FORM_ID::kWEAP) {
            return static_cast<RE::BGSKeywordForm*>(static_cast<RE::TESObjectWEAP*>(a_form));
        }
        else if (a_form->GetFormType() == RE::ENUM_FORM_ID::kARMO) {
            return static_cast<RE::BGSKeywordForm*>(static_cast<RE::TESObjectARMO*>(a_form));
        }
        else if (a_form->GetFormType() == RE::ENUM_FORM_ID::kAMMO) {
            return static_cast<RE::BGSKeywordForm*>(static_cast<RE::TESAmmo*>(a_form));
        }
        return nullptr;
    }

    bool HasKeywordString(RE::TESForm* a_form, RE::ExtraDataList* a_extraList, const std::string& a_keyword) {
        if (!a_form) return false;

        if (a_keyword == "IS_WEAPON" && a_form->Is(RE::ENUM_FORM_ID::kWEAP)) return true;
        if (a_keyword == "IS_ARMOR" && a_form->Is(RE::ENUM_FORM_ID::kARMO)) return true;

        RE::BGSKeywordForm* baseKeywords = SafeGetKeywordForm(a_form);
        if (baseKeywords) {
            for (std::uint32_t i = 0; i < baseKeywords->numKeywords; ++i) {
                if (baseKeywords->keywords && baseKeywords->keywords[i]) {
                    const char* edid = baseKeywords->keywords[i]->GetFormEditorID();
                    if (edid && _stricmp(edid, a_keyword.c_str()) == 0) return true;
                }
            }
        }

        if (a_extraList) {
            if (auto instExtra = a_extraList->GetByType<RE::ExtraInstanceData>()) {
                if (instExtra->data) {
                    if (RE::BGSKeywordForm* dynKeywords = instExtra->data->GetKeywordData()) {
                        for (std::uint32_t i = 0; i < dynKeywords->numKeywords; ++i) {
                            if (dynKeywords->keywords && dynKeywords->keywords[i]) {
                                const char* edid = dynKeywords->keywords[i]->GetFormEditorID();
                                if (edid && _stricmp(edid, a_keyword.c_str()) == 0) return true;
                            }
                        }
                    }
                }
            }
        }
        return false;
    }

    // =========================================================================
    // 模板：从指定子目录加载 JSON profiles，统一处理 formIDs + keywords 解析
    // a_parseFields — 类型特定的字段解析 lambda: (const json&, T&) -> void
    // =========================================================================
    template<typename T, typename F>
    void LoadProfilesFromDirectory(
        const std::filesystem::path& a_baseDir,
        const std::string& a_subDir,
        std::vector<T>& a_outProfiles,
        F&& a_parseFields)
    {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(a_baseDir / a_subDir)) {
                if (entry.path().extension() != ".json") continue;
                try {
                    std::ifstream file(entry.path());
                    json j = json::parse(file, nullptr, true, true);
                    if (!j.contains("profiles") || !j["profiles"].is_array()) continue;
                    for (const auto& p : j["profiles"]) {
                        T profile;
                        a_parseFields(p, profile);

                        // 通用字段：formIDs
                        // 支持两种格式：数组 ["0x123", "0x456"] 或对象 {"Plugin.esm": ["0x123"]}
                        if (p.contains("formIDs")) {
                            auto& fIDsNode = p["formIDs"];
                            if (fIDsNode.is_array()) {
                                for (const auto& fID : fIDsNode) {
                                    if (fID.is_string()) {
                                        std::uint32_t runtimeID = ParseFormID(fID.get<std::string>());
                                        if (runtimeID != 0) profile.formIDs.push_back(runtimeID);
                                    }
                                }
                            } else if (fIDsNode.is_object()) {
                                for (auto& [pluginName, idArray] : fIDsNode.items()) {
                                    if (idArray.is_array()) {
                                        for (const auto& fID : idArray) {
                                            if (fID.is_string()) {
                                                std::string combined = pluginName + "|" + fID.get<std::string>();
                                                std::uint32_t runtimeID = ParseFormID(combined);
                                                if (runtimeID != 0) profile.formIDs.push_back(runtimeID);
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 通用字段：keywords
                        // 自动适配两种格式：
                        //   - 扁平 vector<string>（RepairKitProfile 等）
                        //   - AND-group vector<vector<string>>（标准 profile 类型）
                        if constexpr (requires { std::declval<T>().keywords; }) {
                            if (p.contains("keywords") && p["keywords"].is_array()) {
                                using KWValType = typename decltype(std::declval<T>().keywords)::value_type;
                                if constexpr (std::is_same_v<KWValType, std::string>) {
                                    // 扁平关键词
                                    for (const auto& kwd : p["keywords"]) {
                                        if (kwd.is_string()) profile.keywords.push_back(kwd.get<std::string>());
                                    }
                                } else if constexpr (std::is_same_v<KWValType, std::vector<std::string>>) {
                                    // AND-group 关键词
                                    for (const auto& andGroup : p["keywords"]) {
                                        if (andGroup.is_array()) {
                                            std::vector<std::string> kwds;
                                            for (const auto& kwd : andGroup) {
                                                if (kwd.is_string()) kwds.push_back(kwd.get<std::string>());
                                            }
                                            profile.keywords.push_back(kwds);
                                        }
                                    }
                                }
                            }
                        }

                        a_outProfiles.push_back(profile);
                    }
                }
                catch (...) {}
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            REX::WARN("[ProfileManager] 无法读取 {} 目录: {}", a_subDir, e.what());
        }
    }

    void LoadRepairKitProfilesFromTemplate(std::vector<RepairKitProfile>& a_out) {
        LoadProfilesFromDirectory<RepairKitProfile>(
            std::filesystem::path("Data\\F4SE\\Plugins\\ConditionSystemFramework"),
            "RepairKits", a_out,
            [](const json& p, RepairKitProfile& profile) {
                profile.isValid = true;
                profile.name = p.value("name", "");
                profile.canRepairWeapon = p.value("canRepairWeapon", false);
                profile.canRepairArmor = p.value("canRepairArmor", false);
                if (p.contains("baseRepairPoints")) {
                    profile.useFlatPoints = true;
                    profile.baseRepairPoints = p.value("baseRepairPoints", 500.0f);
                } else {
                    profile.useFlatPoints = false;
                    profile.baseRepairPercent = p.value("baseRepairPercent", 0.50f);
                }
                float fallbackLimit = p.value("canOverRepair", false) ? 2.0f : 1.0f;
                profile.maxConditionLimit = p.value("maxConditionLimit", fallbackLimit);
            });
    }

    void LoadAllProfiles() {
        std::unique_lock<std::shared_mutex> lock(g_profileMutex);
        g_weaponProfiles.clear();
        g_armorProfiles.clear();
        g_ammoProfiles.clear();
        g_modifierProfiles.clear();
        g_omodProfiles.clear();
        g_overrideRules.clear();
        g_overRepairRules.clear();
        g_juryRiggingRules.clear();
        g_lootBonusRule = LootBonusRule();

        // 恢复默认公式
        g_damageFormula = DamageFormulaConfig();

        auto baseDir = std::filesystem::path("Data\\F4SE\\Plugins\\ConditionSystemFramework");
        std::vector<std::string> dirs = { "Weapons", "Armors", "Ammos", "Exclusions", "Modifiers", "Omods", "Rules" };
        std::error_code ec;
        for (const auto& d : dirs) {
            if (!std::filesystem::exists(baseDir / d)) std::filesystem::create_directories(baseDir / d, ec);
        }

        try {
            for (const auto& entry : std::filesystem::directory_iterator(baseDir / "Rules")) {
                if (entry.path().extension() == ".json") {
                    try {
                        std::ifstream file(entry.path());
                        json j = json::parse(file, nullptr, true, true);
                        auto dataHandler = RE::TESDataHandler::GetSingleton();

                        // 新增：解析全局伤害公式配置
                        if (j.contains("DamageFormula") && j["DamageFormula"].is_object()) {
                            auto& df = j["DamageFormula"];
                            g_damageFormula.enabled = df.value("enabled", true);
                            g_damageFormula.baseHardness = df.value("baseHardness", 1.0f);
                            g_damageFormula.glanceThreshold = df.value("glanceThreshold", 0.3f);
                            g_damageFormula.glanceMultiplier = df.value("glanceMultiplier", 0.1f);
                            g_damageFormula.scratchThreshold = df.value("scratchThreshold", 0.7f);
                            g_damageFormula.scratchMultiplier = df.value("scratchMultiplier", 0.4f);
                            g_damageFormula.durabilityDamageConstant = df.value("durabilityDamageConstant", 0.15f);
                        }

                        if (j.contains("OverRepairRules") && j["OverRepairRules"].is_array()) {
                            for (const auto& r : j["OverRepairRules"]) {
                                OverRepairRule rule;
                                rule.ruleName = r.value("RuleName", "UnknownRule");
                                rule.avStep = r.value("AVStep", 25.0f);
                                if (r.contains("TargetKeywords")) {
                                    for (const auto& kwd : r["TargetKeywords"]) {
                                        if (kwd.is_string()) rule.targetKeywords.push_back(kwd.get<std::string>());
                                    }
                                }
                                if (r.contains("RequiredSkill")) {
                                    auto& pk = r["RequiredSkill"];
                                    rule.requiredSkill.type = pk.value("Type", "Perk");
                                    rule.requiredSkill.pluginName = pk.value("Plugin", "");
                                    std::string fID = pk.value("FormID", "");
                                    try { rule.requiredSkill.localFormID = std::stoul(fID, nullptr, 16); }
                                    catch (...) {}
                                    if (pk.contains("MinRank")) rule.requiredSkill.minRank = pk.value("MinRank", 0.0f);
                                    else if (pk.contains("MinLevel")) rule.requiredSkill.minRank = pk.value("MinLevel", 0.0f);
                                    if (dataHandler && !rule.requiredSkill.pluginName.empty() && rule.requiredSkill.localFormID != 0) {
                                        auto form = dataHandler->LookupForm(rule.requiredSkill.localFormID, rule.requiredSkill.pluginName);
                                        if (form) rule.requiredSkill.runtimeFormID = form->GetFormID();
                                    }
                                }
                                if (r.contains("RankLimits")) {
                                    for (const auto& limit : r["RankLimits"]) {
                                        if (limit.is_number()) rule.rankLimits.push_back(limit.get<float>());
                                    }
                                }
                                g_overRepairRules.push_back(rule);
                            }
                        }

                        if (j.contains("JuryRiggingRules") && j["JuryRiggingRules"].is_array()) {
                            for (const auto& r : j["JuryRiggingRules"]) {
                                JuryRiggingRule rule;
                                rule.ruleName = r.value("RuleName", "Unknown");
                                rule.description = r.value("Description", "");
                                rule.allowCrossCategory = r.value("AllowCrossCategory", false);
                                rule.materialConditionMultiplier = r.value("MaterialConditionMultiplier", 1.0f);
                                if (r.contains("MatchCategories")) {
                                    for (const auto& kwd : r["MatchCategories"]) {
                                        if (kwd.is_string()) rule.matchCategories.push_back(kwd.get<std::string>());
                                    }
                                }
                                rule.maxConditionCap = r.value("MaxConditionCap", 1.0f);
                                if (r.contains("TargetKeywords")) {
                                    for (const auto& kwd : r["TargetKeywords"]) {
                                        if (kwd.is_string()) rule.targetKeywords.push_back(kwd.get<std::string>());
                                    }
                                }
                                if (r.contains("RequiredSkill")) {
                                    auto& pk = r["RequiredSkill"];
                                    rule.requiredSkill.type = pk.value("Type", "Perk");
                                    rule.requiredSkill.pluginName = pk.value("Plugin", "");
                                    std::string fID = pk.value("FormID", "");
                                    try { rule.requiredSkill.localFormID = std::stoul(fID, nullptr, 16); }
                                    catch (...) {}
                                    if (pk.contains("MinRank")) rule.requiredSkill.minRank = pk.value("MinRank", 0.0f);
                                    else if (pk.contains("MinLevel")) rule.requiredSkill.minRank = pk.value("MinLevel", 0.0f);
                                    if (dataHandler && !rule.requiredSkill.pluginName.empty() && rule.requiredSkill.localFormID != 0) {
                                        auto form = dataHandler->LookupForm(rule.requiredSkill.localFormID, rule.requiredSkill.pluginName);
                                        if (form) rule.requiredSkill.runtimeFormID = form->GetFormID();
                                    }
                                }
                                g_juryRiggingRules.push_back(rule);
                            }
                        }

                        if (j.contains("LootBonusRule") && j["LootBonusRule"].is_object()) {
                            auto& r = j["LootBonusRule"];
                            g_lootBonusRule.enabled = r.value("Enabled", true);
                            g_lootBonusRule.globalCap = r.value("GlobalCap", 2.00f);
                            if (r.contains("RequiredAV")) {
                                auto& pk = r["RequiredAV"];
                                std::string plugin = pk.value("Plugin", "Fallout4.esm");
                                std::string fID = pk.value("FormID", "0x000002C8");  // Luck (was wrongly 0x000002CB = AttackConditionAlt2)
                                std::string combined = plugin + "|" + fID;
                                g_lootBonusRule.requiredAV.runtimeFormID = ParseFormID(combined);
                                if (g_lootBonusRule.requiredAV.runtimeFormID == 0) {
                                    try { g_lootBonusRule.requiredAV.runtimeFormID = std::stoul(fID, nullptr, 16); }
                                    catch (...) {}
                                }
                            }
                            if (r.contains("Tiers") && r["Tiers"].is_array()) {
                                for (const auto& t : r["Tiers"]) {
                                    LootBonusTier tier;
                                    tier.minAVLevel = t.value("MinAVLevel", 0.0f);
                                    tier.hitChance = t.value("HitChance", 0.0f);
                                    tier.bonusMinPct = t.value("BonusMinPct", 0.0f);
                                    tier.bonusMaxPct = t.value("BonusMaxPct", 0.0f);
                                    g_lootBonusRule.tiers.push_back(tier);
                                }
                                std::sort(g_lootBonusRule.tiers.begin(), g_lootBonusRule.tiers.end(), [](const auto& a, const auto& b) {
                                    return a.minAVLevel > b.minAVLevel;
                                    });
                            }
                        }
                    }
                    catch (...) {}
                }
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            REX::WARN("[ProfileManager] 无法读取 Rules 目录: {}", e.what());
        }

        // =====================================================================
        // 使用模板函数批量加载 profile 类型的 JSON 配置
        // 模板处理：目录遍历 → JSON 解析 → profiles 数组 → formIDs + keywords → push_back
        // =====================================================================
        LoadProfilesFromDirectory(baseDir, "Weapons", g_weaponProfiles,
            [](const json& p, WeaponProfile& profile) {
                profile.name = p.value("name", "UnknownWeapon");
                profile.priority = p.value("priority", 0);
                profile.maxDurability = p.value("maxDurability", 1000.0f);
                profile.degradeRate = p.value("degradeRate", 1.0f);
                profile.isExcluded = p.value("isExcluded", false);
                profile.canJam = p.value("canJam", true);
            });

        LoadProfilesFromDirectory(baseDir, "Armors", g_armorProfiles,
            [](const json& p, ArmorProfile& profile) {
                profile.name = p.value("name", "UnknownArmor");
                profile.priority = p.value("priority", 0);
                profile.maxDurability = p.value("maxDurability", 500.0f);
                profile.isExcluded = p.value("isExcluded", false);
                profile.useFlatDegrade = p.value("useFlatDegrade", false);
                profile.degradeRate = p.value("degradeRate", 0.0f);
            });

        LoadProfilesFromDirectory(baseDir, "Ammos", g_ammoProfiles,
            [](const json& p, AmmoProfile& profile) {
                profile.name = p.value("name", "UnknownAmmo");
                profile.weaponWearMult = p.value("weaponWearMult", 1.0f);
                profile.armorDamageMult = p.value("armorDamageMult", 1.0f);
            });

        LoadProfilesFromDirectory(baseDir, "Modifiers", g_modifierProfiles,
            [](const json& p, ModifierProfile& profile) {
                profile.name = p.value("name", "UnknownModifier");
                profile.priority = p.value("priority", 0);
                profile.degradeMult = p.value("degradeMult", 1.0f);
                profile.immuneToJam = p.value("immuneToJam", false);
            });

        LoadProfilesFromDirectory(baseDir, "Omods", g_omodProfiles,
            [](const json& p, OmodProfile& profile) {
                profile.name = p.value("name", "UnknownOmod");
                profile.priority = p.value("priority", 0);
                profile.degradeMult = p.value("degradeMult", 1.0f);
                profile.degradeRateFlat = p.value("degradeRateFlat", 0.0f);
                profile.maxDurabilityMult = p.value("maxDurabilityMult", 1.0f);
                profile.immuneToJam = p.value("immuneToJam", false);
            });

        try {
            for (const auto& entry : std::filesystem::directory_iterator(baseDir / "Exclusions")) {
                if (entry.path().extension() == ".json") {
                    try {
                        std::ifstream file(entry.path());
                        json j = json::parse(file, nullptr, true, true);

                        if (j.contains("rules") && j["rules"].is_array()) {
                            for (const auto& r : j["rules"]) {
                                OverrideRule rule;
                                rule.name = r.value("name", "Unnamed Override Rule");
                                rule.enableDurabilitySystem = r.value("enableDurabilitySystem", false);
                                rule.showDurabilityUI = r.value("showDurabilityUI", false);

                                if (r.contains("keywords") && r["keywords"].is_array()) {
                                    for (const auto& k : r["keywords"]) if (k.is_string()) rule.keywords.push_back(k.get<std::string>());
                                }
                                if (r.contains("formIDs") && r["formIDs"].is_array()) {
                                    for (const auto& f : r["formIDs"]) {
                                        if (f.is_string()) {
                                            std::uint32_t runtimeID = ParseFormID(f.get<std::string>());
                                            if (runtimeID != 0) rule.formIDs.push_back(runtimeID);
                                        }
                                    }
                                }
                                if (r.contains("plugins") && r["plugins"].is_array()) {
                                    for (const auto& pl : r["plugins"]) if (pl.is_string()) rule.plugins.push_back(pl.get<std::string>());
                                }
                                g_overrideRules.push_back(rule);
                            }
                        }
                        else {
                            OverrideRule legacyRule;
                            legacyRule.name = "Legacy Blacklist";
                            legacyRule.enableDurabilitySystem = false;
                            legacyRule.showDurabilityUI = false;

                            if (j.contains("keywords") && j["keywords"].is_array()) {
                                for (const auto& k : j["keywords"]) if (k.is_string()) legacyRule.keywords.push_back(k.get<std::string>());
                            }
                            if (j.contains("formIDs") && j["formIDs"].is_array()) {
                                for (const auto& f : j["formIDs"]) {
                                    if (f.is_string()) {
                                        std::uint32_t runtimeID = ParseFormID(f.get<std::string>());
                                        if (runtimeID != 0) legacyRule.formIDs.push_back(runtimeID);
                                    }
                                }
                            }
                            if (j.contains("plugins") && j["plugins"].is_array()) {
                                for (const auto& pl : j["plugins"]) if (pl.is_string()) legacyRule.plugins.push_back(pl.get<std::string>());
                            }

                            if (!legacyRule.keywords.empty() || !legacyRule.formIDs.empty() || !legacyRule.plugins.empty()) {
                                g_overrideRules.push_back(legacyRule);
                            }
                        }
                    }
                    catch (...) {}
                }
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            REX::WARN("[ProfileManager] 无法读取 Exclusions 目录: {}", e.what());
        }

        auto sortByPriority = [](const auto& a, const auto& b) { return a.priority > b.priority; };
        std::sort(g_weaponProfiles.begin(), g_weaponProfiles.end(), sortByPriority);
        std::sort(g_armorProfiles.begin(), g_armorProfiles.end(), sortByPriority);
        std::sort(g_modifierProfiles.begin(), g_modifierProfiles.end(), sortByPriority);
        std::sort(g_omodProfiles.begin(), g_omodProfiles.end(), sortByPriority);

        ConditionSystem::Repair::LoadRepairKitProfiles();

        REX::INFO("[ProfileManager] 加载完毕: 武器 {}, 护甲 {}, 改装件(OMOD) {}, 修饰器 {}, 全局特例覆盖规则 {}",
            g_weaponProfiles.size(), g_armorProfiles.size(), g_omodProfiles.size(), g_modifierProfiles.size(), g_overrideRules.size());
    }

    // =========================================================================
    // 模板：formID 精确匹配 + keyword AND-group 匹配
    // a_form 必须支持 GetFormID() 且可隐式转为 TESForm*（用于 HasKeywordString）
    // =========================================================================
    template<typename TProfile, typename TForm>
    TProfile MatchProfile(
        const std::vector<TProfile>& a_profiles,
        TForm* a_form,
        const TProfile& a_default)
    {
        if (!a_form) return a_default;
        std::uint32_t targetID = a_form->GetFormID();
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);

        // 优先按 formID 精确匹配
        for (const auto& profile : a_profiles) {
            if (!profile.formIDs.empty()) {
                if (std::find(profile.formIDs.begin(), profile.formIDs.end(), targetID) != profile.formIDs.end()) return profile;
            }
        }
        // 再按关键词 AND-group 匹配
        for (const auto& profile : a_profiles) {
            if (profile.keywords.empty()) continue;
            for (const auto& andGroup : profile.keywords) {
                bool andMatched = true;
                for (const auto& kwd : andGroup) {
                    if (!HasKeywordString(a_form, nullptr, kwd)) { andMatched = false; break; }
                }
                if (andMatched) return profile;
            }
        }
        return a_default;
    }

    WeaponProfile GetProfileForWeapon(RE::TESObjectWEAP* weap) {
        return MatchProfile(g_weaponProfiles, weap, g_defaultWeaponProfile);
    }

    ArmorProfile GetProfileForArmor(RE::TESObjectARMO* armor) {
        return MatchProfile(g_armorProfiles, armor, g_defaultArmorProfile);
    }

    AmmoProfile GetProfileForAmmo(RE::TESAmmo* a_ammo) {
        return MatchProfile(g_ammoProfiles, a_ammo, g_defaultAmmoProfile);
    }

    ModifierProfile GetProfileForModifier(RE::TESForm* omodForm) {
        return MatchProfile(g_modifierProfiles, omodForm, g_defaultModifierProfile);
    }

    OmodProfile GetProfileForOmod(RE::BGSMod::Attachment::Mod* omod) {
        return MatchProfile(g_omodProfiles, omod, g_defaultOmodProfile);
    }

    // =========================================================================
    // 共享：解析 RequiredSkill 的当前值（AV / Global / Perk）
    // =========================================================================
    static float ResolveSkillValue(RE::TESForm* skillForm, const std::string& type, RE::PlayerCharacter* player) {
        if (!skillForm || !player) return 0.0f;
        if (type == "AV") {
            if (auto avObj = skillForm->As<RE::ActorValueInfo>()) return player->GetActorValue(*avObj);
            if (auto globObj = skillForm->As<RE::TESGlobal>()) return globObj->value;
        } else {
            if (auto perkObj = skillForm->As<RE::BGSPerk>()) return static_cast<float>(player->GetPerkRank(perkObj));
        }
        return 0.0f;
    }

    float GetPlayerOverRepairLimit(RE::TESBoundObject* targetObj, RE::ExtraDataList* a_targetExtra) {
        if (!targetObj) return 1.0f;
        if (!g_mcmSettings.enableOverCondition.load()) return 1.0f;
        auto player = RE::PlayerCharacter::GetSingleton();
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);

        for (const auto& rule : g_overRepairRules) {
            bool isTargetMatch = false;
            for (const auto& kwd : rule.targetKeywords) {
                if (HasKeywordString(targetObj, a_targetExtra, kwd)) {
                    isTargetMatch = true; break;
                }
            }
            if (isTargetMatch) {
                std::uint32_t currentRank = 0;
                if (player && rule.requiredSkill.runtimeFormID != 0) {
                    auto skillForm = RE::TESForm::GetFormByID(rule.requiredSkill.runtimeFormID);
                    if (skillForm) {
                        float skillValue = ResolveSkillValue(skillForm, rule.requiredSkill.type, player);
                        if (rule.requiredSkill.type == "AV" && rule.avStep > 0.1f)
                            currentRank = static_cast<std::uint32_t>(std::floor(skillValue / rule.avStep));
                        else
                            currentRank = static_cast<std::uint32_t>(skillValue);
                    }
                }
                if (rule.rankLimits.empty()) return 1.0f;
                if (currentRank >= rule.rankLimits.size()) currentRank = static_cast<std::uint32_t>(rule.rankLimits.size() - 1);
                return rule.rankLimits[currentRank];
            }
        }
        return 1.0f;
    }

    JuryRigResult EvaluateJuryRigging(RE::TESBoundObject* a_target, RE::ExtraDataList* a_targetExtra, RE::TESBoundObject* a_material, RE::ExtraDataList* a_materialExtra) {
        if (!a_target || !a_material) return { false, 1.0f };
        if (a_target->GetFormID() == a_material->GetFormID()) return { true, 1.0f };

        auto player = RE::PlayerCharacter::GetSingleton();
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);

        for (const auto& rule : g_juryRiggingRules) {
            if (!rule.targetKeywords.empty()) {
                bool targetMatch = false;
                for (const auto& kwd : rule.targetKeywords) {
                    if (HasKeywordString(a_target, a_targetExtra, kwd)) { targetMatch = true; break; }
                }
                if (!targetMatch) continue;

                bool materialMatch = false;
                for (const auto& kwd : rule.targetKeywords) {
                    if (HasKeywordString(a_material, a_materialExtra, kwd)) { materialMatch = true; break; }
                }
                if (!materialMatch) continue;
            }

            if (!rule.matchCategories.empty()) {
                bool categoryFailed = false;
                for (const auto& cat : rule.matchCategories) {
                    bool tHas = HasKeywordString(a_target, a_targetExtra, cat);
                    bool mHas = HasKeywordString(a_material, a_materialExtra, cat);
                    if (tHas != mHas) { categoryFailed = true; break; }
                }
                if (categoryFailed) continue;
            }                            if (rule.requiredSkill.runtimeFormID != 0 && player) {
                                bool skillMet = false;
                                auto skillForm = RE::TESForm::GetFormByID(rule.requiredSkill.runtimeFormID);
                                if (skillForm && ResolveSkillValue(skillForm, rule.requiredSkill.type, player) >= rule.requiredSkill.minRank)
                                    skillMet = true;
                                if (!skillMet) continue;
            }

            return { true, rule.materialConditionMultiplier };
        }
        return { false, 1.0f };
    }

    std::string GetJuryRiggingSkillString(RE::TESBoundObject* a_target, RE::ExtraDataList* a_targetExtra) {
        if (!a_target) return "";
        auto player = RE::PlayerCharacter::GetSingleton();

        std::shared_lock<std::shared_mutex> lock(g_profileMutex);

        for (const auto& rule : g_juryRiggingRules) {
            bool targetMatch = false;
            for (const auto& kwd : rule.targetKeywords) {
                if (HasKeywordString(a_target, a_targetExtra, kwd)) { targetMatch = true; break; }
            }
            if (!targetMatch) continue;

            if (rule.requiredSkill.runtimeFormID != 0 && player) {
                auto skillForm = RE::TESForm::GetFormByID(rule.requiredSkill.runtimeFormID);
                if (skillForm) {
                    float skillValue = ResolveSkillValue(skillForm, rule.requiredSkill.type, player);
                    return LOC("$CSF_RepairSkillPrefix") + std::to_string(static_cast<int>(rule.requiredSkill.minRank)) + " / " + std::to_string(static_cast<int>(skillValue));
                }
            }
            else {
                return LOC("$CSF_NoSkillLimit");
            }
        }
        return LOC("$CSF_CrossCategoryNotSupported");
    }

    static ItemFlags GetItemFlagsImpl(RE::TESForm* a_form, bool a_applyGlobalConditionSwitch) {
        ItemFlags flags{ false, false };
        if (!a_form) return flags;

        if (a_form->Is(RE::ENUM_FORM_ID::kWEAP)) {
            auto weap = a_form->As<RE::TESObjectWEAP>();
            if (weap) {
                auto wType = weap->weaponData.type.get();
                if (wType == RE::WEAPON_TYPE::kGrenade || wType == RE::WEAPON_TYPE::kMine) {
                    flags = { false, false };
                }
                else {
                    flags = { true, true };
                }
            }
        }
        else if (a_form->Is(RE::ENUM_FORM_ID::kARMO)) {
            if (HasKeywordString(a_form, nullptr, "ArmorTypePower")) {
                flags = { false, true };
            }
            else {
                flags = { true, true };
            }
        }
        else if (a_form->Is(RE::ENUM_FORM_ID::kAMMO)) {
            if (a_form->GetFormID() == 0x00075FE4 || HasKeywordString(a_form, nullptr, "AmmoTypeFusionCore")) {
                flags = { false, true };
            }
        }

        std::uint32_t formID = a_form->GetFormID();
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);

        for (const auto& rule : g_overrideRules) {
            bool matched = false;

            for (auto id : rule.formIDs) {
                if (id == formID) { matched = true; break; }
            }
            if (!matched) {
                for (const auto& kwd : rule.keywords) {
                    if (HasKeywordString(a_form, nullptr, kwd)) { matched = true; break; }
                }
            }
            if (!matched && !rule.plugins.empty()) {
                auto dataHandler = RE::TESDataHandler::GetSingleton();
                if (dataHandler) {
                    for (const auto& pluginName : rule.plugins) {
                        const RE::TESFile* modInfo = dataHandler->LookupModByName(pluginName);
                        if (modInfo && modInfo->IsFormInMod(formID)) { matched = true; break; }
                    }
                }
            }

            if (matched) {
                flags.enableDurabilitySystem = rule.enableDurabilitySystem;
                flags.showDurabilityUI = rule.showDurabilityUI;
                if (a_applyGlobalConditionSwitch && a_form->Is(RE::ENUM_FORM_ID::kWEAP) && !g_mcmSettings.enableWeaponCondition.load()) {
                    flags = { false, false };
                } else if (a_applyGlobalConditionSwitch && a_form->Is(RE::ENUM_FORM_ID::kARMO) && !g_mcmSettings.enableArmorCondition.load()) {
                    flags = { false, false };
                }
                return flags;
            }
        }

        if (a_applyGlobalConditionSwitch && a_form->Is(RE::ENUM_FORM_ID::kWEAP) && !g_mcmSettings.enableWeaponCondition.load()) {
            flags = { false, false };
        } else if (a_applyGlobalConditionSwitch && a_form->Is(RE::ENUM_FORM_ID::kARMO) && !g_mcmSettings.enableArmorCondition.load()) {
            flags = { false, false };
        }

        return flags;
    }

    ItemFlags GetItemFlags(RE::TESForm* a_form) {
        return GetItemFlagsImpl(a_form, true);
    }

    ItemFlags GetItemFlagsRaw(RE::TESForm* a_form) {
        return GetItemFlagsImpl(a_form, false);
    }
}

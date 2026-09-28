#include "pch.h"
#include "ConditionCore.h"
#include "ClassificationManager.h"
#include "ProfileManager.h"
#include "ConditionMath.h"
#include "Repair/RepairSystem.h"
#include "Translation.h" 

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace ConditionSystem {
    const RE::TESFile* FindLoadedPlugin(std::string_view a_pluginName)
    {
        if (a_pluginName.empty()) return nullptr;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;

        // LookupModByName dereferences an internal lookup structure that is not
        // reliable during the GameDataReady callback on this runtime. The
        // active file array is already populated at this point and is the same
        // source used for load-order and compile-index resolution.
        for (auto* file : dataHandler->files) {
            if (!file || !file->IsActive()) continue;
            const auto filename = file->GetFilename();
            if (filename.size() == a_pluginName.size() &&
                _strnicmp(filename.data(), a_pluginName.data(), a_pluginName.size()) == 0)
                return file;
        }
        return nullptr;
    }

    namespace {
        // Classification is immutable between profile reloads, while the global
        // weapon/armor switches are applied after this cached base result.
        std::shared_mutex g_itemFlagsCacheMutex;
        std::unordered_map<RE::TESForm*, ItemFlags> g_itemFlagsCache;
    }

    struct ProviderConfigFile {
        std::filesystem::path path;
        std::string requiredPlugin;
        std::uint32_t loadOrder{ 0 };
        int providerPriority{ 0 };
    };

    enum class ProviderSelectionMode : std::uint8_t {
        JsonPriorityFirst,
        PluginLoadOrderFirst
    };

    static ProviderSelectionMode ReadProviderSelectionMode(
        const std::filesystem::path& a_selectionFile,
        std::string_view a_module)
    {
        try {
            std::ifstream file(a_selectionFile);
            if (!file.is_open()) {
                REX::WARN("[ProfileManager] Provider selection file '{}' is missing; using JsonPriorityFirst.",
                    a_selectionFile.string());
                return ProviderSelectionMode::JsonPriorityFirst;
            }
            const auto root = json::parse(file, nullptr, true, true);
            if (root.value("schemaVersion", 0) != 1 || !root.contains("modules") ||
                !root["modules"].is_object() || !root["modules"].contains(a_module)) {
                REX::WARN("[ProfileManager] Provider selection file '{}' has no valid '{}' policy; using JsonPriorityFirst.",
                    a_selectionFile.string(), a_module);
                return ProviderSelectionMode::JsonPriorityFirst;
            }
            const auto mode = root["modules"][a_module].value("mode", "");
            if (mode == "JsonPriorityFirst") return ProviderSelectionMode::JsonPriorityFirst;
            if (mode == "PluginLoadOrderFirst") return ProviderSelectionMode::PluginLoadOrderFirst;
            REX::WARN("[ProfileManager] Unknown Provider selection mode '{}' for '{}'; using JsonPriorityFirst.",
                mode, a_module);
        } catch (const std::exception& e) {
            REX::WARN("[ProfileManager] Failed to read Provider selection file '{}': {}",
                a_selectionFile.string(), e.what());
        }
        return ProviderSelectionMode::JsonPriorityFirst;
    }

    static std::optional<std::uint32_t> GetLoadedPluginOrder(
        const std::string& a_pluginName,
        RE::TESDataHandler* a_dataHandler)
    {
        if (!a_dataHandler || a_pluginName.empty()) return std::nullopt;

        std::uint32_t loadOrder = 0;
        for (auto* file : a_dataHandler->files) {
            if (!file) {
                ++loadOrder;
                continue;
            }

            const auto filename = file->GetFilename();
            if (file->IsActive() && filename.size() == a_pluginName.size() &&
                _strnicmp(filename.data(), a_pluginName.c_str(), filename.size()) == 0) {
                return loadOrder;
            }
            ++loadOrder;
        }
        return std::nullopt;
    }

    // Exactly one provider file is selected for a module. A higher plugin load
    // order wins, while canonical filenames let a mod manager resolve same-plugin
    // config conflicts before CSF sees the Data directory.
    static std::optional<ProviderConfigFile> SelectProviderConfig(
        const std::vector<std::filesystem::path>& a_rulesDirs,
        RE::TESDataHandler* a_dataHandler,
        ProviderSelectionMode a_mode)
    {
        if (!a_dataHandler) return std::nullopt;

        std::optional<ProviderConfigFile> selected;
        for (const auto& rulesDir : a_rulesDirs) {
            if (!std::filesystem::exists(rulesDir)) continue;
            try {
                for (const auto& entry : std::filesystem::directory_iterator(rulesDir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

                try {
                    std::ifstream file(entry.path());
                    const auto root = json::parse(file, nullptr, true, true);
                    const std::string requiredPlugin = root.value("RequiredPlugin", "");
                    const std::string expectedFilename = requiredPlugin + ".json";

                    if (requiredPlugin.empty()) {
                        REX::WARN("[ProfileManager] Ignoring provider '{}': RequiredPlugin is required.", entry.path().filename().string());
                        continue;
                    }
                    if (_stricmp(entry.path().filename().string().c_str(), expectedFilename.c_str()) != 0) {
                        REX::WARN("[ProfileManager] Ignoring provider '{}': expected canonical filename '{}'.", entry.path().filename().string(), expectedFilename);
                        continue;
                    }

                    const auto loadOrder = GetLoadedPluginOrder(requiredPlugin, a_dataHandler);
                    if (!loadOrder) {
                        REX::INFO("[ProfileManager] Provider '{}' skipped because '{}' is not loaded.", entry.path().filename().string(), requiredPlugin);
                        continue;
                    }

                    ProviderConfigFile candidate{
                        entry.path(), requiredPlugin, *loadOrder, root.value("ProviderPriority", 0)
                    };
                    const auto stronger = [&](const ProviderConfigFile& left, const ProviderConfigFile& right) {
                        if (a_mode == ProviderSelectionMode::JsonPriorityFirst) {
                            if (left.providerPriority != right.providerPriority)
                                return left.providerPriority > right.providerPriority;
                            if (left.loadOrder != right.loadOrder) return left.loadOrder > right.loadOrder;
                        } else {
                            if (left.loadOrder != right.loadOrder) return left.loadOrder > right.loadOrder;
                            if (left.providerPriority != right.providerPriority)
                                return left.providerPriority > right.providerPriority;
                        }
                        return left.path.filename().string() < right.path.filename().string();
                    };
                    if (!selected || stronger(candidate, *selected)) {
                        selected = std::move(candidate);
                    }
                }
                catch (const std::exception& e) {
                    REX::WARN("[ProfileManager] Failed to inspect provider '{}': {}", entry.path().filename().string(), e.what());
                }
            }
        }
            catch (const std::filesystem::filesystem_error& e) {
                REX::WARN("[ProfileManager] Unable to scan provider directory '{}': {}", rulesDir.string(), e.what());
            }
        }

        if (selected) {
            REX::INFO("[ProfileManager] Selected {} provider '{}' (plugin='{}', ProviderPriority={}, loadOrder={}).",
                selected->path.parent_path().filename().string(), selected->path.filename().string(),
                selected->requiredPlugin, selected->providerPriority, selected->loadOrder);
        }
        return selected;
    }

    static std::optional<json> ReadProviderConfig(
        const std::optional<ProviderConfigFile>& a_provider,
        std::string_view a_moduleName)
    {
        if (!a_provider) return std::nullopt;
        try {
            std::ifstream file(a_provider->path);
            if (!file.is_open()) {
                REX::WARN("[ProfileManager] Unable to open selected {} provider '{}'.", a_moduleName, a_provider->path.string());
                return std::nullopt;
            }
            return json::parse(file, nullptr, true, true);
        }
        catch (const std::exception& e) {
            REX::WARN("[ProfileManager] Failed to parse selected {} provider '{}': {}", a_moduleName, a_provider->path.string(), e.what());
            return std::nullopt;
        }
    }


    std::shared_mutex g_profileMutex;

    std::vector<WeaponProfile> g_weaponProfiles;
    std::vector<ArmorProfile> g_armorProfiles;
    std::vector<AmmoProfile> g_ammoProfiles;
    std::vector<ModifierProfile> g_modifierProfiles;
    std::vector<OmodProfile> g_omodProfiles;
    std::vector<DurabilityCategoryModifier> g_durabilityCategoryModifiers;
    std::vector<SkillWearModifier> g_skillWearModifiers;

    std::vector<OverRepairRule> g_overRepairRules;
    std::vector<JuryRiggingRule> g_juryRiggingRules;
    LootBonusRule g_lootBonusRule;

    // 新增：公式配置实例初始化
    DamageFormulaConfig g_damageFormula;

    WeaponProfile g_defaultWeaponProfile{ "DefaultWeapon", 0, 1000.0f, 1.0f, false, true, WeaponMechanism::Auto, {}, {}, {} };
    ArmorProfile g_defaultArmorProfile{ "DefaultArmor", 0, 500.0f, false, false, 0.0f, {}, {}, {} };
    AmmoProfile g_defaultAmmoProfile{ "DefaultAmmo", 1.0f, 1.0f, {}, {} };
    ModifierProfile g_defaultModifierProfile{ "DefaultModifier", 0, 1.0f, false, {}, {} };
    OmodProfile g_defaultOmodProfile{ "DefaultOmod", 0, 1.0f, 0.0f, 1.0f, false, {}, {} };

    std::uint32_t ParseFormID(const std::string& a_identifier) {
        auto pos = a_identifier.find('|');
        if (pos != std::string::npos) {
            std::string plugin = a_identifier.substr(0, pos);
            std::string hexStr = a_identifier.substr(pos + 1);
            try {
                const auto rawID = static_cast<RE::TESFormID>(std::stoul(hexStr, nullptr, 16));
                auto dataHandler = RE::TESDataHandler::GetSingleton();
                if (!dataHandler) return 0;
                const auto mod = FindLoadedPlugin(plugin);
                if (!mod) return 0;

                // Accept both plugin-local IDs and full load-order IDs without
                // entering TESDataHandler's name lookup path during
                // GameDataReady. The loaded TESFile already owns the compile
                // indices needed to construct the runtime FormID.
                std::uint32_t fullID = 0;
                if (mod->IsLight()) {
                    constexpr std::uint32_t kLightPrefix = 0xFE000000u;
                    const auto smallFileIndex =
                        static_cast<std::uint32_t>(mod->GetSmallFileCompileIndex()) & 0x0FFFu;
                    const bool hasLightPrefix = (rawID & 0xFF000000u) == kLightPrefix;
                    if (hasLightPrefix) {
                        const auto encodedIndex = (rawID >> 12) & 0x0FFFu;
                        if (encodedIndex != smallFileIndex) return 0;
                        fullID = rawID;
                    } else {
                        fullID = kLightPrefix | (smallFileIndex << 12) | (rawID & 0x0FFFu);
                    }
                } else {
                    const auto compileIndex =
                        static_cast<std::uint32_t>(mod->GetCompileIndex()) & 0xFFu;
                    const auto encodedIndex = rawID >> 24;
                    fullID = encodedIndex == compileIndex
                        ? rawID
                        : (compileIndex << 24) | (rawID & 0x00FFFFFFu);
                }

                if (fullID != 0 && RE::TESForm::GetFormByID(fullID)) return fullID;
            }
            catch (...) {}
        }
        else {
            try {
                return std::stoul(a_identifier, nullptr, 16);
            }
            catch (...) {
                if (auto form = RE::TESForm::GetFormByEditorID(RE::BSFixedString(a_identifier))) {
                    return form->GetFormID();
                }
            }
        }
        return 0;
    }

    static std::string NormalizeSkillType(std::string a_type) {
        a_type.erase(std::remove_if(a_type.begin(), a_type.end(), [](unsigned char c) {
            return std::isspace(c) || c == '_' || c == '-';
        }), a_type.end());
        std::transform(a_type.begin(), a_type.end(), a_type.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (a_type == "global") {
            return "Global";
        }
        if (a_type == "av" || a_type == "actorvalue" || a_type == "actorvalueinfo") {
            return "AV";
        }
        return "Perk";
    }

    static void LoadSkillRequirement(const json& a_node, SkillRequirement& a_requirement, RE::TESDataHandler* a_dataHandler, std::string_view a_ruleName) {
        a_requirement.configured = true;
        a_requirement.type = NormalizeSkillType(a_node.value("Type", "Perk"));
        a_requirement.pluginName = a_node.value("Plugin", "");
        const std::string editorID = a_node.value("EditorID", "");
        const std::string formID = a_node.value("FormID", "");

        try {
            a_requirement.localFormID = std::stoul(formID, nullptr, 16);
        }
        catch (...) {
            a_requirement.localFormID = 0;
        }

        if (a_node.contains("MinRank")) {
            a_requirement.minRank = a_node.value("MinRank", 0.0f);
        }
        else if (a_node.contains("MinLevel")) {
            a_requirement.minRank = a_node.value("MinLevel", 0.0f);
        }

        if (a_node.contains("ScaleFactor")) {
            a_requirement.scaleFactor = a_node.value("ScaleFactor", 1.0f);
        }
        if (!std::isfinite(a_requirement.scaleFactor) || a_requirement.scaleFactor <= 0.0f) {
            REX::WARN("[ProfileManager] Invalid ScaleFactor for rule '{}'; using 1.0.", a_ruleName);
            a_requirement.scaleFactor = 1.0f;
        }
        if (!editorID.empty()) {
            if (auto form = RE::TESForm::GetFormByEditorID(RE::BSFixedString(editorID))) {
                a_requirement.runtimeFormID = form->GetFormID();
            }
        }
        else if (a_requirement.localFormID != 0) {
            if (a_dataHandler && !a_requirement.pluginName.empty()) {
                a_requirement.runtimeFormID = ParseFormID(a_requirement.pluginName + "|" + formID);
            }
            else {
                // Plugin-less values are full runtime IDs, useful for Fallout4.esm forms.
                a_requirement.runtimeFormID = a_requirement.localFormID;
            }
        }

        if (a_requirement.runtimeFormID == 0) {
            REX::WARN("[ProfileManager] RequiredSkill unresolved: rule='{}', type='{}', plugin='{}', form='{}'", a_ruleName, a_requirement.type, a_requirement.pluginName, formID);
        }
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
    template<typename T, typename F, typename Filter = std::nullptr_t>
    void LoadProfilesFromDirectory(
        const std::filesystem::path& a_baseDir,
        const std::string& a_subDir,
        std::vector<T>& a_outProfiles,
        F&& a_parseFields,
        std::string_view a_wrapperKey = "profiles",
        bool a_allowFlatFallback = false,
        Filter&& a_filenameFilter = nullptr)
    {
        // 内部 lambda：处理单个 JSON profile 对象，抽取通用字段
        auto ProcessSingle = [&](const json& p) {
            T profile;
            a_parseFields(p, profile);

            // 通用字段：formIDs
            // 支持两种格式：数组 ["0x123", "0x456"] 或对象 {"Plugin.esm": ["0x123"]}
            // 注意：必须用 if constexpr 守卫，因为并非所有 T 都有 formIDs（如 OverRepairRule）
            if constexpr (requires { std::declval<T>().formIDs; }) {
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

            // 通用字段：plugins（插件名过滤器，用于 Exclusions 等模块）
            if constexpr (requires { std::declval<T>().plugins; }) {
                if (p.contains("plugins") && p["plugins"].is_array()) {
                    for (const auto& pl : p["plugins"]) {
                        if (pl.is_string()) profile.plugins.push_back(pl.get<std::string>());
                    }
                }
            }

            a_outProfiles.push_back(profile);
        };

        try {
            for (const auto& entry : std::filesystem::directory_iterator(a_baseDir / a_subDir)) {
                if (entry.path().extension() != ".json") continue;
                // 可选的快速名称过滤（用于 Perk_/AV_ 前缀筛选等）
                if constexpr (!std::is_same_v<std::decay_t<Filter>, std::nullptr_t>) {
                    if (!a_filenameFilter(entry.path().filename().string())) continue;
                }
                try {
                    std::ifstream file(entry.path());
                    json j = json::parse(file, nullptr, true, true);
                    std::string wrapper(a_wrapperKey);

                    // 标准模式：从 wrapper 数组中解析
                    if (j.contains(wrapper) && j[wrapper].is_array()) {
                        for (const auto& p : j[wrapper]) {
                            ProcessSingle(p);
                        }
                    }
                    // 平坦后备模式：整个 JSON 对象视为单个 profile（兼容旧版 Exclusions 格式）
                    else if (a_allowFlatFallback && j.is_object()) {
                        ProcessSingle(j);
                    }
                }
                catch (...) {}
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            REX::WARN("[ProfileManager] 无法读取 {} 目录: {}", a_subDir, e.what());
        }
    }

    static void ReadCategoryArray(const json& a_node, const char* a_key, std::vector<std::string>& a_output) {
        if (!a_node.contains(a_key) || !a_node[a_key].is_array()) return;
        for (const auto& value : a_node[a_key]) {
            if (value.is_string()) a_output.push_back(value.get<std::string>());
        }
    }

    static CategoryPredicate ReadCategoryPredicate(const json& a_node) {
        CategoryPredicate result;
        if (!a_node.is_object()) return result;
        ReadCategoryArray(a_node, "all", result.all);
        ReadCategoryArray(a_node, "any", result.any);
        ReadCategoryArray(a_node, "none", result.none);
        return result;
    }

    static bool MatchesCategoryPredicate(const Classification::Result& a_categories, const CategoryPredicate& a_predicate) {
        for (const auto& category : a_predicate.all) {
            if (!a_categories.Has(category)) return false;
        }
        if (!a_predicate.any.empty() && std::none_of(a_predicate.any.begin(), a_predicate.any.end(), [&](const auto& category) {
                return a_categories.Has(category);
            })) return false;
        return std::none_of(a_predicate.none.begin(), a_predicate.none.end(), [&](const auto& category) {
            return a_categories.Has(category);
        });
    }

    static bool ReadJuryCompatibility(const json& a_node, JuryRiggingRule& a_rule) {
        if (!a_node.is_object() || !a_node.contains("all") || !a_node["all"].is_array()) return false;
        for (const auto& entry : a_node["all"]) {
            if (!entry.is_object()) return false;
            const auto type = entry.value("type", "");
            JuryCompatibilityCondition condition;
            if (type == "always") condition.type = JuryCompatibilityType::Always;
            else if (type == "sameBase") condition.type = JuryCompatibilityType::SameBase;
            else if (type == "sameGroup") condition.type = JuryCompatibilityType::SameGroup;
            else if (type == "sameOptionalGroup") condition.type = JuryCompatibilityType::SameOptionalGroup;
            else if (type == "sharesAnyCategory") condition.type = JuryCompatibilityType::SharesAnyCategory;
            else if (type == "groupPair") condition.type = JuryCompatibilityType::GroupPair;
            else return false;

            condition.group = entry.value("group", "");
            condition.symmetric = entry.value("symmetric", false);
            if ((condition.type == JuryCompatibilityType::SameGroup ||
                    condition.type == JuryCompatibilityType::SameOptionalGroup ||
                    condition.type == JuryCompatibilityType::SharesAnyCategory ||
                    condition.type == JuryCompatibilityType::GroupPair) &&
                condition.group.empty()) return false;

            if (condition.type == JuryCompatibilityType::GroupPair) {
                const auto& pairs = entry.value("pairs", json::array());
                if (!pairs.is_array() || pairs.empty()) return false;
                for (const auto& pair : pairs) {
                    if (!pair.is_array() || pair.size() != 2 || !pair[0].is_string() || !pair[1].is_string()) return false;
                    condition.pairs.emplace_back(pair[0].get<std::string>(), pair[1].get<std::string>());
                }
            }
            a_rule.compatibility.push_back(std::move(condition));
        }
        return !a_rule.compatibility.empty();
    }

    static bool MatchesCompatibility(
        const JuryRiggingRule& a_rule,
        RE::TESBoundObject* a_target,
        RE::TESBoundObject* a_material,
        const Classification::Result& a_targetCategories,
        const Classification::Result& a_materialCategories)
    {
        for (const auto& condition : a_rule.compatibility) {
            if (condition.type == JuryCompatibilityType::Always) continue;
            if (condition.type == JuryCompatibilityType::SameBase) {
                if (a_target->GetFormID() != a_material->GetFormID()) return false;
                continue;
            }

            const auto target = a_targetCategories.GetSelected(condition.group);
            const auto material = a_materialCategories.GetSelected(condition.group);
            if (condition.type == JuryCompatibilityType::SharesAnyCategory) {
                const auto prefix = condition.group + ".";
                const bool shared = std::any_of(a_targetCategories.categories.begin(), a_targetCategories.categories.end(),
                    [&](const auto& category) {
                        return category.starts_with(prefix) &&
                            std::find(a_materialCategories.categories.begin(), a_materialCategories.categories.end(), category) !=
                                a_materialCategories.categories.end();
                    });
                if (!shared) return false;
                continue;
            }
            if (condition.type == JuryCompatibilityType::SameOptionalGroup) {
                if (target.has_value() != material.has_value()) return false;
                if (target && *target != *material) return false;
                continue;
            }
            if (!target || !material) return false;
            if (condition.type == JuryCompatibilityType::SameGroup) {
                if (*target != *material) return false;
                continue;
            }

            const auto pairMatches = [&](const auto& pair) {
                return *target == pair.first && *material == pair.second;
            };
            const auto reversePairMatches = [&](const auto& pair) {
                return *target == pair.second && *material == pair.first;
            };
            if (std::none_of(condition.pairs.begin(), condition.pairs.end(), pairMatches) &&
                (!condition.symmetric || std::none_of(condition.pairs.begin(), condition.pairs.end(), reversePairMatches))) return false;
        }
        return true;
    }

    static std::string DescribeCategories(const Classification::Result& a_categories) {
        std::string result;
        for (const auto& category : a_categories.categories) {
            if (!result.empty()) result += ',';
            result += category;
        }
        return result.empty() ? "<none>" : result;
    }

    static void LogJuryEvaluation(
        RE::TESBoundObject* a_target,
        RE::TESBoundObject* a_material,
        const Classification::Result& a_targetCategories,
        const Classification::Result& a_materialCategories,
        const JuryRigResult& a_result)
    {
        if (!g_mcmSettings.enableLogging.load()) return;

        // A repair-menu refresh compares many inventory stacks. Keep the
        // diagnostic useful without turning an enabled debug log into a flood.
        static std::atomic_uint32_t loggedCount{ 0 };
        if (loggedCount.fetch_add(1) >= 160) return;

        REX::INFO(
            "[JuryDiag] target={:08X} categories=[{}] material={:08X} categories=[{}] allowed={} rule='{}' priority={}",
            a_target ? a_target->GetFormID() : 0,
            DescribeCategories(a_targetCategories),
            a_material ? a_material->GetFormID() : 0,
            DescribeCategories(a_materialCategories),
            a_result.allowed,
            a_result.matchedRule,
            a_result.priority);
    }

    template<typename T, typename F>
    void LoadProfilesFromPaths(
        const std::vector<std::filesystem::path>& a_paths,
        std::vector<T>& a_outProfiles,
        F&& a_parseFields,
        std::string_view a_wrapperKey = "profiles",
        bool a_allowFlatFallback = false)
    {
        for (const auto& path : a_paths) {
            if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) continue;
            LoadProfilesFromDirectory<T>(
                path.parent_path(), path.filename().string(), a_outProfiles,
                a_parseFields, a_wrapperKey, a_allowFlatFallback);
        }
    }

    void LoadRepairKitProfilesFromTemplate(std::vector<RepairKitProfile>& a_out) {
        const auto parseProfile = [](const json& p, RepairKitProfile& profile) {
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
        };

        constexpr std::string_view canonicalPath =
            "Data\\F4SE\\Plugins\\ConditionSystemFramework\\Repair\\RepairKits";

        LoadProfilesFromPaths<RepairKitProfile>({ std::filesystem::path(canonicalPath) }, a_out, parseProfile);

        REX::INFO("[ProfileManager] Loaded {} repair-kit profiles.", a_out.size());
    }

    void LoadAllProfiles() {
        std::unique_lock<std::shared_mutex> lock(g_profileMutex);
        {
            std::unique_lock<std::shared_mutex> cacheLock(g_itemFlagsCacheMutex);
            g_itemFlagsCache.clear();
        }
        g_weaponProfiles.clear();
        g_armorProfiles.clear();
        g_ammoProfiles.clear();
        g_modifierProfiles.clear();
        g_omodProfiles.clear();
        g_durabilityCategoryModifiers.clear();
        g_skillWearModifiers.clear();
        g_overRepairRules.clear();
        g_juryRiggingRules.clear();
        g_lootBonusRule = LootBonusRule();

        // 恢复默认公式
        g_damageFormula = DamageFormulaConfig();

        auto baseDir = std::filesystem::path("Data\\F4SE\\Plugins\\ConditionSystemFramework");
        const auto consumptionDir = baseDir / "Durability" / "Consumption";
        const auto repairDir = baseDir / "Repair";
        const auto lootDir = baseDir / "Loot";
        const auto durabilityDir = baseDir / "Durability";
        std::vector<std::filesystem::path> dirs = {
            consumptionDir / "Weapons", consumptionDir / "Armors", consumptionDir / "Ammos",
            consumptionDir / "CategoryModifiers", consumptionDir / "Modifiers", consumptionDir / "Omods", consumptionDir / "SkillModifiers",
            repairDir / "OverRepair", repairDir / "JuryRigging", repairDir / "RepairKits",
            lootDir / "ConditionBonus", durabilityDir / "DamageFormula", durabilityDir / "Jamming"
        };
        std::error_code ec;
        for (const auto& dir : dirs) {
            if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir, ec);
        }

        // Classifications are a shared foundation. Every contribution whose
        // dependencies are loaded is merged before consumer modules are read.
        Classification::Load();

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        const auto repairSelectionFile = repairDir / "ProviderSelection.json";
        const auto lootSelectionFile = lootDir / "ProviderSelection.json";
        const auto overRepairSelection =
            ReadProviderSelectionMode(repairSelectionFile, "OverRepair");
        const auto juryRiggingSelection =
            ReadProviderSelectionMode(repairSelectionFile, "JuryRigging");
        const auto conditionBonusSelection =
            ReadProviderSelectionMode(lootSelectionFile, "ConditionBonus");

        // Each module selects one provider. Different providers are resolved by
        // actual plugin load order; same-provider config conflicts are resolved
        // by the mod manager through the canonical <RequiredPlugin>.json name.
        if (const auto root = ReadProviderConfig(
                SelectProviderConfig({ repairDir / "OverRepair" }, dataHandler, overRepairSelection),
                "OverRepairRules")) {
            const auto& rules = *root;
            if (!rules.contains("OverRepairRules") || !rules["OverRepairRules"].is_array()) {
                REX::WARN("[ProfileManager] Selected OverRepairRules provider does not contain an OverRepairRules array.");
            }
            else for (const auto& p : rules["OverRepairRules"]) {
                OverRepairRule rule;
                rule.ruleName = p.value("RuleName", "UnknownRule");
                if (p.contains("AppliesTo") && p["AppliesTo"].is_object()) {
                    const auto& appliesTo = p["AppliesTo"];
                    if (appliesTo.contains("categories") && appliesTo["categories"].is_array()) {
                        for (const auto& category : appliesTo["categories"]) {
                            if (category.is_string()) rule.targetCategories.push_back(category.get<std::string>());
                        }
                    }
                }
                if (p.contains("RequiredSkill") && p["RequiredSkill"].is_object()) {
                    LoadSkillRequirement(p["RequiredSkill"], rule.requiredSkill, dataHandler, rule.ruleName);
                }
                if (p.contains("SkillThresholds")) {
                    for (const auto& tier : p["SkillThresholds"]) {
                        if (tier.is_object() && tier.contains("threshold") && tier.contains("maxValue")) {
                            OverRepairRule::SkillThreshold st;
                            st.threshold = tier.value("threshold", 0.0f);
                            st.maxValue = tier.value("maxValue", 1.0f);
                            if (!std::isfinite(st.threshold) || !std::isfinite(st.maxValue) || st.maxValue < 0.0f) {
                                REX::WARN("[ProfileManager] Ignoring invalid SkillThreshold in OverRepair rule '{}'.", rule.ruleName);
                                continue;
                            }
                            rule.skillThresholds.push_back(st);
                        }
                    }
                }
                std::sort(rule.skillThresholds.begin(), rule.skillThresholds.end(), [](const auto& lhs, const auto& rhs) {
                    return lhs.threshold < rhs.threshold;
                });
                if (rule.targetCategories.empty() || !rule.requiredSkill.configured || rule.skillThresholds.empty()) {
                    REX::WARN("[ProfileManager] Ignoring incomplete OverRepair rule '{}'.", rule.ruleName);
                    continue;
                }
                g_overRepairRules.push_back(std::move(rule));
            }
        }

        if (const auto root = ReadProviderConfig(
                SelectProviderConfig({ repairDir / "JuryRigging" }, dataHandler, juryRiggingSelection),
                "JuryRiggingRules")) {
            const auto& rules = *root;
            if (!rules.contains("JuryRiggingRules") || !rules["JuryRiggingRules"].is_array()) {
                REX::WARN("[ProfileManager] Selected JuryRiggingRules provider does not contain a JuryRiggingRules array.");
            }
            else for (const auto& p : rules["JuryRiggingRules"]) {
                JuryRiggingRule rule;
                rule.ruleName = p.value("RuleName", "Unknown");
                rule.description = p.value("Description", "");
                rule.priority = p.value("Priority", 0);
                rule.materialConditionMultiplier = p.value("MaterialConditionMultiplier", 1.0f);
                if (!std::isfinite(rule.materialConditionMultiplier) || rule.materialConditionMultiplier < 0.0f) {
                    REX::WARN("[ProfileManager] Ignoring JuryRigging rule '{}' with invalid MaterialConditionMultiplier.", rule.ruleName);
                    continue;
                }
                const auto& appliesTo = p.value("AppliesTo", json::object());
                if (!appliesTo.is_object() || !appliesTo.contains("target") || !appliesTo.contains("material")) {
                    REX::WARN("[ProfileManager] Ignoring JuryRigging rule '{}' without AppliesTo.target/material predicates.", rule.ruleName);
                    continue;
                }
                rule.targetPredicate = ReadCategoryPredicate(appliesTo["target"]);
                rule.materialPredicate = ReadCategoryPredicate(appliesTo["material"]);
                if (!ReadJuryCompatibility(p.value("Compatibility", json::object()), rule)) {
                    REX::WARN("[ProfileManager] Ignoring JuryRigging rule '{}' with invalid Compatibility.all.", rule.ruleName);
                    continue;
                }
                if (const auto& modifiers = p.value("EfficiencyModifiers", json::array());
                    modifiers.is_array()) {
                    for (const auto& modifier : modifiers) {
                        if (!modifier.is_object() ||
                            modifier.value("type", "") != "optionalGroupMultiplier") {
                            REX::WARN("[ProfileManager] Ignoring JuryRigging rule '{}' with invalid EfficiencyModifier.",
                                rule.ruleName);
                            rule.efficiencyModifiers.clear();
                            break;
                        }
                        JuryEfficiencyModifier value;
                        value.group = modifier.value("group", "");
                        value.same = modifier.value("same", 1.0f);
                        value.different = modifier.value("different", 1.0f);
                        value.oneMissing = modifier.value("oneMissing", 1.0f);
                        if (value.group.empty() || !std::isfinite(value.same) ||
                            !std::isfinite(value.different) || !std::isfinite(value.oneMissing) ||
                            value.same < 0.0f || value.different < 0.0f || value.oneMissing < 0.0f) {
                            REX::WARN("[ProfileManager] Ignoring invalid EfficiencyModifier in '{}'.", rule.ruleName);
                            rule.efficiencyModifiers.clear();
                            break;
                        }
                        rule.efficiencyModifiers.push_back(std::move(value));
                    }
                }
                if (p.contains("RequiredSkill")) {
                    LoadSkillRequirement(p["RequiredSkill"], rule.requiredSkill, dataHandler, rule.ruleName);
                }
                if (p.contains("SkillThresholds")) {
                    for (const auto& tier : p["SkillThresholds"]) {
                        if (tier.is_object() && tier.contains("threshold") && tier.contains("maxValue")) {
                            JuryRiggingRule::SkillThreshold st;
                            st.threshold = tier.value("threshold", 0.0f);
                            st.maxValue = tier.value("maxValue", 1.0f);
                            if (!std::isfinite(st.threshold) || !std::isfinite(st.maxValue) || st.maxValue < 0.0f) {
                                REX::WARN("[ProfileManager] Ignoring invalid SkillThreshold in JuryRigging rule '{}'.", rule.ruleName);
                                continue;
                            }
                            rule.skillThresholds.push_back(st);
                        }
                    }
                }
                std::sort(rule.skillThresholds.begin(), rule.skillThresholds.end(), [](const auto& lhs, const auto& rhs) {
                    return lhs.threshold < rhs.threshold;
                });
                if (rule.requiredSkill.configured && rule.skillThresholds.empty()) {
                    REX::WARN("[ProfileManager] Ignoring JuryRigging rule '{}' because its configured skill has no thresholds.", rule.ruleName);
                    continue;
                }
                g_juryRiggingRules.push_back(std::move(rule));
            }
        }

        if (const auto root = ReadProviderConfig(
                SelectProviderConfig({ lootDir / "ConditionBonus" }, dataHandler, conditionBonusSelection),
                "LootBonusRules")) {
            const auto& j = *root;
            auto& r = j.contains("LootBonusRule") && j["LootBonusRule"].is_object()
                ? j["LootBonusRule"] : j;
            if (!r.is_object()) {
                REX::WARN("[ProfileManager] Selected LootBonusRules provider does not contain a LootBonusRule object.");
            }
            else {

            LootBonusRule candidate;
            candidate.globalCap = r.value("GlobalCap", 2.00f);

            if (r.contains("RequiredSkill")) {
                LoadSkillRequirement(r["RequiredSkill"], candidate.requiredAV, dataHandler, "LootBonus");
            }
            else if (r.contains("RequiredAV")) {
                auto& pk = r["RequiredAV"];
                candidate.requiredAV.configured = true;
                candidate.requiredAV.type = "AV";
                candidate.requiredAV.pluginName = pk.value("Plugin", "Fallout4.esm");
                std::string fID = pk.value("FormID", "0x000002C8");
                std::string combined = candidate.requiredAV.pluginName + "|" + fID;
                candidate.requiredAV.runtimeFormID = ParseFormID(combined);
                if (candidate.requiredAV.runtimeFormID == 0) {
                    try { candidate.requiredAV.runtimeFormID = std::stoul(fID, nullptr, 16); }
                    catch (...) {}
                }
                if (candidate.requiredAV.runtimeFormID == 0) {
                    REX::WARN("[ProfileManager] LootBonus RequiredAV unresolved: plugin='{}', form='{}'",
                        candidate.requiredAV.pluginName, fID);
                } else {
                    REX::INFO("[ProfileManager] LootBonus using legacy RequiredAV format");
                }
            }

            if (r.contains("Tiers") && r["Tiers"].is_array()) {
                for (const auto& t : r["Tiers"]) {
                    LootBonusTier tier;
                    tier.minLevel = t.value("minLevel", t.value("MinAVLevel", 0.0f));
                    tier.hitChance = t.value("hitChance", t.value("HitChance", 0.0f));
                    tier.bonusMin = t.value("bonusMin", t.value("BonusMinPct", 0.0f));
                    tier.bonusMax = t.value("bonusMax", t.value("BonusMaxPct", 0.0f));
                    candidate.tiers.push_back(tier);
                }
                std::sort(candidate.tiers.begin(), candidate.tiers.end(), [](const auto& a, const auto& b) {
                    return a.minLevel > b.minLevel;
                });
            }

            if (!candidate.requiredAV.configured || candidate.tiers.empty()) {
                REX::WARN("[ProfileManager] Ignoring incomplete LootBonus rule set.");
            }
            else {
                g_lootBonusRule = std::move(candidate);
            }
            }
        }

        // --- 加载 DamageFormula ---
        try {
            const auto formulaDir = durabilityDir / "DamageFormula";
            if (std::filesystem::exists(formulaDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(formulaDir)) {
                if (entry.path().extension() != ".json") continue;
                if (std::filesystem::is_directory(entry.path())) continue;
                try {
                    std::ifstream file(entry.path());
                    json j = json::parse(file, nullptr, true, true);
                    if (j.contains("DamageFormula") && j["DamageFormula"].is_object()) {
                        auto& df = j["DamageFormula"];
                        g_damageFormula.enabled = df.value("enabled", true);
                        g_damageFormula.baseHardness = df.value("baseHardness", 1.0f);
                        g_damageFormula.glanceThreshold = df.value("glanceThreshold", 0.7f);
                        g_damageFormula.glanceMultiplier = df.value("glanceMultiplier", 0.1f);
                        g_damageFormula.scratchThreshold = df.value("scratchThreshold", 0.3f);
                        g_damageFormula.scratchMultiplier = df.value("scratchMultiplier", 0.4f);
                        g_damageFormula.durabilityDamageConstant = df.value("durabilityDamageConstant", 0.15f);
                        Math::NormalizePhaseThresholds(g_damageFormula.glanceThreshold, g_damageFormula.scratchThreshold);
                    }
                }
                catch (...) {}
            }
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            REX::WARN("[ProfileManager] 无法读取 DamageFormula 配置目录: {}", e.what());
        }

        // =====================================================================
        // 使用模板函数批量加载 profile 类型的 JSON 配置
        // 模板处理：目录遍历 → JSON 解析 → profiles 数组 → formIDs + keywords → push_back
        // =====================================================================
        LoadProfilesFromPaths<WeaponProfile>({ consumptionDir / "Weapons" }, g_weaponProfiles,
            [](const json& p, WeaponProfile& profile) {
                profile.name = p.value("name", "UnknownWeapon");
                profile.priority = p.value("priority", 0);
                profile.maxDurability = p.value("maxDurability", 1000.0f);
                profile.degradeRate = p.value("degradeRate", 1.0f);
                profile.isExcluded = p.value("isExcluded", false);
                profile.canJam = p.value("canJam", true);
                if (p.contains("mechanism") && p["mechanism"].is_string()) {
                    std::string mechanism = p["mechanism"].get<std::string>();
                    std::transform(mechanism.begin(), mechanism.end(), mechanism.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (mechanism == "ballistic" || mechanism == "ballistic_jam" || mechanism == "jam") {
                        profile.mechanism = WeaponMechanism::BallisticJam;
                    } else if (mechanism == "energy" || mechanism == "energy_fault" || mechanism == "fault") {
                        profile.mechanism = WeaponMechanism::EnergyFault;
                    } else if (mechanism == "overheat" || mechanism == "heat") {
                        profile.mechanism = WeaponMechanism::Overheat;
                    } else if (mechanism == "none" || mechanism == "disabled" || mechanism == "off") {
                        profile.mechanism = WeaponMechanism::None;
                    } else if (mechanism == "auto" || mechanism == "default") {
                        profile.mechanism = WeaponMechanism::Auto;
                    } else {
                        REX::WARN("[ProfileManager] 未知 weapon mechanism '{}', 使用 auto", mechanism);
                    }
                }
                profile.appliesTo = ReadCategoryPredicate(p.value("AppliesTo", json::object()));
            });

        LoadProfilesFromPaths<ArmorProfile>({ consumptionDir / "Armors" }, g_armorProfiles,
            [](const json& p, ArmorProfile& profile) {
                profile.name = p.value("name", "UnknownArmor");
                profile.priority = p.value("priority", 0);
                profile.maxDurability = p.value("maxDurability", 500.0f);
                profile.isExcluded = p.value("isExcluded", false);
                profile.useFlatDegrade = p.value("useFlatDegrade", false);
                profile.degradeRate = p.value("degradeRate", 0.0f);
                profile.appliesTo = ReadCategoryPredicate(p.value("AppliesTo", json::object()));
            });

        LoadProfilesFromPaths<AmmoProfile>({ consumptionDir / "Ammos" }, g_ammoProfiles,
            [](const json& p, AmmoProfile& profile) {
                profile.name = p.value("name", "UnknownAmmo");
                profile.weaponWearMult = p.value("weaponWearMult", 1.0f);
                profile.armorDamageMult = p.value("armorDamageMult", 1.0f);
            });

        LoadProfilesFromPaths<ModifierProfile>({ consumptionDir / "Modifiers" }, g_modifierProfiles,
            [](const json& p, ModifierProfile& profile) {
                profile.name = p.value("name", "UnknownModifier");
                profile.priority = p.value("priority", 0);
                profile.degradeMult = p.value("degradeMult", 1.0f);
                profile.immuneToJam = p.value("immuneToJam", false);
            });

        LoadProfilesFromPaths<OmodProfile>({ consumptionDir / "Omods" }, g_omodProfiles,
            [](const json& p, OmodProfile& profile) {
                profile.name = p.value("name", "UnknownOmod");
                profile.priority = p.value("priority", 0);
                profile.degradeMult = p.value("degradeMult", 1.0f);
                profile.degradeRateFlat = p.value("degradeRateFlat", 0.0f);
                profile.maxDurabilityMult = p.value("maxDurabilityMult", 1.0f);
                profile.immuneToJam = p.value("immuneToJam", false);
            });

        LoadProfilesFromPaths<DurabilityCategoryModifier>({ consumptionDir / "CategoryModifiers" }, g_durabilityCategoryModifiers,
            [](const json& p, DurabilityCategoryModifier& rule) {
                rule.name = p.value("name", "UnknownCategoryModifier");
                rule.domain = p.value("Domain", "Any");
                rule.priority = p.value("priority", 0);
                rule.appliesTo = ReadCategoryPredicate(p.value("AppliesTo", json::object()));
                rule.maxDurabilityMult = p.value("maxDurabilityMult", 1.0f);
                rule.degradeMult = p.value("degradeMult", 1.0f);
                rule.degradeRateFlat = p.value("degradeRateFlat", 0.0f);
            },
            "modifiers");

        LoadProfilesFromPaths<SkillWearModifier>({ consumptionDir / "SkillModifiers" }, g_skillWearModifiers,
            [&](const json& p, SkillWearModifier& rule) {
                rule.name = p.value("name", "UnknownSkillWearModifier");
                rule.domain = p.value("Domain", "Any");
                rule.priority = p.value("priority", 0);
                rule.appliesTo = ReadCategoryPredicate(p.value("AppliesTo", json::object()));
                if (p.contains("RequiredSkill") && p["RequiredSkill"].is_object()) {
                    LoadSkillRequirement(p["RequiredSkill"], rule.requiredSkill, dataHandler, rule.name);
                }
                if (!p.contains("SkillThresholds") || !p["SkillThresholds"].is_array()) return;
                for (const auto& tier : p["SkillThresholds"]) {
                    if (!tier.is_object()) continue;
                    SkillWearModifier::Threshold parsed;
                    parsed.threshold = tier.value("threshold", 0.0f);
                    parsed.wearMultiplier = tier.value("wearMultiplier", 1.0f);
                    parsed.wearRateFlat = tier.value("wearRateFlat", 0.0f);
                    if (!std::isfinite(parsed.threshold) || !std::isfinite(parsed.wearMultiplier) ||
                        !std::isfinite(parsed.wearRateFlat) || parsed.wearMultiplier < 0.0f) {
                        REX::WARN("[ProfileManager] Ignoring invalid SkillWear threshold in '{}'.", rule.name);
                        continue;
                    }
                    rule.thresholds.push_back(parsed);
                }
            },
            "modifiers");

        auto sortByPriority = [](const auto& a, const auto& b) { return a.priority > b.priority; };
        std::sort(g_weaponProfiles.begin(), g_weaponProfiles.end(), sortByPriority);
        std::sort(g_armorProfiles.begin(), g_armorProfiles.end(), sortByPriority);
        std::sort(g_modifierProfiles.begin(), g_modifierProfiles.end(), sortByPriority);
        std::sort(g_omodProfiles.begin(), g_omodProfiles.end(), sortByPriority);
        std::sort(g_durabilityCategoryModifiers.begin(), g_durabilityCategoryModifiers.end(), sortByPriority);
        std::sort(g_skillWearModifiers.begin(), g_skillWearModifiers.end(), sortByPriority);
        REX::INFO("[ProfileManager] Loaded {} skill wear modifiers.", g_skillWearModifiers.size());

        ConditionSystem::Repair::LoadRepairKitProfiles();

        REX::INFO("[ProfileManager] 加载完毕: 武器 {}, 护甲 {}, 分类耐久修正 {}, 改装件(OMOD) {}, 修饰器 {}, 过量维修规则 {}, 混修规则 {}, 战利品加成 {}",
            g_weaponProfiles.size(), g_armorProfiles.size(), g_durabilityCategoryModifiers.size(), g_omodProfiles.size(), g_modifierProfiles.size(),
            g_overRepairRules.size(), g_juryRiggingRules.size(), g_lootBonusRule.tiers.size());
    }

    // =========================================================================
    // 模板：formID 精确匹配 + keyword AND-group 匹配
    // a_form 必须支持 GetFormID() 且可隐式转为 TESForm*（用于 HasKeywordString）
    // =========================================================================
    template<typename TProfile, typename TForm>
    TProfile MatchProfile(
        const std::vector<TProfile>& a_profiles,
        TForm* a_form,
        RE::ExtraDataList* a_extraList,
        const TProfile& a_default,
        const Classification::Result* a_cachedCategories = nullptr)
    {
        if (!a_form) return a_default;
        std::uint32_t targetID = a_form->GetFormID();
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);

        // 分类条件是耐久基础 Profile 的首要匹配依据，并使用当前实例结果。
        if constexpr (requires { std::declval<TProfile>().appliesTo; }) {
            const auto categories = a_cachedCategories
                ? *a_cachedCategories
                : Classification::Evaluate(a_form, a_extraList);
            for (const auto& profile : a_profiles) {
                const bool hasPredicate = !profile.appliesTo.all.empty() ||
                    !profile.appliesTo.any.empty() || !profile.appliesTo.none.empty();
                if (hasPredicate && MatchesCategoryPredicate(categories, profile.appliesTo)) return profile;
            }
        }

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
                    if (!HasKeywordString(a_form, a_extraList, kwd)) { andMatched = false; break; }
                }
                if (andMatched) return profile;
            }
        }
        return a_default;
    }

    static bool DurabilityDomainMatches(const std::string& a_domain, bool a_weapon) {
        return a_domain == "Any" || (a_weapon && a_domain == "Weapon") || (!a_weapon && a_domain == "Armor");
    }

    WeaponProfile GetProfileForWeapon(RE::TESObjectWEAP* weap, RE::ExtraDataList* a_extraList) {
        const auto categories = Classification::Evaluate(weap, a_extraList);
        auto profile = MatchProfile(g_weaponProfiles, weap, a_extraList, g_defaultWeaponProfile, &categories);
        if (!weap) return profile;
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);
        for (const auto& modifier : g_durabilityCategoryModifiers) {
            const bool hasPredicate = !modifier.appliesTo.all.empty() ||
                !modifier.appliesTo.any.empty() || !modifier.appliesTo.none.empty();
            if (!DurabilityDomainMatches(modifier.domain, true) ||
                !hasPredicate ||
                !MatchesCategoryPredicate(categories, modifier.appliesTo)) continue;
            profile.maxDurability *= modifier.maxDurabilityMult;
            profile.degradeRate = profile.degradeRate * modifier.degradeMult + modifier.degradeRateFlat;
        }
        return profile;
    }

    ArmorProfile GetProfileForArmor(RE::TESObjectARMO* armor, RE::ExtraDataList* a_extraList) {
        const auto categories = Classification::Evaluate(armor, a_extraList);
        auto profile = MatchProfile(g_armorProfiles, armor, a_extraList, g_defaultArmorProfile, &categories);
        if (!armor) return profile;
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);
        for (const auto& modifier : g_durabilityCategoryModifiers) {
            const bool hasPredicate = !modifier.appliesTo.all.empty() ||
                !modifier.appliesTo.any.empty() || !modifier.appliesTo.none.empty();
            if (!DurabilityDomainMatches(modifier.domain, false) ||
                !hasPredicate ||
                !MatchesCategoryPredicate(categories, modifier.appliesTo)) continue;
            profile.maxDurability *= modifier.maxDurabilityMult;
            profile.degradeRate = profile.degradeRate * modifier.degradeMult + modifier.degradeRateFlat;
        }
        return profile;
    }

    AmmoProfile GetProfileForAmmo(RE::TESAmmo* a_ammo) {
        return MatchProfile(g_ammoProfiles, a_ammo, nullptr, g_defaultAmmoProfile);
    }

    ModifierProfile GetProfileForModifier(RE::TESForm* omodForm) {
        return MatchProfile(g_modifierProfiles, omodForm, nullptr, g_defaultModifierProfile);
    }

    OmodProfile GetProfileForOmod(RE::BGSMod::Attachment::Mod* omod) {
        return MatchProfile(g_omodProfiles, omod, nullptr, g_defaultOmodProfile);
    }

    float GetEffectiveMaxDurability(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList) {
        if (!a_object) return 0.0f;
        float maximum = 0.0f;
        if (auto* weapon = a_object->As<RE::TESObjectWEAP>()) {
            maximum = GetProfileForWeapon(weapon, a_extraList).maxDurability;
        } else if (auto* armor = a_object->As<RE::TESObjectARMO>()) {
            maximum = GetProfileForArmor(armor, a_extraList).maxDurability;
        } else {
            return 0.0f;
        }

        const float fallbackMaximum = a_object->Is(RE::ENUM_FORM_ID::kWEAP)
            ? g_defaultWeaponProfile.maxDurability : g_defaultArmorProfile.maxDurability;
        maximum = Math::ValidMaximum(maximum, fallbackMaximum);
        if (!a_extraList) return maximum;
        const auto* instance = a_extraList->GetByType<RE::BGSObjectInstanceExtra>();
        if (!instance || !instance->values || instance->itemIndex == static_cast<std::uint16_t>(-1)) return maximum;
        for (const auto& indexData : instance->GetIndexData()) {
            if (indexData.disabled) continue;
            auto* form = RE::TESForm::GetFormByID(indexData.objectID);
            auto* omod = form ? form->As<RE::BGSMod::Attachment::Mod>() : nullptr;
            if (omod) maximum *= GetProfileForOmod(omod).maxDurabilityMult;
        }
        return Math::ValidMaximum(maximum, fallbackMaximum);
    }

    // =========================================================================
    // 共享：解析 RequiredSkill 的当前值（AV / Global / Perk）
    // =========================================================================
    static bool TryResolveSkillValue(RE::TESForm* a_skillForm, const std::string& a_type, RE::PlayerCharacter* a_player, float& a_value) {
        a_value = 0.0f;
        if (!a_skillForm || !a_player) return false;

        if (a_type == "AV") {
            if (auto avObj = a_skillForm->As<RE::ActorValueInfo>()) {
                a_value = a_player->GetActorValue(*avObj);
                return true;
            }
            return false;
        }

        if (a_type == "Global") {
            if (auto globObj = a_skillForm->As<RE::TESGlobal>()) {
                a_value = globObj->GetValue();
                return true;
            }
            return false;
        }

        if (auto perkObj = a_skillForm->As<RE::BGSPerk>()) {
            a_value = static_cast<float>(a_player->GetPerkRank(perkObj));
            return true;
        }
        return false;
    }

bool TryGetSkillRequirementValue(const SkillRequirement& a_requirement, RE::PlayerCharacter* a_player, float& a_value) {
        if (!a_requirement.configured) {
            a_value = 0.0f;
            return true;
        }
        if (!a_player || a_requirement.runtimeFormID == 0) return false;
        auto skillForm = RE::TESForm::GetFormByID(a_requirement.runtimeFormID);
        if (!skillForm) return false;
        float rawValue = 0.0f;
        if (!TryResolveSkillValue(skillForm, a_requirement.type, a_player, rawValue)) return false;
        a_value = rawValue * a_requirement.scaleFactor;
        return true;
    }

    static bool TryGetRequiredSkillValue(const SkillRequirement& a_requirement, RE::PlayerCharacter* a_player, float& a_value) {
        return TryGetSkillRequirementValue(a_requirement, a_player, a_value);
    }

    void GetSkillWearEffects(
        RE::TESBoundObject* a_object,
        RE::ExtraDataList* a_extraList,
        bool a_weapon,
        float& a_multiplier,
        float& a_flat)
    {
        a_multiplier = 1.0f;
        a_flat = 0.0f;
        if (!a_object) return;

        const auto categories = Classification::Evaluate(a_object, a_extraList);
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);
        for (const auto& modifier : g_skillWearModifiers) {
            if (!DurabilityDomainMatches(modifier.domain, a_weapon)) continue;
            const bool hasPredicate = !modifier.appliesTo.all.empty() ||
                !modifier.appliesTo.any.empty() || !modifier.appliesTo.none.empty();
            if (hasPredicate && !MatchesCategoryPredicate(categories, modifier.appliesTo)) continue;
            if (modifier.thresholds.empty()) continue;

            float skillValue = 0.0f;
            if (!TryGetRequiredSkillValue(modifier.requiredSkill, player, skillValue) || !std::isfinite(skillValue)) continue;

            if (skillValue < modifier.requiredSkill.minRank) continue;
            const auto* selected = Math::SelectSkillThreshold(modifier.thresholds, skillValue);
            if (!selected) continue;
            a_multiplier *= selected->wearMultiplier;
            a_flat += selected->wearRateFlat;
        }
    }

    float GetPlayerOverRepairLimit(RE::TESBoundObject* targetObj, RE::ExtraDataList* a_targetExtra) {
        if (!targetObj) return 1.0f;
        if (!g_mcmSettings.enableOverCondition.load()) return 1.0f;
        auto player = RE::PlayerCharacter::GetSingleton();
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);

        for (const auto& rule : g_overRepairRules) {
            if (Classification::HasAnyCategory(targetObj, a_targetExtra, rule.targetCategories)) {
                float skillValue = 0.0f;
                if (TryGetRequiredSkillValue(rule.requiredSkill, player, skillValue)) {
                    if (!std::isfinite(skillValue)) {
                        REX::WARN("[DEBUG-LIMIT-b673] Non-finite skill value in OverRepair rule '{}'; using normal repair limit.", rule.ruleName);
                        return 1.0f;
                    }
                    float result = 1.0f;
                    for (auto it = rule.skillThresholds.rbegin(); it != rule.skillThresholds.rend(); ++it) {
                        if (skillValue >= it->threshold) {
                            result = it->maxValue;
                            break;
                        }
                    }
                    if (!std::isfinite(result) || result < 0.0f) {
                        REX::WARN("[DEBUG-LIMIT-b673] Non-finite OverRepair limit in rule '{}'; using normal repair limit.", rule.ruleName);
                        return 1.0f;
                    }
                    return result;
                }
                // The selected provider can still contain an optional rule whose
                // record is unavailable. Keep scanning for a later valid rule.
                continue;
            }
        }
        return 1.0f;
    }


    float GetJuryRepairLimit(RE::TESBoundObject* targetObj, RE::ExtraDataList* a_targetExtra) {
        float methodCap = g_mcmSettings.juryRepairCap.load();
        if (!std::isfinite(methodCap) || methodCap < 0.0f) {
            REX::WARN("[DEBUG-LIMIT-b673] Invalid Jury repair cap {}; using 1.0.", methodCap);
            methodCap = 1.0f;
        }
        const float skillLimit = GetPlayerOverRepairLimit(targetObj, a_targetExtra);
        const float result = (std::min)(methodCap, skillLimit);
        return std::isfinite(result) && result >= 0.0f ? result : 1.0f;
    }

    float GetWorkbenchRepairLimit(RE::TESBoundObject* targetObj, RE::ExtraDataList* a_targetExtra) {
        float methodCap = g_mcmSettings.workbenchRepairCap.load();
        if (!std::isfinite(methodCap) || methodCap < 0.0f) {
            REX::WARN("[DEBUG-LIMIT-b673] Invalid workbench repair cap {}; using 2.0.", methodCap);
            methodCap = 2.0f;
        }
        const float skillLimit = GetPlayerOverRepairLimit(targetObj, a_targetExtra);
        const float result = (std::min)(methodCap, skillLimit);
        if (!std::isfinite(result) || result < 0.0f) {
            REX::WARN("[DEBUG-LIMIT-b673] Invalid workbench repair limit (cap={}, skill={}); using 1.0.", methodCap, skillLimit);
            return 1.0f;
        }
        return result;
    }

    float GetRepairKitLimit(float a_kitMaxConditionLimit) {
        float kitCap = a_kitMaxConditionLimit;
        if (!std::isfinite(kitCap) || kitCap < 0.0f) {
            REX::WARN("[DEBUG-LIMIT-b673] Invalid repair-kit limit {}; using 1.0.", kitCap);
            return 1.0f;
        }
        if (!g_mcmSettings.enableOverCondition.load() && kitCap > 1.0f) {
            kitCap = 1.0f;
        }
        return std::max(0.0f, kitCap);
    }

    JuryRigResult EvaluateJuryRigging(RE::TESBoundObject* a_target, RE::ExtraDataList* a_targetExtra, RE::TESBoundObject* a_material, RE::ExtraDataList* a_materialExtra) {
        if (!a_target || !a_material) return { false, 1.0f };

        auto player = RE::PlayerCharacter::GetSingleton();
        std::shared_lock<std::shared_mutex> lock(g_profileMutex);
        const auto targetCategories = Classification::Evaluate(a_target, a_targetExtra);
        const auto materialCategories = Classification::Evaluate(a_material, a_materialExtra);
        JuryRigResult bestResult;

        for (const auto& rule : g_juryRiggingRules) {
            if (!MatchesCategoryPredicate(targetCategories, rule.targetPredicate) ||
                !MatchesCategoryPredicate(materialCategories, rule.materialPredicate)) continue;
            if (!MatchesCompatibility(rule, a_target, a_material, targetCategories, materialCategories)) continue;

            if (rule.requiredSkill.configured) {
                float skillValue = 0.0f;
                if (!TryGetRequiredSkillValue(rule.requiredSkill, player, skillValue) || (rule.skillThresholds.empty() ? false : skillValue < rule.skillThresholds[0].threshold)) {
                    continue;
                }
            }

            float efficiency = rule.materialConditionMultiplier;
            for (const auto& modifier : rule.efficiencyModifiers) {
                const auto target = targetCategories.GetSelected(modifier.group);
                const auto material = materialCategories.GetSelected(modifier.group);
                if (target && material) efficiency *= (*target == *material) ? modifier.same : modifier.different;
                else if (target || material) efficiency *= modifier.oneMissing;
                else efficiency *= modifier.same;
            }
            if (!std::isfinite(efficiency) || efficiency < 0.0f) continue;
            if (!bestResult.allowed || rule.priority > bestResult.priority ||
                (rule.priority == bestResult.priority &&
                    (efficiency > bestResult.efficiencyMult ||
                        (efficiency == bestResult.efficiencyMult && rule.ruleName < bestResult.matchedRule)))) {
                bestResult = { true, efficiency, rule.priority, rule.ruleName };
            }
        }
        LogJuryEvaluation(a_target, a_material, targetCategories, materialCategories, bestResult);
        return bestResult;
    }

    std::string GetJuryRiggingSkillString(RE::TESBoundObject* a_target, RE::ExtraDataList* a_targetExtra) {
        if (!a_target) return "";
        auto player = RE::PlayerCharacter::GetSingleton();
        bool foundUnavailableRequirement = false;

        std::shared_lock<std::shared_mutex> lock(g_profileMutex);
        const auto categories = Classification::Evaluate(a_target, a_targetExtra);
        const JuryRiggingRule* bestRule = nullptr;
        for (const auto& rule : g_juryRiggingRules) {
            if (!MatchesCategoryPredicate(categories, rule.targetPredicate)) continue;
            if (!rule.requiredSkill.configured) continue;
            if (!bestRule || rule.priority > bestRule->priority ||
                (rule.priority == bestRule->priority && rule.ruleName < bestRule->ruleName)) bestRule = &rule;
        }
        if (bestRule) {
            float skillValue = 0.0f;
            if (TryGetRequiredSkillValue(bestRule->requiredSkill, player, skillValue)) {
                const std::string srPrefix = LOC("$CSF_RepairSkillPrefix");
                const float minThreshold = bestRule->skillThresholds.empty() ? 0.0f : bestRule->skillThresholds[0].threshold;
                return srPrefix + std::to_string(static_cast<int>(minThreshold)) + " / " + std::to_string(static_cast<int>(skillValue));
            }
            foundUnavailableRequirement = true;
        }
        if (!bestRule) return LOC("$CSF_NoSkillLimit");
        if (foundUnavailableRequirement) return LOC("$CSF_SkillRequirementUnavailable");
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

        bool cached = false;
        {
            std::shared_lock<std::shared_mutex> cacheLock(g_itemFlagsCacheMutex);
            if (const auto it = g_itemFlagsCache.find(a_form); it != g_itemFlagsCache.end()) {
                flags = it->second;
                cached = true;
            }
        }

        if (!cached) {
            if (auto* object = a_form->As<RE::TESBoundObject>(); object) {
                const auto ignoreCategory = a_form->Is(RE::ENUM_FORM_ID::kWEAP)
                    ? "Weapon.Flag.Ignore"
                    : "Armor.Flag.Ignore";
                if (Classification::HasCategory(object, nullptr, ignoreCategory)) {
                    flags = { false, false };
                }
                if (a_form->Is(RE::ENUM_FORM_ID::kARMO) &&
                    Classification::HasCategory(object, nullptr, "Equipment.Domain.PowerArmor")) {
                    flags = { false, false };
                }
            }

            std::unique_lock<std::shared_mutex> cacheLock(g_itemFlagsCacheMutex);
            g_itemFlagsCache.insert_or_assign(a_form, flags);
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

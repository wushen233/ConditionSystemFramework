#include "pch.h"
#include "ArmorConceptRegistry.h"
#include "ProfileManager.h"

#include <RE/B/BGSListForm.h>
#include <RE/B/BGSMod.h>
#include <RE/B/BGSObjectInstanceExtra.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace ConditionSystem::ArmorConcepts
{
    namespace
    {
        enum class Role : std::uint8_t { Unknown, Size, Tier, Lining, Weave, Paint, Legendary, Misc };

        struct RoleRegistration
        {
            Role role{ Role::Unknown };
            std::unordered_map<std::string, int> priorities;
        };

        struct Source
        {
            std::string provider;
            std::string listID;
            int providerPriority{ 0 };
            std::uint32_t pluginLoadOrder{ 0 };
            int listPriority{ 0 };
            bool baseContext{ false };
            bool instanceContext{ false };
            std::unordered_set<RE::BGSKeyword*> keywords;
            std::unordered_set<RE::BGSMod::Attachment::Mod*> omods;
            std::unordered_set<RE::TESObjectARMO*> armors;
        };

        struct Concept
        {
            std::string group;
            std::string category;
            std::vector<Source> sources;
        };

        std::shared_mutex g_mutex;
        std::vector<Concept> g_concepts;
        std::unordered_map<RE::BGSKeyword*, RoleRegistration> g_attachRoles;
        std::unordered_set<std::string> g_registeredLists;
        std::mutex g_conflictLogMutex;
        std::unordered_set<std::string> g_loggedConflicts;
        bool g_jsonPriorityFirst{ true };
        int g_baseContextPriority{ 100 };
        int g_instanceContextPriority{ 200 };

        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        bool DependenciesLoaded(const json& root)
        {
            if (!root.contains("requires") || !root["requires"].is_array()) return false;
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) return false;
            for (const auto& dependency : root["requires"]) {
                if (!dependency.is_string() || !FindLoadedPlugin(dependency.get<std::string>())) return false;
            }
            return true;
        }

        std::uint32_t ProviderLoadOrder(const json& root)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) return 0;
            std::uint32_t result = 0;
            std::uint32_t index = 0;
            for (auto* file : data->files) {
                if (file && file->IsActive()) {
                    for (const auto& dependency : root["requires"]) {
                        const auto name = dependency.get<std::string>();
                        const auto filename = file->GetFilename();
                        if (filename.size() == name.size() &&
                            _strnicmp(filename.data(), name.c_str(), filename.size()) == 0)
                            result = std::max(result, index);
                    }
                }
                ++index;
            }
            return result;
        }

        int CompareSelection(const Selection& left, const Selection& right)
        {
            if (g_jsonPriorityFirst) {
                if (left.providerPriority != right.providerPriority)
                    return left.providerPriority > right.providerPriority ? 1 : -1;
                if (left.pluginLoadOrder != right.pluginLoadOrder)
                    return left.pluginLoadOrder > right.pluginLoadOrder ? 1 : -1;
            } else {
                if (left.pluginLoadOrder != right.pluginLoadOrder)
                    return left.pluginLoadOrder > right.pluginLoadOrder ? 1 : -1;
                if (left.providerPriority != right.providerPriority)
                    return left.providerPriority > right.providerPriority ? 1 : -1;
            }
            if (left.contextPriority != right.contextPriority)
                return left.contextPriority > right.contextPriority ? 1 : -1;
            if (left.modRolePriority != right.modRolePriority)
                return left.modRolePriority > right.modRolePriority ? 1 : -1;
            if (left.listPriority != right.listPriority)
                return left.listPriority > right.listPriority ? 1 : -1;
            return 0;
        }

        std::optional<std::pair<std::string, std::string>> ParseConcept(std::string id)
        {
            const auto dot = id.rfind('.');
            if (dot == std::string::npos) return std::nullopt;
            auto dimension = id.substr(0, dot);
            auto value = id.substr(dot + 1);
            if (dimension == "Armor.Material" &&
                (value == "Textile" || value == "Flexible" || value == "Rigid" || value == "Scrap"))
                return std::pair{ dimension, id };
            if (dimension == "Armor.Region" &&
                (value == "Torso" || value == "Limbs" || value == "Head" || value == "Face" || value == "Accessory"))
                return std::pair{ dimension, id };
            if (dimension == "Armor.Grade" && (value == "A" || value == "B" || value == "C"))
                return std::pair{ dimension, id };
            if (id == "Armor.Flag.Ignore") return std::pair{ "Armor.Flag", id };
            if (id == "Equipment.Domain.PowerArmor") return std::pair{ "Equipment.Domain", id };
            return std::nullopt;
        }

        Role ParseRole(std::string value)
        {
            value = Lower(std::move(value));
            if (value == "size") return Role::Size;
            if (value == "tier") return Role::Tier;
            if (value == "lining") return Role::Lining;
            if (value == "weave") return Role::Weave;
            if (value == "paint") return Role::Paint;
            if (value == "legendary") return Role::Legendary;
            if (value == "misc") return Role::Misc;
            return Role::Unknown;
        }

        RE::BGSKeyword* GetAttachPoint(RE::BGSMod::Attachment::Mod* omod)
        {
            if (!omod || omod->attachPoint.keywordIndex == 0xFFFF) return nullptr;
            return RE::detail::BGSKeywordGetTypedKeywordByIndex(
                RE::KeywordType::kAttachPoint, omod->attachPoint.keywordIndex);
        }

        const RoleRegistration* GetRole(RE::BGSMod::Attachment::Mod* omod)
        {
            const auto attachPoint = GetAttachPoint(omod);
            if (const auto it = g_attachRoles.find(attachPoint); it != g_attachRoles.end()) return std::addressof(it->second);
            return nullptr;
        }

        int RolePriority(const RoleRegistration* role, std::string_view group)
        {
            if (!role) return -1;
            if (const auto it = role->priorities.find(std::string(group)); it != role->priorities.end()) return it->second;
            return -1;
        }

        std::vector<RE::BGSMod::Attachment::Mod*> GetInstalledOmods(RE::ExtraDataList* extra)
        {
            std::vector<RE::BGSMod::Attachment::Mod*> result;
            if (!extra) return result;
            auto* instance = extra->GetByType<RE::BGSObjectInstanceExtra>();
            if (!instance || !instance->values || instance->itemIndex == static_cast<std::uint16_t>(-1)) return result;
            for (const auto& index : instance->GetIndexData()) {
                if (index.disabled) continue;
                auto* form = RE::TESForm::GetFormByID(index.objectID);
                if (auto* omod = form ? form->As<RE::BGSMod::Attachment::Mod>() : nullptr) result.push_back(omod);
            }
            return result;
        }

        std::unordered_set<RE::BGSKeyword*> GetKeywords(RE::BGSKeywordForm* form)
        {
            std::unordered_set<RE::BGSKeyword*> result;
            if (!form || !form->keywords) return result;
            for (std::uint32_t i = 0; i < form->numKeywords; ++i)
                if (form->keywords[i]) result.insert(form->keywords[i]);
            return result;
        }

        void CompileList(
            RE::BGSListForm* list,
            Source& source,
            bool allowArmor,
            bool roleList,
            std::unordered_set<std::uint32_t>& visiting)
        {
            if (!list || !visiting.insert(list->GetFormID()).second) {
                if (list) REX::WARN("[ArmorConcepts] Nested FLST cycle detected at {:08X} in '{}'.", list->GetFormID(), source.listID);
                return;
            }
            list->ForEachForm([&](RE::TESForm* form) {
                if (!form) return RE::BSContainer::ForEachResult::kContinue;
                if (auto* nested = form->As<RE::BGSListForm>()) {
                    CompileList(nested, source, allowArmor, roleList, visiting);
                } else if (auto* keyword = form->As<RE::BGSKeyword>()) {
                    source.keywords.insert(keyword);
                } else if (!roleList && !allowArmor) {
                    if (auto* omod = form->As<RE::BGSMod::Attachment::Mod>()) source.omods.insert(omod);
                    else REX::WARN("[ArmorConcepts] Ignoring {:08X} ({}) in '{}'; classification FLST accepts KYWD/OMOD only.",
                        form->GetFormID(), static_cast<int>(form->GetFormType()), source.listID);
                } else if (allowArmor) {
                    if (auto* armor = form->As<RE::TESObjectARMO>()) source.armors.insert(armor);
                    else REX::WARN("[ArmorConcepts] Ignoring {:08X} ({}) in '{}'; ArmorIgnore accepts KYWD/ARMO/nested FLST only.",
                        form->GetFormID(), static_cast<int>(form->GetFormType()), source.listID);
                } else {
                    REX::WARN("[ArmorConcepts] Ignoring {:08X} ({}) in '{}'; ModRole FLST accepts Attach Point KYWD/nested FLST only.",
                        form->GetFormID(), static_cast<int>(form->GetFormType()), source.listID);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            visiting.erase(list->GetFormID());
        }

        Concept& FindConcept(const std::pair<std::string, std::string>& parsed)
        {
            auto it = std::find_if(g_concepts.begin(), g_concepts.end(),
                [&](const auto& value) { return value.category == parsed.second; });
            if (it == g_concepts.end()) {
                g_concepts.push_back({ parsed.first, parsed.second, {} });
                return g_concepts.back();
            }
            return *it;
        }

        RE::BGSListForm* ResolveList(const std::string& provider, const std::string& listID)
        {
            const auto separator = listID.find('|');
            if (separator == std::string::npos) {
                REX::WARN("[ArmorConcepts] Invalid list identifier '{}'.", listID);
                return nullptr;
            }
            auto* data = RE::TESDataHandler::GetSingleton();
            const auto plugin = listID.substr(0, separator);
            if (!data || !FindLoadedPlugin(plugin)) {
                REX::INFO("[ArmorConcepts] Provider '{}' skipped list '{}': plugin is not loaded.", provider, listID);
                return nullptr;
            }
            const auto runtimeID = ParseFormID(listID);
            auto* form = runtimeID ? RE::TESForm::GetFormByID(runtimeID) : nullptr;
            auto* list = form ? form->As<RE::BGSListForm>() : nullptr;
            if (!list) REX::WARN("[ArmorConcepts] Provider '{}' list '{}' is missing or not FLST.", provider, listID);
            return list;
        }

        void ReadRoles(
            const json& root,
            const std::string& provider,
            int providerPriority,
            std::uint32_t loadOrder)
        {
            const auto& roles = root.value("modRoles", json::object());
            if (!roles.is_object()) return;
            for (const auto& [roleName, values] : roles.items()) {
                const auto role = ParseRole(roleName);
                if (role == Role::Unknown || !values.is_array()) continue;
                for (const auto& value : values) {
                    const std::string listID = value.is_string() ? value.get<std::string>() : value.value("list", "");
                    if (listID.empty()) continue;
                    auto* list = ResolveList(provider, listID);
                    if (!list) continue;
                    RoleRegistration registration;
                    registration.role = role;
                    if (value.is_object()) {
                        const auto& priorities = value.value("priorities", json::object());
                        if (!priorities.is_object()) throw std::runtime_error("'priorities' must be an object");
                        for (const auto& [group, priority] : priorities.items()) {
                            if (priority.is_number_integer()) registration.priorities[group] = priority.get<int>();
                        }
                    }
                    Source source{ provider, listID, providerPriority, loadOrder };
                    std::unordered_set<std::uint32_t> visiting;
                    CompileList(list, source, false, true, visiting);
                    for (auto* keyword : source.keywords) {
                        if (const auto [it, inserted] = g_attachRoles.emplace(keyword, registration);
                            !inserted && it->second.role != role)
                            REX::WARN("[ArmorConcepts] Attach Point {:08X} registered to multiple ModRoles; ignoring later role '{}'.",
                                keyword->GetFormID(), roleName);
                    }
                }
            }
        }

        void LoadFile(const std::filesystem::path& path)
        {
            try {
                std::ifstream file(path);
                const auto root = json::parse(file, nullptr, true, true);
                if (root.value("module", "") != "armorClassifications") return;
                if (root.value("schemaVersion", 0) != 3 || root.value("domain", "") != "armor")
                    throw std::runtime_error("invalid schemaVersion, module, or domain");
                if (!DependenciesLoaded(root)) {
                    REX::INFO("[ArmorConcepts] Skipping '{}' because a required plugin is not loaded.",
                        path.filename().string());
                    return;
                }
                const auto provider = root.value("provider", path.filename().string());
                const auto providerPriority = root.value("ProviderPriority", 0);
                const auto loadOrder = ProviderLoadOrder(root);
                ReadRoles(root, provider, providerPriority, loadOrder);
                const auto& classifications = root.value("classifications", json::object());
                if (!classifications.is_object()) throw std::runtime_error("'classifications' must be an object");
                for (const auto& [conceptID, entries] : classifications.items()) {
                    const bool isIgnore = conceptID == "Armor.Flag.Ignore";
                    const auto parsed = ParseConcept(conceptID);
                    if (!parsed) {
                        REX::WARN("[ArmorConcepts] Unknown concept '{}' in '{}'.", conceptID, path.filename().string());
                        continue;
                    }
                    if (!entries.is_array()) continue;
                    for (const auto& entry : entries) {
                        if (!entry.is_object()) continue;
                        const std::string listID = entry.value("list", "");
                        if (listID.empty()) continue;
                        auto* list = ResolveList(provider, listID);
                        if (!list) continue;
                        const auto dedupeKey = parsed->second + "|" + std::to_string(list->GetFormID());
                        if (!g_registeredLists.insert(dedupeKey).second) continue;
                        Source source{ provider, listID, providerPriority, loadOrder };
                        source.listPriority = entry.value("listPriority", 0);
                        const auto& contexts = entry.value("contexts", json::array());
                        if (!contexts.is_array()) throw std::runtime_error("'contexts' must be an array");
                        for (const auto& context : contexts) {
                            if (context == "base") source.baseContext = true;
                            else if (context == "instance") source.instanceContext = true;
                            else throw std::runtime_error("unsupported context in " + conceptID);
                        }
                        if (!source.baseContext && !source.instanceContext)
                            throw std::runtime_error("source has no contexts in " + conceptID);
                        std::unordered_set<std::uint32_t> visiting;
                        CompileList(list, source, isIgnore, false, visiting);
                        FindConcept(*parsed).sources.push_back(std::move(source));
                    }
                }
            } catch (const std::exception& error) {
                REX::WARN("[ArmorConcepts] Failed to load '{}': {}", path.string(), error.what());
            }
        }

        std::optional<Selection> Resolve(
            const Concept& classification,
            const std::unordered_set<RE::BGSKeyword*>& baseKeywords,
            const std::unordered_set<RE::BGSKeyword*>& effectiveKeywords,
            const std::vector<RE::BGSMod::Attachment::Mod*>& omods,
            bool instancePass,
            int contextPriority)
        {
            std::optional<Selection> best;
            for (const auto& source : classification.sources) {
                if ((!instancePass && !source.baseContext) || (instancePass && !source.instanceContext)) continue;
                bool matched = false;
                int rolePriority = 0;
                if (!instancePass) {
                    matched = std::any_of(source.keywords.begin(), source.keywords.end(),
                        [&](auto* keyword) { return baseKeywords.contains(keyword); });
                } else {
                    for (auto* omod : omods) {
                        const auto priority = RolePriority(GetRole(omod), classification.group);
                        if (priority < 0) continue;
                        if (source.omods.contains(omod) ||
                            std::any_of(source.keywords.begin(), source.keywords.end(),
                                [&](auto* keyword) { return effectiveKeywords.contains(keyword) && !baseKeywords.contains(keyword); })) {
                            matched = true;
                            rolePriority = std::max(rolePriority, priority);
                            break;
                        }
                    }
                }
                if (!matched) continue;
                Selection candidate{
                    classification.group, classification.category, source.provider + ":" + source.listID,
                    source.providerPriority, source.pluginLoadOrder, contextPriority,
                    rolePriority, source.listPriority, instancePass
                };
                if (!best || CompareSelection(candidate, *best) > 0) best = std::move(candidate);
                else if (CompareSelection(candidate, *best) == 0 && candidate.category != best->category)
                    return std::nullopt;
            }
            return best;
        }
    }

    void Load()
    {
        std::unique_lock lock(g_mutex);
        g_concepts.clear();
        g_attachRoles.clear();
        g_registeredLists.clear();
        g_jsonPriorityFirst = true;
        g_baseContextPriority = 100;
        g_instanceContextPriority = 200;
        {
            std::scoped_lock conflictLock(g_conflictLogMutex);
            g_loggedConflicts.clear();
        }
        const std::filesystem::path directory("Data\\F4SE\\Plugins\\ConditionSystemFramework\\Classifications");
        try {
            std::ifstream selectionFile(directory / "ProviderSelection.json");
            const auto selection = json::parse(selectionFile, nullptr, true, true);
            const auto mode = selection.value("conflictResolution", json::object()).value("mode", "");
            if (mode == "JsonPriorityFirst") g_jsonPriorityFirst = true;
            else if (mode == "PluginLoadOrderFirst") g_jsonPriorityFirst = false;
            else throw std::runtime_error("unsupported conflictResolution.mode");
        } catch (const std::exception& e) {
            REX::WARN("[ArmorConcepts] Invalid ProviderSelection; using JsonPriorityFirst: {}", e.what());
        }
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec))
            if (!ec && entry.is_regular_file() && entry.path().extension() == ".json" &&
                _stricmp(entry.path().filename().string().c_str(), "ProviderSelection.json") != 0)
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            try {
                std::ifstream file(path);
                const auto root = json::parse(file, nullptr, true, true);
                if (root.value("module", "") == "armorClassifications") {
                    const auto priorities = root.value("resolution", json::object())
                                                .value("contextPriorities", json::object());
                    g_baseContextPriority = priorities.value("base", g_baseContextPriority);
                    g_instanceContextPriority = priorities.value("instance", g_instanceContextPriority);
                }
            } catch (...) {
            }
            LoadFile(path);
        }
        REX::INFO("[ArmorConcepts] Compiled {} concepts and {} attach-point roles.", g_concepts.size(), g_attachRoles.size());
    }

    std::vector<Selection> Evaluate(RE::TESBoundObject* object, RE::ExtraDataList* extra)
    {
        std::shared_lock lock(g_mutex);
        std::vector<Selection> result;
        auto* armor = object ? object->As<RE::TESObjectARMO>() : nullptr;
        if (!armor) return result;
        const auto baseKeywords = GetKeywords(SafeGetKeywordForm(armor));
        std::unordered_set<RE::BGSKeyword*> effectiveKeywords;
        if (extra) {
            if (auto* instance = extra->GetByType<RE::ExtraInstanceData>(); instance && instance->data) {
                effectiveKeywords = GetKeywords(instance->data->GetKeywordData());
            } else if (auto* objectInstance = extra->GetByType<RE::BGSObjectInstanceExtra>();
                objectInstance && objectInstance->values &&
                objectInstance->itemIndex != static_cast<std::uint16_t>(-1)) {
                RE::BSTSmartPointer<RE::TBO_InstanceData> resolvedData;
                objectInstance->CreateBaseInstanceData(*object, resolvedData);
                if (resolvedData) effectiveKeywords = GetKeywords(resolvedData->GetKeywordData());
            }
        }
        const auto omods = GetInstalledOmods(extra);

        if (const auto ignore = std::find_if(g_concepts.begin(), g_concepts.end(),
                [](const auto& value) { return value.group == "Armor.Flag" && value.category == "Armor.Flag.Ignore"; });
            ignore != g_concepts.end()) {
            for (const auto& source : ignore->sources) {
                const bool exactMatch = source.armors.contains(armor);
                const bool keywordMatch = std::any_of(source.keywords.begin(), source.keywords.end(),
                    [&](auto* keyword) { return baseKeywords.contains(keyword); });
                if (exactMatch || keywordMatch) {
                    result.push_back({ "Armor.Flag", "Armor.Flag.Ignore",
                        source.provider + ":" + source.listID, source.providerPriority,
                        source.pluginLoadOrder, g_baseContextPriority, 0, source.listPriority, false });
                    return result;
                }
            }
        }

        if (const auto domain = std::find_if(g_concepts.begin(), g_concepts.end(),
                [](const auto& value) {
                    return value.group == "Equipment.Domain" &&
                        value.category == "Equipment.Domain.PowerArmor";
                });
            domain != g_concepts.end()) {
            if (auto hit = Resolve(
                    *domain, baseKeywords, effectiveKeywords, omods, false, g_baseContextPriority)) {
                result.push_back(*hit);
                return result;
            }
        }

        for (const auto& group : { std::string("Armor.Material"), std::string("Armor.Region"), std::string("Armor.Grade") }) {
            std::vector<Selection> hits;
            const bool hasEligibleInstance = std::any_of(omods.begin(), omods.end(),
                [&](auto* omod) { return RolePriority(GetRole(omod), group) >= 0; });
            for (const auto& classification : g_concepts) {
                if (classification.group != group) continue;
                if (hasEligibleInstance) {
                    if (auto hit = Resolve(
                            classification, baseKeywords, effectiveKeywords, omods, true,
                            g_instanceContextPriority))
                        hits.push_back(*hit);
                }
            }
            if (hits.empty()) {
                for (const auto& classification : g_concepts) {
                    if (classification.group != group) continue;
                    if (auto hit = Resolve(
                            classification, baseKeywords, effectiveKeywords, omods, false,
                            g_baseContextPriority))
                        hits.push_back(*hit);
                }
            }
            if (hits.empty() && group == "Armor.Region") {
                const auto has = [&](RE::BIPED_OBJECT slot) { return armor->FillsBipedSlot(std::to_underlying(slot)); };
                if (has(RE::BIPED_OBJECT::kEyes) || has(RE::BIPED_OBJECT::kMouth) || has(RE::BIPED_OBJECT::kBeard))
                    hits.push_back({ group, "Armor.Region.Face", "BOD2", 0, 0, 50, 0, 0, false });
                else if (has(RE::BIPED_OBJECT::kRing) || has(RE::BIPED_OBJECT::kNeck))
                    hits.push_back({ group, "Armor.Region.Accessory", "BOD2", 0, 0, 50, 0, 0, false });
                else if (has(RE::BIPED_OBJECT::kFaceGenHead) || has(RE::BIPED_OBJECT::kHairTop) ||
                    has(RE::BIPED_OBJECT::kHairLong) || has(RE::BIPED_OBJECT::kHeadband))
                    hits.push_back({ group, "Armor.Region.Head", "BOD2", 0, 0, 50, 0, 0, false });
                else if (has(RE::BIPED_OBJECT::kBody) || has(RE::BIPED_OBJECT::kAboveTorso))
                    hits.push_back({ group, "Armor.Region.Torso", "BOD2", 0, 0, 50, 0, 0, false });
                else if (has(RE::BIPED_OBJECT::kAboveLeftArm) || has(RE::BIPED_OBJECT::kAboveRightArm) ||
                    has(RE::BIPED_OBJECT::kAboveLeftLeg) || has(RE::BIPED_OBJECT::kAboveRightLeg))
                    hits.push_back({ group, "Armor.Region.Limbs", "BOD2", 0, 0, 50, 0, 0, false });
            }
            if (hits.empty()) continue;
            const auto strongest = *std::max_element(hits.begin(), hits.end(),
                [](const auto& a, const auto& b) { return CompareSelection(a, b) < 0; });
            std::unordered_set<std::string> winners;
            for (const auto& hit : hits)
                if (CompareSelection(hit, strongest) == 0) winners.insert(hit.category);
            if (winners.size() != 1) {
                const auto conflictKey =
                    group + ":" + std::to_string(armor->GetFormID()) + ":" +
                    std::to_string(strongest.providerPriority) + ":" + std::to_string(strongest.listPriority);
                bool shouldLog = false;
                {
                    std::scoped_lock conflictLock(g_conflictLogMutex);
                    shouldLog = g_loggedConflicts.insert(conflictKey).second;
                }
                if (shouldLog) {
                    std::vector<std::string> sortedWinners(winners.begin(), winners.end());
                    std::ranges::sort(sortedWinners);
                    std::string categories;
                    for (const auto& winner : sortedWinners) {
                        if (!categories.empty()) categories += ", ";
                        categories += winner;
                    }
                    REX::WARN("[ArmorConcepts] Unresolved {} conflict for armor {:08X}: [{}]; returning Unknown.",
                        group, armor->GetFormID(), categories);
                }
                continue;
            }
            result.push_back(*std::find_if(hits.begin(), hits.end(),
                [&](const auto& hit) {
                    return CompareSelection(hit, strongest) == 0 && hit.category == *winners.begin();
                }));
        }
        return result;
    }
}

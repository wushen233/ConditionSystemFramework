#include "pch.h"
#include "ClassificationManager.h"
#include "ClassificationInstanceState.h"
#include "ArmorConceptRegistry.h"
#include "ProfileManager.h"

#include <RE/B/BGSListForm.h>
#include <RE/B/BGSMod.h>
#include <RE/B/BGSObjectInstanceExtra.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace ConditionSystem::Classification
{
    namespace
    {
        struct BaseMatch
        {
            std::unordered_set<std::uint32_t> forms;
            std::unordered_set<std::uint32_t> formListMembers;
            std::unordered_set<std::uint32_t> classificationKeywords;
            std::vector<std::string> keywordsAny;
            std::vector<std::string> keywordsAll;
            std::vector<RE::BIPED_OBJECT> bipedAny;
            std::vector<RE::BIPED_OBJECT> bipedAll;
            std::vector<RE::BIPED_OBJECT> bipedNone;
            bool armorTemplateFallback{ false };
        };

        struct InstanceMatch
        {
            std::string slot;
            std::unordered_set<std::uint32_t> omodForms;
            std::unordered_set<std::uint32_t> omodFormListMembers;
            std::unordered_set<std::uint32_t> classificationKeywords;
            std::unordered_set<std::uint32_t> classificationOmods;
            std::vector<std::string> keywordsAny;
            std::vector<std::string> keywordsAll;
        };

        struct CategoryRule
        {
            std::string ruleID;
            std::string id;
            std::string exclusiveGroup;
            Domain domain{ Domain::Any };
            int sourceTier{ 0 };
            int resolutionPriority{ 0 };
            int priority{ 0 };
            bool combineAll{ false };
            BaseMatch base;
            std::vector<InstanceMatch> instances;
        };

        struct CategoryHit
        {
            std::string ruleID;
            std::string id;
            std::string exclusiveGroup;
            int sourceTier{ 0 };
            std::uint32_t pluginLoadOrder{ 0 };
            int resolutionPriority{ 0 };
            int priority{ 0 };
            bool instanceMatch{ false };
        };

        struct GroupPolicy
        {
            bool resolutionPriorityBeforeSource{ false };
            bool unresolvedTieIsUnknown{ false };
            bool coexisting{ false };
        };

        struct ClassificationListMembers
        {
            std::unordered_set<std::uint32_t> keywords;
            std::unordered_set<std::uint32_t> omods;
            std::unordered_set<std::uint32_t> forms;
        };

        enum class ProviderOrderMode : std::uint8_t
        {
            JsonPriorityFirst,
            PluginLoadOrderFirst
        };

        struct WeaponSource
        {
            std::string ruleID;
            std::string category;
            std::string group;
            std::string providerFile;
            int providerPriority{ 0 };
            std::uint32_t pluginLoadOrder{ 0 };
            int listPriority{ 0 };
            bool baseContext{ false };
            bool instanceContext{ false };
            ClassificationListMembers members;
        };

        struct WeaponRole
        {
            std::string name;
            std::unordered_map<std::string, int> priorities;
        };

        std::shared_mutex g_mutex;
        std::vector<CategoryRule> g_rules;
        std::unordered_map<std::string, GroupPolicy> g_groupPolicies;
        std::vector<WeaponSource> g_weaponSources;
        std::unordered_map<std::uint32_t, WeaponRole> g_weaponRoles;
        std::unordered_map<std::string, int> g_unknownRolePriorities;
        ProviderOrderMode g_providerOrderMode{ ProviderOrderMode::JsonPriorityFirst };

        struct ResultCacheKey
        {
            RE::TESBoundObject* object{ nullptr };
            RE::ExtraDataList* extra{ nullptr };
            const void* instanceData{ nullptr };
            const void* keywordData{ nullptr };
            std::uint32_t keywordCount{ 0 };
            InstanceState instanceState;
            std::uint64_t generation{ 0 };

            bool operator==(const ResultCacheKey& a_other) const
            {
                return object == a_other.object && extra == a_other.extra &&
                    instanceData == a_other.instanceData && keywordData == a_other.keywordData &&
                    keywordCount == a_other.keywordCount && instanceState == a_other.instanceState &&
                    generation == a_other.generation;
            }
        };

        struct ResultCacheKeyHash
        {
            std::size_t operator()(const ResultCacheKey& a_key) const
            {
                std::size_t hash = std::hash<const void*>{}(a_key.object);
                const auto mix = [&hash](std::size_t value) {
                    hash ^= value + static_cast<std::size_t>(0x9e3779b97f4a7c15ull) +
                        (hash << 6) + (hash >> 2);
                };
                mix(std::hash<const void*>{}(a_key.extra));
                mix(std::hash<const void*>{}(a_key.instanceData));
                mix(std::hash<const void*>{}(a_key.keywordData));
                mix(std::hash<std::uint32_t>{}(a_key.keywordCount));
                for (auto id : a_key.instanceState.keywords) mix(std::hash<std::uint32_t>{}(id));
                mix(a_key.instanceState.keywords.size());
                for (auto id : a_key.instanceState.omods) mix(std::hash<std::uint32_t>{}(id));
                mix(std::hash<std::uint64_t>{}(a_key.generation));
                return hash;
            }
        };

        std::shared_mutex g_resultCacheMutex;
        std::unordered_map<ResultCacheKey, Result, ResultCacheKeyHash> g_resultCache;
        std::atomic<std::uint64_t> g_resultCacheGeneration{ 1 };

        ResultCacheKey MakeResultCacheKey(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList)
        {
            ResultCacheKey key;
            key.object = a_object;
            key.extra = a_extraList;
            key.generation = g_resultCacheGeneration.load(std::memory_order_acquire);

            if (a_extraList) {
                if (auto* instance = a_extraList->GetByType<RE::ExtraInstanceData>(); instance && instance->data) {
                    key.instanceData = instance->data.get();
                    if (auto* keywords = instance->data->GetKeywordData()) {
                        key.keywordData = keywords;
                        key.keywordCount = keywords->numKeywords;
                        for (std::uint32_t i = 0; keywords->keywords && i < keywords->numKeywords; ++i) {
                            if (auto* keyword = keywords->keywords[i])
                                key.instanceState.keywords.push_back(keyword->GetFormID());
                        }
                    }
                }
                if (auto* instance = a_extraList->GetByType<RE::BGSObjectInstanceExtra>(); instance &&
                    instance->values && instance->itemIndex != static_cast<std::uint16_t>(-1)) {
                    for (const auto& indexData : instance->GetIndexData()) {
                        if (!indexData.disabled) key.instanceState.omods.push_back(indexData.objectID);
                    }
                }
            }
            return key;
        }

        struct InstanceKeywordCache
        {
            bool resolved{ false };
            std::unordered_set<std::string> editorIDs;
            std::unordered_set<std::uint32_t> formIDs;
            std::unordered_set<std::uint32_t> addedFormIDs;
        };

        void AddClassificationListMembers(
            RE::BGSListForm* a_list,
            ClassificationListMembers& a_output,
            std::unordered_set<std::uint32_t>& a_visiting,
            std::string_view a_source,
            bool a_allowWeaponForms = false);

        std::optional<RE::BIPED_OBJECT> ParseBipedSlot(std::string_view a_name)
        {
            static const std::unordered_map<std::string, RE::BIPED_OBJECT> slots{
                { "head", RE::BIPED_OBJECT::kFaceGenHead }, { "body", RE::BIPED_OBJECT::kBody },
                { "torso", RE::BIPED_OBJECT::kAboveTorso }, { "leftArm", RE::BIPED_OBJECT::kAboveLeftArm },
                { "rightArm", RE::BIPED_OBJECT::kAboveRightArm }, { "leftLeg", RE::BIPED_OBJECT::kAboveLeftLeg },
                { "rightLeg", RE::BIPED_OBJECT::kAboveRightLeg }, { "ring", RE::BIPED_OBJECT::kRing },
                { "pipboy", RE::BIPED_OBJECT::kPipboy }
            };
            const auto it = slots.find(std::string(a_name));
            return it == slots.end() ? std::nullopt : std::optional<RE::BIPED_OBJECT>{ it->second };
        }

        void ReadBipedArray(const json& a_node, const char* a_key, std::vector<RE::BIPED_OBJECT>& a_output)
        {
            if (!a_node.contains(a_key) || !a_node[a_key].is_array()) return;
            for (const auto& value : a_node[a_key]) {
                if (!value.is_string()) continue;
                if (const auto slot = ParseBipedSlot(value.get<std::string>())) a_output.push_back(*slot);
                else REX::WARN("[Classification] Unknown biped slot '{}' in '{}'.", value.get<std::string>(), a_key);
            }
        }

        bool IsLoaded(const std::string& a_plugin)
        {
            if (a_plugin.empty()) return true;
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler && FindLoadedPlugin(a_plugin);
        }

        bool DependenciesLoaded(const json& a_root)
        {
            if (!a_root.contains("requires")) return true;
            if (!a_root["requires"].is_array()) {
                REX::WARN("[Classification] 'requires' must be an array.");
                return false;
            }
            for (const auto& dependency : a_root["requires"]) {
                if (!dependency.is_string()) {
                    REX::WARN("[Classification] Every 'requires' entry must be a plugin filename.");
                    return false;
                }
                if (!IsLoaded(dependency.get<std::string>())) return false;
            }
            return true;
        }

        std::uint32_t GetProviderLoadOrder(const json& a_root)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data || !a_root.contains("requires") || !a_root["requires"].is_array()) return 0;
            std::uint32_t result = 0;
            std::uint32_t index = 0;
            for (auto* file : data->files) {
                if (!file) {
                    ++index;
                    continue;
                }
                for (const auto& dependency : a_root["requires"]) {
                    if (!dependency.is_string()) continue;
                    const auto name = dependency.get<std::string>();
                    const auto filename = file->GetFilename();
                    if (file->IsActive() && filename.size() == name.size() &&
                        _strnicmp(filename.data(), name.c_str(), filename.size()) == 0) {
                        result = std::max(result, index);
                    }
                }
                ++index;
            }
            return result;
        }

        void LoadProviderSelection(const std::filesystem::path& a_path)
        {
            try {
                std::ifstream file(a_path);
                if (!file.is_open()) throw std::runtime_error("file not found");
                const auto root = json::parse(file, nullptr, true, true);
                if (root.value("schemaVersion", 0) != 1 ||
                    root.value("module", "") != "Classifications.ProviderSelection") {
                    throw std::runtime_error("invalid schemaVersion or module");
                }
                const auto loading = root.value("loading", json::object());
                if (loading.value("mode", "") != "AllRequirementsSatisfied" ||
                    loading.value("missingRequirement", "") != "SkipProvider") {
                    throw std::runtime_error("unsupported loading policy");
                }
                const auto mode = root.value("conflictResolution", json::object()).value("mode", "");
                if (mode == "JsonPriorityFirst") {
                    g_providerOrderMode = ProviderOrderMode::JsonPriorityFirst;
                } else if (mode == "PluginLoadOrderFirst") {
                    g_providerOrderMode = ProviderOrderMode::PluginLoadOrderFirst;
                } else {
                    throw std::runtime_error("unsupported conflict resolution mode");
                }
            } catch (const std::exception& e) {
                REX::WARN("[Classification] Failed to load ProviderSelection '{}': {}; using JsonPriorityFirst.",
                    a_path.string(), e.what());
                g_providerOrderMode = ProviderOrderMode::JsonPriorityFirst;
            }
        }

        ClassificationListMembers ResolveClassificationList(
            std::string_view a_listID,
            bool a_allowWeaponForms = false)
        {
            ClassificationListMembers result;
            const auto runtimeID = ParseFormID(std::string(a_listID));
            auto* form = runtimeID ? RE::TESForm::GetFormByID(runtimeID) : nullptr;
            auto* list = form ? form->As<RE::BGSListForm>() : nullptr;
            if (!list) {
                REX::WARN("[Classification] Classification source '{}' is missing or not FLST.", a_listID);
                return result;
            }
            std::unordered_set<std::uint32_t> visiting;
            AddClassificationListMembers(list, result, visiting, a_listID, a_allowWeaponForms);
            return result;
        }

        void AddResolvedListMembers(
            RE::BGSListForm* a_list,
            std::unordered_set<std::uint32_t>& a_output,
            std::unordered_set<std::uint32_t>& a_visiting)
        {
            if (!a_list || !a_visiting.insert(a_list->GetFormID()).second) return;

            a_list->ForEachForm([&](RE::TESForm* a_form) {
                if (!a_form) return RE::BSContainer::ForEachResult::kContinue;
                if (auto* childList = a_form->As<RE::BGSListForm>()) {
                    AddResolvedListMembers(childList, a_output, a_visiting);
                } else {
                    a_output.insert(a_form->GetFormID());
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });

            a_visiting.erase(a_list->GetFormID());
        }

        std::unordered_set<std::uint32_t> ResolveFormLists(const json& a_node)
        {
            std::unordered_set<std::uint32_t> result;
            if (!a_node.is_array()) return result;

            for (const auto& value : a_node) {
                if (!value.is_string()) continue;
                const auto runtimeID = ParseFormID(value.get<std::string>());
                if (runtimeID == 0) {
                    REX::WARN("[Classification] Unresolved FormID List '{}'.", value.get<std::string>());
                    continue;
                }

                auto* form = RE::TESForm::GetFormByID(runtimeID);
                auto* list = form ? form->As<RE::BGSListForm>() : nullptr;
                if (!list) {
                    REX::WARN("[Classification] '{}' is not a FormID List.", value.get<std::string>());
                    continue;
                }

                std::unordered_set<std::uint32_t> visiting;
                AddResolvedListMembers(list, result, visiting);
            }
            return result;
        }

        void AddClassificationListMembers(
            RE::BGSListForm* a_list,
            ClassificationListMembers& a_output,
            std::unordered_set<std::uint32_t>& a_visiting,
            std::string_view a_source,
            bool a_allowWeaponForms)
        {
            if (!a_list) return;
            if (!a_visiting.insert(a_list->GetFormID()).second) {
                REX::WARN("[Classification] Nested FLST cycle detected at {:08X} in '{}'.",
                    a_list->GetFormID(), a_source);
                return;
            }

            a_list->ForEachForm([&](RE::TESForm* a_form) {
                if (!a_form) return RE::BSContainer::ForEachResult::kContinue;
                if (auto* childList = a_form->As<RE::BGSListForm>()) {
                    AddClassificationListMembers(
                        childList, a_output, a_visiting, a_source, a_allowWeaponForms);
                } else if (a_form->As<RE::BGSKeyword>()) {
                    a_output.keywords.insert(a_form->GetFormID());
                } else if (a_form->As<RE::BGSMod::Attachment::Mod>()) {
                    a_output.omods.insert(a_form->GetFormID());
                } else if (a_allowWeaponForms && a_form->Is(RE::ENUM_FORM_ID::kWEAP)) {
                    a_output.forms.insert(a_form->GetFormID());
                } else {
                    REX::WARN(
                        "[Classification] Ignoring {:08X} ({}) in '{}'; this classification FLST accepts {}.",
                        a_form->GetFormID(), static_cast<int>(a_form->GetFormType()), a_source,
                        a_allowWeaponForms ? "KYWD/OMOD/WEAP/nested FLST only" :
                                             "KYWD/OMOD/nested FLST only");
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            a_visiting.erase(a_list->GetFormID());
        }

        ClassificationListMembers ResolveClassificationFormLists(const json& a_node)
        {
            ClassificationListMembers result;
            if (!a_node.is_array()) return result;

            for (const auto& value : a_node) {
                if (!value.is_string()) continue;
                const auto source = value.get<std::string>();
                const auto runtimeID = ParseFormID(source);
                if (runtimeID == 0) {
                    REX::WARN("[Classification] Unresolved classification FormID List '{}'.", source);
                    continue;
                }
                auto* form = RE::TESForm::GetFormByID(runtimeID);
                auto* list = form ? form->As<RE::BGSListForm>() : nullptr;
                if (!list) {
                    REX::WARN("[Classification] Classification source '{}' is not a FormID List.", source);
                    continue;
                }
                std::unordered_set<std::uint32_t> visiting;
                AddClassificationListMembers(list, result, visiting, source);
            }
            return result;
        }

        void ReadStringArray(const json& a_node, const char* a_key, std::vector<std::string>& a_output)
        {
            if (!a_node.contains(a_key) || !a_node[a_key].is_array()) return;
            for (const auto& value : a_node[a_key]) {
                if (value.is_string()) a_output.push_back(value.get<std::string>());
            }
        }

        std::unordered_set<std::uint32_t> ResolveForms(const json& a_node, const char* a_key)
        {
            std::unordered_set<std::uint32_t> result;
            if (!a_node.contains(a_key) || !a_node[a_key].is_array()) return result;
            for (const auto& value : a_node[a_key]) {
                if (!value.is_string()) continue;
                const auto runtimeID = ParseFormID(value.get<std::string>());
                if (runtimeID != 0) result.insert(runtimeID);
                else REX::WARN("[Classification] Unresolved form '{}'.", value.get<std::string>());
            }
            return result;
        }

        BaseMatch ReadBaseMatch(const json& a_node)
        {
            BaseMatch result;
            if (!a_node.is_object()) return result;
            result.forms = ResolveForms(a_node, "forms");
            result.formListMembers = ResolveFormLists(a_node.value("formLists", json::array()));
            result.classificationKeywords =
                ResolveClassificationFormLists(a_node.value("classificationFormLists", json::array())).keywords;
            ReadStringArray(a_node, "keywordsAny", result.keywordsAny);
            ReadStringArray(a_node, "keywordsAll", result.keywordsAll);
            ReadBipedArray(a_node, "bipedAny", result.bipedAny);
            ReadBipedArray(a_node, "bipedAll", result.bipedAll);
            ReadBipedArray(a_node, "bipedNone", result.bipedNone);
            result.armorTemplateFallback = a_node.value("armorTemplateFallback", false);
            return result;
        }

        InstanceMatch ReadInstanceMatch(const json& a_node)
        {
            InstanceMatch result;
            if (!a_node.is_object()) return result;
            result.slot = a_node.value("slot", "");
            result.omodForms = ResolveForms(a_node, "omodForms");
            result.omodFormListMembers = ResolveFormLists(a_node.value("omodFormLists", json::array()));
            auto classification =
                ResolveClassificationFormLists(a_node.value("classificationFormLists", json::array()));
            result.classificationKeywords = std::move(classification.keywords);
            result.classificationOmods = std::move(classification.omods);
            ReadStringArray(a_node, "effectiveKeywordsAny", result.keywordsAny);
            ReadStringArray(a_node, "effectiveKeywordsAll", result.keywordsAll);
            return result;
        }

        bool HasBaseKeyword(RE::TESBoundObject* a_object, const std::string& a_keyword)
        {
            if (!a_object) return false;
            if (a_keyword == "IS_WEAPON" && a_object->Is(RE::ENUM_FORM_ID::kWEAP)) return true;
            if (a_keyword == "IS_ARMOR" && a_object->Is(RE::ENUM_FORM_ID::kARMO)) return true;
            auto* keywords = SafeGetKeywordForm(a_object);
            if (!keywords) return false;
            for (std::uint32_t i = 0; i < keywords->numKeywords; ++i) {
                if (!keywords->keywords || !keywords->keywords[i]) continue;
                const char* editorID = keywords->keywords[i]->GetFormEditorID();
                if (editorID && _stricmp(editorID, a_keyword.c_str()) == 0) return true;
            }
            return false;
        }

        bool AnyBaseKeyword(RE::TESBoundObject* a_object, const std::vector<std::string>& a_keywords)
        {
            for (const auto& keyword : a_keywords) {
                if (HasBaseKeyword(a_object, keyword)) return true;
            }
            return false;
        }

        bool AllBaseKeywords(RE::TESBoundObject* a_object, const std::vector<std::string>& a_keywords)
        {
            for (const auto& keyword : a_keywords) {
                if (!HasBaseKeyword(a_object, keyword)) return false;
            }
            return true;
        }

        std::unordered_set<std::uint32_t> GetBaseKeywordIDs(RE::TESBoundObject* a_object)
        {
            std::unordered_set<std::uint32_t> result;
            auto* keywords = SafeGetKeywordForm(a_object);
            if (!keywords || !keywords->keywords) return result;
            for (std::uint32_t i = 0; i < keywords->numKeywords; ++i) {
                if (keywords->keywords[i]) result.insert(keywords->keywords[i]->GetFormID());
            }
            return result;
        }

        bool AnyKeywordID(
            const std::unordered_set<std::uint32_t>& a_itemKeywords,
            const std::unordered_set<std::uint32_t>& a_candidates)
        {
            return std::any_of(a_candidates.begin(), a_candidates.end(),
                [&](std::uint32_t a_formID) { return a_itemKeywords.contains(a_formID); });
        }

        void ResolveInstanceKeywords(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra, InstanceKeywordCache& a_cache)
        {
            if (a_cache.resolved) return;
            a_cache.resolved = true;
            if (!a_object || !a_extra) return;
            RE::BGSKeywordForm* keywords = nullptr;
            if (auto* instance = a_extra->GetByType<RE::ExtraInstanceData>(); instance && instance->data) {
                keywords = instance->data->GetKeywordData();
            }
            RE::BSTSmartPointer<RE::TBO_InstanceData> resolvedData;
            if (!keywords) {
                if (auto* instance = a_extra->GetByType<RE::BGSObjectInstanceExtra>(); instance && instance->values && instance->itemIndex != static_cast<std::uint16_t>(-1)) {
                    instance->CreateBaseInstanceData(*a_object, resolvedData);
                    if (resolvedData) keywords = resolvedData->GetKeywordData();
                }
            }
            if (!keywords) return;
            for (std::uint32_t i = 0; i < keywords->numKeywords; ++i) {
                if (!keywords->keywords || !keywords->keywords[i]) continue;
                const char* editorID = keywords->keywords[i]->GetFormEditorID();
                if (editorID) a_cache.editorIDs.emplace(editorID);
                a_cache.formIDs.insert(keywords->keywords[i]->GetFormID());
            }
            const auto baseKeywords = GetBaseKeywordIDs(a_object);
            for (const auto formID : a_cache.formIDs) {
                if (!baseKeywords.contains(formID)) a_cache.addedFormIDs.insert(formID);
            }
        }

        bool HasInstanceKeyword(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra, InstanceKeywordCache& a_cache, const std::string& a_keyword)
        {
            ResolveInstanceKeywords(a_object, a_extra, a_cache);
            return std::any_of(a_cache.editorIDs.begin(), a_cache.editorIDs.end(), [&](const auto& editorID) {
                return _stricmp(editorID.c_str(), a_keyword.c_str()) == 0;
            });
        }

        bool AnyInstanceKeyword(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra, InstanceKeywordCache& a_cache, const std::vector<std::string>& a_keywords)
        {
            for (const auto& keyword : a_keywords) {
                if (HasInstanceKeyword(a_object, a_extra, a_cache, keyword)) return true;
            }
            return false;
        }

        bool AllInstanceKeywords(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra, InstanceKeywordCache& a_cache, const std::vector<std::string>& a_keywords)
        {
            for (const auto& keyword : a_keywords) {
                if (!HasInstanceKeyword(a_object, a_extra, a_cache, keyword)) return false;
            }
            return true;
        }

        bool HasBaseConditions(const BaseMatch& a_match)
        {
            return !a_match.forms.empty() || !a_match.formListMembers.empty() ||
                !a_match.classificationKeywords.empty() ||
                !a_match.keywordsAny.empty() || !a_match.keywordsAll.empty() ||
                !a_match.bipedAny.empty() || !a_match.bipedAll.empty() || !a_match.bipedNone.empty();
        }

        bool ArmorFillsBipedSlot(const RE::TESObjectARMO* a_armor, RE::BIPED_OBJECT a_slot)
        {
            if (!a_armor) return false;
            const auto slot = std::to_underlying(a_slot);
            if (a_armor->bipedModelData.bipedObjectSlots != 0) {
                return a_armor->FillsBipedSlot(slot);
            }

            for (const auto& model : a_armor->modelArray) {
                if (model.armorAddon && model.armorAddon->FillsBipedSlot(slot)) return true;
            }
            return false;
        }

        bool MatchesBiped(const BaseMatch& a_match, RE::TESBoundObject* a_object)
        {
            const auto* armor = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr;
            if (!armor) return a_match.bipedAny.empty() && a_match.bipedAll.empty() && a_match.bipedNone.empty();
            const auto has = [&](RE::BIPED_OBJECT a_slot) { return ArmorFillsBipedSlot(armor, a_slot); };
            if (!a_match.bipedAny.empty() && std::none_of(a_match.bipedAny.begin(), a_match.bipedAny.end(), has)) return false;
            if (!std::all_of(a_match.bipedAll.begin(), a_match.bipedAll.end(), has)) return false;
            return std::none_of(a_match.bipedNone.begin(), a_match.bipedNone.end(), has);
        }

        bool MatchesBaseDirect(const CategoryRule& a_rule, RE::TESBoundObject* a_object)
        {
            if (!a_object || !HasBaseConditions(a_rule.base) || !MatchesBiped(a_rule.base, a_object)) return false;
            const auto formID = a_object->GetFormID();
            const bool hasFormSelector = !a_rule.base.forms.empty() || !a_rule.base.formListMembers.empty();
            const bool hasClassificationSelector = !a_rule.base.classificationKeywords.empty();
            const bool hasAnyKeywordSelector = !a_rule.base.keywordsAny.empty();
            const bool hasAllKeywordSelector = !a_rule.base.keywordsAll.empty();
            const bool hasBipedSelector = !a_rule.base.bipedAny.empty() || !a_rule.base.bipedAll.empty();

            bool selectorMatched = false;
            if (hasFormSelector) {
                selectorMatched = a_rule.base.forms.contains(formID) || a_rule.base.formListMembers.contains(formID);
            }
            if (hasClassificationSelector &&
                AnyKeywordID(GetBaseKeywordIDs(a_object), a_rule.base.classificationKeywords)) {
                selectorMatched = true;
            }
            if (hasAnyKeywordSelector && AnyBaseKeyword(a_object, a_rule.base.keywordsAny)) {
                selectorMatched = true;
            }
            if (hasAllKeywordSelector && !AllBaseKeywords(a_object, a_rule.base.keywordsAll)) {
                return false;
            }
            if (hasBipedSelector) selectorMatched = true;

            // A rule with only keywordsAll uses that group as its selector.
            return selectorMatched || (hasAllKeywordSelector && !hasFormSelector &&
                !hasClassificationSelector && !hasAnyKeywordSelector);
        }

        bool MatchesBase(const CategoryRule& a_rule, RE::TESBoundObject* a_object)
        {
            if (MatchesBaseDirect(a_rule, a_object)) return true;
            if (!a_rule.base.armorTemplateFallback) return false;

            auto* armor = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr;
            std::unordered_set<std::uint32_t> visited;
            for (std::size_t depth = 0; armor && armor->armorTemplate && depth < 8; ++depth) {
                armor = armor->armorTemplate;
                if (!visited.insert(armor->GetFormID()).second) {
                    REX::WARN("[Classification] Armor template loop while evaluating '{}'.", a_rule.ruleID);
                    return false;
                }
                if (MatchesBaseDirect(a_rule, armor)) return true;
            }
            return false;
        }

        std::vector<RE::BGSMod::Attachment::Mod*> GetInstalledOmods(RE::ExtraDataList* a_extra)
        {
            std::vector<RE::BGSMod::Attachment::Mod*> result;
            if (!a_extra) return result;
            auto* instance = a_extra->GetByType<RE::BGSObjectInstanceExtra>();
            if (!instance || !instance->values || instance->itemIndex == static_cast<std::uint16_t>(-1)) return result;

            for (const auto& indexData : instance->GetIndexData()) {
                if (indexData.disabled) continue;
                auto* form = RE::TESForm::GetFormByID(indexData.objectID);
                if (auto* omod = form ? form->As<RE::BGSMod::Attachment::Mod>() : nullptr) result.push_back(omod);
            }
            return result;
        }

        std::string GetAttachPointEditorID(RE::BGSMod::Attachment::Mod* a_omod)
        {
            if (!a_omod || a_omod->attachPoint.keywordIndex == 0xFFFF) return {};
            auto* keyword = RE::detail::BGSKeywordGetTypedKeywordByIndex(
                RE::KeywordType::kAttachPoint,
                a_omod->attachPoint.keywordIndex);
            if (!keyword) return {};
            const char* editorID = keyword->GetFormEditorID();
            return editorID ? editorID : "";
        }

        int GetWeaponRolePriority(
            RE::BGSMod::Attachment::Mod* a_omod,
            const std::string& a_group)
        {
            int fallback = 0;
            if (const auto it = g_unknownRolePriorities.find(a_group); it != g_unknownRolePriorities.end())
                fallback = it->second;
            if (!a_omod || a_omod->attachPoint.keywordIndex == 0xFFFF) return fallback;
            auto* keyword = RE::detail::BGSKeywordGetTypedKeywordByIndex(
                RE::KeywordType::kAttachPoint, a_omod->attachPoint.keywordIndex);
            if (!keyword) return fallback;
            const auto role = g_weaponRoles.find(keyword->GetFormID());
            if (role == g_weaponRoles.end()) return fallback;
            const auto priority = role->second.priorities.find(a_group);
            return priority == role->second.priorities.end() ? fallback : priority->second;
        }

        bool MatchesInstance(
            const InstanceMatch& a_match,
            RE::TESBoundObject* a_object,
            RE::ExtraDataList* a_extra,
            const std::vector<RE::BGSMod::Attachment::Mod*>& a_omods,
            InstanceKeywordCache& a_keywordCache)
        {
            const bool hasClassificationKeywordSelector = !a_match.classificationKeywords.empty();
            const bool hasClassificationOmodSelector = !a_match.classificationOmods.empty();
            const bool hasNamedKeywordSelector =
                !a_match.keywordsAny.empty() || !a_match.keywordsAll.empty();
            if (!a_match.keywordsAny.empty() && !AnyInstanceKeyword(a_object, a_extra, a_keywordCache, a_match.keywordsAny)) return false;
            if (!a_match.keywordsAll.empty() && !AllInstanceKeywords(a_object, a_extra, a_keywordCache, a_match.keywordsAll)) return false;
            bool classificationKeywordMatched = false;
            if (hasClassificationKeywordSelector) {
                ResolveInstanceKeywords(a_object, a_extra, a_keywordCache);
                classificationKeywordMatched =
                    AnyKeywordID(a_keywordCache.addedFormIDs, a_match.classificationKeywords);
            }

            const bool hasExplicitOmodSelector = !a_match.slot.empty() ||
                !a_match.omodForms.empty() || !a_match.omodFormListMembers.empty();
            bool explicitOmodMatched = !hasExplicitOmodSelector;
            bool classificationOmodMatched = false;
            for (auto* omod : a_omods) {
                if (!omod) continue;
                const auto omodID = omod->GetFormID();
                if (hasClassificationOmodSelector &&
                    a_match.classificationOmods.contains(omodID)) {
                    classificationOmodMatched = true;
                }
                if (!hasExplicitOmodSelector) continue;
                if (!a_match.slot.empty()) {
                    const auto slot = GetAttachPointEditorID(omod);
                    if (_stricmp(slot.c_str(), a_match.slot.c_str()) != 0) continue;
                }
                const bool hasOmodFormSelector =
                    !a_match.omodForms.empty() || !a_match.omodFormListMembers.empty();
                if (hasOmodFormSelector && !a_match.omodForms.contains(omodID) &&
                    !a_match.omodFormListMembers.contains(omodID)) continue;
                explicitOmodMatched = true;
            }

            const bool hasClassificationSelector =
                hasClassificationKeywordSelector || hasClassificationOmodSelector;
            if (hasClassificationSelector &&
                !classificationKeywordMatched && !classificationOmodMatched) {
                return false;
            }
            if (!explicitOmodMatched) return false;
            if (!hasExplicitOmodSelector && !hasClassificationSelector) {
                // Dynamic instance keywords are valid even when the item has
                // no installed OMOD entries.
                return hasNamedKeywordSelector;
            }
            return true;
        }

        void LoadWeaponProvider(const std::filesystem::path& a_path, const json& a_root)
        {
            if (a_root.value("schemaVersion", 0) != 3 ||
                a_root.value("module", "") != "weaponClassifications" ||
                a_root.value("domain", "") != "weapon") {
                REX::WARN("[Classification] Ignoring invalid weapon Provider '{}'.", a_path.filename().string());
                return;
            }
            if (!DependenciesLoaded(a_root)) {
                REX::INFO("[Classification] Skipping weapon Provider '{}' because a required plugin is not loaded.",
                    a_path.filename().string());
                return;
            }

            const auto providerPriority = a_root.value("ProviderPriority", 0);
            const auto loadOrder = GetProviderLoadOrder(a_root);
            const auto& groups = a_root.value("groups", json::object());
            if (!groups.is_object()) throw std::runtime_error("'groups' must be an object");
            for (const auto& [group, value] : groups.items()) {
                const auto mode = value.value("mode", "");
                GroupPolicy policy;
                policy.unresolvedTieIsUnknown = value.value("unresolvedTie", "") == "unknown";
                if (mode == "coexisting") policy.coexisting = true;
                else if (mode != "exclusive" && mode != "optionalExclusive")
                    throw std::runtime_error("unsupported group mode for " + group);
                g_groupPolicies[group] = policy;
            }

            const auto& roles = a_root.value("modRoles", json::object());
            if (!roles.is_object()) throw std::runtime_error("'modRoles' must be an object");
            for (const auto& [roleName, entries] : roles.items()) {
                if (roleName == "unknownModRole") {
                    const auto priorities = entries.value("priorities", json::object());
                    for (const auto& [group, priority] : priorities.items()) {
                        if (priority.is_number_integer()) g_unknownRolePriorities[group] = priority.get<int>();
                    }
                    continue;
                }
                if (!entries.is_array()) continue;
                for (const auto& entry : entries) {
                    if (!entry.is_object()) continue;
                    const auto listID = entry.value("list", "");
                    if (listID.empty()) continue;
                    const auto members = ResolveClassificationList(listID);
                    WeaponRole role;
                    role.name = roleName;
                    const auto priorities = entry.value("priorities", json::object());
                    for (const auto& [group, priority] : priorities.items()) {
                        if (priority.is_number_integer()) role.priorities[group] = priority.get<int>();
                    }
                    for (const auto keywordID : members.keywords) {
                        if (const auto [it, inserted] = g_weaponRoles.emplace(keywordID, role);
                            !inserted && it->second.name != roleName) {
                            REX::WARN("[Classification] Attach Point {:08X} belongs to both '{}' and '{}'; keeping '{}'.",
                                keywordID, it->second.name, roleName, it->second.name);
                        }
                    }
                }
            }

            const auto& classifications = a_root.value("classifications", json::object());
            if (!classifications.is_object()) throw std::runtime_error("'classifications' must be an object");
            for (const auto& [category, entries] : classifications.items()) {
                const auto separator = category.rfind('.');
                if (separator == std::string::npos || !entries.is_array()) continue;
                const auto group = category.substr(0, separator);
                if (!g_groupPolicies.contains(group)) {
                    REX::WARN("[Classification] Category '{}' references undeclared group '{}'.", category, group);
                    continue;
                }
                std::size_t entryIndex = 0;
                for (const auto& entry : entries) {
                    if (!entry.is_object()) continue;
                    const auto listID = entry.value("list", "");
                    if (listID.empty()) continue;
                    WeaponSource source;
                    source.ruleID = a_path.filename().string() + ":" + category + ":" + std::to_string(entryIndex++);
                    source.category = category;
                    source.group = group;
                    source.providerFile = a_path.filename().string();
                    source.providerPriority = providerPriority;
                    source.pluginLoadOrder = loadOrder;
                    source.listPriority = entry.value("listPriority", 0);
                    const auto contexts = entry.value("contexts", json::array());
                    if (!contexts.is_array()) throw std::runtime_error("'contexts' must be an array");
                    for (const auto& context : contexts) {
                        if (!context.is_string()) continue;
                        if (context == "base") source.baseContext = true;
                        else if (context == "instance") source.instanceContext = true;
                        else throw std::runtime_error("unsupported context in " + category);
                    }
                    if (!source.baseContext && !source.instanceContext)
                        throw std::runtime_error("source has no contexts in " + category);
                    source.members = ResolveClassificationList(
                        listID, category == "Weapon.Flag.Ignore");
                    g_weaponSources.push_back(std::move(source));
                }
            }

            REX::INFO("[Classification] Registered weapon Provider '{}' (ProviderPriority={}, loadOrder={}).",
                a_path.filename().string(), providerPriority, loadOrder);
        }

        void LoadFile(const std::filesystem::path& a_path)
        {
            try {
                std::ifstream file(a_path);
                const auto root = json::parse(file, nullptr, true, true);
                if (root.value("module", "") == "weaponClassifications") {
                    LoadWeaponProvider(a_path, root);
                    return;
                }
                if (root.value("module", "") == "armorClassifications") return;
                if (root.value("module", "") == "Classifications.ProviderSelection") return;
                // Armor concept providers are handled by ArmorConceptRegistry.
                // They intentionally share this directory with legacy/shared
                // schema 2 contributions.
                if (root.contains("classifications")) return;
                if (root.value("schemaVersion", 0) != 2) {
                    REX::WARN("[Classification] Ignoring '{}' because schemaVersion is not 2.", a_path.filename().string());
                    return;
                }
                if (root.value("module", "") != "classifications") {
                    REX::WARN("[Classification] Ignoring '{}' because module is not 'classifications'.", a_path.filename().string());
                    return;
                }
                if (!DependenciesLoaded(root)) {
                    REX::INFO("[Classification] Skipping '{}' because a required plugin is not loaded.", a_path.filename().string());
                    return;
                }
                if (const auto policies = root.find("groupPolicies");
                    policies != root.end() && policies->is_object()) {
                    for (const auto& [group, node] : policies->items()) {
                        if (!node.is_object()) continue;
                        const auto order = node.value("resolutionOrder", "sourceThenInstance");
                        if (order != "sourceThenInstance" &&
                            order != "sourceThenResolutionPriorityThenInstance") {
                            REX::WARN("[Classification] Ignoring invalid resolutionOrder '{}' for group '{}'.",
                                order, group);
                            continue;
                        }
                        g_groupPolicies[group] = {
                            order == "sourceThenResolutionPriorityThenInstance",
                            node.value("unresolvedTie", "") == "unknown"
                        };
                    }
                }
                if (!root.contains("categories") || !root["categories"].is_array()) return;
                const int fileTier = root.value("sourceTier", 0);

                for (const auto& node : root["categories"]) {
                    if (!node.is_object()) continue;
                    CategoryRule rule;
                    rule.id = node.value("id", "");
                    rule.ruleID = node.value("ruleId", rule.id);
                    rule.exclusiveGroup = node.value("exclusiveGroup", "");
                    rule.sourceTier = node.value("sourceTier", fileTier);
                    rule.resolutionPriority = node.value("resolutionPriority", 0);
                    rule.priority = node.value("priority", 0);
                    const auto domain = node.value("domain", root.value("domain", "any"));
                    if (domain == "weapon") rule.domain = Domain::Weapon;
                    else if (domain == "armor") rule.domain = Domain::Armor;
                    else if (domain != "any") { REX::WARN("[Classification] Ignoring '{}' with invalid domain '{}'.", rule.id, domain); continue; }
                    if (rule.id.empty()) {
                        REX::WARN("[Classification] Ignoring unnamed category in '{}'.", a_path.filename().string());
                        continue;
                    }

                    const auto& match = node.value("match", json::object());
                    const auto combine = match.value("combine", "any");
                    if (combine != "any" && combine != "all") { REX::WARN("[Classification] Ignoring '{}' with invalid combine '{}'.", rule.id, combine); continue; }
                    rule.combineAll = combine == "all";
                    rule.base = ReadBaseMatch(match.value("base", json::object()));
                    const bool usesArmorOnlyMatcher = !rule.base.bipedAny.empty() || !rule.base.bipedAll.empty() ||
                        !rule.base.bipedNone.empty() || rule.base.armorTemplateFallback;
                    if (usesArmorOnlyMatcher && rule.domain != Domain::Armor) {
                        REX::WARN("[Classification] Ignoring '{}' because BOD2/template matching requires domain 'armor'.", rule.id);
                        continue;
                    }
                    const auto& instances = match.value("instance", json::array());
                    if (instances.is_array()) {
                        for (const auto& instance : instances) rule.instances.push_back(ReadInstanceMatch(instance));
                    } else if (instances.is_object()) {
                        rule.instances.push_back(ReadInstanceMatch(instances));
                    }
                    if (!HasBaseConditions(rule.base) && rule.instances.empty()) {
                        REX::WARN("[Classification] Ignoring category '{}' with no match conditions.", rule.id);
                        continue;
                    }
                    g_rules.push_back(std::move(rule));
                }
            }
            catch (const std::exception& e) {
                REX::WARN("[Classification] Failed to load '{}': {}", a_path.string(), e.what());
            }
        }

        std::vector<CategoryHit> GetHits(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra)
        {
            std::vector<CategoryHit> hits;
            if (!a_object) return hits;
            const auto omods = GetInstalledOmods(a_extra);
            InstanceKeywordCache keywordCache;
            for (const auto& rule : g_rules) {
                if ((rule.domain == Domain::Weapon && !a_object->Is(RE::ENUM_FORM_ID::kWEAP)) ||
                    (rule.domain == Domain::Armor && !a_object->Is(RE::ENUM_FORM_ID::kARMO))) continue;
                const bool baseMatched = MatchesBase(rule, a_object);
                bool instanceMatched = false;
                for (const auto& instance : rule.instances) {
                    if (MatchesInstance(instance, a_object, a_extra, omods, keywordCache)) {
                        instanceMatched = true;
                        break;
                    }
                }
                const bool matched = rule.combineAll
                    ? ((!HasBaseConditions(rule.base) || baseMatched) && (rule.instances.empty() || instanceMatched))
                    : (baseMatched || instanceMatched);
                if (matched) {
                    hits.push_back({ rule.ruleID, rule.id, rule.exclusiveGroup, rule.sourceTier, 0,
                        rule.resolutionPriority, rule.priority, instanceMatched });
                }
            }
            if (a_object->Is(RE::ENUM_FORM_ID::kWEAP)) {
                const auto baseKeywords = GetBaseKeywordIDs(a_object);
                ResolveInstanceKeywords(a_object, a_extra, keywordCache);
                for (const auto& source : g_weaponSources) {
                    if (source.baseContext &&
                        (source.members.forms.contains(a_object->GetFormID()) ||
                         AnyKeywordID(baseKeywords, source.members.keywords))) {
                        hits.push_back({
                            source.ruleID + ":base", source.category, source.group,
                            source.providerPriority, source.pluginLoadOrder, 0,
                            source.listPriority, false
                        });
                    }
                    if (!source.instanceContext) continue;
                    bool matched = AnyKeywordID(keywordCache.addedFormIDs, source.members.keywords);
                    int rolePriority = g_unknownRolePriorities.contains(source.group)
                        ? g_unknownRolePriorities.at(source.group) : 0;
                    for (auto* omod : omods) {
                        if (!omod) continue;
                        if (source.members.omods.contains(omod->GetFormID())) matched = true;
                        rolePriority = std::max(rolePriority, GetWeaponRolePriority(omod, source.group));
                    }
                    if (matched) {
                        hits.push_back({
                            source.ruleID + ":instance", source.category, source.group,
                            source.providerPriority, source.pluginLoadOrder, rolePriority,
                            source.listPriority, true
                        });
                    }
                }
            }
            return hits;
        }

        int CompareHitStrength(
            const CategoryHit& a_left,
            const CategoryHit& a_right,
            const GroupPolicy& a_policy)
        {
            if (g_providerOrderMode == ProviderOrderMode::JsonPriorityFirst) {
                if (a_left.sourceTier != a_right.sourceTier)
                    return a_left.sourceTier > a_right.sourceTier ? 1 : -1;
                if (a_left.pluginLoadOrder != a_right.pluginLoadOrder)
                    return a_left.pluginLoadOrder > a_right.pluginLoadOrder ? 1 : -1;
            } else {
                if (a_left.pluginLoadOrder != a_right.pluginLoadOrder)
                    return a_left.pluginLoadOrder > a_right.pluginLoadOrder ? 1 : -1;
                if (a_left.sourceTier != a_right.sourceTier)
                    return a_left.sourceTier > a_right.sourceTier ? 1 : -1;
            }
            if (a_policy.resolutionPriorityBeforeSource &&
                a_left.resolutionPriority != a_right.resolutionPriority) {
                return a_left.resolutionPriority > a_right.resolutionPriority ? 1 : -1;
            }
            if (a_left.instanceMatch != a_right.instanceMatch)
                return a_left.instanceMatch ? 1 : -1;
            if (!a_policy.resolutionPriorityBeforeSource &&
                a_left.resolutionPriority != a_right.resolutionPriority) {
                return a_left.resolutionPriority > a_right.resolutionPriority ? 1 : -1;
            }
            if (a_left.priority != a_right.priority)
                return a_left.priority > a_right.priority ? 1 : -1;
            return 0;
        }
    }

    void Load()
    {
        // Bump the generation without taking the cache lock before acquiring
        // g_mutex. Old entries become unreachable lazily, which avoids an
        // inverted cache/configuration lock order during a reload.
        g_resultCacheGeneration.fetch_add(1, std::memory_order_acq_rel);
        std::unique_lock lock(g_mutex);
        g_rules.clear();
        g_groupPolicies.clear();
        g_weaponSources.clear();
        g_weaponRoles.clear();
        g_unknownRolePriorities.clear();
        const std::filesystem::path directory("Data\\F4SE\\Plugins\\ConditionSystemFramework\\Classifications");
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (!std::filesystem::exists(directory)) return;

        LoadProviderSelection(directory / "ProviderSelection.json");
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
            if (!ec && entry.is_regular_file() && entry.path().extension() == ".json" &&
                _stricmp(entry.path().filename().string().c_str(), "ProviderSelection.json") != 0)
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files) LoadFile(path);
        ArmorConcepts::Load();
        REX::INFO("[Classification] Loaded {} legacy rules and {} weapon sources from {} Provider files.",
            g_rules.size(), g_weaponSources.size(), files.size());
    }

    bool Result::Has(std::string_view a_category) const
    {
        return std::find(categories.begin(), categories.end(), a_category) != categories.end();
    }

    std::optional<std::string> Result::GetSelected(std::string_view a_group) const
    {
        const auto it = selectedByGroup.find(std::string(a_group));
        return it == selectedByGroup.end() ? std::nullopt : std::optional<std::string>{ it->second };
    }

    Result Evaluate(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList)
    {
        Result result;
        const auto cacheKey = MakeResultCacheKey(a_object, a_extraList);
        {
            std::shared_lock cacheLock(g_resultCacheMutex);
            if (const auto it = g_resultCache.find(cacheKey); it != g_resultCache.end()) {
                return it->second;
            }
        }

        {
            std::shared_lock lock(g_mutex);
            const auto hits = GetHits(a_object, a_extraList);
            std::vector<CategoryHit> selected;
            std::unordered_map<std::string, CategoryHit> exclusive;
            std::unordered_map<std::string, CategoryHit> coexisting;
            std::unordered_set<std::string> unresolvedGroups;
            for (const auto& hit : hits) {
                if (hit.exclusiveGroup.empty()) {
                    selected.push_back(hit);
                    continue;
                }
                auto it = exclusive.find(hit.exclusiveGroup);
                const auto policyIt = g_groupPolicies.find(hit.exclusiveGroup);
                const GroupPolicy policy =
                    policyIt == g_groupPolicies.end() ? GroupPolicy{} : policyIt->second;
                if (policy.coexisting) {
                    const auto key = hit.exclusiveGroup + "\n" + hit.id;
                    if (const auto existing = coexisting.find(key); existing == coexisting.end()) {
                        coexisting.emplace(key, hit);
                    } else if (CompareHitStrength(hit, existing->second, policy) > 0) {
                        existing->second = hit;
                    }
                    continue;
                }
                if (it == exclusive.end()) {
                    exclusive[hit.exclusiveGroup] = hit;
                    continue;
                }
                const int comparison = CompareHitStrength(hit, it->second, policy);
                if (comparison > 0) {
                    it->second = hit;
                    unresolvedGroups.erase(hit.exclusiveGroup);
                } else if (comparison == 0) {
                    if (policy.unresolvedTieIsUnknown && hit.id != it->second.id) {
                        unresolvedGroups.insert(hit.exclusiveGroup);
                    } else if (hit.ruleID < it->second.ruleID) {
                        it->second = hit;
                    }
                }
            }
            for (const auto& [key, hit] : coexisting) selected.push_back(hit);
            for (const auto& [group, hit] : exclusive) {
                if (unresolvedGroups.contains(group)) {
                    REX::WARN("[Classification] Unresolved '{}' conflict for {:08X}; returning Unknown.",
                        group, a_object ? a_object->GetFormID() : 0);
                    continue;
                }
                selected.push_back(hit);
            }
            std::sort(selected.begin(), selected.end(), [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });

            std::unordered_set<std::string> seen;
            result.categories.reserve(selected.size());
            for (const auto& hit : selected) {
                if (seen.insert(hit.id).second) result.categories.push_back(hit.id);
                const auto policy = g_groupPolicies.find(hit.exclusiveGroup);
                if (!hit.exclusiveGroup.empty() &&
                    (policy == g_groupPolicies.end() || !policy->second.coexisting))
                    result.selectedByGroup[hit.exclusiveGroup] = hit.id;
                result.diagnostics.push_back({ hit.ruleID, hit.id, hit.exclusiveGroup, hit.sourceTier,
                    hit.resolutionPriority, hit.priority, hit.instanceMatch });
            }
            if (a_object && a_object->Is(RE::ENUM_FORM_ID::kWEAP) &&
                std::find(result.categories.begin(), result.categories.end(), "weapon") == result.categories.end()) {
                result.categories.push_back("weapon");
            }
            if (a_object && a_object->Is(RE::ENUM_FORM_ID::kARMO)) {
                if (std::find(result.categories.begin(), result.categories.end(), "armor") == result.categories.end()) {
                    result.categories.push_back("armor");
                }
                for (const auto& group : { std::string("Armor.Material"), std::string("Armor.Region"), std::string("Armor.Grade"),
                         std::string("Armor.Flag"), std::string("Equipment.Domain") }) {
                    result.categories.erase(std::remove_if(result.categories.begin(), result.categories.end(),
                        [&](const auto& value) { return value.starts_with(group + "."); }), result.categories.end());
                    result.selectedByGroup.erase(group);
                }
                for (const auto& selection : ArmorConcepts::Evaluate(a_object, a_extraList)) {
                    result.categories.push_back(selection.category);
                    result.selectedByGroup[selection.group] = selection.category;
                    result.diagnostics.push_back({ selection.source, selection.category, selection.group,
                        0, selection.modRolePriority, selection.listPriority, selection.instance });
                }
            }
        }

        {
            std::unique_lock cacheLock(g_resultCacheMutex);
            // Bound historical entries when inventory instances are modified
            // or freed. Old profile generations are also reclaimed here.
            if (g_resultCache.size() >= 4096) g_resultCache.clear();
            g_resultCache.insert_or_assign(cacheKey, result);
        }
        return result;
    }

    std::vector<std::string> GetCategories(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList)
    {
        return Evaluate(a_object, a_extraList).categories;
    }

    bool HasCategory(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList, const std::string& a_category)
    {
        const auto categories = GetCategories(a_object, a_extraList);
        return std::find(categories.begin(), categories.end(), a_category) != categories.end();
    }

    bool HasAnyCategory(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList, const std::vector<std::string>& a_categories)
    {
        const auto categories = GetCategories(a_object, a_extraList);
        for (const auto& category : a_categories) {
            if (std::find(categories.begin(), categories.end(), category) != categories.end()) return true;
        }
        return false;
    }

    std::optional<std::string> GetCategoryInGroup(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList, std::string_view a_exclusiveGroup)
    {
        return Evaluate(a_object, a_extraList).GetSelected(a_exclusiveGroup);
    }

    bool ShareCategory(RE::TESBoundObject* a_left, RE::ExtraDataList* a_leftExtra, RE::TESBoundObject* a_right, RE::ExtraDataList* a_rightExtra, const std::string& a_exclusiveGroup)
    {
        if (a_exclusiveGroup.empty()) return false;
        const auto leftCategory = Evaluate(a_left, a_leftExtra).GetSelected(a_exclusiveGroup);
        const auto rightCategory = Evaluate(a_right, a_rightExtra).GetSelected(a_exclusiveGroup);
        return leftCategory && rightCategory && *leftCategory == *rightCategory;
    }
}

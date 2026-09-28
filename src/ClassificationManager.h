#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RE
{
    class TESBoundObject;
    class ExtraDataList;
}

namespace ConditionSystem::Classification
{
    enum class Domain : std::uint8_t
    {
        Any,
        Weapon,
        Armor
    };

    struct MatchedRule
    {
        std::string ruleID;
        std::string categoryID;
        std::string exclusiveGroup;
        int sourceTier{ 0 };
        int resolutionPriority{ 0 };
        int priority{ 0 };
        bool instanceMatch{ false };
    };

    struct Result
    {
        std::vector<std::string> categories;
        std::unordered_map<std::string, std::string> selectedByGroup;
        std::vector<MatchedRule> diagnostics;

        [[nodiscard]] bool Has(std::string_view a_category) const;
        [[nodiscard]] std::optional<std::string> GetSelected(std::string_view a_group) const;
    };

    // Loads all classification contributions. Unlike skill providers, every
    // contribution whose dependencies are loaded is merged into the registry.
    void Load();

    Result Evaluate(
        RE::TESBoundObject* a_object,
        RE::ExtraDataList* a_extraList);

    // Returns the effective categories for the current item instance. Base
    // form matches are evaluated first; matching instance OMOD rules override
    // categories in the same exclusive group.
    std::vector<std::string> GetCategories(
        RE::TESBoundObject* a_object,
        RE::ExtraDataList* a_extraList);

    bool HasCategory(
        RE::TESBoundObject* a_object,
        RE::ExtraDataList* a_extraList,
        const std::string& a_category);

    bool HasAnyCategory(
        RE::TESBoundObject* a_object,
        RE::ExtraDataList* a_extraList,
        const std::vector<std::string>& a_categories);

    std::optional<std::string> GetCategoryInGroup(
        RE::TESBoundObject* a_object,
        RE::ExtraDataList* a_extraList,
        std::string_view a_exclusiveGroup);

    bool ShareCategory(
        RE::TESBoundObject* a_left,
        RE::ExtraDataList* a_leftExtra,
        RE::TESBoundObject* a_right,
        RE::ExtraDataList* a_rightExtra,
        const std::string& a_exclusiveGroup);
}

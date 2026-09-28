#pragma once

#include <string>
#include <vector>

namespace RE
{
    class ExtraDataList;
    class TESBoundObject;
}

namespace ConditionSystem::ArmorConcepts
{
    struct Selection
    {
        std::string group;
        std::string category;
        std::string source;
        int providerPriority{ 0 };
        std::uint32_t pluginLoadOrder{ 0 };
        int contextPriority{ 0 };
        int modRolePriority{ 0 };
        int listPriority{ 0 };
        bool instance{ false };
    };

    void Load();
    std::vector<Selection> Evaluate(RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra);
}

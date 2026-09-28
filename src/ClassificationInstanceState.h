#pragma once

#include <cstdint>
#include <vector>

namespace ConditionSystem::Classification
{
    // Store values, not just engine allocation addresses: both arrays can be
    // edited in place while their owner and length remain unchanged.
    struct InstanceState
    {
        std::vector<std::uint32_t> keywords;
        std::vector<std::uint32_t> omods;
        bool operator==(const InstanceState&) const = default;
    };
}

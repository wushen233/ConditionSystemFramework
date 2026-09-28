#pragma once

#include <algorithm>
#include <cmath>

namespace ConditionSystem::Math
{
    enum class ArmorWearPhase { Scratch, Normal, Glance };

    inline void NormalizePhaseThresholds(float& glance, float& scratch)
    {
        if (!std::isfinite(glance) || !std::isfinite(scratch)) {
            glance = 0.7f;
            scratch = 0.3f;
            return;
        }
        glance = (std::clamp)(glance, 0.0f, 1.0f);
        scratch = (std::clamp)(scratch, 0.0f, 1.0f);
        if (scratch > glance) std::swap(scratch, glance);
        if (scratch == glance) {
            glance = 0.7f;
            scratch = 0.3f;
        }
    }

    inline ArmorWearPhase SelectArmorWearPhase(float blocked, float glance, float scratch)
    {
        NormalizePhaseThresholds(glance, scratch);
        if (blocked >= glance) return ArmorWearPhase::Glance;
        if (blocked <= scratch) return ArmorWearPhase::Scratch;
        return ArmorWearPhase::Normal;
    }

    inline float ValidMaximum(float value, float fallback)
    {
        return std::isfinite(value) && value > 0.0f ? value : fallback;
    }

    template<class Thresholds>
    auto SelectSkillThreshold(const Thresholds& thresholds, float skillValue)
        -> const typename Thresholds::value_type*
    {
        if (!std::isfinite(skillValue)) return nullptr;
        const typename Thresholds::value_type* selected = nullptr;
        for (const auto& tier : thresholds) {
            if (skillValue >= tier.threshold && (!selected || tier.threshold > selected->threshold))
                selected = &tier;
        }
        return selected;
    }
}

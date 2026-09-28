#include "ConditionMath.h"
#include "ClassificationInstanceState.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace ConditionSystem;

void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int main()
{
    try {
        using Phase = Math::ArmorWearPhase;
        // Reproduce the original branch order before exercising the production
        // selector. The legacy defaults cannot reach Normal at any ratio.
        int legacyNormal = 0;
        int fixedNormal = 0;
        for (int i = 0; i <= 100; ++i) {
            const float blocked = i / 100.0f;
            if (!(blocked >= 0.3f) && !(blocked <= 0.7f)) ++legacyNormal;
            if (Math::SelectArmorWearPhase(blocked, 0.3f, 0.7f) == Phase::Normal) ++fixedNormal;
        }
        Require(legacyNormal == 0 && fixedNormal > 0, "Normal interception must be reachable with legacy defaults");
        for (const auto reversed : {false, true}) {
            const float glance = reversed ? 0.3f : 0.7f;
            const float scratch = reversed ? 0.7f : 0.3f;
            Require(Math::SelectArmorWearPhase(0.1f, glance, scratch) == Phase::Scratch, "Low block phase");
            Require(Math::SelectArmorWearPhase(0.3f, glance, scratch) == Phase::Scratch, "Scratch boundary");
            Require(Math::SelectArmorWearPhase(0.5f, glance, scratch) == Phase::Normal, "Normal block phase");
            Require(Math::SelectArmorWearPhase(0.7f, glance, scratch) == Phase::Glance, "Glance boundary");
            Require(Math::SelectArmorWearPhase(0.9f, glance, scratch) == Phase::Glance, "High block phase");
        }
        Require(Math::SelectArmorWearPhase(0.25f, 0.95f, 0.1f) == Phase::Normal, "Custom thresholds must not be overridden by hardcoded limits");
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const auto inf = std::numeric_limits<float>::infinity();
        for (const float bad : {nan, inf, 0.5f}) {
            float glance = bad, scratch = bad;
            Math::NormalizePhaseThresholds(glance, scratch);
            Require(std::isfinite(glance) && scratch < glance, "Invalid thresholds must preserve a normal band");
        }
        for (const float bad : {0.0f, -1.0f, nan, inf}) {
            const auto maximum = Math::ValidMaximum(bad, 500.0f);
            Require(maximum == 500.0f && std::isfinite(std::round(0.6f * maximum) / maximum), "Repair maximum must not divide by zero or NaN");
        }
        Require(Math::ValidMaximum(750.0f, 500.0f) == 750.0f, "Valid custom maximum must survive");

        struct Tier { float threshold; float wearMultiplier; };
        const std::vector<Tier> tiers{{75.0f, 0.8f}, {1.0f, 1.15f}, {25.0f, 1.0f}};
        Require(!Math::SelectSkillThreshold(tiers, 0.0f), "Missing perk must not grant wear bonus");
        Require(Math::SelectSkillThreshold(tiers, 1.0f)->wearMultiplier == 1.15f, "Built to Destroy wear");
        Require(Math::SelectSkillThreshold(tiers, 50.0f)->threshold == 25.0f, "Highest eligible unsorted tier");
        Require(Math::SelectSkillThreshold(tiers, 100.0f)->threshold == 75.0f, "Highest tier");
        Require(!Math::SelectSkillThreshold(tiers, nan), "Invalid skill must not match");

        Classification::InstanceState original{{10, 20}, {100, 200}};
        auto changed = original;
        const auto* oldAllocation = changed.omods.data();
        changed.omods[1] = 300;
        Require(oldAllocation == changed.omods.data() && original != changed, "OMOD replacement at the same address and length must invalidate cache");
        changed = original;
        changed.keywords[1] = 30;
        Require(original != changed, "Keyword replacement with the same count must invalidate cache");
        changed = original;
        changed.omods.pop_back();
        Require(original != changed, "Removed or disabled OMOD must invalidate cache");
        Require(original == original, "Unchanged instance must retain cache identity");
        std::cout << "[PASS] armor phases, skill tiers, finite repair maxima, and instance cache state\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}

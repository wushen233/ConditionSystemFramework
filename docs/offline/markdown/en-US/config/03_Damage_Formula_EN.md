# Damage Formula Module

Runtime directory:

```text
Data/F4SE/Plugins/ConditionSystemFramework/Durability/DamageFormula/
```

The current loader reads a `DamageFormula` object. These are the fields present in the active runtime structure:

```jsonc
{
  "DamageFormula": {
    "enabled": true,
    "baseHardness": 1.0,
    "glanceThreshold": 0.3,
    "glanceMultiplier": 0.1,
    "scratchThreshold": 0.7,
    "scratchMultiplier": 0.4,
    "durabilityDamageConstant": 0.15
  }
}
```

## Calculation Path

When hit data is available, CSF calculates:

```text
blockPercentage = totalResisted / totalRawDamage
```

It then selects a wear phase:

| Condition | Phase | Multiplier |
|---|---|---|
| `blockPercentage >= glanceThreshold`, or >= 0.90 | Perfect defense / armor absorbs the impact | `glanceMultiplier` |
| `blockPercentage <= scratchThreshold`, or <= 0.20 | Armor is penetrated / provides little interception | `scratchMultiplier` |
| Otherwise | Normal interception | `1.0` |

With hit data, the base durability loss is:

```text
totalResisted
  * durabilityDamageConstant
  * phase multiplier
  * inner/outer layer multiplier
  * power armor frame multiplier
  * ammo armorDamageMult
```

Without usable hit data, CSF derives hardness from effective resistance:

```text
hardness = max(1.0, baseHardness + effectiveResist)
durabilityCost = (totalImpact / hardness)
                 * durabilityDamageConstant
                 * inner/outer layer multiplier
                 * power armor frame multiplier
                 * ammo armorDamageMult
```

Armor OMODs, skill adjustments and `fArmorWearMultiplier` are applied afterwards. The final armor loss is clamped to a minimum of 0.01.

## Fields

| Field | Purpose |
|---|---|
| `enabled` | Parsed by the loader, but not currently used as a bypass switch |
| `baseHardness` | Base hardness in the no-hit-data path |
| `glanceThreshold` | Threshold for the low-loss interception phase |
| `glanceMultiplier` | Multiplier for the low-loss phase |
| `scratchThreshold` | Threshold for the penetration/low-interception phase |
| `scratchMultiplier` | Multiplier for the penetration phase |
| `durabilityDamageConstant` | Global damage-to-durability constant |

Thresholds are ratios, normally between `0.0` and `1.0`; do not write them as percentages such as `30` or `70`.

## `ratingMultipliers` Status

The current `01_DamageFormula.json` also contains `ratingMultipliers` and damage-type notes. The current `ProfileManager.cpp` loader does not read that object, and `DamageFormulaConfig` has no corresponding field. Editing `ratingMultipliers` therefore has no runtime effect at present.

It remains in the example as a historical design trace, not as an active configuration feature. Enabling damage-type-specific hardness multipliers would require changes to the C++ structure, loader, calculation path and documentation.

## Relationship to MCM

- `enabled` is parsed, but the current calculation path does not use it to skip the formula.
- `bEnableArmorCondition` is the MCM master switch for armor durability.
- `bArmorConditionAffectsResistance` controls whether current armor condition affects game resistance.
- `fArmorWearMultiplier` is the final total armor-wear multiplier.


# Loot Module

Runtime directory:

```text
Data/F4SE/Plugins/ConditionSystemFramework/Loot/
```

The active submodule is `Loot/ConditionBonus/`. `Loot/InitialCondition/` currently contains no active JSON; random initial condition ranges are controlled by MCM.

## Provider Selection

`Loot/ConditionBonus/` selects one Provider. The file must be named:

```text
<RequiredPlugin>.json
```

The Provider is skipped when its dependency is not loaded. Current `Loot/ProviderSelection.json` uses `PluginLoadOrderFirst`: compare the required plugin load order first, then `ProviderPriority`.

## ConditionBonus

Current `Loot/ConditionBonus/Fallout4.esm.json` uses:

```jsonc
{
  "LootBonusRule": {
    "GlobalCap": 2.0,
    "RequiredSkill": {
      "Type": "AV",
      "Plugin": "Fallout4.esm",
      "FormID": "0x000002C8",
      "ScaleFactor": 100.0
    },
    "Tiers": [
      {
        "minLevel": 5.0,
        "hitChance": 2.0,
        "bonusMin": 0.05,
        "bonusMax": 0.15
      }
    ]
  },
  "RequiredPlugin": "Fallout4.esm",
  "ProviderPriority": 0,
  "schemaVersion": 1,
  "module": "Loot.ConditionBonus"
}
```

Fields:

| Field | Purpose |
|---|---|
| `GlobalCap` | Highest condition multiplier after the bonus, e.g. `2.0` means 200% |
| `RequiredSkill` | Player skill or attribute to read |
| `minLevel` | Minimum skill value for the tier |
| `hitChance` | Percentage chance for the tier, using 0 to 100 |
| `bonusMin` / `bonusMax` | Random condition increase after a successful roll |

Runtime uses the highest `minLevel` reached by the current skill. Keep the array ordered from low to high `minLevel`; the last eligible tier becomes active.

The flow is:

1. The item must be managed by CSF.
2. Read `RequiredSkill`.
3. Select the highest eligible tier.
4. Roll `hitChance`.
5. On success, add a random value between `bonusMin` and `bonusMax`.
6. Clamp the result with `GlobalCap` and MCM `bEnableOverCondition`.

This module affects initial condition for newly obtained items. It does not change repair efficiency or combat wear.

## Initial Condition and MCM

When `bRandomLootDurability=1`:

- weapons are randomized between `fWeaponLootDurabilityMin` and `fWeaponLootDurabilityMax`;
- normal armor is randomized between `fArmorLootDurabilityMin` and `fArmorLootDurabilityMax`;
- ConditionBonus adds the skill-based bonus to the randomized result.

`LootBonus_Perk_Example.json.disabled` is not active configuration because its extension is `.disabled`.


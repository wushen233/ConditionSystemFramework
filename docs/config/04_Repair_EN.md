# Repair Module

Runtime directory:

```text
Data/F4SE/Plugins/ConditionSystemFramework/Repair/
```

The repair module has three independent submodules:

```text
Repair/JuryRigging/   Jury Rigging: whether a donor can repair a target
Repair/OverRepair/    Over-Repair: the maximum allowed condition
Repair/RepairKits/    Repair kits: what a kit can repair and how much
```

Repair eligibility, the repair cap and kit strength are separate concerns. Do not treat `DisplaySkill`, `RequiredSkill` and the MCM caps as one condition.

## Provider Selection

Repair Providers are single-selection files. Each candidate must satisfy:

1. The root object has `RequiredPlugin`.
2. The filename is exactly `<RequiredPlugin>.json`, case-insensitively.
3. `RequiredPlugin` is loaded.
4. `Repair/ProviderSelection.json` selects the winning candidate.

Current modes:

| Submodule | Current mode |
|---|---|
| `JuryRigging` | `JsonPriorityFirst`: ProviderPriority, then plugin load order |
| `OverRepair` | `PluginLoadOrderFirst`: plugin load order, then ProviderPriority |
| `RepairKits` | Reads the `0_DefaultKits.json` template rather than selecting a skill Provider |

`Fallout4.esm.json`, `Classic Skill System.esp.json`, `StandaloneRepair.esp.json` and `You Are Exceptional.esp.json` are independent Provider candidates. They are not merged into one rule set.

## RequiredSkill

Repair rules use:

```jsonc
{
  "Type": "AV",
  "Plugin": "Classic Skill System.esp",
  "FormID": "0xFA0",
  "MinRank": 1.0,
  "ScaleFactor": 1.0
}
```

Supported types:

- `AV`: reads an Actor Value.
- `Perk`: reads the player's Perk Rank.
- `Global`: reads a Global value.

`EditorID` may be used instead. `MinRank` is used as a minimum value by rules that rely on it; `ScaleFactor` is applied before thresholds are evaluated. If the plugin or FormID cannot resolve, the rule does not grant repair permission.

## Jury Rigging

### Provider Root

```jsonc
{
  "RequiredPlugin": "Classic Skill System.esp",
  "schemaVersion": 3,
  "module": "Repair.JuryRigging",
  "ProviderPriority": 100,
  "DisplaySkill": { "Type": "AV", "Plugin": "Classic Skill System.esp", "FormID": "0xFA0" },
  "RepairEfficiency": { "Type": "FNV", "Skill": { "Type": "AV", "Plugin": "Classic Skill System.esp", "FormID": "0xFA0" } },
  "JuryRiggingRules": []
}
```

### DisplaySkill

`DisplaySkill` controls only the skill value shown in the top-right area of the Jury Rigging screen. It does not:

- grant cross-category repair permission;
- change `RequiredSkill`; or
- change the workbench UI.

This allows the UI to show Repair skill while eligibility remains controlled by `RequiredSkill` or the game's own Perk conditions.

### RepairEfficiency

The current loader accepts only `Type: "FNV"`. The mixed repair addition is calculated as:

```text
better        = max(targetCondition, materialCondition)
lower         = min(targetCondition, materialCondition)
combined      = better
              + lower * LowerConditionMultiplier
              + BaseBonus
              + RepairSkill * SkillMultiplier
rawAddition   = max(0, combined - targetCondition)
finalAddition = rawAddition * JuryRuleEfficiency
```

The current Classic Skill System Provider uses:

```text
BaseBonus                = 0.05
LowerConditionMultiplier = 0.05
SkillMultiplier          = 0.0015
```

`JuryRuleEfficiency` comes from `MaterialConditionMultiplier` and then `EfficiencyModifiers`. Repair skill therefore affects both the eligibility layer and the amount repaired, rather than only appearing in the UI.

### JuryRiggingRules

Each rule needs target and donor predicates plus a compatibility condition:

```jsonc
{
  "RuleName": "Same Family Repair",
  "Priority": 100,
  "MaterialConditionMultiplier": 0.8,
  "AppliesTo": {
    "target": { "all": ["weapon"] },
    "material": { "all": ["weapon"] }
  },
  "Compatibility": {
    "all": [{ "type": "sameGroup", "group": "Weapon.Handling" }]
  }
}
```

`AppliesTo.target` and `AppliesTo.material` are evaluated independently. Predicate fields mean:

- `all`: every category is required.
- `any`: at least one category is required.
- `none`: none of the categories may be present.

For example:

```jsonc
"none": ["Weapon.Flag.Ignore"]
```

### Compatibility Types

| Type | Condition |
|---|---|
| `always` | Adds no category relationship; target/donor predicates still apply |
| `sameBase` | Both items have the same base FormID |
| `sameGroup` | Both select the same category in the group; missing categories fail |
| `sameOptionalGroup` | Both are missing the group, or both select the same category; one missing and one present fails |
| `sharesAnyCategory` | Both share at least one category under the group prefix |
| `groupPair` | Target and donor categories must be in `pairs`; `symmetric: true` also accepts the reverse |

All entries in `Compatibility.all` must pass.

### Rule Priority

All matching rules are evaluated. The winner is selected by:

1. Higher `Priority`.
2. Higher efficiency when Priority ties.
3. Lexicographically earlier `RuleName` as the deterministic final tie-break.

`MaterialConditionMultiplier` below 1.0 represents an efficiency penalty. `SkillThresholds` on a Jury rule currently acts primarily as an entry threshold: when `RequiredSkill` is configured, the current value must reach the first sorted threshold. `maxValue` is not used as a Jury repair cap; OverRepair and MCM control the cap separately.

### Current CSF Repair Design

The current Classic Skill System Provider is designed as follows:

- Exact same-base repairs use the normal repair rule and do not require Jury Rigging.
- Other cross-record repairs normally require `NVSS_Perk_JuryRigging`.
- Ranged weapons use Family, Handling and Configuration to calculate compatibility and efficiency.
- Melee weapons use Family and Handling.
- Pipe weapons receive an additional cross-Handling rule through `Weapon.Modifier.Pipe`; ordinary weapons are not made universally interchangeable.
- Armor uses Material, Region and Grade. `Armor.Flag.Ignore` and `Armor.Flag.NoDonor` are excluded.

The old `TargetKeywords`, `MatchCategories` and `RankLimits` fields are not read by the current Jury loader. New rules should use `AppliesTo`, `Compatibility`, `RequiredSkill` and `SkillThresholds`.

## OverRepair

Path: `Repair/OverRepair/<RequiredPlugin>.json`.

```jsonc
{
  "RequiredPlugin": "Classic Skill System.esp",
  "schemaVersion": 3,
  "module": "Repair.OverRepair",
  "ProviderPriority": 100,
  "OverRepairRules": [
    {
      "RuleName": "Over-Repair Firearms",
      "RequiredSkill": { "Type": "AV", "Plugin": "Classic Skill System.esp", "FormID": "0xFA0" },
      "SkillThresholds": [
        { "threshold": 0.0, "maxValue": 1.0 },
        { "threshold": 50.0, "maxValue": 1.5 },
        { "threshold": 100.0, "maxValue": 2.0 }
      ],
      "AppliesTo": {
        "categories": ["Weapon.Handling.Pistol", "Weapon.Handling.Rifle"]
      }
    }
  ]
}
```

OverRepair selects the highest threshold that does not exceed the current skill value and returns its `maxValue`. The final cap is:

```text
Jury Rigging: min(MCM fJuryRepairCap, OverRepair skill cap)
Workbench:     min(MCM fWorkbenchRepairCap, OverRepair skill cap)
```

The workbench does not display Repair skill; it only uses the resulting cap. When `bEnableOverCondition=0`, OverRepair and repair-kit caps are limited to 100%.

## RepairKits

Path: `Repair/RepairKits/*.json`. The current template root key is `profiles`.

```jsonc
{
  "profiles": [
    {
      "name": "Expert Weapon Repair Kit",
      "formIDs": { "CSF_RepairKit.esp": ["F09"] },
      "canRepairWeapon": true,
      "canRepairArmor": false,
      "baseRepairPoints": 1200,
      "maxConditionLimit": 1.25
    }
  ]
}
```

- `baseRepairPercent`: repair a percentage of target maximum durability.
- `baseRepairPoints`: repair a fixed number of durability points; its presence selects point mode.
- `maxConditionLimit`: kit-specific maximum target condition.
- `canRepairWeapon` / `canRepairArmor`: item-type permissions.
- `formIDs` and `keywords`: identify the kit.

The kit limit is still affected by MCM `bEnableOverCondition`. `DisplaySkill` does not grant additional kit permissions.


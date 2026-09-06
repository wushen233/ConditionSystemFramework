# Durability Consumption Module

Base directory:

```text
Data/F4SE/Plugins/ConditionSystemFramework/Durability/Consumption/
```

This module controls how items wear down. It does not define what an item is; that comes from the [Classifications](01_Classifications_EN.md) module.

## Shared Profile Matching

`Weapons/*.json`, `Armors/*.json`, `Ammos/*.json`, `Modifiers/*.json` and `Omods/*.json` use the following general shape:

```jsonc
{
  "profiles": [
    {
      "name": "My Profile",
      "priority": 100,
      "formIDs": ["MyMod.esp|000123"],
      "keywords": [["Keyword_A", "Keyword_B"]]
    }
  ]
}
```

`formIDs` accepts an array or a plugin-keyed object. `keywords` uses outer OR and inner AND semantics: the example matches when both `Keyword_A` and `Keyword_B` are present, or when another keyword group matches.

For weapon and armor base profiles, the effective order is:

1. `AppliesTo` category predicates.
2. Exact FormID.
3. Keyword AND groups.
4. Default profile.

Profiles with `AppliesTo` are sorted by priority. Use explicit priorities and file organization when several FormID or keyword profiles could match.

## Weapon Base Profiles

Path: `Durability/Consumption/Weapons/*.json`

| Field | Purpose |
|---|---|
| `name` | Name used in logs and diagnostics |
| `priority` | Profile ordering priority |
| `maxDurability` | Base maximum durability points |
| `degradeRate` | Base wear per valid shot or melee attack |
| `canJam` | Allows the automatic fault mechanism |
| `mechanism` | `auto`, `ballistic`, `energy`, `overheat` or `none` |
| `isExcluded` | Skips the matched weapon in durability-loss paths; not a full exclusion |
| `AppliesTo` | Category predicate |

`mechanism: auto` lets the code determine ballistic jamming, energy faults or overheating from the weapon and ammo. Explicit `none` disables the fault mechanism; explicit `overheat` routes the weapon to the heat system.

The current `VanillaWeapons.json` selects a base profile from Handling, then applies Family and Configuration adjustments through CategoryModifiers.

## Armor Base Profiles

Path: `Durability/Consumption/Armors/*.json`

| Field | Purpose |
|---|---|
| `maxDurability` | Base maximum durability points |
| `useFlatDegrade` | Uses fixed wear per hit instead of the damage formula |
| `degradeRate` | Fixed wear value or special profile base value |
| `isExcluded` | Skips the matched armor in durability-loss paths; not a full exclusion |
| `keywords` | Base ARMO keyword AND groups |
| `AppliesTo` | Material, Region, Grade and other categories |

Normal armor generally gets its base maximum from Material profiles. Region and Grade are added through CategoryModifiers.

## Ammo Profiles

Path: `Durability/Consumption/Ammos/*.json`

```jsonc
{
  "profiles": [
    {
      "name": "Armor Piercing",
      "weaponWearMult": 1.5,
      "armorDamageMult": 2.0,
      "formIDs": ["MyAmmo.esp|000123"]
    }
  ]
}
```

- `weaponWearMult` multiplies weapon wear.
- `armorDamageMult` multiplies the impact dealt to armor durability.
- Ammo profiles do not define weapon mechanisms. An ammo wear profile alone cannot turn a normal 10mm pistol into an overheating weapon.

## CategoryModifiers

Path: `Durability/Consumption/CategoryModifiers/*.json`.

The root key is `modifiers`. Every matching modifier is applied:

```text
maxDurability = maxDurability * maxDurabilityMult
degradeRate   = degradeRate * degradeMult + degradeRateFlat
```

`maxDurabilityMult` and `degradeMult` multiply; `degradeRateFlat` values add. `Domain` may be `Weapon`, `Armor` or `Any`. `AppliesTo` uses `all`, `any` and `none`.

The current `VanillaCategoryModifiers.json` adjusts weapon Families, weapon Configurations, armor Grades and armor Regions.

## OMOD and Legendary Modifiers

Paths:

```text
Durability/Consumption/Omods/*.json
Durability/Consumption/Modifiers/*.json
```

OMOD fields:

- `degradeMult`: wear multiplier; all installed OMOD values multiply.
- `degradeRateFlat`: fixed wear; all installed OMOD values add.
- `maxDurabilityMult`: maximum durability multiplier; all installed OMOD values multiply.
- `immuneToJam`: any matching OMOD can grant jam immunity.

Legendary `ModifierProfile` uses the same `degradeMult` and `immuneToJam` concepts, but matches the legendary modifier instead of a normal OMOD.

## SkillModifiers

Path: `Durability/Consumption/SkillModifiers/*.json`.

Skill durability modifiers change wear; they do not grant repair permission:

```jsonc
{
  "modifiers": [
    {
      "name": "BuiltToDestroy",
      "Domain": "Weapon",
      "RequiredSkill": {
        "Type": "Perk",
        "Plugin": "Classic Skill System.esp",
        "FormID": "0xD166",
        "MinRank": 1
      },
      "SkillThresholds": [
        { "threshold": 1.0, "wearMultiplier": 1.15, "wearRateFlat": 0.0 }
      ],
      "AppliesTo": { "all": ["weapon"] }
    }
  ]
}
```

For each matching rule, runtime selects the highest threshold that does not exceed the current skill value. The selected multipliers multiply together and the selected flat values add. The current Classic Skill System file implements Built to Destroy's +15% weapon wear while leaving its critical-hit effect to the skill system.

## Weapon Wear Pipeline

The effective wear is approximately:

```text
base degradeRate
  * legendary/OMOD degradeMult
  * ammo weaponWearMult
  * SkillModifiers wearMultiplier
  + OMOD degradeRateFlat
  + SkillModifiers wearRateFlat
  * MCM fWeaponWearMultiplier
  * overheat multiplier, when the current weapon is overheated
```

Armor wear starts with the damage formula result and then applies OMOD, skill and MCM armor-wear modifiers.

## `isExcluded` Scope

`WeaponProfile` and `ArmorProfile` support `isExcluded: true` together with exact `formIDs`. The current source uses this to skip relevant durability-loss and some fault/loot processing paths. It is not a classification marker and does not replace `Weapon.Flag.Ignore` or `Armor.Flag.Ignore`.

If the goal is to remove an item from all CSF processing, including durability UI and repair target/donor lists, use an Ignore FLST from the Classifications module. Use `isExcluded` when the goal is specifically to stop wear.


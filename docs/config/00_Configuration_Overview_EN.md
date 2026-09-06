# CSF Configuration Overview

This document set describes the configuration that the current `ConditionSystemFramework` source actually reads. The runtime configuration root is:

```text
Data/F4SE/Plugins/ConditionSystemFramework/
```

Configuration files are parsed as JSONC, so `//` and `/* ... */` comments are supported. After changing a configuration file, fully restart the game to avoid stale caches or previously selected Providers.

## Module Documents

| Document | Runtime path | Purpose |
|---|---|---|
| [Classifications](01_Classifications_EN.md) | `Classifications/` | Supplies weapon and armor classification facts |
| [Durability Consumption](02_Durability_Consumption_EN.md) | `Durability/Consumption/` | Controls base durability, wear, ammo, OMOD and skill modifiers |
| [Damage Formula](03_Damage_Formula_EN.md) | `Durability/DamageFormula/` | Converts incoming damage into armor durability loss |
| [Repair](04_Repair_EN.md) | `Repair/` | Controls Jury Rigging, Over-Repair and repair kits |
| [Loot](05_Loot_EN.md) | `Loot/` | Controls random initial condition and skill bonuses |
| [Item Cards](06_Item_Cards_EN.md) | `ItemUICards.json` | Controls the CND row anchor in IIF item cards |
| [Provider Selection](07_Provider_Selection_EN.md) | Each module's `ProviderSelection.json` | Explains merged and single-provider loading |
| [MCM and INI](08_MCM_and_INI_EN.md) | `MCM/Config/ConditionSystemFramework/settings.ini` | Runtime switches, values and widgets |

## Loading Model

CSF uses more than one loading model:

1. `Classifications/` uses contribution-style Providers. Every file whose `requires` dependencies are loaded is read and merged through classification-group rules.
2. `Repair/` and `Loot/` use single-provider selection. One Provider file is selected for each submodule.
3. Profiles under `Durability/Consumption/` are all loaded. A profile becomes active when its match conditions apply; priority and the module's implementation determine whether one result is selected or multiple modifiers are accumulated.
4. `ItemUICards.json` and the MCM INI each have one effective configuration file.

Runtime files belong in the project's `data/` image. Development runs through the MO2 overlay; release packages should be built from the same `data/` image.

## FormID Syntax

Generic `formIDs` fields accept both forms:

```jsonc
"formIDs": [
  "Fallout4.esm|0001A2B3",
  "SomeMod.esp|000123"
]

"formIDs": {
  "SomeMod.esp": ["003", "004"]
}
```

CSF resolves regular and ESL/ESL-flagged plugins against the current load order. ESL records may use a plugin-local record number or a full `FE` FormID. If the plugin is not loaded, the reference cannot resolve and the related profile or Provider will not behave as intended.

Schema 3 classification `list` values refer to plugin FormID Lists, not directly to an item FormID:

```jsonc
"list": "MyClassificationPatch.esp|0x000800"
```

The FLST members are the KYWD, OMOD, WEAP or ARMO records. To exclude one exact weapon or armor, put the WEAP/ARMO in a `Weapon.Flag.Ignore` or `Armor.Flag.Ignore` FLST; see the [Classifications](01_Classifications_EN.md) document.

## Skill and Attribute References

`RequiredSkill`, `DisplaySkill` and loot skill references use this structure:

```jsonc
{
  "Type": "AV",
  "Plugin": "Classic Skill System.esp",
  "FormID": "0xFA0",
  "MinRank": 1.0,
  "ScaleFactor": 1.0
}
```

`EditorID` may be used instead of `Plugin` and `FormID`. `AV` reads an Actor Value, `Perk` reads the player's current perk rank, and `Global` reads a Global value. `ScaleFactor` changes the value used by the rule; it does not change the in-game skill itself.

## Troubleshooting Order

1. Confirm that the file is under the correct module directory and has the `.json` extension, rather than being placed under `docs/`.
2. Validate JSONC syntax, especially commas, strings and comments.
3. Confirm that every `requires` or `RequiredPlugin` name matches the actual loaded plugin name.
4. Confirm that a single-provider filename is exactly `<RequiredPlugin>.json`.
5. Enable `Debug/bEnableLogging` and inspect `ConditionSystemFramework.log` for Provider, profile and classification messages.
6. Fully restart the game before testing; classification results, item flags and some UI state are cached.

## Current Boundaries

The old `Rules/`, old `Exclusions/`, `TargetKeywords`, `MatchCategories` and `RankLimits` formats are not fields of the current active loaders. Migrate them into the current module schemas instead of copying the old files into the new directories.


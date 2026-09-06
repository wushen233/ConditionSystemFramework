# Provider Selection and Loading

CSF currently uses three loading patterns: merged classification Providers, single-selection Repair/Loot Providers, and all-loaded durability profiles. This document explains selection; see each module document for fields.

## Classification Providers

File:

```text
Classifications/ProviderSelection.json
```

Current settings:

```jsonc
{
  "schemaVersion": 1,
  "module": "Classifications.ProviderSelection",
  "loading": {
    "mode": "AllRequirementsSatisfied",
    "missingRequirement": "SkipProvider"
  },
  "conflictResolution": {
    "mode": "JsonPriorityFirst",
    "finalTieBreak": "ProviderFileName"
  }
}
```

Meaning:

- Every classification Provider whose `requires` dependencies are loaded is read.
- A missing dependency skips the whole Provider.
- `JsonPriorityFirst` compares ProviderPriority/sourceTier first, then plugin load order. Schema 2 continues with `resolutionPriority`; Schema 3 uses context, Attach Point role and `listPriority` strength fields.
- `ProviderFileName` is only the deterministic final tie-break; do not use filenames as user-priority settings.

Classification groups decide whether results coexist or are exclusive. ProviderPriority is not a global “override every other file” switch.

## Repair Providers

File:

```text
Repair/ProviderSelection.json
```

Current settings:

```jsonc
{
  "schemaVersion": 1,
  "module": "Repair.ProviderSelection",
  "modules": {
    "JuryRigging": { "mode": "JsonPriorityFirst" },
    "OverRepair": { "mode": "PluginLoadOrderFirst" }
  }
}
```

For each candidate `.json`, the loader checks:

1. `RequiredPlugin` exists.
2. The filename is `<RequiredPlugin>.json`.
3. `RequiredPlugin` is loaded.
4. The module mode selects exactly one file.

Therefore `Classic Skill System.esp.json` does not merge with `Fallout4.esm.json`; it replaces that submodule when selected.

## Loot Providers

File:

```text
Loot/ProviderSelection.json
```

The current `ConditionBonus` module uses `PluginLoadOrderFirst`. `Loot/ConditionBonus/Fallout4.esm.json` participates only when Fallout4.esm is loaded.

## Durability Profiles

Profiles under `Durability/Consumption/` are not single-selection Providers:

- `Weapons`, `Armors`, `Ammos`, `Modifiers`, `Omods`, `CategoryModifiers` and `SkillModifiers` scan the `.json` files in their respective directories.
- Base profiles depend on match conditions; CategoryModifiers, OMOD effects and skill modifiers may accumulate.
- `priority` provides deterministic ordering, while whether one result or all matching modifiers are used is defined by the module implementation.

## Filename Guidance

- Single-selection Providers should use the exact dependency filename, for example `Classic Skill System.esp.json`.
- Contribution-style classification files may use feature names such as `CSF_Pipe_Weapons.json`, but their `requires` list must be correct.
- Do not copy a ProviderSelection file into a candidate directory; files named `ProviderSelection.json` are skipped specially.

## Log Confirmation

With logging enabled, these messages confirm selection:

- `[ProfileManager] Selected ... provider ...`
- `[Classification] Registered ... Provider ...`
- `[ArmorConcepts] Compiled ... concepts ...`

`expected canonical filename` means the Provider filename is invalid. `not loaded` means the dependency is not in the current MO2 load list.


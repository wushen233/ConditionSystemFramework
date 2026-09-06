# Classifications Module

Runtime directory:

```text
Data/F4SE/Plugins/ConditionSystemFramework/Classifications/
```

The classifications module answers “what facts describe this item?”. Durability, repair and fault modules consume those facts. Classification files do not edit ESP records and do not directly change durability.

## Provider Types

| Schema | `module` | Purpose |
|---|---|---|
| 3 | `weaponClassifications` | Weapon Family, Handling, Configuration and Flag categories |
| 3 | `armorClassifications` | Armor Material, Region, Grade, Flag and equipment domain |
| 2 | `classifications` | General rules with direct FormID, keyword and instance matching |

The main classification data comes from FLST records in plugins. `CSF_Weapons.json`, `CSF_Armor_Concepts.json` and the exclusion files reference lists stored in CSF or bridge plugins.

## Weapon Groups

| Group | Resolution | Current use |
|---|---|---|
| `Weapon.Family` | Coexisting | Ballistic, Energy, Launcher, Melee, Unarmed, Electromagnetic, Gamma, Alien |
| `Weapon.Handling` | Exclusive | Pistol, Rifle, Heavy, Melee1H, Melee2H, Unarmed |
| `Weapon.Configuration` | Optional exclusive | Shotgun and Sniper; may be empty |
| `Weapon.Flag` | Coexisting | `Weapon.Flag.Ignore` and other marker categories |

A weapon may belong to several Families; for example, a radium rifle may be both Ballistic and Gamma. Handling is the main form factor and keeps one final result. Equal-strength conflicts in an exclusive group return Unknown. Configuration may have no result.

Instance classification checks both the base WEAP and OMODs installed on the current item instance. Base keywords and newly effective instance keywords use different context priorities. Attach Point roles can change Family, Handling or Configuration.

### Pipe Weapons

`Integrations/CSF_Pipe_Weapons.json` uses a Schema 2 general rule. It checks the base WEAP and effective instance keywords for `dn_weap_Pipe`, then adds:

```text
Weapon.Modifier.Pipe
```

This is a repair-oriented modifier category. It does not replace `Weapon.Family` or `Weapon.Handling`. The current Jury Rigging configuration uses it to add a wider cross-Handling repair rule for Pipe pistols, rifles and shotguns without making standard weapons universally interchangeable.

## Armor Groups

| Group | Resolution | Current use |
|---|---|---|
| `Equipment.Domain` | Exclusive | `Equipment.Domain.PowerArmor` leaves the normal armor durability path |
| `Armor.Material` | Exclusive | Textile, Flexible, Rigid, Scrap |
| `Armor.Region` | Exclusive | Torso, Limbs, Head, Face, Accessory |
| `Armor.Grade` | Optional exclusive | A, B, C; mainly driven by Size roles and instance OMODs |
| `Armor.Flag` | Coexisting | `Armor.Flag.Ignore` |

Material and Region normally come from the base ARMO. If Region is not matched, C++ uses a BOD2 fallback. The Size role may change Grade through an installed instance OMOD. Tier, Lining, Weave, Paint, Legendary and Misc are recognized instance roles, but do not participate in the three core classification layers; they are available to the durability modifier system.

When `Equipment.Domain.PowerArmor` is selected, CSF leaves the item out of normal armor durability handling and preserves the vanilla power armor path.

## Full Exclusion

### Weapons

The `Weapon.Flag.Ignore` FLST accepts KYWD, WEAP and nested FLST members. A hit marks the weapon as outside the durability system and keeps it out of repair targets and donors.

### Armor

The `Armor.Flag.Ignore` FLST accepts KYWD, ARMO and nested FLST members. It likewise removes the armor from CSF durability and repair processing.

To exclude one exact FormID:

1. Create an FLST in your own patch plugin.
2. Add the target WEAP or ARMO directly to that list.
3. Register that FLST under the matching Ignore category in a classification Provider.
4. Add the patch plugin to `requires` so the Provider is skipped cleanly when the dependency is absent.

Example:

```jsonc
{
  "schemaVersion": 3,
  "module": "armorClassifications",
  "domain": "armor",
  "provider": "My Armor Exclusions",
  "requires": ["My Armor Patch.esp"],
  "ProviderPriority": 100,
  "groups": { "Armor.Flag": { "mode": "coexisting" } },
  "classifications": {
    "Armor.Flag.Ignore": [
      {
        "list": "My Armor Patch.esp|0x000800",
        "contexts": ["base"],
        "listPriority": 100
      }
    ]
  }
}
```

Direct `formIDs` arrays are primarily exact selectors for durability profiles. Schema 3 classification exclusions use FLST records; the `list` field must resolve to an FLST.

## Custom Classification Provider

Basic weapon Schema 3 structure:

```jsonc
{
  "schemaVersion": 3,
  "module": "weaponClassifications",
  "domain": "weapon",
  "provider": "My Weapon Provider",
  "requires": ["My Weapon Patch.esp"],
  "ProviderPriority": 100,
  "groups": {
    "Weapon.Family": { "mode": "coexisting" }
  },
  "classifications": {
    "Weapon.Family.Ballistic": [
      {
        "list": "My Weapon Patch.esp|0x000801",
        "contexts": ["base", "instance"],
        "listPriority": 100
      }
    ]
  }
}
```

Schema 2 is useful when an extension needs direct FormID or keyword matching:

```jsonc
{
  "schemaVersion": 2,
  "module": "classifications",
  "domain": "weapon",
  "provider": "My Exact Weapon Rules",
  "requires": ["My Weapon Patch.esp"],
  "sourceTier": 200,
  "categories": [
    {
      "id": "Weapon.Modifier.MyGroup",
      "domain": "weapon",
      "match": {
        "combine": "any",
        "base": {
          "forms": ["My Weapon Patch.esp|000812"],
          "keywordsAny": ["My_Keyword"]
        }
      }
    }
  ]
}
```

Schema 2 supports `forms`, `formLists`, `keywordsAny`, `keywordsAll`, BOD2 conditions and instance OMOD/keyword conditions. It is best used for extension markers; core weapon and armor classification should use the current Schema 3 providers.

## Dependencies and Diagnostics

- If any plugin in `requires` is missing, the whole Provider is skipped.
- `Classifications/ProviderSelection.json` only controls conflict ordering; it does not force a missing dependency to load.
- Do not use `WeaponTypeUnarmed` as a weapon exclusion condition; normal fist weapons may carry that keyword.
- After creating a classification FLST, confirm that the referenced record is an FLST, not a KYWD, WEAP or ARMO.
- Useful log prefixes: `[Classification]` and `[ArmorConcepts]`.


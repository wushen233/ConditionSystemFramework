# Condition System Framework — Configuration Guide (English)

Every rule in this framework is defined through **JSON config files** located in:

```
Data/F4SE/Plugins/ConditionSystemFramework/
```

You don't need to write code — just edit the JSON as described here to customize durability behavior for any weapon, armor, ammo, modification, legendary effect, repair kit, and more.

> 📦 **Dependencies**: this framework relies on two UI prerequisites — **PrismaUI_F4** (renders the on-screen durability HUD widget) and **Item Integration Framework (IIF)** (renders the condition bar on item info cards). Both are maintained separately; CSF simply calls their APIs.

> Tip: `//` line comments are allowed in these JSON files (the framework's parser strips them). But keep valid JSON otherwise (commas, quotes, brackets) — a single syntax error makes the whole file fail to load.

---

## 1. Core Concepts (read this first)

### 1.1 Whole folders are scanned
**Every `.json` file** inside each sub-folder (`Weapons/`, `Armors/`, etc.) is loaded. You can split rules across multiple files (e.g. `01_Vanilla.json`, `02_MyMod.json`); file names only affect read order, not logic.

### 1.2 `priority`
When an item matches multiple profiles, the one with the **highest `priority`** wins. Give more specific rules a higher priority and your catch-all rule `0`.

### 1.3 Matching: FormID first, then keywords
For each item the framework looks for a rule in this order:
1. **Exact `formIDs` match** (highest precedence — wins immediately).
2. **`keywords` match** (checked from highest priority down; the first matching profile wins).

### 1.4 Keyword AND/OR nesting (important)
`keywords` is a **2-dimensional array**:
- **Outer array = OR**: any group matching is enough.
- **Inner array = AND**: every keyword in that group must be present.

```jsonc
"keywords": [ ["WeaponTypePistol"] ]            // has WeaponTypePistol
"keywords": [ ["A"], ["B"] ]                     // has A OR has B
"keywords": [ ["ReceiverPowerful", "Receiver_Automatic"] ]  // must have BOTH
```

### 1.5 Two FormID notations
- **`formIDs` lists** (weapons/armor/ammo/mods/legendary/exclusions): use `"PluginName|HexID"`, e.g.:
  ```jsonc
  "formIDs": ["Fallout4.esm|1CC2AD", "MunitionsReloaded.esp|40920"]
  ```
- **Repair-kit `formIDs`**: an object `{ "PluginName": ["HexID", ...] }` (see the Repair Kits section).
- **`RequiredSkill` / `RequiredAV`**: an object `{ "Plugin": "...", "FormID": "0x..." }`, with a full `0x`-prefixed hex FormID.

> Finding a FormID: open the plugin in FO4Edit/xEdit, locate the record, copy its FormID (drop the leading load-order byte — use the plugin-local ID). A vanilla quick-reference table is at the end.

---

## 2. Weapons `Weapons/*.json`

Controls a weapon's max durability, wear rate, and whether it can jam.

```jsonc
{
  "profiles": [
    {
      "name": "Sniper_Rifles",         // label only, name it anything
      "priority": 90,                   // default 0
      "maxDurability": 600.0,           // full-condition durability points; default 1000
      "degradeRate": 4.0,               // base wear per shot; default 1.0
      "canJam": true,                   // participates in the jam system; default true
      "keywords": [ ["WeaponTypeSniper"] ],
      "formIDs": []                     // optional: exact match by weapon FormID
    }
  ]
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `name` | string | — | Rule name (label only) |
| `priority` | int | 0 | Highest wins on conflict |
| `maxDurability` | float | 1000 | Durability points at full condition |
| `degradeRate` | float | 1.0 | Base wear per shot (further scaled by ammo/mods) |
| `canJam` | bool | true | Whether it can jam |
| `keywords` | 2D array | — | OR/AND keyword match |
| `formIDs` | array | — | `"Plugin|Hex"` exact match |

---

## 3. Armor `Armors/*.json`

```jsonc
{
  "profiles": [
    {
      "name": "Heavy_Power_Armor_Plating",
      "priority": 100,                  // default 0
      "maxDurability": 2000.0,          // default 500
      "useFlatDegrade": true,           // see below; default false
      "degradeRate": 1.5,               // only used when useFlatDegrade=true; default 0.0
      "keywords": [ ["ArmorTypePower"] ],
      "formIDs": []
    }
  ]
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `name` | string | — | Rule name |
| `priority` | int | 0 | Highest wins on conflict |
| `maxDurability` | float | 500 | Durability points at full condition |
| `useFlatDegrade` | bool | false | `false` = wear scales dynamically from the item's native DR/ER resistances (more realistic); `true` = each hit removes a flat `degradeRate` points |
| `degradeRate` | float | 0.0 | Flat wear per hit; only used when `useFlatDegrade=true` |
| `keywords` | 2D array | — | OR/AND keyword match |
| `formIDs` | array | — | exact match |

> ⚠️ **About `damageResponses` (deprecated)**: earlier armor examples had a `damageResponses` block (per-damage-type immunity/multipliers). **The current code does not read this field at all; it has been removed from the default configs — do not use it.** Per-damage-type armor durability behavior is now driven globally by the engine's native resistances plus `ratingMultipliers` in `Rules/01_DamageFormula.json` (applied globally by damage type, not per individual armor piece).

---

## Appendix: Armor Wear Formula & Tuning

If players report that "armor breaks too fast", you can tune the following configuration knobs to adjust armor durability consumption.

### Wear Calculation Formula

Non-Power-Armor armor pieces use a **dynamic formula** for per-hit wear:

```
durabilityCost = totalResisted × durabilityDamageConstant(0.15) × impactMultiplier
```

Where:
- `totalResisted` = damage absorbed by the item's native DR + ER resistances
- `durabilityDamageConstant` = global coefficient (default 0.15)
- `impactMultiplier` = varies by protection quality (0.1 on glancing hits, 0.4 on penetrating hits)

> ⚠️ **Counter-intuitive behavior**: higher armor resistance absorbs more damage → larger `totalResisted` → **more wear**, not less!

### Tuning Levers (in recommended order)

| Method | Location | Default | Direction | Effect |
|--------|----------|---------|-----------|--------|
| **Lower global coefficient** | `Rules/01_DamageFormula.json` → `durabilityDamageConstant` | **0.15** | → 0.05 | ~3× less wear for all armor, most recommended |
| **Lower glance/scratch multipliers** | Same → `glanceMultiplier` / `scratchMultiplier` | 0.1 / 0.4 | Decrease | Less wear on glancing/penetrating hits |
| **Increase durability pool** | `Armors/*.json` → `maxDurability` | 300-2000 | **Increase** | Directly extends armor lifespan |
| **Switch to flat degrade** | `Armors/*.json` → `useFlatDegrade: true` + `degradeRate` | false/0 | Set small value | Fixed wear per hit, independent of resistances |

### Quick-Fix Example

```jsonc
// Rules/01_DamageFormula.json
{
  "DamageFormula": {
    "durabilityDamageConstant": 0.05,  // 0.15 → 0.05, ~3× less wear
    "glanceMultiplier": 0.05,          // less wear on glancing hits
    "scratchMultiplier": 0.2           // less wear on penetrating hits
  }
}
```

### Expected Lifespan Reference

Based on a 100-damage hit:

| Armor Type | maxDurability | Resistance | Wear per hit | Expected hits |
|-----------|-------------|-----------|-------------|--------------|
| Combat Armor | 1200 | High (90% DR) | ~1.35 | ~888 hits |
| Raider Armor | 600 | Medium (50% DR) | ~1.50 | ~400 hits |
| Clothing | 300 | Low (10% DR) | ~1.80 | ~166 hits |
| Power Armor (flat) | 2000 | — | 1.5/hit | ~1333 hits |

---

## 4. Ammo `Ammos/*.json`

Lets different ammo types affect weapon wear / enemy-armor durability differently.

```jsonc
{
  "profiles": [
    {
      "name": "MR_10mm_AP",             // armor-piercing
      "weaponWearMult": 1.5,            // weapon wear multiplier; default 1.0
      "armorDamageMult": 2.0,           // durability damage dealt to enemy armor; default 1.0
      "formIDs": ["MunitionsReloaded.esp|40920"],
      "keywords": []                    // keyword matching also supported
    }
  ]
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `weaponWearMult` | float | 1.0 | Multiplier on the firing weapon's wear |
| `armorDamageMult` | float | 1.0 | Multiplier on durability damage to enemy armor |
| `formIDs` / `keywords` | — | — | Same matching as above |

---

## 5. Legendary Effects `Modifiers/*.json`

Overrides wear/jam for legendary OMODs (e.g. "Endless", "Indestructible").

```jsonc
{
  "profiles": [
    {
      "name": "NeverBreak",
      "priority": 100,                  // default 0
      "degradeMult": 0.0,               // final wear multiplier; 0.0 = never degrades; default 1.0
      "immuneToJam": true,              // never jams; default false
      "formIDs": ["Fallout4.esm|1CC2AD"]
    }
  ]
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `degradeMult` | float | 1.0 | Final wear multiplier (0 = no wear, 1.5 = faster wear) |
| `immuneToJam` | bool | false | Immune to jamming |

---

## 6. Modifications `Omods/*.json`

Fine-tunes durability per weapon/armor part (receiver, barrel, suppressor, …). Multiple mods **stack**.

```jsonc
{
  "profiles": [
    {
      "name": "HeavyBarrel",
      "priority": 40,                   // default 0
      "degradeMult": 0.8,               // wear multiplier (multiplicative); default 1.0
      "degradeRateFlat": 0.0,           // flat wear add/sub; default 0.0
      "maxDurabilityMult": 1.3,         // max-durability multiplier; default 1.0
      "immuneToJam": false,             // default false
      "formIDs": [],
      "keywords": [["BarrelHeavy"]]
    }
  ]
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `degradeMult` | float | 1.0 | **Multiplicative** wear (1.3 = +30% wear, 0.7 = -30%) |
| `degradeRateFlat` | float | 0.0 | **Additive** flat wear per shot (can be negative) |
| `maxDurabilityMult` | float | 1.0 | **Multiplicative** max durability (1.2 = +20%) |
| `immuneToJam` | bool | false | This part grants jam immunity |

---

## 7. Repair Kits `RepairKits/*.json`

Defines consumable repair items.

```jsonc
{
  "profiles": [
    {
      "name": "Expert Weapon Repair Kit",
      "formIDs": { "Condition System Framework.esp": [ "F09" ] },  // note the format!
      "canRepairWeapon": true,          // default false
      "canRepairArmor": false,          // default false
      "baseRepairPoints": 1200,         // weapons: repair by flat durability points
      // "baseRepairPercent": 0.40,     // armor: repair by percentage (use one or the other)
      "maxConditionLimit": 1.25         // highest condition this kit can reach; default 1.0
    }
  ]
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `formIDs` | object | — | `{ "Plugin": ["Hex", ...] }` — **different** from the array format used elsewhere |
| `canRepairWeapon` | bool | false | Can repair weapons |
| `canRepairArmor` | bool | false | Can repair armor |
| `baseRepairPoints` | float | — | Repair by **flat points** (typical for weapons). Present → flat-points mode |
| `baseRepairPercent` | float | — | Repair by **percentage** (e.g. 0.40 = 40%, typical for armor) |
| `maxConditionLimit` | float | 1.0 | Highest condition this kit can repair up to (1.25 = 125%, mild over-repair) |

> Use either `baseRepairPoints` or `baseRepairPercent`: if `baseRepairPoints` is present it uses flat points, otherwise percentage.

---

## 8. Exclusions / Blacklist `Exclusions/*.json`

Excludes items from the durability system and/or hides the condition bar (Pip-Boy, quest items, power armor).

```jsonc
{
  "rules": [
    {
      "name": "Full Exclusions",
      "keywords": ["Unplayable", "ArmorTypePipBoy"],   // 1D string array (any match excludes)
      "formIDs": ["Fallout4.esm|021B3B"],
      "enableDurabilitySystem": false,  // default false: no durability calc
      "showDurabilityUI": false         // default false: hide condition in UI
    }
  ]
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `keywords` | string array | — | Matches if it has any one keyword (this is **1D**, unlike weapons/armor) |
| `formIDs` | array | — | `"Plugin|Hex"` |
| `enableDurabilitySystem` | bool | false | Whether matched items get durability loss |
| `showDurabilityUI` | bool | false | Whether to show condition in UI (can differ from above — e.g. power armor `enableDurabilitySystem=false` but `showDurabilityUI=true`) |

---

## 9. Damage Formula `Rules/01_DamageFormula.json`

Global durability-damage formula (usually no need to touch).

```jsonc
{
  "DamageFormula": {
    "enabled": true,                    // master switch
    "baseHardness": 1.0,
    "glanceThreshold": 0.3,             // glancing-hit threshold
    "glanceMultiplier": 0.1,            // durability-damage multiplier on glancing hits
    "scratchThreshold": 0.7,
    "scratchMultiplier": 0.4,
    "durabilityDamageConstant": 0.15,   // overall durability-damage coefficient
    "ratingMultipliers": {              // converts each damage type's resistance into "hardness"
      "default": 0.2,                   // physical bullets: DR × 0.2
      "DamageTypeEnergy": 0.1,
      "DamageTypeFire": 0.05,
      "DamageTypeCryo": 0.3,
      "DamageTypePoison": 0.0,
      "DamageTypeRadiation": 0.0
    }
  }
}
```

> This is the real entry point for "armor behaves differently per damage type" (replacing the non-functional `damageResponses`). A higher coefficient → that damage type is more offset by armor hardness → relatively less durability wear.

---

## 10. Over-Repair Rules `Rules/01_OverRepairRules.json`

Caps how high a category of item can be repaired (possibly above 100%) based on the player's **skill rank**.

```jsonc
{
  "OverRepairRules": [
    {
      "RuleName": "Over-Repair Firearms (Gun Nut)",
      "TargetKeywords": ["WeaponTypePistol", "WeaponTypeRifle"],  // applicable items
      "RequiredSkill": {
        "Type": "Perk",                 // "Perk" = read perk rank; "AV" = read an actor value
        "Plugin": "Fallout4.esm",
        "FormID": "0x0004A0DA"          // Gun Nut (GunNut01)
      },
      "AVStep": 25,                     // step size when Type=AV; ignored for Perk rules
      "RankLimits": [1.0, 1.25, 1.50, 1.75, 2.0]
    }
  ]
}
```

- `RankLimits`: **index = skill rank**. Index 0 = no perk (limit 100% = 1.0), index 1 = rank 1 (1.25 = 125%), and so on.
- With `Type: "Perk"` the framework reads the player's rank of that perk; with `Type: "AV"` it reads an actor value and buckets it by `AVStep`.

---

## 11. Jury-Rigging / Cross-Repair `Rules/03_JuryRigging_Vanilla.json`

Allows repairing one item with another (across types), gated by skill.

```jsonc
{
  "JuryRiggingRules": [
    {
      "RuleName": "Ballistic Firearms: Cross-Type Repair (Gun Nut Rank 4)",
      "TargetKeywords": ["WeaponTypePistol", "WeaponTypeRifle"],  // both target & material must match this broad category
      "MatchCategories": [],            // empty = free cross-type repair within the category; set = both items must share one exact tag
      "MaterialConditionMultiplier": 1.0, // material-to-target durability conversion efficiency
      "RequiredSkill": {
        "Type": "Perk", "Plugin": "Fallout4.esm",
        "FormID": "0x0004A0DA", "MinRank": 4  // requires this perk at rank 4
      }
    }
  ]
}
```

| Field | Meaning |
|-------|---------|
| `TargetKeywords` | Broad-category tags both the target and material item must match |
| `MatchCategories` | Category lock: empty = any cross-type repair inside the category; set = both items must share one of these tags |
| `MaterialConditionMultiplier` | Efficiency of converting material durability into target durability (1.0 = 100%) |
| `RequiredSkill.MinRank` | Minimum required perk rank |

---

## 12. Item Card UI `ItemUICards.json`

> 📦 **Dependency note**: the condition bar on item info cards is **not rendered by CSF itself** — it is rendered by the prerequisite framework **Item Integration Framework (IIF)**. CSF reads this file to get the anchor layout, then registers a card named `CND` through IIF's API. In other words: this file controls *where* the CND card anchors, while *how it looks / is laid out* is IIF's job. Players can also re-order cards via IIF's **in-game card editor**; for advanced card customization see the documentation files shipped with Item Integration Framework.

Controls where the condition bar anchors on the item info card (usually no need to change).

```jsonc
{
  "ItemUICards": {
    "CND": {
      "priority": 0,
      "anchorTarget": "$ammo",          // which row to anchor to ($ammo = ammo row)
      "anchorMode": "after",            // before / after
      "fallbackAnchorTarget": "TOP",    // fallback if the anchor isn't found
      "fallbackAnchorMode": "after"
    }
  }
}
```

---

## Appendix: Vanilla FormID Quick Reference (verified with xEdit)

### SPECIAL stats (Actor Values, for `RequiredAV` / `Type:"AV"`)
| Stat | FormID |
|------|--------|
| Strength | `0x0002C1` |
| Perception | `0x0002C3` |
| Endurance | `0x0002C5` |
| Charisma | `0x0002C7` |
| Intelligence | `0x0002C9` |
| Agility | `0x0002CB` (note: **not** Luck!) |
| **Luck** | **`0x0002C8`** |

### Crafting perks (for `RequiredSkill` `Type:"Perk"` — base records of multi-rank perks)
| Perk | FormID | EditorID |
|------|--------|----------|
| Gun Nut | `0x0004A0DA` | GunNut01 |
| Blacksmith | `0x0004B253` | Blacksmith01 |
| Armorer | `0x0004B254` | Armorer01 |
| Science! | `0x000264D9` | Science01 |

> When using custom FormIDs, always verify in FO4Edit/xEdit that the copied FormID's **EditorID/name** is actually the record you want — don't trust similar-looking numbers.

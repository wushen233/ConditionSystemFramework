# MCM and INI Module

Runtime user configuration:

```text
Data/MCM/Config/ConditionSystemFramework/settings.ini
```

The project default template is:

```text
projects/ConditionSystemFramework/data/MCM/Config/ConditionSystemFramework/settings.ini
```

The MO2 overwrite or a user-specific configuration takes precedence over the project default. MCM writes values back to the user configuration; after manual edits, reopen MCM or restart the game to trigger a reload.

## General

| Key | Purpose |
|---|---|
| `bEnable` | CSF master switch |
| `fWidgetX` / `fWidgetY` | SWF CND widget position |
| `fWidgetScale` | SWF CND widget scale |
| `iWidgetType` | Main CND backend: `0` PrismaUI, `1` SWF |
| `iSwfCndStyle` | SWF CND style selection |

The unjam and heat widgets can select their backends independently through `iUnjamWidgetType` and `iHeatWidgetType`. If any widget uses PrismaUI, CSF loads the PrismaUI backend.

## UI

| Key | Purpose |
|---|---|
| `bShowWidgetText` | Show widget text |
| `iUnjamWidgetType` | Unjam/fault widget backend |
| `iHeatWidgetType` | Heat widget backend |
| `bShowItemCardCND` | Show CND on item cards |
| `iItemCardCNDPosition` | Item-card CND position |
| `fUnjamX/Y/Scale` | Unjam widget position and scale |
| `fHeatWidgetX/Y/Scale` | Heat widget position and scale |
| `fJuryStatsOffsetX/Y` | Jury Rigging stats offset |
| `fJuryStatsRowSpacing` | Jury Rigging stats row spacing |
| `iJuryStatsColorMode` | Jury stats color mode |
| `iCndColorMode` | CND widget color mode |
| `iUnjamColorMode` | Unjam widget color mode |
| `iHeatColorMode` | Heat widget color mode |
| `bUseCustomColor` | Enable the custom color value |
| `sCustomColor` | Six-digit hexadecimal RGB color, for example `FFFFFF` |

Each widget reads its own color mode. HUD-following and custom-color modes are supported. Heat-stage colors and the overheated-state override are special states and are not fully replaced by the normal color mode.

## Mechanics: Switches and Initial Condition

| Key | Purpose |
|---|---|
| `bRandomLootDurability` | Randomize initial condition for new weapons and armor |
| `bEnableWeaponCondition` | Weapon durability system |
| `bEnableArmorCondition` | Normal armor durability system |
| `bEnableOverCondition` | Allow condition above 100% |
| `bDialogueFullDurability` | Keep dialogue-related new items at full condition |
| `fWeaponLootDurabilityMin/Max` | Weapon initial condition range |
| `fArmorLootDurabilityMin/Max` | Armor initial condition range |
| `fWeaponWearMultiplier` | Total weapon-wear multiplier; defaults to 1.0 when absent |
| `fArmorWearMultiplier` | Total armor-wear multiplier; defaults to 1.0 when absent |
| `fRepairMaterialCostMultiplier` | Repair material cost multiplier |

Ranges use ratios, so `0.25` means 25%.

## Mechanics: Faults and Heat

| Key | Purpose |
|---|---|
| `fJamThreshold` | Start random jam checks below this condition ratio |
| `fMaxJamChance` | Maximum jam chance |
| `bEnableEnergyFault` | Energy weapon faults |
| `fEnergyFaultChanceMultiplier` | Energy fault multiplier during firing |
| `fEnergyReloadFaultChanceMultiplier` | Energy fault multiplier during reload |
| `fEnergyFaultDuration` | Energy fault duration |
| `bEnableWeaponOverheat` | Weapon overheat system |
| `fOverheatHeatPerShot` | Base heat added per shot |
| `fOverheatCooldownRate` | Cooling speed after firing stops |
| `fOverheatRecoveryThreshold` | Recovery threshold for the overheated state |
| `fOverheatWearMultiplier` | Extra weapon wear while overheated |
| `iJammingPhase` | `0` firing jams, `1` reload jams |
| `bQuickUnjam` | Quick fault clearing |

Heat eligibility comes from the weapon Profile mechanism and the code's weapon/ammo identity checks. An ammo wear profile by itself does not turn a normal 10mm pistol into an overheating weapon.

## Mechanics: Performance and Repair

| Key | Purpose |
|---|---|
| `iDegradationMode` | `0` NV stepped performance loss, `1` FO76 linear loss |
| `fPenaltyThreshold` | Condition threshold where performance penalties begin |
| `fPenaltyMultMid` | Mid-condition performance multiplier |
| `fPenaltyMultLow` | Low-condition performance multiplier |
| `fLinearMinMult` | Minimum multiplier in linear mode |
| `bWeaponConditionAffectsDamage` | Weapon condition affects damage |
| `bWeaponConditionAffectsValue` | Weapon condition affects value |
| `bArmorConditionAffectsResistance` | Armor condition affects resistance |
| `bArmorConditionAffectsValue` | Armor condition affects value |
| `iRepairMaterialMode` | Repair material mode |
| `bDynamicRepairMaterialCost` | Dynamic material cost |
| `bEnablePipboyJuryRepair` | Pip-Boy Jury Rigging |
| `bEnableWorkbenchMaterialRepair` | Workbench material repair |
| `bEnableRepairKits` | Repair kits |
| `bConfirmConsumeFavoriteMaterial` | Confirm before consuming favorite material |
| `bConfirmConsumeLegendaryMaterial` | Confirm before consuming legendary material |
| `fJuryRepairCap` | Pip-Boy Jury Rigging method cap |
| `fWorkbenchRepairCap` | Workbench method cap |

The final repair cap also passes through skill tiers from `Repair/OverRepair/`; the smaller of the two caps is used.

## Debug

| Key | Purpose |
|---|---|
| `bEnableLogging` | Write detailed diagnostics to `ConditionSystemFramework.log` |

When a Provider is not selected, a FormID does not resolve, a category is wrong or a skill is not recognized, enable this setting and fully restart before reproducing. The log distinguishes “configuration was not loaded” from “the rule loaded but its condition did not match.”


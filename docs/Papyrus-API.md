# CSF Papyrus API

`ConditionFramework` is a native, hidden Papyrus script installed with CSF.
Condition percentages use the inclusive `0.0` through `100.0` range.
Query functions return `-1.0` when no supported equipped item exists.

```papyrus
float weaponCondition = ConditionFramework.GetEquippedWeaponConditionPct(Game.GetPlayer())
if weaponCondition >= 0.0
    ConditionFramework.ModEquippedWeaponConditionPct(Game.GetPlayer(), -5.0)
endif
```

| Function | Purpose |
| --- | --- |
| `IsConditionManaged(Form)` | Whether the form is currently controlled by CSF. |
| `GetEquippedWeaponConditionPct(Actor)` | Reads the actor's exact equipped weapon stack. |
| `SetEquippedWeaponConditionPct(Actor, float)` | Sets the exact equipped weapon stack to `0-100`. |
| `ModEquippedWeaponConditionPct(Actor, float)` | Adds or subtracts condition percentage points. |
| `GetEquippedArmorConditionPct(Actor)` | Returns the average of supported equipped armor stacks. |
| `CanJuryRig(Form, Form)` | Evaluates the current jury-rigging rules and player skill requirements. |
| `OpenRepairMenu()` | Opens CSF's Pip-Boy jury-rigging menu. |

Inventory entries that are not equipped are intentionally not exposed. Papyrus does not provide a stable exact stack identifier, and selecting by FormID would modify the wrong copy when the player owns duplicates.

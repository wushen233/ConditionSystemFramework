# Item Cards Module

Configuration file:

```text
Data/F4SE/Plugins/ConditionSystemFramework/ItemUICards.json
```

This file controls the CND row position inside IIF item cards. It does not control the HUD CND widget and does not decide whether an item is managed by the durability system.

## Current Structure

```jsonc
{
  "ItemUICards": {
    "CND": {
      "priority": 0,
      "anchorTarget": "$ammo",
      "anchorMode": "after",
      "fallbackAnchorTarget": "TOP",
      "fallbackAnchorMode": "after"
    }
  }
}
```

| Field | Purpose |
|---|---|
| `priority` | Ordering priority when multiple card rows are registered |
| `anchorTarget` | Preferred anchor, such as `$ammo` |
| `anchorMode` | Insertion position relative to the preferred anchor; current value is `after` |
| `fallbackAnchorTarget` | Anchor used when the preferred target is absent |
| `fallbackAnchorMode` | Insertion position for the fallback anchor |

If an item card has no ammo row, CSF uses the fallback anchor. Adjust the anchor rather than hard-coding coordinates in the SWF.

This module depends on the Item Integration Framework item-card API. If the IIF DLL is not loaded, the CND row is not registered. That is separate from the workbench repair-button path and the HUD widget backends.


# Classifications and Repair Documentation Entry

Classifications and repair are maintained as separate module documents:

- [Classifications](config/01_Classifications_EN.md)
- [Repair](config/04_Repair_EN.md)

The current boundary is:

- `Classifications/` supplies weapon and armor facts.
- `Repair/JuryRigging/` decides whether a target and donor are compatible.
- `Repair/OverRepair/` decides the skill and method condition cap.
- `Durability/Consumption/SkillModifiers/` changes wear only; it does not grant repair permission.

Use the module documents and the active files under `data/F4SE/Plugins/ConditionSystemFramework/` as the source of truth. The old `TargetKeywords`, `MatchCategories` and `RankLimits` examples are not fields of the current loader.


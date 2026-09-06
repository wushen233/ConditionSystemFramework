# 分类与维修文档入口

分类和维修已经拆分为两个独立模块，不再在本文件维护重复字段说明：

- [分类模块](config/01_分类_CN.md)
- [维修模块](config/04_维修_CN.md)

当前逻辑的边界是：

- `Classifications/` 只提供武器和护甲的分类事实。
- `Repair/JuryRigging/` 决定目标和供体是否兼容。
- `Repair/OverRepair/` 决定技能和维修方式允许的最高状况。
- `Durability/Consumption/SkillModifiers/` 只改变耐久消耗，不授予维修资格。

请以模块文档和 `projects/ConditionSystemFramework/data/F4SE/Plugins/ConditionSystemFramework/` 下的活动配置为准。旧版 `TargetKeywords`、`MatchCategories` 和 `RankLimits` 示例不属于当前 loader 字段。


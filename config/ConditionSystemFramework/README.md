# Condition System Framework Configuration

JSON 文件支持 `//` 和 `/* ... */` 注释。

## 目录职责

- `Classifications/`：装备事实分类。武器和普通护甲均使用 Schema 3。
- `Durability/Consumption/`：武器、护甲、弹药和 OMOD 的耐久消耗 Profile。
- `Durability/DamageFormula/`：伤害到耐久损耗的全局公式。
- `Repair/JuryRigging/`：混修技能与兼容规则 Provider。
- `Repair/OverRepair/`：过量修理上限 Provider。
- `Repair/RepairKits/`：修理包 Profile。
- `Loot/ConditionBonus/`：战利品初始状况技能 Provider。
- `ItemUICards.json`：IIF 物品卡片锚点。

## Provider

分类 Provider 是贡献式：所有 `requires` 均满足的文件都会加载并虚拟合并。
排序方式由 `Classifications/ProviderSelection.json` 控制。

Repair 与 Loot 每个子模块只选择一个 Provider。Provider 必须声明
`RequiredPlugin`；插件缺失时跳过。选择方式由模块根目录的
`ProviderSelection.json` 控制：

- `JsonPriorityFirst`
- `PluginLoadOrderFirst`

## 分类到修理

武器分类：

```text
Weapon.Family.*
Weapon.Handling.*
Weapon.Configuration.*
```

护甲分类：

```text
Armor.Material.*
Armor.Region.*
Armor.Grade.*
Armor.Flag.Ignore
Equipment.Domain.PowerArmor
```

运行时还会给 WEAP 添加 `weapon`，给 ARMO 添加 `armor`。Jury Rigging
使用这些分类和 `sameBase`、`sameGroup`、`sharesAnyCategory` 等条件决定供体。

详细作者文档：

```text
projects/ConditionSystemFramework/docs/Classification_Repair_Author_Guide_CN.md
projects/ConditionSystemFramework/docs/Classification_Repair_Author_Guide_EN.md
```

当前开发交接：

```text
projects/ConditionSystemFramework/docs/DEVELOPMENT_STATUS.md
```

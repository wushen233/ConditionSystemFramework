# Provider 选择与加载

CSF 当前有三种配置加载方式：分类 Provider 合并、Repair/Loot Provider 单选，以及耐久 Profile 全部读取。本文件只解释选择规则；具体字段请看对应模块文档。

## 分类 Provider

文件：

```text
Classifications/ProviderSelection.json
```

当前设置：

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

含义：

- 所有 `requires` 都满足的分类 Provider 都加载。
- 缺少任意依赖时跳过整个 Provider。
- `JsonPriorityFirst` 先比较 ProviderPriority/sourceTier，再比较插件加载顺序；Schema 2 继续使用 `resolutionPriority`，Schema 3 继续使用实例上下文、Attach Point 角色和 `listPriority` 等强度字段。
- `ProviderFileName` 只负责最后确定性决胜，不应当用文件名表达用户优先级。

分类组本身决定命中结果是共存还是互斥。ProviderPriority 不是“让整个文件覆盖所有其他分类”的全局开关。

## Repair Provider

文件：

```text
Repair/ProviderSelection.json
```

当前设置：

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

Repair 子模块会在候选目录中读取每个 `.json`，然后检查：

1. `RequiredPlugin` 是否存在。
2. 文件名是否等于 `<RequiredPlugin>.json`。
3. `RequiredPlugin` 是否已加载。
4. 按子模块的 mode 排序并选择一个文件。

因此 `Classic Skill System.esp.json` 不是一个会自动和 `Fallout4.esm.json` 合并的补丁，而是当它胜出时整套替换该子模块。

## Loot Provider

文件：

```text
Loot/ProviderSelection.json
```

当前 `ConditionBonus` 使用 `PluginLoadOrderFirst`。`Loot/ConditionBonus/Fallout4.esm.json` 只在 Fallout4.esm 已加载时参与竞争。

## 耐久 Profile

`Durability/Consumption/` 下的 profile 文件不是 Provider 单选：

- `Weapons`、`Armors`、`Ammos`、`Modifiers`、`Omods`、`CategoryModifiers` 和 `SkillModifiers` 会遍历各自目录中的 `.json`。
- 基础 Profile 依赖匹配条件；CategoryModifiers、OMOD 和技能 modifier 可以叠加。
- `priority` 负责确定性排序，具体是否“只取一个”或“全部叠加”由模块实现决定。

## 文件命名建议

- 单选 Provider：使用真实依赖插件的精确文件名，例如 `Classic Skill System.esp.json`。
- 贡献式分类：文件名可以按功能命名，例如 `CSF_Pipe_Weapons.json`，但必须保证 `requires` 正确。
- 不要把 Provider 选择文件复制到候选目录；loader 会专门跳过名为 `ProviderSelection.json` 的文件。

## 日志确认

开启日志后，以下信息可以确认选择结果：

- `[ProfileManager] Selected ... provider ...`
- `[Classification] Registered ... Provider ...`
- `[ArmorConcepts] Compiled ... concepts ...`

如果看到 `expected canonical filename`，说明 Provider 文件名不符合规则；如果看到 `not loaded`，说明依赖插件没有进入当前 MO2 实例的加载列表。

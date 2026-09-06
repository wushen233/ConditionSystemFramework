# MCM 与 INI 模块

运行时用户配置：

```text
Data/MCM/Config/ConditionSystemFramework/settings.ini
```

项目默认模板位于：

```text
projects/ConditionSystemFramework/data/MCM/Config/ConditionSystemFramework/settings.ini
```

MO2 的 `overwrite` 或用户专用配置优先于项目默认模板。MCM 保存设置时会把值写回用户配置；手动编辑后需要重新打开 MCM 或重启游戏触发重新读取。

## General

| 键 | 作用 |
|---|---|
| `bEnable` | CSF 总开关 |
| `fWidgetX` / `fWidgetY` | SWF CND 小部件位置 |
| `fWidgetScale` | SWF CND 小部件缩放 |
| `iWidgetType` | 主 CND 小部件后端：`0` PrismaUI，`1` SWF |
| `iSwfCndStyle` | SWF CND 样式选择 |

各故障小部件还可以单独通过 UI 段的 `iUnjamWidgetType` 和 `iHeatWidgetType` 选择后端；任意一个小部件使用 PrismaUI 时，CSF 会加载 PrismaUI 后端。

## UI

| 键 | 作用 |
|---|---|
| `bShowWidgetText` | 显示小部件文字 |
| `iUnjamWidgetType` | 卡壳/故障小部件后端 |
| `iHeatWidgetType` | 过热小部件后端 |
| `bShowItemCardCND` | 物品卡片显示 CND |
| `iItemCardCNDPosition` | 物品卡片 CND 位置 |
| `fUnjamX/Y/Scale` | 卡壳小部件位置和缩放 |
| `fHeatWidgetX/Y/Scale` | 过热小部件位置和缩放 |
| `fJuryStatsOffsetX/Y` | Jury Rigging 右侧统计偏移 |
| `fJuryStatsRowSpacing` | Jury Rigging 统计行距 |
| `iJuryStatsColorMode` | Jury 统计颜色模式 |
| `iCndColorMode` | CND 小部件颜色模式 |
| `iUnjamColorMode` | 卡壳小部件颜色模式 |
| `iHeatColorMode` | 过热小部件颜色模式 |
| `bUseCustomColor` | 启用自定义颜色值 |
| `sCustomColor` | 六位十六进制 RGB 颜色，例如 `FFFFFF` |

颜色模式由各小部件独立读取。当前实现支持 HUD 颜色和自定义颜色；过热条的阶段色以及过热状态覆盖色属于特殊状态，不会被普通颜色模式完全替换。

## Mechanics：总开关与随机初始耐久

| 键 | 作用 |
|---|---|
| `bRandomLootDurability` | 新获得武器/护甲是否随机初始状况 |
| `bEnableWeaponCondition` | 武器耐久系统 |
| `bEnableArmorCondition` | 普通护甲耐久系统 |
| `bEnableOverCondition` | 是否允许超过 100% |
| `bDialogueFullDurability` | 对话相关新物品是否保持满耐久 |
| `fWeaponLootDurabilityMin/Max` | 武器初始状况范围 |
| `fArmorLootDurabilityMin/Max` | 护甲初始状况范围 |
| `fWeaponWearMultiplier` | 武器总磨损倍率；缺失时默认为 1.0 |
| `fArmorWearMultiplier` | 护甲总磨损倍率；缺失时默认为 1.0 |
| `fRepairMaterialCostMultiplier` | 修理材料消耗倍率 |

范围使用比例值，例如 `0.25` 表示 25%。

## Mechanics：故障与过热

| 键 | 作用 |
|---|---|
| `fJamThreshold` | 低于该耐久比例后开始计算随机卡壳 |
| `fMaxJamChance` | 最大卡壳概率 |
| `bEnableEnergyFault` | 能量武器故障 |
| `fEnergyFaultChanceMultiplier` | 射击阶段能量故障倍率 |
| `fEnergyReloadFaultChanceMultiplier` | 换弹阶段能量故障倍率 |
| `fEnergyFaultDuration` | 能量故障持续时间 |
| `bEnableWeaponOverheat` | 过热系统 |
| `fOverheatHeatPerShot` | 每次开火增加的基础热量 |
| `fOverheatCooldownRate` | 停止射击后的冷却速度 |
| `fOverheatRecoveryThreshold` | 过热状态恢复阈值 |
| `fOverheatWearMultiplier` | 过热状态下的额外武器磨损倍率 |
| `iJammingPhase` | `0` 射击卡壳，`1` 换弹卡壳 |
| `bQuickUnjam` | 快速解除故障 |

过热资格由武器 Profile 的 `mechanism` 和代码中的武器/弹药识别共同决定。单独修改弹药磨损 profile 不会把 10mm 手枪变成过热武器。

## Mechanics：性能、修理和维修

| 键 | 作用 |
|---|---|
| `iDegradationMode` | `0` NV 阶梯性能衰减，`1` FO76 线性衰减 |
| `fPenaltyThreshold` | 性能惩罚开始阈值 |
| `fPenaltyMultMid` | 中段耐久性能倍率 |
| `fPenaltyMultLow` | 低段耐久性能倍率 |
| `fLinearMinMult` | 线性模式最低倍率 |
| `bWeaponConditionAffectsDamage` | 武器耐久影响伤害 |
| `bWeaponConditionAffectsValue` | 武器耐久影响价格 |
| `bArmorConditionAffectsResistance` | 护甲耐久影响抗性 |
| `bArmorConditionAffectsValue` | 护甲耐久影响价格 |
| `iRepairMaterialMode` | 修理材料模式 |
| `bDynamicRepairMaterialCost` | 动态材料消耗 |
| `bEnablePipboyJuryRepair` | Pip-Boy 混修 |
| `bEnableWorkbenchMaterialRepair` | 工作台材料修理 |
| `bEnableRepairKits` | 修理包 |
| `bConfirmConsumeFavoriteMaterial` | 消耗收藏材料前确认 |
| `bConfirmConsumeLegendaryMaterial` | 消耗传奇材料前确认 |
| `fJuryRepairCap` | Pip-Boy 混修方法上限 |
| `fWorkbenchRepairCap` | 工作台方法上限 |

维修最终上限还会经过 `Repair/OverRepair/` 中的技能档位，实际使用两者的较小值。

## Debug

| 键 | 作用 |
|---|---|
| `bEnableLogging` | 写入 `ConditionSystemFramework.log` 的详细诊断日志 |

遇到 Provider 未选中、FormID 未解析、分类错误或技能未识别时，先开启该项，再完全重启游戏复现。日志比只看 MCM 页面更能区分“配置未加载”和“规则加载但条件未命中”。


# ConditionSystemFramework Development Status

Last updated: 2026-07-31

这是下一位 AI 或开发者的交接入口。开始工作前同时阅读 `mod.json`、作者指南、
实际发布 JSON 和最新运行日志。

## 已完成

- 普通护甲 Schema 3：Material、Region、Grade、Ignore 与 PowerArmor 装备域。
- 护甲 FLST 递归编译、类型验证、循环检测和 BOD2 Region 兜底。
- 护甲实例角色：Size 只决定 Grade；Tier、Lining、Weave、Paint、Legendary、Misc
  作为修改器职责保留，不改变三层分类。
- `Equipment.Domain.PowerArmor` 在普通护甲分类前退出，保留原版动力装甲耐久。
- 可选 ArmorKeywords Bridge，且桥接 Provider 可覆盖原版基线。
- 武器 Classification Schema 3：
  - 可共存 `Weapon.Family`
  - 互斥 `Weapon.Handling`
  - 可选互斥 `Weapon.Configuration`
  - base/instance 上下文
  - 当前安装 OMOD 与 effective-added Keyword
  - Attach Point 角色及分组优先级
- Provider 排序模式：
  - `JsonPriorityFirst`
  - `PluginLoadOrderFirst`
- Jury Rigging Schema 3：
  - `sameBase`
  - `sameGroup`
  - `sameOptionalGroup`
  - `sharesAnyCategory`
  - `groupPair`
  - `always`
  - `optionalGroupMultiplier`
- Standalone Repair、Fallout4、YAE、Classic Skill System 技能 Provider。
- Over Repair Provider 已迁移到 Schema 3 分类：
  - 枪械使用 `Weapon.Handling.Pistol/Rifle/Heavy`
  - 近战使用 `Weapon.Family.Melee`
  - 护甲使用 `Armor.Material.*`
- 通用类别：WEAP 结果包含 `weapon`，ARMO 结果包含 `armor`。
- xEdit 审计脚本已区分导出武器与护甲 TSV。
- RobCo 武器修正规则用于校准明确的原版 Keyword 错误。
- 武器与护甲排除均已迁移到分类 Provider：
  - `Weapon.Flag.Ignore`
  - `Armor.Flag.Ignore`
  - 旧 `Durability/Consumption/Exclusions` 不再是运行时资格来源。
- 护甲 Jury Rigging 已使用可选 Grade 语义：
  - 两边都没有 Grade 时，`sameOptionalGroup` 允许匹配。
  - 只有一边有 Grade，或双方 Grade 不同，仍不匹配。
- 耐久消耗已接入分类结果：
  - 武器基础耐久由 `Weapon.Handling` 选择，Family 与 Configuration 提供叠加修正。
  - 护甲基础耐久由 `Armor.Material` 选择，Grade 与 Region 提供叠加修正。
  - 当前实例 OMOD 的最大耐久和磨损修正继续生效。
  - HUD、维修、战利品初始化、条件查询与实际扣减统一使用实例有效最大耐久。
  - 护甲磨损只响应 `TESHitEvent` 的物理、能量和爆炸命中；辐射及通用魔法效果不再扣耐久。

## 实机验证

最新日志确认：

- `CSF_Weapons.json` 与武器排除 Provider 成功注册，共 22 个武器来源。
- 护甲注册表成功编译 14 个概念和 7 个 Attach Point 角色。
- Jury Rigging 选择 `StandaloneRepair.esp.json`，
  `ProviderPriority=1000`。
- 同基础 WEAP 仍需具有相同 `Weapon.Handling`，命中
  `Exact Base Weapon With Same Handling`，优先级 300；同一模块化基础记录的
  Pistol 与 Rifle 实例不会再因 FormID 相同而互相供体。
- 10mm 手枪和自制手枪都得到
  `Weapon.Family.Ballistic + Weapon.Handling.Pistol`。
- Repair 25 后，不同基础记录但 Family 与 Handling 相同的装备可以混修。
- 双手近战跨基础记录命中
  `Melee: Same Handling (Repair 25)`，优先级 200。
- Ballistic Pistol 与 Ballistic Rifle 因 Handling 不同而被拒绝。
- Ballistic Rifle 与 Energy Rifle 在 Repair 25 因不共享 Family 而被拒绝。
- 普通护甲在 Repair 25 使用 Material + Region + 可选 Grade；无 Grade 的同材质、
  同区域衣物可以正常互修。
- Textile 衣物在 Repair 25 可使用同 Region 的服装规则；其他材质仍遵循护甲规则。
- Repair 50 放宽为同 Material + Region，Repair 75 放宽为同 Region，
  Repair 100 允许普通护甲通用混修。
- `Armor.Flag.Ignore`、`Weapon.Flag.Ignore` 与 `Armor.Flag.NoDonor` 在候选生成前过滤。
- 已验证日志中没有错误、警告或未解决分类冲突。

日志路径：

```text
D:/UserData/Documents/My Games/Fallout4/F4SE/ConditionSystemFramework.log
```

## 权威实现与配置

```text
src/ClassificationManager.cpp
src/ArmorConceptRegistry.cpp
src/ProfileManager.cpp

data/F4SE/Plugins/ConditionSystemFramework/Classifications/Defaults/CSF_Weapons.json
data/F4SE/Plugins/ConditionSystemFramework/Classifications/Defaults/CSF_Weapon_Exclusions.json
data/F4SE/Plugins/ConditionSystemFramework/Classifications/Defaults/CSF_Armor_Concepts.json
data/F4SE/Plugins/ConditionSystemFramework/Classifications/Defaults/CSF_Armor_Exclusions.json
data/F4SE/Plugins/ConditionSystemFramework/Classifications/Integrations/CSF_ArmorKeywords_Bridge.json
data/F4SE/Plugins/ConditionSystemFramework/Classifications/Integrations/CSF_ArmorKeywords_Exclusions.json
data/F4SE/Plugins/ConditionSystemFramework/Classifications/ProviderSelection.json

data/F4SE/Plugins/ConditionSystemFramework/Durability/Consumption/Weapons/VanillaWeapons.json
data/F4SE/Plugins/ConditionSystemFramework/Durability/Consumption/Armors/01_Vanilla_Armors.json
data/F4SE/Plugins/ConditionSystemFramework/Durability/Consumption/CategoryModifiers/VanillaCategoryModifiers.json

data/F4SE/Plugins/ConditionSystemFramework/Repair/ProviderSelection.json
data/F4SE/Plugins/ConditionSystemFramework/Repair/JuryRigging/StandaloneRepair.esp.json
data/F4SE/Plugins/ConditionSystemFramework/Repair/OverRepair/You Are Exceptional.esp.json
```

## 测试、构建和同步

```powershell
projects/ConditionSystemFramework/scripts/test-weapon-classification.ps1
xmake -y ConditionSystemFramework
scripts/sync-overlay.ps1 -Name ConditionSystemFramework
```

最近测试结果：

```text
JSON files parsed: 26, invalid: 0
[PASS] Weapon classification, Over Repair, Jury Rigging schema 3, and RobCo correction checks passed.
[100%]: build ok
```

MO2 目标：

```text
D:/TMR AE/mods/ConditionSystemFramework Dev
```

## 下一阶段

- 审核 `Durability/Consumption` 的武器、护甲、OMOD、弹药和 Modifier 消耗配置。
- 评估耐久消耗是否应直接复用 Classification Schema 3 的概念，而不是继续维护
  独立的具体 FormID/Keyword 名单。
- 核对开火、近战命中、物理受击和魔法受击事件到最终耐久扣除的完整计算链。
- 核对 DamageFormula、基础最大耐久、平面消耗、倍率修正与当前实例 OMOD 的组合顺序。
- 添加多 Family、Grip 切换 Pistol/Rifle、Receiver 改 Family、
  Shotgun/Sniper 冲突、社区 AP 和 ArmorKeywords 优先级测试。
- 系统稳定后降低或按开关控制 `JuryDiag` 日志量。
- 继续处理天然攻击、机器人、炮塔和不可持有 WEAP 的资格过滤，不污染分类 FLST。

## 不变量

- 存档不保存分类结果，每次数据加载后重新计算。
- 外部 FLST 不写入 CSF 主插件。
- 不扫描全部可安装 OMOD。
- 不使用 FLST 成员顺序表达优先级。
- 不用遍历顺序解决同强度互斥冲突。
- CSF 主 ESP 不直接依赖 ArmorKeywords。
- JSON 不使用解析后的 `FE...` FormID。
- 分类 ID 大小写必须完全一致。

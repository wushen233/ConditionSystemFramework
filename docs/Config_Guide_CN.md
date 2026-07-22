# Condition System Framework — 配置编写指南（中文）

本框架的所有规则都通过 **JSON 配置文件** 定义，位于：

```
Data/F4SE/Plugins/ConditionSystemFramework/
```

你不需要写代码，只要按本文档编辑 JSON 即可为任意武器、护甲、弹药、改装件、传奇词缀、修理包等定制耐久行为。

> 📦 **前置依赖**：本框架依赖两个 UI 前置 —— **PrismaUI_F4**（渲染屏幕上的耐久 HUD 小部件）与 **Item Integration Framework (IIF)**（在物品信息卡上渲染耐久条）。这两个框架由各自维护，CSF 只是调用它们的 API。

> 提示：JSON 文件里可以写 `//` 行注释（本框架的解析器允许）。但请保持标准 JSON 语法（逗号、引号、括号都不能错），否则该文件会整份加载失败。

---

## 一、通用概念（务必先读）

### 1. 目录会被整体扫描
每个子目录（`Weapons/`、`Armors/` 等）下的**所有 `.json` 文件都会被加载**。你可以把规则拆成多个文件（例如 `01_Vanilla.json`、`02_MyMod.json`），互不影响。文件名只影响阅读顺序，不影响逻辑。

### 2. 优先级 `priority`
当一件物品同时匹配多个 profile 时，**`priority` 数值最大的胜出**。建议越具体的规则给越高的优先级，兜底规则给 `0`。

### 3. 匹配方式：FormID 优先，其次关键词
对每件物品，框架按以下顺序找规则：
1. **先按 `formIDs` 精确匹配**（最高优先，命中即用）。
2. **再按 `keywords` 关键词匹配**（按 priority 从高到低，第一个匹配的 profile 生效）。

### 4. 关键词的"与/或"嵌套（重点）
`keywords` 是一个**二维数组**：
- **外层数组 = 或（OR）**：满足任意一组即可。
- **内层数组 = 且（AND）**：该组内所有关键词都必须同时具备。

```jsonc
"keywords": [ ["WeaponTypePistol"] ]            // 含 WeaponTypePistol
"keywords": [ ["A"], ["B"] ]                     // 含 A 或 含 B
"keywords": [ ["ReceiverPowerful", "Receiver_Automatic"] ]  // 同时含这两个才匹配
```

### 5. FormID 的两种写法
- **`formIDs` 列表**（武器/护甲/弹药/改装/传奇/排除）：用 `"插件名|十六进制ID"`，例如：
  ```jsonc
  "formIDs": ["Fallout4.esm|1CC2AD", "MunitionsReloaded.esp|40920"]
  ```
- **修理包 `formIDs`**：是一个对象 `{ "插件名": ["十六进制ID", ...] }`（见下方修理包章节）。
- **`RequiredSkill` / `RequiredAV`**：是一个对象 `{ "Plugin": "...", "FormID": "0x..." }`，FormID 用 `0x` 开头的完整十六进制。

> 如何查 FormID：用 FO4Edit/xEdit 打开对应插件，找到记录，复制其 FormID（去掉前两位的加载序号，用插件内的局部 ID）。文末附常用原版 FormID 速查表。

---

## 二、武器 `Weapons/*.json`

控制武器最大耐久、磨损速度、是否会卡壳。

```jsonc
{
  "profiles": [
    {
      "name": "Sniper_Rifles",        // 仅作标识，可任意命名
      "priority": 90,                  // 默认 0
      "maxDurability": 600.0,          // 满耐久点数；默认 1000
      "degradeRate": 4.0,              // 每次射击的基础磨损点数；默认 1.0
      "canJam": true,                  // 是否可能卡壳；默认 true
      "keywords": [ ["WeaponTypeSniper"] ],
      "formIDs": []                    // 可选：按具体武器 FormID 精确匹配
    }
  ]
}
```

| 字段 | 类型 | 默认 | 含义 |
|------|------|------|------|
| `name` | 字符串 | — | 规则名称（仅标识） |
| `priority` | 整数 | 0 | 冲突时数值大者胜 |
| `maxDurability` | 浮点 | 1000 | 满状况对应的耐久点数 |
| `degradeRate` | 浮点 | 1.0 | 每发射击基础磨损（再受弹药/改装倍率影响） |
| `canJam` | 布尔 | true | 是否参与卡壳系统 |
| `keywords` | 二维数组 | — | OR/AND 关键词匹配 |
| `formIDs` | 数组 | — | `"插件|十六进制"` 精确匹配 |

---

## 三、护甲 `Armors/*.json`

```jsonc
{
  "profiles": [
    {
      "name": "Heavy_Power_Armor_Plating",
      "priority": 100,                 // 默认 0
      "maxDurability": 2000.0,         // 默认 500
      "useFlatDegrade": true,          // 见下；默认 false
      "degradeRate": 1.5,              // 仅当 useFlatDegrade=true 时使用；默认 0.0
      "keywords": [ ["ArmorTypePower"] ],
      "formIDs": []
    }
  ]
}
```

| 字段 | 类型 | 默认 | 含义 |
|------|------|------|------|
| `name` | 字符串 | — | 规则名称 |
| `priority` | 整数 | 0 | 冲突时数值大者胜 |
| `maxDurability` | 浮点 | 500 | 满状况耐久点数 |
| `useFlatDegrade` | 布尔 | false | `false`=按原生 DR/ER 抗性动态计算磨损（更拟真）；`true`=每次命中固定扣 `degradeRate` 点 |
| `degradeRate` | 浮点 | 0.0 | 仅在 `useFlatDegrade=true` 时生效，每次命中固定磨损点数 |
| `keywords` | 二维数组 | — | OR/AND 关键词匹配 |
| `formIDs` | 数组 | — | 精确匹配 |

> ⚠️ **关于 `damageResponses`（已废弃）**：早期版本的护甲示例里有 `damageResponses`（按伤害类型设免疫/倍率）。**当前代码完全不读取该字段，已从默认配置中移除，请勿再使用。** 护甲针对不同伤害类型的耐久表现，现在统一由引擎原生抗性 + 下方 `Rules/01_DamageFormula.json` 的 `ratingMultipliers` 决定（按伤害类型全局生效，而非逐件护甲配置）。

---

## 附：护甲磨损公式与调优

如果玩家反映"护甲太容易坏了"，可以通过调整以下配置来改变护甲的耐久消耗速度。

### 磨损计算公式

非动力甲护甲默认使用**动态公式**计算每次命中的磨损：

```
durabilityCost = totalResisted × durabilityDamageConstant(0.15) × impactMultiplier
```

其中：
- `totalResisted` = 引擎原生 DR + ER 抗性吸收的伤害量
- `durabilityDamageConstant` = 全局系数（默认 0.15）
- `impactMultiplier` = 根据防护效果动态变化（完美防御时 0.1，被贯穿时 0.4）

> ⚠️ **反直觉现象**：护甲抗性越高，吸收的伤害越多 → `totalResisted` 越大 → 磨损反而更快！

### 调优杠杆（按推荐程度排序）

| 方法 | 位置 | 默认值 | 调整方向 | 效果 |
|------|------|-------|---------|------|
| **降低全局系数** | `Rules/01_DamageFormula.json` → `durabilityDamageConstant` | **0.15** | → 0.05 | 所有护甲磨损降至约 1/3，最推荐 |
| **降低刮擦倍率** | 同上 → `glanceMultiplier` / `scratchMultiplier` | 0.1 / 0.4 | 减小 | 完美防御或被贯穿时磨损更少 |
| **提高耐久池** | `Armors/*.json` → `maxDurability` | 300-2000 | **增大** | 直接拉长耐久消耗时间 |
| **改用固定磨损** | `Armors/*.json` → `useFlatDegrade: true` + `degradeRate` | false/0 | 设小值 | 每次命中固定扣点，不再依赖抗性 |

### 最简方案示例

```jsonc
// Rules/01_DamageFormula.json
{
  "DamageFormula": {
    "durabilityDamageConstant": 0.05,  // 0.15 → 0.05，磨损减少约 3 倍
    "glanceMultiplier": 0.05,          // 完美防御磨损更少
    "scratchMultiplier": 0.2           // 被贯穿磨损也更少
  }
}
```

### 各场景耐久消耗参考

以 100 点伤害命中为例：

| 护甲类型 | maxDurability | 抗性 | 每次磨损 | 可用次数 |
|---------|-------------|------|---------|---------|
| Combat Armor | 1200 | 高（90%减伤） | ~1.35 | ~888次 |
| Raider Armor | 600 | 中（50%减伤） | ~1.50 | ~400次 |
| Clothing | 300 | 低（10%减伤） | ~1.80 | ~166次 |
| Power Armor（固定磨损） | 2000 | — | 1.5/次 | ~1333次 |

---

## 四、弹药 `Ammos/*.json`

让不同弹种对武器磨损 / 敌方护甲耐久产生不同影响。

```jsonc
{
  "profiles": [
    {
      "name": "MR_10mm_AP",            // 穿甲弹
      "weaponWearMult": 1.5,           // 武器磨损倍率；默认 1.0
      "armorDamageMult": 2.0,          // 对敌方护甲的耐久破坏倍率；默认 1.0
      "formIDs": ["MunitionsReloaded.esp|40920"],
      "keywords": []                   // 也可用关键词匹配
    }
  ]
}
```

| 字段 | 类型 | 默认 | 含义 |
|------|------|------|------|
| `weaponWearMult` | 浮点 | 1.0 | 该弹种让武器磨损的倍率 |
| `armorDamageMult` | 浮点 | 1.0 | 该弹种破坏敌方护甲耐久的倍率 |
| `formIDs` / `keywords` | — | — | 匹配方式同上 |

---

## 五、传奇词缀 `Modifiers/*.json`

针对传奇 OMOD（如"无尽""坚不可摧"）覆盖磨损与卡壳。

```jsonc
{
  "profiles": [
    {
      "name": "NeverBreak",
      "priority": 100,                 // 默认 0
      "degradeMult": 0.0,              // 最终磨损倍率；0.0=永不损耗；默认 1.0
      "immuneToJam": true,             // 完全免疫卡壳；默认 false
      "formIDs": ["Fallout4.esm|1CC2AD"]
    }
  ]
}
```

| 字段 | 类型 | 默认 | 含义 |
|------|------|------|------|
| `degradeMult` | 浮点 | 1.0 | 最终磨损倍率（0=不损耗，1.5=损耗更快） |
| `immuneToJam` | 布尔 | false | 是否免疫卡壳 |

---

## 六、改装件 `Omods/*.json`

针对武器/护甲的部件（机匣、枪管、消音器等）微调耐久。多个改装件效果会**叠加**。

```jsonc
{
  "profiles": [
    {
      "name": "HeavyBarrel",
      "priority": 40,                  // 默认 0
      "degradeMult": 0.8,              // 磨损倍率(累乘)；默认 1.0
      "degradeRateFlat": 0.0,          // 固定磨损加成(累加)；默认 0.0
      "maxDurabilityMult": 1.3,        // 最大耐久倍率(累乘)；默认 1.0
      "immuneToJam": false,            // 默认 false
      "formIDs": [],
      "keywords": [["BarrelHeavy"]]
    }
  ]
}
```

| 字段 | 类型 | 默认 | 含义 |
|------|------|------|------|
| `degradeMult` | 浮点 | 1.0 | 磨损**乘**倍率（1.3=快30%，0.7=慢30%） |
| `degradeRateFlat` | 浮点 | 0.0 | 每次射击**加/减**的固定磨损点数 |
| `maxDurabilityMult` | 浮点 | 1.0 | 最大耐久**乘**倍率（1.2=+20%） |
| `immuneToJam` | 布尔 | false | 该部件是否免疫卡壳 |

---

## 七、修理包 `RepairKits/*.json`

定义可消耗修理物品的修复能力。

```jsonc
{
  "profiles": [
    {
      "name": "Expert Weapon Repair Kit",
      "formIDs": { "Condition System Framework.esp": [ "F09" ] },  // 注意格式!
      "canRepairWeapon": true,         // 默认 false
      "canRepairArmor": false,         // 默认 false
      "baseRepairPoints": 1200,        // 武器：按固定耐久点数修复
      // "baseRepairPercent": 0.40,    // 护甲：按百分比修复（二选一）
      "maxConditionLimit": 1.25        // 本修理包能修到的状况上限；默认 1.0
    }
  ]
}
```

| 字段 | 类型 | 默认 | 含义 |
|------|------|------|------|
| `formIDs` | 对象 | — | `{ "插件名": ["十六进制", ...] }`，与其它配置的数组格式**不同** |
| `canRepairWeapon` | 布尔 | false | 能否修武器 |
| `canRepairArmor` | 布尔 | false | 能否修护甲 |
| `baseRepairPoints` | 浮点 | — | 按**固定点数**修复（多用于武器）。填了它就走点数模式 |
| `baseRepairPercent` | 浮点 | — | 按**百分比**修复（如 0.40=40%，多用于护甲） |
| `maxConditionLimit` | 浮点 | 1.0 | 该修理包最多修到多少状况（1.25=125%，可轻度过修） |

> `baseRepairPoints` 与 `baseRepairPercent` 选其一即可：写了 `baseRepairPoints` 就用固定点数，否则用百分比。

---

## 八、排除/黑名单 `Exclusions/*.json`

---

## 九、伤害公式 `Rules/01_DamageFormula.json`

全局耐久伤害公式（一般无需修改）。

```jsonc
{
  "DamageFormula": {
    "enabled": true,                    // 总开关
    "baseHardness": 1.0,
    "glanceThreshold": 0.3,             // 刮擦（擦伤）阈值
    "glanceMultiplier": 0.1,            // 刮擦时的耐久伤害倍率
    "scratchThreshold": 0.7,
    "scratchMultiplier": 0.4,
    "durabilityDamageConstant": 0.15,   // 全局耐久伤害系数
    "ratingMultipliers": {              // 各伤害类型的抗性→硬度转换系数
      "default": 0.2,                   // 物理子弹：DR × 0.2
      "DamageTypeEnergy": 0.1,
      "DamageTypeFire": 0.05,
      "DamageTypeCryo": 0.3,
      "DamageTypePoison": 0.0,
      "DamageTypeRadiation": 0.0
    }
  }
}
```

> 这里的 `ratingMultipliers` 就是替代了已废弃的逐件护甲 `damageResponses` 配置。系数越高 → 该伤害类型越容易被护甲硬度吸收 → 护甲受的耐久磨损**相对更少**。

让某些物品**不参与耐久系统**或**不显示状况条**（如哔哔小子、剧情关键物品、动力甲）。

```jsonc
{
  "rules": [
    {
      "name": "Full Exclusions",
      "keywords": ["Unplayable", "ArmorTypePipBoy"],   // 一维字符串数组（含任一即排除）
      "formIDs": ["Fallout4.esm|021B3B"],
      "enableDurabilitySystem": false,  // 默认 false：不计算耐久
      "showDurabilityUI": false         // 默认 false：UI 不显示状况
    }
  ]
}
```

| 字段 | 类型 | 默认 | 含义 |
|------|------|------|------|
| `keywords` | 字符串数组 | — | 含任一关键词即命中（注意这里是**一维**，与武器/护甲不同） |
| `formIDs` | 数组 | — | `"插件|十六进制"` |
| `enableDurabilitySystem` | 布尔 | false | 是否对命中物品计算耐久损耗 |
| `showDurabilityUI` | 布尔 | false | 是否在 UI 显示状况（可单独控制：例如动力甲 `enableDurabilitySystem=false` 但 `showDurabilityUI=true`） |

---

## 十、过量维修规则 `Rules/01_OverRepairRules.json`

按玩家**技能等级**决定某类物品可修复到的状况上限（可超过 100%）。

```jsonc
{
  "OverRepairRules": [
    {
      "RuleName": "Over-Repair Firearms (Gun Nut)",
      "TargetKeywords": ["WeaponTypePistol", "WeaponTypeRifle"],  // 适用物品
      "RequiredSkill": {
        "Type": "Perk",                // "Perk"=读取特技等级；"AV"=读取属性值
        "Plugin": "Fallout4.esm",
        "FormID": "0x0004A0DA"         // 枪械迷 GunNut01
      },
      "AVStep": 25,                    // Type=AV 时的步长；Perk 规则忽略
      "RankLimits": [1.0, 1.25, 1.50, 1.75, 2.0]
    }
  ]
}
```

- `RankLimits`：**索引 = 技能等级**。索引 0 = 无该特技（上限 100%=1.0），索引 1 = 等级 1（1.25=125%），以此类推。
- `Type: "Perk"` 时框架读取玩家该 Perk 的等级；`Type: "AV"` 时读取属性值并按 `AVStep` 分级。

---

## 十一、急造/跨类修理 `Rules/03_JuryRigging_Vanilla.json`

允许用一件物品修理另一件（跨种类），并设技能门槛。

```jsonc
{
  "JuryRiggingRules": [
    {
      "RuleName": "Ballistic Firearms: Cross-Type Repair (Gun Nut Rank 4)",
      "TargetKeywords": ["WeaponTypePistol", "WeaponTypeRifle"],  // 目标与材料都要属于此大类
      "MatchCategories": [],            // 留空=大类内可跨种类互修；填了=两件必须共享其中一个精确标签
      "MaterialConditionMultiplier": 1.0, // 材料耐久转化效率
      "RequiredSkill": {
        "Type": "Perk", "Plugin": "Fallout4.esm",
        "FormID": "0x0004A0DA", "MinRank": 4  // 需要该 Perk 达到 4 级
      }
    }
  ]
}
```

| 字段 | 含义 |
|------|------|
| `TargetKeywords` | 目标物品和材料物品都必须匹配的大类标签 |
| `MatchCategories` | 分类锁：留空=大类内随便互修；填写=两件必须共享其中一个标签 |
| `MaterialConditionMultiplier` | 材料耐久转成目标耐久的效率（1.0=100%） |
| `RequiredSkill.MinRank` | 需要的最低 Perk 等级 |

---

## 十二、物品卡片 UI `ItemUICards.json`

> 📦 **依赖说明**：物品信息卡上的耐久条**不是 CSF 自己渲染的**，而是由前置框架 **Item Integration Framework (IIF)** 统一渲染。CSF 读取本文件得到锚点设置后，通过 IIF 的 API 注册一张名为 `CND` 的卡片。也就是说：本文件控制"CND 卡锚定在哪"，而"长什么样、怎么排版"由 IIF 负责。玩家还可以用 IIF 的**游戏内卡片编辑器**进一步调整顺序；卡片系统的进阶定制请参阅 IIF 自带的文档文件。

控制状况条在物品信息卡上的锚点位置（一般无需改）。

```jsonc
{
  "ItemUICards": {
    "CND": {
      "priority": 0,
      "anchorTarget": "$ammo",         // 锚定到哪一行（$ammo=弹药行）
      "anchorMode": "after",           // before / after
      "fallbackAnchorTarget": "TOP",   // 找不到锚点时的兜底
      "fallbackAnchorMode": "after"
    }
  }
}
```

---

## 附录：常用原版 FormID 速查（已用 xEdit 校验）

### SPECIAL 属性（Actor Value，用于 `RequiredAV` / `Type:"AV"`）
| 属性 | FormID |
|------|--------|
| 力量 Strength | `0x0002C1` |
| 感知 Perception | `0x0002C3` |
| 耐力 Endurance | `0x0002C5` |
| 魅力 Charisma | `0x0002C7` |
| 智力 Intelligence | `0x0002C9` |
| 敏捷 Agility | `0x0002CB`（注意：**不是**幸运！）|
| **幸运 Luck** | **`0x0002C8`** |

### 制造特技（Perk，用于 `RequiredSkill` `Type:"Perk"`，均为多级特技的基础记录）
| 特技 | FormID | EditorID |
|------|--------|----------|
| 枪械迷 Gun Nut | `0x0004A0DA` | GunNut01 |
| 铁匠 Blacksmith | `0x0004B253` | Blacksmith01 |
| 护甲商 Armorer | `0x0004B254` | Armorer01 |
| 科学！ Science! | `0x000264D9` | Science01 |

> 自定义时务必用 FO4Edit/xEdit 核对：复制的 FormID 必须是你想要的那条记录的 **EditorID/名称**对得上，别只看数字相近。

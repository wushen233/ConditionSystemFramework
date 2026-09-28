# CSF Classification Providers

配置支持 JSONC 注释。表单引用支持 `Plugin|localFormID`、
`Plugin|fullFormID` 和直接使用 `EditorID`。普通 ESP 与 ESL/ESL-flagged ESP
均可识别，分类 ID 区分大小写。

例如 ESL 记录可以写成：

```jsonc
"list": "S7 System.esp|0xFE02716A"
```

如果记录有稳定的 EditorID，也可以直接写成：

```jsonc
"forms": ["S7_SkillPistols"]
```

## 目录

- `Defaults/`：CSF 原版和 DLC 基线。
- `Integrations/`：可选框架或模组桥接。
- `Overrides/`：整合作者或用户修正。
- `ProviderSelection.json`：Provider 冲突时优先比较 JSON 优先级还是插件顺序。

排除规则使用独立 Provider 文件：

- `Defaults/CSF_Armor_Exclusions.json`：CSF 护甲排除列表。
- `Defaults/CSF_Weapon_Exclusions.json`：CSF 武器排除列表。
- `Integrations/CSF_ArmorKeywords_Exclusions.json`：ArmorKeywords 桥接排除列表。
- `Integrations/CSF_CCSBJFO4003_Grenade.json`：可选的 Creation Club 手雷发射器桥接。

所有 `requires` 均满足的分类 Provider 都会加载并虚拟合并。外部 FLST 不会写入
CSF 主插件。

`CSF_CCSBJFO4003_Grenade.esp` 只在安装了 `ccSBJFO4003-Grenade.esl` 时启用；
没有该 Creation Club 内容时保持禁用即可，主插件不再依赖它。

## 当前 Schema

### 武器 Schema 3

```jsonc
{
  "schemaVersion": 3,
  "module": "weaponClassifications",
  "domain": "weapon",
  "provider": "Example",
  "requires": ["Example.esp"],
  "ProviderPriority": 100,
  "classifications": {
    "Weapon.Family.Ballistic": [
      {
        "list": "Example.esp|0x000800",
        "contexts": ["base", "instance"],
        "listPriority": 100
      }
    ]
  }
}
```

- `Weapon.Family`：可共存。
- `Weapon.Handling`：互斥。
- `Weapon.Configuration`：可选互斥。
- base 检查 WEAP。
- instance 只检查当前安装 OMOD 和实例新增的最终有效 Keyword。
- FLST 支持 KYWD、OMOD、嵌套 FLST。
- `Weapon.Flag.Ignore` 的 FLST 额外接受具体 WEAP。
- 一个 FLST 内成员共享 `listPriority`，成员顺序不是优先级。
- `modRoles` 用 Attach Point KYWD FLST 注册 Grip、Barrel、Receiver 等角色。

`Weapon.Flag.Ignore` 是可共存标记组。命中后物品不进入耐久系统、维修目标或
维修供体。不要直接使用 `WeaponTypeUnarmed` 作为排除 Keyword，因为正常拳套类
玩家武器也携带该 Keyword。

### 普通护甲 Schema 3

```jsonc
{
  "schemaVersion": 3,
  "module": "armorClassifications",
  "domain": "armor",
  "provider": "Example",
  "requires": ["Example.esp"],
  "ProviderPriority": 100,
  "classifications": {
    "Armor.Material.Flexible": [
      {
        "list": "Example.esp|0x000900",
        "contexts": ["base"],
        "listPriority": 100
      }
    ]
  }
}
```

普通护甲使用 `Armor.Material`、`Armor.Region`、`Armor.Grade` 与独立
`Armor.Flag.Ignore`。Material 和 Region 只读取基础 ARMO；只有 Size 角色可以
通过当前实例 OMOD 决定 Grade。Tier、Lining、Weave、Paint、Legendary、Misc
被识别为实例修正角色，但不参与三层分类。Region 未命中时使用 BOD2 兜底。

`Equipment.Domain.PowerArmor` 在普通护甲分类前执行。命中后退出 CSF 普通护甲
耐久流程，保留 Fallout 4 原版动力装甲耐久，供未来独立桥接模块使用。

完整说明：

```text
projects/ConditionSystemFramework/docs/Classification_Repair_Author_Guide_CN.md
projects/ConditionSystemFramework/docs/Classification_Repair_Author_Guide_EN.md
```

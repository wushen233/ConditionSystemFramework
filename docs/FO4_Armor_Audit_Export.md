# FO4 Armor And Weapon Audit Export

脚本：[FO4 - Export Armor Audit Data.pas](../../../vendor/tools/xedit/Edit%20Scripts/FO4%20-%20Export%20Armor%20Audit%20Data.pas)

这是只读审计工具：不会创建、复制、覆盖或保存任何 ESP/ESM/ESL 记录。

## 使用

1. 通过 MO2 以 Fallout 4 模式启动 FO4Edit，并加载需要检查的插件。
2. 在左侧选择以下任一种来源后执行 `Apply Script`：
   - 一条或多条 `ARMO`、`WEAP` 或 `OMOD`；
   - 一个包含这些记录的记录组；
   - 一个或多个插件根节点。选中插件根节点时，脚本会扫描其 `ARMO`、`WEAP` 和 `OMOD` 顶层组。
3. 运行 `FO4 - Export Armor Audit Data`。
4. 在模式窗口中选择：`Yes` 为 Raw，`No` 为 Winning Override，`Cancel` 退出。
5. 在保存对话框中选择输出目录。取消对话框时，文件写入 `Edit Scripts` 目录。

## 模式

- `Raw`：导出所选记录或所选插件顶层组中实际存在的记录。适合观察作者原始定义和单个补丁覆盖。
- `Winning Override`：每条候选记录先解析至当前加载顺序的获胜覆盖，再按获胜记录 FormID 去重。适合审计整合后的实际结果。

## 输出文件

- `ArmorRecords.tsv`：来源/获胜插件、FormID、EDID、名称、模板、可装备标记、重量、价值、效果、BOD2、ARMA、抗性、APPR、INNR 和关键词汇总。
- `ArmorKeywords.tsv`：每一条 ARMO 与 KYWD 关系各占一行。
- `ArmorKeywordSummary.tsv`：关键词被导出的护甲引用的次数。
- `ArmorOMODRecords.tsv`：目标类型为 Armor 的对象改装记录。
- `WeaponRecords.tsv`：来源/获胜插件、FormID、EDID、名称、模板、重量、价值、效果、装备类型、弹药、基础伤害、动画类型、射速/延迟、射程、弹容量、AP 消耗、瞄准模型、APPR、INNR 和关键词汇总。
- `WeaponKeywords.tsv`：每一条 WEAP 与 KYWD 关系各占一行。
- `WeaponKeywordSummary.tsv`：关键词被导出的武器引用的次数。
- `WeaponOMODRecords.tsv`：目标类型为 Weapon 的对象改装记录；它与 WEAP 的 APPR/Keyword 数据组合后，可用于判断实例实际安装改装所表达的武器类型。
- `OtherOMODRecords.tsv`：目标类型既不是 Armor 也不是 Weapon 的对象改装，单独保留以免丢失审计信息。
- `PluginKeywordCatalog.tsv`：来源插件自身的完整 `KYWD` 目录。这是跨护甲/武器的插件级参考表。

只检查武器时，提供 `WeaponRecords.tsv`、`WeaponKeywords.tsv`、`WeaponKeywordSummary.tsv` 和 `WeaponOMODRecords.tsv` 即可。

## 已知限制

- FO4 的 ARMO、WEAP、ARMA、APPR、INRD 和伤害类型子字段并非所有插件都完整使用；缺失字段保持为空，脚本继续执行。
- `Playable` 依据 BOD2 的 `Non-Playable` 标志推断；模组自定义逻辑可能与此不同。
- 伤害抗性通过 `DAMA - Resistances` 按 Physical、Energy、Radiation 的 Damage Type EDID 匹配。使用自定义 DMGT 或特殊模板时可能为空。
- OMOD 的 `PropertyModifications` 是可读的原始属性运算摘要，例如 `ADD Weight=3,3 step=0 (Float)`；它不会替 OMOD 自动判定 Light、Standard 或 Heavy。
- 关键词、ARMA 和 APPR 的显示值使用 `Plugin:LocalFormID:EditorID`；空 EDID 是合法情况。
- `LocalFormID` 始终移除加载顺序和插件内部 master 索引；普通插件与 ESL 均可直接用于 `PluginName|LocalFormID`。
- `TStringList.SaveToFile` 使用当前 xEdit/PascalScript 运行时的文本编码。若需要稳定 UTF-8，请用能明确设置编码的外部工具转换导出的 TSV。

# CSF FO4Edit Automation

CSF 的 ESP、ESL、ESM、FormID List 和记录编辑必须通过 MO2 启动的 FO4Edit automation pipe 完成。

## 连接顺序

1. 用户通过 MO2 启动 `FO4Edit`，启动项包含 `-FO4 -automation-serve -IKnowWhatImDoing`。
2. AI 读取 `ai/task-guides/xedit-manual-mo2-attach.md`、`ai/task-guides/xedit-save-durability.md` 和 `xedit-mo2` skill。
3. 在工作区执行：

```powershell
. .\tools\xedit-mo2\Invoke-XEdit.ps1
Test-XEditAutomation
```

4. 使用 `Invoke-XEditRequest` 执行所有检查、`scripts.write`、`scripts.run`、保存与读回。

## 禁止替代方式

不要通过桌面点击、鼠标控制、OCR 截图、直接解析 ESP 二进制或独立启动 xEdit 来替代 automation pipe。

截图只用于让用户快速确认 UI；ESP 内容、FormID List 内容和落盘结果必须用 automation 请求读取确认。

## CSF 运行时配置与 ESP

- `projects/ConditionSystemFramework/data/` 是配置和运行时资产的源目录。
- 通过 `scripts/sync-overlay.ps1 -Name ConditionSystemFramework` 同步到 MO2 的 `ConditionSystemFramework Dev` 覆盖层。
- `Condition System Framework.esp` 与 `CSF_RepairKit.esp` 属于运行时可变文件；同步时保持 MO2 覆盖层内由 FO4Edit 修改的版本，避免用项目旧副本覆盖。
- 修改 ESP 后，`session.save` 出现 `savedFilesPendingShutdown` 时，用户必须自然关闭 FO4Edit；重新启动后，AI 再通过 automation pipe 读回对应记录，才算持久完成。

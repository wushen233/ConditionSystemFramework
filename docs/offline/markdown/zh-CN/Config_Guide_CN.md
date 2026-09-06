# Condition System Framework 配置文档

这是当前中文配置文档的入口。内容按运行时配置模块拆分，英文版本已经同步。

## 模块文档

1. [配置总览](config/00_配置总览_CN.md)
2. [分类模块](config/01_分类_CN.md)
3. [耐久消耗模块](config/02_耐久消耗_CN.md)
4. [伤害公式模块](config/03_伤害公式_CN.md)
5. [维修模块](config/04_维修_CN.md)
6. [战利品模块](config/05_战利品_CN.md)
7. [物品卡片模块](config/06_物品卡片_CN.md)
8. [Provider 选择与加载](config/07_Provider选择_CN.md)
9. [MCM 与 INI 模块](config/08_MCM与INI_CN.md)

## 配置根目录

```text
Data/F4SE/Plugins/ConditionSystemFramework/
```

MCM 用户配置位于：

```text
Data/MCM/Config/ConditionSystemFramework/settings.ini
```

## 阅读顺序

第一次配置时先看“配置总览”和“Provider 选择与加载”，再根据目标功能进入对应模块。需要添加新装备分类时看“分类模块”；需要修改磨损、Built to Destroy 或弹药倍率时看“耐久消耗模块”；需要调整 Jury Rigging、Repair 技能和工作台上限时看“维修模块”。

旧版单文件说明已经由上述模块文档取代。英文入口见 `Config_Guide_EN.md`。

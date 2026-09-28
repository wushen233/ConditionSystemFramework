# CSF 动画接口与 OAR 条件

ConditionSystemFramework 在检测到 `OpenAnimationReplacer.dll` 后，会在 F4SE `kPostLoad` / `kPostPostLoad` 阶段注册以下 OAR 条件。OAR 未安装时 CSF 不会因为这些接口改变原有逻辑。

## OAR 条件

| 条件 | 含义 |
|---|---|
| `CSF_IsWeaponJammed` | 玩家当前武器处于卡壳状态 |
| `CSF_IsUnjamming` | CSF 完整排障流程正在执行 |
| `CSF_IsWeaponDrawn` | CSF 已记录武器拔出，或游戏当前处于拔枪状态 |
| `CSF_WeaponDurability` | 当前 CSF 武器耐久度满足比较条件 |
| `CSF_IsEnergyFault` | 当前故障属于能量系统故障 |
| `CSF_IsOverheated` | 当前武器处于过热锁定 |
| `CSF_WeaponHeat` | 当前旋转/重型武器热量满足比较条件 |

`CSF_WeaponDurability` 的数值使用真实比例：`1.0` 是 100%，`0.75` 是 75%。JSON 示例：

```json
{
  "condition": "CSF_WeaponDurability",
  "comparison": 4,
  "value": 0.5
}
```

`comparison` 枚举为：`0 ==`、`1 !=`、`2 >`、`3 >=`、`4 <`、`5 <=`。

## 用现有动画模拟排障

CSF 的完整排障流程本身会执行：

1. 设置 `CSF_IsUnjamming` 状态。
2. 调用游戏原生收枪流程。
3. 等待排障进度完成。
4. 调用游戏原生拔枪流程。

因此没有专用排障 `.hkx` 时，依然可以直接使用原生收枪/拔枪作为动作回退。社区动画作者可以让 OAR 在 `CSF_IsUnjamming` 条件下替换这两个原生动画槽：

- 收枪槽：以 OAR Animation Log 中实际记录的 `weaponSheathe` 对应路径为准。
- 拔枪槽：以 OAR Animation Log 中实际记录的 `weaponDraw` 对应路径为准。

推荐给两个替换项都添加 `CSF_IsUnjamming`，并保留行为动画需要的原生事件。这样即使替换包缺失，CSF 仍然回退到原生收枪/拔枪，状态和排障逻辑不会失效。

## CSF F4SE 状态接口

其他 F4SE 插件可以监听消息 `CSAI`，并读取 CSF 返回的 `CSAO` 接口。公开头文件为：

```text
projects/ConditionSystemFramework/src/AnimationAPI.h
```

调用方发送 `ConditionSystem::AnimationAPI::kMessage_RequestInterface`，CSF 会返回 `Interface`：

- `weaponJammed`
- `unjamming`
- `weaponDrawn`
- `realWeaponEquipped`
- `weaponDurability`
- `unjamProgress`

这个接口只读运行时状态，不要求调用方持有 CSF 内部对象指针，也不改变存档格式。

## 能量系统故障

低耐久能量武器现在使用独立故障类型。它不会显示普通枪械的“枪机卡壳”，而是显示能量发射器稳定化失败，并使用较低的故障倍率：

- 开火故障倍率默认 `0.5x`。
- 换弹完成故障倍率默认 `0.25x`。
- MCM 可以关闭能量系统故障，或调整两个倍率。

能量系统故障现在是短暂的软故障：本次射击失败，输入在稳定化期间被吞掉，但不会设置普通卡壳状态，也不要求重新装弹；稳定化时间由 MCM 控制，默认 0.8 秒。`CSF_IsEnergyFault` 表示这段耐久相关的能量系统故障状态。

旋转机枪、加特林武器以及带重型武器分类的能量武器现在支持连续射击热量：每次确认消耗弹药的 `WeaponFire` 事件增加热量，停止射击后自动冷却，达到 100% 时进入过热状态。热量增加采用高热区递减曲线，越接近满值时单发增量越小，但持续射击仍然可以达到过热阈值。过热后继续射击不会锁定武器，但会按照 MCM 的过热耐久倍率增加磨损；热量降到恢复阈值后恢复正常磨损。`CSF_WeaponHeat` 使用 `0.0` 到 `1.0` 的热量比例。

# 宽高比与 HUD 研究说明（截至 v0.3 封版）

## v0.3 封版宽高比 / HUD 结论

HUD 本身不是 test14 返回标题错误的根因。test15 保留所有既有 HUD/输入稳定算法，只修显示生命周期，并已在 `BaseHeight=480` 实机通过：进入 `854x480`、退出回 `640x480`，HUD/输入链可用。v0.3 直接以该实现封版。

### 当前游戏内公式

```text
TargetHeight = BaseHeight
TargetWidth  = round(BaseHeight × AspectWidth / AspectHeight)
```

推荐 fixed-Y 基准：480 或 600。`BaseHeight=1080` 在 16:9 下是真正 1920x1080 内部世界，用户已实机观察到严重 FPS 下跌；它不是 4K 输出模式。

### 生命周期

- FRONTEND：不做主 HUD 宽屏平移，不做 gameplay Steam JMM；原版 mode 4 640x480。
- GAMEPLAY：Strategy state=3 后才允许 HUD 居中和 Steam delayed JMM。
- EXIT：test15 在原版 Strategy 清理完成后强制恢复 mode 4，再交给新前端 UI 自己创建/布局；该路径已实机通过。

### 稳定红线

- 底部主 HUD 只移动根节点；
- 小地图/右侧顶层 UI 继续贴边；
- 0x0B / 0x0E 顶部按钮使用 global release fallback；
- world press 只负责防点击穿透；
- 永久禁止再次包装 `0x004B44F0`。

---

## test11 基线正文（历史保留，当前结论以上方增补为准）

## fixed-Y / auto-X 的游戏内目标

游戏内世界分辨率：

```text
TargetHeight = BaseHeight
TargetWidth  = round(BaseHeight × AspectWidth / AspectHeight)
```

`BaseHeight` 任意正整数，可作为世界缩放/类 FOV 参数。常见 16:9：480→854x480、720→1280x720、1080→1920x1080。

极少数比 4:3 更窄的比例允许底部 HUD 少量裁切；不为特殊比例缩放/重排整个 GUI。

## AspectRatio=Auto 当前语义

当前 Auto 读取显示器比例，不是 cnc-ddraw 客户区比例。若 cnc-ddraw 自定义一个非标准超宽窗口，游戏内部仍按显示器比例计算 TargetWidth；未来可改成优先客户区、显示器 fallback，但用户目前决定暂缓。

## 主 HUD 居中

主 HUD vtable：`0x0052A7D4`，布局 `+0x58 -> 0x004B2B90`。

参考宽度：

- `BaseHeight < 600`：640 系模板；
- `BaseHeight >= 600`：800 系模板。

```text
MainHUDCenterDelta = (TargetWidth - NativeBaseWidth) / 2
```

只平移底部主 HUD 根节点；小地图、右侧按钮等其它顶层 UI 保持边缘锚定。v0.2-test1 已实机确认该视觉结果“非常完美”。

## 属性 / 道具顶部按钮

A/B 实机日志确认真实 control ID：`0x0B / 0x0E`，不是早期误判的 `0x0F / 0x10`。

- `0x0B` 对应目标窗口全局槽：`0x00559274`；
- `0x0E` 对应目标窗口全局槽：`0x0055BDC8`；
- active 查询：`0x004B1DB0`；
- 原版窗口显示切换：目标对象 `vtable+0x1C(!active,0)`。

当前输入方案：

- 释放：`0x00406086 -> 0x004B4560` 原版先行，未生效才 fallback；
- 按下穿透：只在世界输入 `0x004060EB -> 0x00473F10` 最后 callsite 命中 0x0B/0x0E 时跳过。

禁止重新包装 `0x004B44F0`：test7 已实机证明会让普通地图左键和 Alt+F4 失效，根因是该函数依赖调用者保留 ESI 作为隐藏上下文。

## 前端/动画与游戏内必须分离

原版前端固定 640x480 / 4:3；只有进入游戏后才接受游戏内分辨率。旧版在 ASI 初始化时就把分辨率分支改成高 BaseHeight，会造成：

- 主菜单只出现在大 surface 左上角；
- 其余区域黑屏；
- 未清理区域可能显示黄色旧 3D/动画 surface 残影。

v0.3-test11 改成 FRONTEND/GAMEPLAY 双 profile：前端恢复原版代码规则，主 HUD 出现后才启用动态世界分辨率；HUD 析构时恢复前端规则。详见《前端动画与主菜单研究说明.md》。

## Steam 游戏内 GUI

Steam/ComeOn.dll 缺少非 Steam 自然发生的一轮完整 JMM apply。test10 已实机通过：等 HUD、顶层 UI、资源根成熟后 one-shot 调 `0x004B35F0(0,W,H)`，日志 `result=1 / done=1 / child_layout_changed=1`，最终 GUI 位置正确。

v0.3 封版继续保持这条 Steam test10 路径不回归。

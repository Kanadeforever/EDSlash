# 平台与Steam兼容说明

> 文档维护约定：本文按主题维护当前有效的技术正文，不再追加版本更迭、日期同步或发布补丁章节。版本变化、旧版散列、阶段测试与用户反馈统一记入[版本与更新记录](版本与更新记录.md)；技术结论变化时直接修订对应正文。

## 1. 本体

Steam EXE 主体与非 Steam 版本高度接近，Steam 版额外通过启动桩加载 `ComeOn.dll`。DisplayFix 的 Steam 专项主要是 delayed full JMM apply；非 Steam 路径不执行该补偿。

Steam 性能/帧速历史问题不是非Steam手柄验收覆盖的变量，后续单独调查。

## 2. 外传多语言

外传 Steam 目录依赖语言化资源：

```text
ResJM.Lib.chs
ResJM.Lib.cht
ResJM.Lib.eng
ResJM.Lib.jpn
```

已确认正式方案：只对 basename 精确为 `ResJM.Lib` 的裸请求追加当前语言后缀；不覆盖整个 `CreateFileA` IAT。

关键地址：

- `CreateFileA` IAT：`0x005511E4`
- 主 EXE 低层 CreateFileA CALL：`0x0052824E`

## 3. 外传 Steam OpenGL 根因

最终实机闭环证明：ComeOn.dll 的全局 `CreateWindowExA` post-create callback 对 `lpClassName` 只检查 NULL，却没有识别合法 `MAKEINTATOM(atom)`；之后把低地址 atom 当 C 字符串解引用，导致 cnc-ddraw OpenGL 路径崩溃。

正式修复只为：

- `lpClassName == NULL`
- `0 < lpClassName < 0x10000` 的合法 class atom

跳过字符串类名解析；普通字符串类名仍走官方逻辑。

必须继续保留：

- SteamAPI
- 多语言
- 全局 CreateWindowExA Hook
- 官方 EDIT HWND 记录
- 官方 `SetWindowLongA` 自定义 WndProc

## 4. 禁止恢复的 Steam 影片路线

DirectShow/ActiveMovie/MovieManager 的历史逆向地址仍是知识，但此前实验没有证明 ComeOn.exe 进程内 ComeOn.dll 就是 Steam launcher 开场动画实际实例。统一项目不继续开发 launcher 影片补丁。

## 支持与测试矩阵

DisplayFix平台专项与Controller四份Steam／非Steam准确基线分别判断。扩大声明须分别测试本体Steam/非Steam、外传Steam/非Steam、外传Steam＋cnc-ddraw OpenGL、资源语言切换；旧显示验收不扩大为三模块全支持。

Controller准确Steam本体SHA-256为0887ceae7589999a389ec271d690e1c55204d59575f8f2adb5ef46a1e605b3f5，Steam外传为97ae4c2350618f38a74c3d02bf315c749fc448f705e860129372ebd592ed66e5。地址表中的game_id限定同一游戏再按完整散列选择，四样本签名/CALL/虚表均复核。原Runtime身份和显示专项保持，新Controller支持以四样本静态通过、两作宿主回放为证据，准确Steam外传默认360度、LT面键切组和双扳机动作菜单选择/设置已获用户确认；本体Steam、新动态框/丢弃/肩键收敛及全部场景未全面验收。

全项目核查从生产显示数组/导入独立确认两作依赖；外传未使用显示对象声明已核对并修正为578928，本体548398；不将声明修正当作现有运行故障修复，详见《全项目代码与四版本一致性核查.md》。当前参考资料没有官方ComeOn.dll样本，本轮四EXE检查不包含该DLL专项重新验证，旧class-atom/语言实机结论不扩大。

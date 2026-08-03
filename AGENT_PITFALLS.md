# X翻译项目 · Agent 避坑清单（每次派发必读）

> 本文件汇总本项目已反复踩过的坑与既定约定。**任何开发/验收 agent 开工前必须先读本文件**，
> 严禁重新试错这些已知问题。发现新坑请追加到末尾。
> **追加前先读完全文**：看现有章节号/条目号最大值再顺延，同一主题归到已有章节，勿新建重号章节。

## 一、环境与命令行（PowerShell v1 / Windows）

1. **PowerShell 复杂内联命令里 `$` 变量会被吞** → 多步逻辑一律写成 `.ps1` 脚本文件再执行，不要长串内联。
2. **禁用 `&&` 作语句分隔** → 用 `;`；每步用 `$LASTEXITCODE` 检查。
3. **含中文的 .ps1 脚本要加 UTF-8 BOM**，否则中文乱码；控制台显示中文乱码属代码页问题，判定以 UTF-8 落盘文件 / UIA 读值为准，不以终端显示为准。
4. **中文版 MSVC + Ninja 的依赖行会以 GBK 泄漏进构建日志** → 扫 warning/error 时按 `warning C\d+`/`error C\d+` 精确匹配，别被乱码行误判。

## 二、构建 / 打包 / 进程（每次 build/pack/selftest 前必做）

5. **X翻译是常驻托盘程序，会占用 exe 句柄** → 任何 build/deploy/pack/selftest 前，第一步先
   `Get-Process XTranslate -ErrorAction SilentlyContinue | Stop-Process -Force`，否则构建/删除失败、
   或 hotkey selftest 因 GUI 持有热键假失败（registered=0）。
6. **hotkey selftest 若 registered=0/6全失败**，先查是不是自家 GUI 驻留（第5条），不是才考虑第三方占用。
7. **本机 Alt+R 被第三方长期占用** → hotkey selftest 出现 `failed:["speak_clipboard (Alt+R)"]` 属环境已知，exit=0 非回归。
8. **OpenCV find_package 的 `CMAKE_HAVE_LIBC_PTHREAD - Failed`** 是内部探测，非编译警告，允许。
9. **OCR 模型不在 git 仓库** → 从源码构建前先跑 `fetch_models.ps1`（CMake 缺模型会 FATAL_ERROR 提示）。
10. **WIN32 子系统下 selftest 输出**：靠 AttachConsole(ATTACH_PARENT_PROCESS)，exit code 是底线契约，任何路径不得破坏。

## 三、GUI 自动化取证（真机验收）

11. **UIA 取证被置顶窗/Win11 toast 遮挡** → 被测窗先 raise()+置顶；托盘图标在"隐藏图标"溢出浮窗内，
    需用 DesktopWindowContentBridge 作用域找；toast 常驻会盖住托盘区。
12. **朗读/音频是否发声** → 用 IAudioSessionManager2 按 PID 计量会话级音频峰值（排除背景音），不要靠"听"。
13. **判断窗口点击穿透态** → 读 `WS_EX_TRANSPARENT` 样式位（GetWindowLongPtrW），**不要**用 SendMessage(WM_NCHITTEST)（绕过系统 hit-test 链，恒返回 HTCLIENT，测不出）。
14. **Win11 记事本是 Store 应用**，MainWindowHandle 常为 0；GUI 自动化找经典编辑控件用 WinForms 自建窗更稳。
15. GUI 自动化同一脚手架问题重试 2 次仍失败 → 标 MANUAL-PENDING + 人工步骤，不要无限调脚本。

## 四、代码 / API 契约（本项目硬约束）

16. **既有 objectName / accessibleName 严禁改名删除**（验收脚手架靠它们定位）；只能新增。
17. **OverlayWindow 逐像素分层窗口**：严禁调 `SetLayeredWindowAttributes`（与 Qt 的 UpdateLayeredWindow 互斥，渲染全挂）；改扩展样式用"读-改-写按位或"（GetWindowLongPtr → `|=` → SetWindowLongPtr），不整值覆盖。
18. **不确定的第三方 API 签名先查官方 Examples/文档，不凭记忆猜**（Inno SuppressibleMsgBox 曾连错 3 次）。
19. **Inno Setup usPostUninstall 阶段 SuppressibleMsgBox 不被 /VERYSILENT 抑制、WizardSilent 在 Uninstall 阶段禁用** → 危险操作（删用户数据）用"结构上不可能出错"的方案（永远保留 %APPDATA%）。
20. **Qt QSS 不支持 `!important`**；译文颜色等只作用单控件用 `QPlainTextEdit#objName{color:..}` 选择器，且按钮别挂在会继承 color 的父控件下。
21. **QProcess 拉起 python 子进程管道中文乱码** → 注入 `PYTHONUTF8=1` + `PYTHONIOENCODING=utf-8`。
22. **PrintWindow 对 Acrylic 窗口会渲染黑块** → 截图用"单窗矩形裁剪 + 程序自建纯色背景垫底"。

## 五、翻译 / TTS / 网络

23. **默认走系统代理**（proxy=system）；免费 Google/Bing 端点在国内需代理才通；失败 10s 内降级，落 mock 时 UI 有 ⚠ 提示。
24. **mock 结果不写历史库**（离线占位不该污染历史）；历史空时显示占位文案，别让用户以为坏了。
25. **Edge TTS 的 TrustedClientToken 是公开固定常量**（非用户密钥），硬编码/进公开仓库合规；但 `Sec-MS-GEC-Version`/UA 常量会随服务端策略失效 → 集中放常量，失效时靠"回退系统嗓音"保底。
26. **TTS 按目标语言(dstLang)选嗓音**，别用 guessLang；本机缺某语言语音包时系统引擎无声属环境限制，云端 Edge 引擎可解。

## 六、发布合规（push / Release 前必查）

27. **仓库/README/代码全程零 "Manggo/芒果" 字样**；push 前 grep 白名单文件确认 0 命中。
28. **push 前扫**：无 >2MB 文件、无 .onnx/.dll/.exe、无密钥、无 E:\Transform 绝对路径、无隐私截图（文档截图必须单窗裁剪不含桌面）。
29. **commit 作者匿名** `XTranslate Contributors <xtranslate@users.noreply.github.com>`（不暴露 QQ 号）。
30. **大文件（安装包）走 GitHub Release 附件**（`gh release create`，走 API 不受网页白名单干扰），不进 git；网页上传要拖到底部附件区不是 notes 描述框。
31. **红线**：`e:\Transform\Manggo`（已移除归档）与 `reports/evidence/` 既有文件不得改动；删除/强推等不可逆操作前列清单等用户确认。

## 七、GUI 自动化取证（v0.7.2 验收新增）

32. **Qt 设置窗（SettingsWindow）在 UIA 树里是主窗子窗非桌面顶层窗** → `RootElement 按 pid 找 Children` 找不到；从主窗 Descendants 按 `ClassName='SettingsWindow'` 找。
33. **QListWidget 导航（settingsNav）的 UIA `SelectionItemPattern.Select()` 不触发翻页** → 目标页控件不出现/AutomationId 显通用类名，易误判 objectName 失效；必须**坐标点击**导航项并等 1–2s 再断言。
34. **QComboBox 改选中项只有“鼠标点 combo 弹 popup → 坐标点列表项”可靠** → popup ListItem 的 `Select()` 不提交 currentIndexChanged；`SetFocus()`+方向键焦点不落会把按键打到别处。只读枚举可用 Expand+枚举+`{ESC}`，但 ESC 不可用在选中提交后。
35. **PowerShell v5 `Select-String` 无 `-Recurse` 参数** → 递归 grep 写 `Get-ChildItem -Recurse | Select-String`。
36. **玻璃窗截图扫色必须区分背景透出 vs UI 用色** → 定性三步：实拍图粗扫 → `--shot` 纯色垫底 UI-only 图复扫（权威）→ 仍命中则逐点定位，单像素控件边沿色（#DD9443/#E0AA49 一族）属 ClearType 伪影非回潮。

## 八、发版实操（v0.7.2 发布新增）

37. **#28 绝对路径扫描必须全仓 tracked 文件扫，不能只扫本次 staged** → v0.7.0 已推送的 tools/*.ps1|.py|.bat 里残留 14 处 `e:\Transform` 硬编码；修法统一用脚本位置推导（PS：`Split-Path -Parent $PSScriptRoot`；Py：`os.path.dirname(os.path.abspath(__file__))`；bat：`%~dp0..`）。
38. **AGENT_PITFALLS.md 本身含 Manggo 字样与绝对路径，永不入库** → `git add -A -- . ':(exclude)AGENT_PITFALLS.md'`。
39. **Release 大附件远端 SHA256 核对别靠重新下载**（本机直连 GitHub CDN 易断流，截断文件算出假哈希）→ 用 `gh api repos/<owner>/<repo>/releases/tags/<tag>` 读资产 `digest` 字段（服务端 sha256）+ 核 size。
40. **本项目 `gh.exe` 不在会话 PATH** → 全路径 `C:\Program Files\GitHub CLI\gh.exe` 调用；含中文的发布脚本同样需 UTF-8 BOM（#3）。
41. **匿名提交不改全局 git config** → 单次 `git -c user.name='XTranslate Contributors' -c user.email='xtranslate@users.noreply.github.com' commit …`。
42. **高频通用动作复用 `tools/` 现成脚本，别每次现写**：加 UTF-8 BOM(避坑 #3) 用 `tools/add_bom.ps1`；批量 selftest 用 `tools/run_selftest.ps1`/`phase5_run_selftests.ps1`；GUI 取证复用 `reports/evidence/*/acc_e2e_lib.ps1`。只有“本轮专属”业务脚本才新写，通用工具一律复用（find_gh 类查找已由 #40 的已知路径取代，不必再查）。

## 九、Inno 安装/卸载自动关进程（v0.7.3 新增）

43. **AppMutex / CloseApplications 都实现不了“卸载时自动关进程”**（官方文档实证）：AppMutex 只会弹“请先手动关闭…OK/Cancel”阻塞提示；CloseApplications 仅作用于安装阶段 Preparing to Install 页且依赖 Restart Manager。要“免手动、卸干净”只能 [Code] 段 `Exec(taskkill /IM XTranslate.exe /F)`（exit 0=已杀 128=不存在均正常，杀后 Sleep 等句柄释放）。
44. **卸载侧杀进程钩在 `CurUninstallStepChanged(usUninstall)`，不要钩 `InitializeUninstall`** —— 后者在卸载确认对话框之前就触发，交互卸载点“否”取消时后台程序已被误杀；usUninstall 在用户确认后、删文件前触发，静默/交互路径一致。
45. **本机 Inno 卸载键会记住上次安装目录**（曾装到过 `E:\XTranslate`）→ 安装/卸载验证脚本的 {app} 不能假定 `Program Files`，必须从 `HKLM:\...\Uninstall\{AppId}_is1` 的 `InstallLocation` 读；另外安装到工作区外需在沙箱外执行（沙箱内 exit=0 但文件没落盘，属假成功）。

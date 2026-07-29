; ===========================================================================
;  X翻译 (XTranslate) - Inno Setup 脚本 (Phase 9)
;  产出: installer\Output\XTranslate-Setup-0.7.0.exe
;
;  编译: ISCC.exe XTranslate.iss
;  依赖: dist\XTranslate\ 已由 tools\deploy.ps1 重建
; ===========================================================================

; --- 固定版本号（与 CMakeLists.txt VERSION 0.7.0 / VERSIONINFO.rc 同步）---
#define MyVersion "0.7.0"

[Setup]
; AppId 永不可变 —— 卸载/升级识别用，写死后任何后续版本必须沿用此 GUID。
; 改动会导致 Windows 视为新软件而非升级，旧版本残留无法清理。
AppId={{8F4E6B7A-3C5D-4E2A-9F01-7B8C6D5E4A3B}
AppName=X翻译
AppVersion={#MyVersion}
AppPublisher=学习性复刻项目（自定义）
AppPublisherURL=https://github.com/
AppContact=https://github.com/
DefaultDirName={autopf}\XTranslate
DefaultGroupName=X翻译
DisableProgramGroupPage=yes
AllowNoIcons=yes
LicenseFile=..\dist\XTranslate\LICENSE
OutputDir=Output
OutputBaseFilename=XTranslate-Setup-{#MyVersion}
SetupIconFile=app.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayIcon={app}\XTranslate.exe
UninstallDisplayName=X翻译
; 卸载时清理安装目录（用户数据 %APPDATA% 默认保留，见 [Code] 末页询问）
UninstallFilesDir={app}

[Languages]
; Phase 9：中文安装界面（默认）+ 英文备选。
; ChineseSimplified.isl 来源 kira-96/Inno-Setup-Chinese-Simplified-Translation（适配 6.5.0+），
; 放入 Inno Setup 安装目录的 Languages\ 子目录（非工程内，因其属编译器环境）。
; 中文在前 = 默认语言；英文在后 = 可选切换。
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
; Tasks 描述中英双语（zh.* / en.* 前缀对应 [Languages] 的 Name）。
; 默认语言 zh 的文案在无前缀 key 上也会被 Inno 自动用于 zh。
zh.desktopicon_desc=创建桌面快捷方式
en.desktopicon_desc=Create a &desktop shortcut
zh.startup_desc=开机自启（驻留托盘）
en.startup_desc=Launch on system startup (tray resident)
zh.extratasks_group=附加选项:
en.extratasks_group=Additional options:
zh.launch_desc=启动 X翻译
en.launch_desc=Launch X翻译

[Tasks]
Name: "desktopicon"; Description: "{cm:desktopicon_desc}"; GroupDescription: "{cm:extratasks_group}"; Flags: checkedonce
; 开机自启默认不勾（Flags: unchecked），勾选则写 HKCU Run（与应用内自启开关语义一致：命令行 --minimized 驻留托盘）
Name: "startup"; Description: "{cm:startup_desc}"; GroupDescription: "{cm:extratasks_group}"; Flags: unchecked

[Files]
; 递归打包整个 dist\XTranslate\ → {app}（exe + Qt 运行时 + onnxruntime.dll + models\ + themes/qm + 插件示例 + LICENSE/README）
Source: "..\dist\XTranslate\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
; 开始菜单快捷方式
Name: "{group}\X翻译"; Filename: "{app}\XTranslate.exe"; WorkingDir: "{app}"; Comment: "屏幕翻译工具"
; 桌面快捷方式（Tasks 控制是否创建，默认勾选）
Name: "{autodesktop}\X翻译"; Filename: "{app}\XTranslate.exe"; WorkingDir: "{app}"; Comment: "屏幕翻译工具"; Tasks: desktopicon

[Registry]
; 开机自启：仅当 startup task 被勾选时写 HKCU Run（不勾则不动，避免与应用内自启开关冲突）
; uninsdeletevalue: 卸载时自动删除该项
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "XTranslate"; ValueData: """{app}\XTranslate.exe"" --minimized"; Flags: uninsdeletevalue; Tasks: startup

[Run]
; 安装末页可选"启动 X翻译"
Filename: "{app}\XTranslate.exe"; Description: "{cm:launch_desc}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载时清理安装目录下所有文件（{app} 已由 Inno 自动删除，这里兜底清理可能残留的运行时生成文件）
Type: filesandordirs; Name: "{app}"

; ===========================================================================
; 个人数据保留策略（D4 契约修正：永远保留 %APPDATA%\XTranslate）
; ---------------------------------------------------------------------------
; 历史方案（已全部废弃，结构上杜绝误删）：
;   1. MsgBox + MB_DEFBUTTON2 —— /VERYSILENT 下不被抑制、弹窗阻塞（F2 根因）
;   2. SuppressibleMsgBox + IDNO —— 在 usPostUninstall 回调中实测仍弹窗未抑制
;   3. WizardSilent() 显式判别 —— Inno 禁止在 Uninstall 阶段调用，抛 Internal error
;
; 现方案：彻底移除 [Code] 段与任何 DelTree 调用，从结构上保证 %APPDATA%\XTranslate
; （config.json / history.db / plugins）在任何卸载模式下均保留。如需彻底清除，
; 用户手动删除 %APPDATA%\XTranslate 即可。
; ===========================================================================

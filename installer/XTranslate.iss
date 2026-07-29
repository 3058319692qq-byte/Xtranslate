; ===========================================================================
;  X翻译 (XTranslate) - Inno Setup 脚本 (Phase 9; v0.7.3 加卸载/安装自动关进程)
;  产出: installer\Output\XTranslate-Setup-0.7.3.exe
;
;  编译: ISCC.exe XTranslate.iss
;  依赖: dist\XTranslate\ 已由 tools\deploy.ps1 重建
; ===========================================================================

; --- 固定版本号（与 CMakeLists.txt VERSION 0.7.3 / VERSIONINFO.rc 同步）---
#define MyVersion "0.7.3"

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
; 现方案：[Code] 段不含任何针对 %APPDATA% 的 DelTree/MsgBox 逻辑，从结构上保证
; %APPDATA%\XTranslate（config.json / history.db / plugins）在任何卸载模式下均
; 保留。如需彻底清除，用户手动删除 %APPDATA%\XTranslate 即可。
; （v0.7.3 重新引入的 [Code] 段仅做“自动关闭运行中进程”，见下，不碰用户数据）
; ===========================================================================

; ===========================================================================
; v0.7.3：安装/卸载时自动关闭正在运行的 XTranslate（托盘常驻进程）
; ---------------------------------------------------------------------------
; 目标：用户不手动退后台、直接卸载也能删干净无残留（exe/DLL 被占用时
; Inno 只能标记重启删除，实际留下残留目录）。
; 方案取舍（已查官方文档，避坑 #18）：
;   × AppMutex —— 官方文档明确：只会弹“请先手动关闭…OK/Cancel”阻塞提示，
;     不会自动关进程，与“免手动”目标相反，故不采用。
;   × CloseApplications —— 官方文档仅作用于安装阶段 Preparing to Install 页，
;     对卸载无效；且依赖 Restart Manager，Qt 托盘程序未注册 RM 不可靠。
;   √ [Code] 段 taskkill /IM XTranslate.exe /F：
;       安装侧 PrepareToInstall（升级覆盖前）+ 卸载侧 CurUninstallStepChanged
;       的 usUninstall 步（用户确认后、删文件前；不用 InitializeUninstall，
;       否则交互卸载点“否”取消时进程已被误杀）。
;       两个事件均无任何弹窗，静默/交互卸载路径行为一致（避坑 #19：
;       不碰 WizardSilent、不用任何 MsgBox 变体）。
;       taskkill 退出码：0=已终止，128=进程不存在，均属正常，不阻断流程。
;       杀掉后 Sleep 轮询等待句柄释放，再让 Inno 删文件。
; 签名依据（官方 help，避坑 #18）：
;   function Exec(const Filename, Params, WorkingDir: String; const ShowCmd: Integer;
;                 const Wait: TExecWait; var ResultCode: Integer): Boolean;
;   procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
;   function PrepareToInstall(var NeedsRestart: Boolean): String;
; ===========================================================================
[Code]
// 结束正在运行的 XTranslate.exe；taskkill 不存在该进程时返回 128，同样视为成功。
// 终止后短轮询等待文件句柄释放，避免紧接着的文件覆盖/删除撞上残存锁。
procedure KillRunningApp;
var
  ResultCode: Integer;
begin
  if Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM XTranslate.exe /F', '',
          SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Log(Format('KillRunningApp: taskkill exit=%d (0=killed, 128=not running)', [ResultCode]));
    if ResultCode = 0 then
      Sleep(800); // 进程已被终止，给句柄/托盘图标释放留缓冲
  end
  else
    Log(Format('KillRunningApp: taskkill Exec failed, err=%d', [ResultCode]));
end;

// 安装侧：覆盖安装/升级前关掉旧进程，避免 [Files] 复制时 exe/DLL 被占用。
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  KillRunningApp;
  Result := ''; // 空字符串 = 继续安装，不阻断
end;

// 卸载侧：usUninstall = 用户已确认、即将删文件时才关驻留进程，保证 {app}
// 能被彻底删除；交互卸载点“否”取消时不误杀。静默卸载同样经过此步。
// 注意：此处不得调用 WizardSilent / 任何 MsgBox 变体（避坑 #19）。
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    KillRunningApp;
end;

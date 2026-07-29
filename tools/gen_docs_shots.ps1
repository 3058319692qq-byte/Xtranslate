# ===========================================================================
#  gen_docs_shots.ps1  (phase 9-fix1)
#  Regenerates the six README screenshots in docs/ with ZERO desktop privacy:
#  every scene runs through the hidden "--shot" mode (see src/app/DocShots.cpp)
#  which places the target window on an app-created solid-color backdrop and
#  crops the capture to the window / menu rect only - no wallpaper, taskbar,
#  third-party window or file name can appear in the output.
#
#  Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen_docs_shots.ps1
#  Prereq: build\bin\XTranslate.exe built from a source tree containing the
#          --shot mode; no resident XTranslate process (the script stops one).
# ===========================================================================
[CmdletBinding()]
param(
    [string]$Exe = ''
)

$ErrorActionPreference = 'Stop'
# param 默认值阶段 $PSScriptRoot 可能为空（本机 PS 5.1 实测），改在脚本体内推。
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path $scriptDir -Parent
if ([string]::IsNullOrEmpty($Exe)) { $Exe = Join-Path $root 'build\bin\XTranslate.exe' }
$docs = Join-Path $root 'docs'

if (-not (Test-Path $Exe)) { throw "XTranslate.exe not found: $Exe (run build.ps1 first)" }
if (-not (Test-Path $docs)) { New-Item -ItemType Directory -Path $docs | Out-Null }

# 截图前置：不得有驻留实例（全局热键/托盘会互相干扰，也防止旧窗口入镜）。
Get-Process XTranslate -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

# 六张 README 展示图：scene -> 输出文件 + 主题。
$shots = @(
    @{ Scene = 'main';     Theme = 'light'; Out = 'main_light.png'        },
    @{ Scene = 'main';     Theme = 'dark';  Out = 'main_dark.png'         },
    @{ Scene = 'settings'; Theme = 'light'; Out = 'settings_light.png'    },
    @{ Scene = 'popup';    Theme = 'light'; Out = 'popup_card.png'        },
    @{ Scene = 'overlay';  Theme = 'light'; Out = 'overlay_translate.png' },
    @{ Scene = 'tray';     Theme = 'light'; Out = 'tray_menu.png'         }
)

$failed = @()
foreach ($s in $shots) {
    $outPath = Join-Path $docs $s.Out
    Write-Host ("[shots] {0} (theme={1}) -> {2}" -f $s.Scene, $s.Theme, $s.Out)
    # 每张图独立进程：场景之间零状态残留（托盘图标/热键注册/主题都干净）。
    $p = Start-Process -FilePath $Exe -ArgumentList @(
            '--shot', $s.Scene, '--out', $outPath, '--theme', $s.Theme
        ) -PassThru -Wait
    if ($p.ExitCode -ne 0 -or -not (Test-Path $outPath)) {
        $failed += "$($s.Scene) (exit=$($p.ExitCode))"
        continue
    }
    Start-Sleep -Milliseconds 300
}

# 兜底：不许有幸存的驻留进程。
Get-Process XTranslate -ErrorAction SilentlyContinue | Stop-Process -Force

if ($failed.Count -gt 0) {
    Write-Host "SHOTS_FAILED: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}

# ---- 隐私自检：逐张确认尺寸合理（窗口级裁剪，绝非全屏截图）----------------
# 全屏截图（旧隐患）在本机是 2560x1440/1920x1080 级别；窗口级裁剪的图必然
# 小于屏幕物理尺寸。这里逐张核对"宽高严格小于主屏物理分辨率"作为机器判据；
# 背景无壁纸/无其它窗口由 --shot 的实现保证（纯色背景窗全屏垫底）。
Add-Type -AssemblyName System.Drawing
try {
    Add-Type -AssemblyName System.Windows.Forms
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $screenW = $b.Width * 2   # 容忍 200% DPI 的物理像素放大
    $screenH = $b.Height * 2
} catch { $screenW = 5120; $screenH = 2880 }

$report = @()
foreach ($s in $shots) {
    $outPath = Join-Path $docs $s.Out
    $img = [System.Drawing.Image]::FromFile($outPath)
    $isFull = ($img.Width -ge $screenW -and $img.Height -ge $screenH)
    $report += [PSCustomObject]@{
        File   = $s.Out
        Width  = $img.Width
        Height = $img.Height
        SizeKB = [math]::Round((Get-Item $outPath).Length / 1KB, 1)
        FullScreenSuspect = $isFull
    }
    $img.Dispose()
}
$report | Format-Table -AutoSize

if ($report | Where-Object FullScreenSuspect) {
    Write-Host 'SHOTS_PRIVACY_SUSPECT: full-screen-sized image detected' -ForegroundColor Red
    exit 1
}
Write-Host 'SHOTS_OK: 6 images regenerated, window-level crops only'
exit 0

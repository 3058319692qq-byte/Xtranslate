#requires -version 5
# ===========================================================================
#  XTranslate Phase 9 - 一条龙打包脚本
#  流程: 清场 → 生成图标 → build -Clean → deploy → 校验 dist → ISCC 编译
#  产出: installer\Output\XTranslate-Setup-0.7.2.exe
#  任一步失败即 throw
# ===========================================================================
[CmdletBinding()]
param(
    [switch]$SkipBuildAndDeploy  # 调试用：跳过 build/deploy，直接走 ISCC（要求 dist 已是最新）
)

$ErrorActionPreference = 'Stop'

$root       = Split-Path -Parent $PSScriptRoot   # 仓库根目录（本脚本位于 tools\ 下）
$installer  = Join-Path $root 'installer'
$issPath    = Join-Path $installer 'XTranslate.iss'
$genIcon    = Join-Path $installer 'gen_app_icon.ps1'
$distApp    = Join-Path $root 'dist\XTranslate'
$distExe    = Join-Path $distApp 'XTranslate.exe'
$outputDir  = Join-Path $installer 'Output'
$outputExe  = Join-Path $outputDir 'XTranslate-Setup-0.7.2.exe'

# ISCC 路径自动探测：winget 可能装到 machine-scope (Program Files) 或 user-scope (LOCALAPPDATA)
$isccCandidates = @(
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    throw "ISCC.exe not found in any of: $($isccCandidates -join ', ')"
}
Write-Host "[pack] ISCC: $iscc"

Write-Host '============================================================'
Write-Host '  XTranslate - pack installer (v0.7.2)'
Write-Host '============================================================'

# ---------------------------------------------------------------------------
# 1. 清场: 确认无 XTranslate 进程驻留（避免 build 覆盖占用 exe 失败）
# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '[1/7] checking XTranslate process ...'
$proc = Get-Process XTranslate -ErrorAction SilentlyContinue
if ($proc) {
    $proc | Format-Table Id, ProcessName, StartTime -AutoSize
    throw "XTranslate process still running (PID $($proc.Id -join ',')). Please close it before packing."
}
Write-Host '[1/7] OK: no XTranslate process running'

# ---------------------------------------------------------------------------
# 2. 生成 app.ico（installer\app.ico + resources\app.ico）
# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '[2/7] generating app.ico ...'
& powershell -NoProfile -ExecutionPolicy Bypass -File $genIcon
if ($LASTEXITCODE -ne 0) { throw "gen_app_icon.ps1 failed (exit $LASTEXITCODE)" }

# .rc 引用同目录 app.ico，复制一份到 resources\ 供 build 编译进 exe
$iconSrc = Join-Path $installer 'app.ico'
$iconDst = Join-Path $root 'resources\app.ico'
Copy-Item $iconSrc $iconDst -Force
Write-Host "[2/7] OK: app.ico generated + copied to resources\"

# ---------------------------------------------------------------------------
# 3. build -Clean（含 .rc 编译嵌入图标）
# ---------------------------------------------------------------------------
if ($SkipBuildAndDeploy) {
    Write-Host ''
    Write-Host '[3/7] SKIP build (SkipBuildAndDeploy=on)'
    Write-Host '[4/7] SKIP deploy (SkipBuildAndDeploy=on)'
} else {
    Write-Host ''
    Write-Host '[3/7] building (Clean) ...'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1') -Clean
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed (exit $LASTEXITCODE)" }

    # ---------------------------------------------------------------------------
    # 4. deploy（重建 dist + 15 selftest）
    # ---------------------------------------------------------------------------
    Write-Host ''
    Write-Host '[4/7] deploying dist ...'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\deploy.ps1')
    if ($LASTEXITCODE -ne 0) { throw "deploy.ps1 failed (exit $LASTEXITCODE)" }
}

# ---------------------------------------------------------------------------
# 5. 校验 dist 完整性
# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '[5/7] verifying dist integrity ...'
if (-not (Test-Path $distExe)) { throw "dist exe missing: $distExe" }
if (-not (Test-Path (Join-Path $distApp 'onnxruntime.dll'))) { throw "dist missing onnxruntime.dll" }
if (-not (Test-Path (Join-Path $distApp 'LICENSE'))) { throw "dist missing LICENSE" }
if (-not (Test-Path (Join-Path $distApp 'THIRD-PARTY-NOTICES.txt'))) { throw "dist missing THIRD-PARTY-NOTICES.txt" }
if (-not (Test-Path (Join-Path $distApp 'models\paddleocr\pp-ocrv6-small\PP-OCRv6_small_det.onnx'))) {
    throw "dist missing OCR models"
}
Write-Host '[5/7] OK: dist integrity verified'

# ---------------------------------------------------------------------------
# 6. ISCC 编译 .iss
# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '[6/7] compiling installer with ISCC ...'
if (-not (Test-Path $iscc)) { throw "ISCC.exe not found: $iscc (Inno Setup installed?)" }

# 清理旧产物
if (Test-Path $outputDir) {
    Remove-Item $outputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

Push-Location $installer
try {
    & $iscc $issPath
    $isccCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($isccCode -ne 0) { throw "ISCC failed (exit $isccCode)" }

# ---------------------------------------------------------------------------
# 7. 产物报告
# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '[7/7] pack result:'
if (-not (Test-Path $outputExe)) { throw "installer output missing: $outputExe" }
$info = Get-Item $outputExe
$sizeMB = [math]::Round($info.Length / 1MB, 2)
Write-Host "  path : $($info.FullName)"
Write-Host "  size : $($info.Length) bytes ($sizeMB MB)"
Write-Host "  built: $($info.LastWriteTime)"

Write-Host ''
Write-Host '============================================================'
Write-Host "  PACK_OK -> $outputExe"
Write-Host '============================================================'
exit 0

#requires -version 5
# ===========================================================================
#  XTranslate build script (phase 0)
#  - initializes MSVC x64 env via vcvars64.bat (through cmd /c)
#  - configures + builds with the VS-bundled CMake + Ninja (Release)
#  - deploys Qt runtime with windeployqt and copies onnxruntime.dll
#  Output: <repo>\build\bin\XTranslate.exe
# ===========================================================================
[CmdletBinding()]
param(
    [string]$Config = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

# ---- fixed tool locations (VS2022 Community bundled CMake/Ninja) ----
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$cmake  = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja  = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

# ---- project layout ----
$root          = $PSScriptRoot
$build         = Join-Path $root 'build'
$binOut        = Join-Path $build 'bin'
$opencvInstall = Join-Path $root 'third_party\opencv-install'
$onnxDll       = Join-Path $root 'third_party\onnxruntime\lib\onnxruntime.dll'

foreach ($p in @($vcvars, $cmake, $ninja)) {
    if (-not (Test-Path $p)) { throw "required tool not found: $p" }
}

# ---- locate the Qt msvc2022_64 root (prefer the highest installed version) ----
$qtBase = Join-Path $root 'third_party\Qt'
$qtRoot = $null
if (Test-Path $qtBase) {
    $cand = Get-ChildItem $qtBase -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } |
        Where-Object { Test-Path (Join-Path $_ 'bin\qmake.exe') }
    if ($cand) { $qtRoot = @($cand)[0] }
}
if (-not $qtRoot) { throw "no Qt msvc2022_64 install found under $qtBase" }
Write-Host "[build] Qt root      : $qtRoot"
Write-Host "[build] OpenCV install: $opencvInstall"

# ---- import the MSVC x64 environment produced by vcvars64.bat ----
function Import-VcVars {
    param([string]$BatPath)
    $tmp = [System.IO.Path]::GetTempFileName()
    cmd.exe /c "call `"$BatPath`" > nul 2>&1 && set > `"$tmp`""
    if ($LASTEXITCODE -ne 0) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue; throw "vcvars64.bat failed (exit $LASTEXITCODE)" }
    foreach ($line in Get-Content $tmp) {
        if ($line -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
    Remove-Item $tmp -Force
}

Write-Host "[build] initializing MSVC x64 environment ..."
Import-VcVars -BatPath $vcvars

# Pin the console codepage to the system ACP (936 on zh-CN). With a UTF-8
# (65001) console the localized cl.exe /showIncludes prefix gets mis-decoded
# by CMake at configure time, so Ninja cannot filter dependency lines and
# thousands of "注意: 包含文件:" lines leak into the build log.
$acp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP
cmd.exe /c "chcp $acp > nul" 2>$null

# ---- optional clean ----
if ($Clean -and (Test-Path $build)) {
    Write-Host "[build] cleaning $build"
    Remove-Item $build -Recurse -Force
}

# ---- i18n: refresh .ts and compile .qm (phase 4; qm files are packed
#      into the QRC so they must exist before cmake configure) ----
$lupdate  = Join-Path $qtRoot 'bin\lupdate.exe'
$lrelease = Join-Path $qtRoot 'bin\lrelease.exe'
$i18nDir  = Join-Path $root 'resources\i18n'
if (-not (Test-Path $i18nDir)) { New-Item -ItemType Directory -Path $i18nDir | Out-Null }
$tsFiles = @(
    (Join-Path $i18nDir 'xtranslate_zh_CN.ts'),
    (Join-Path $i18nDir 'xtranslate_en.ts')
)
if (Test-Path $lupdate) {
    Write-Host "[build] lupdate: refreshing .ts files ..."
    & $lupdate -recursive (Join-Path $root 'src') -locations none -ts @tsFiles
    if ($LASTEXITCODE -ne 0) { throw "lupdate failed (exit $LASTEXITCODE)" }
}
if (-not (Test-Path $lrelease)) { throw "lrelease not found: $lrelease" }
foreach ($ts in $tsFiles) {
    if (-not (Test-Path $ts)) { throw "missing translation source: $ts" }
    $qm = [System.IO.Path]::ChangeExtension($ts, '.qm')
    Write-Host "[build] lrelease: $([System.IO.Path]::GetFileName($ts)) -> $([System.IO.Path]::GetFileName($qm))"
    & $lrelease $ts -qm $qm
    if ($LASTEXITCODE -ne 0) { throw "lrelease failed for $ts (exit $LASTEXITCODE)" }
}

# ---- configure ----
$configureArgs = @(
    '-S', $root,
    '-B', $build,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    '-DCMAKE_CXX_COMPILER=cl',
    "-DCMAKE_PREFIX_PATH=$qtRoot;$opencvInstall"
)
Write-Host "[build] configuring ..."
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }

# ---- build ----
Write-Host "[build] building ($Config) ..."
& $cmake --build $build
if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }

$exe = Join-Path $binOut 'XTranslate.exe'
if (-not (Test-Path $exe)) { throw "expected artifact missing: $exe" }

# ---- deploy Qt runtime ----
$windeployqt = Join-Path $qtRoot 'bin\windeployqt.exe'
Write-Host "[build] deploying Qt runtime via windeployqt ..."
& $windeployqt --release --dir $binOut $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed (exit $LASTEXITCODE)" }

# ---- copy ONNX Runtime dll next to the exe ----
Write-Host "[build] copying onnxruntime.dll ..."
Copy-Item $onnxDll $binOut -Force

Write-Host ""
Write-Host "BUILD_DEPLOY_OK -> $exe"
exit 0

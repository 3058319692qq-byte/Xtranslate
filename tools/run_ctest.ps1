#requires -version 5
# ===========================================================================
#  XTranslate ctest runner.
#  - same fixed toolchain as build.ps1 (VS2022-bundled CMake/Ninja/ctest)
#  - stops a resident XTranslate.exe first (tray app holds the exe handle)
#  - configures incrementally, builds only the test targets, runs ctest
#  Exit code: ctest's exit code (0 = all tests pass).
# ===========================================================================
[CmdletBinding()]
param(
    [string]$Config = 'Release',
    # Optional ctest -R regex; empty = run every registered test.
    [string]$TestRegex = ''
)
$ErrorActionPreference = 'Stop'

$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$cmakeBin = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmake = Join-Path $cmakeBin 'cmake.exe'
$ctest = Join-Path $cmakeBin 'ctest.exe'
$ninja = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$opencvInstall = Join-Path $root 'third_party\opencv-install'

foreach ($p in @($vcvars, $cmake, $ctest, $ninja)) {
    if (-not (Test-Path $p)) { throw "required tool not found: $p" }
}

# Resident tray app holds build\bin\XTranslate.exe (AGENT_PITFALLS #5).
Get-Process XTranslate -ErrorAction SilentlyContinue | Stop-Process -Force

# Locate Qt msvc2022_64 (same rule as build.ps1: highest installed version).
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

# Import MSVC x64 env (vcvars64.bat) into this process.
$tmp = [System.IO.Path]::GetTempFileName()
cmd.exe /c "call `"$vcvars`" > nul 2>&1 && set > `"$tmp`""
if ($LASTEXITCODE -ne 0) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue; throw "vcvars64.bat failed (exit $LASTEXITCODE)" }
foreach ($line in Get-Content $tmp) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
Remove-Item $tmp -Force

# Pin console codepage to the system ACP (see build.ps1 for the rationale).
$acp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP
cmd.exe /c "chcp $acp > nul" 2>$null

# Configure (incremental; same arguments as build.ps1).
& $cmake -S $root -B $build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    '-DCMAKE_CXX_COMPILER=cl' `
    "-DCMAKE_PREFIX_PATH=$qtRoot;$opencvInstall"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }

# Build only the test targets (the XTranslate GUI target is not relinked).
& $cmake --build $build --target ConfigAtomicTest
if ($LASTEXITCODE -ne 0) { throw "test build failed (exit $LASTEXITCODE)" }

# Run ctest.
$ctestArgs = @('--test-dir', $build, '--output-on-failure')
if ($TestRegex) { $ctestArgs += @('-R', $TestRegex) }
& $ctest @ctestArgs
$code = $LASTEXITCODE
Write-Host ("CTEST_EXIT=" + $code)
exit $code

#requires -version 5
# ===========================================================================
#  XTranslate deploy script (phase 5)
#  - rebuild (0 warning)
#  - clean dist/ -> copy build/bin/* -> copy models -> copy examples/plugin-echo
#    -> copy LICENSE.txt / README.txt
#  - run 15 selftest inside dist, all exit 0 required
#  - print dist tree
#  Output: <repo>\dist\XTranslate\
# ===========================================================================
[CmdletBinding()]
param(
    [switch]$SkipBuild   # skip build, reuse build\bin\ (debug only)
)

$ErrorActionPreference = 'Stop'

$root       = Split-Path -Parent $PSScriptRoot   # 仓库根目录（本脚本位于 tools\ 下）
$buildBin   = Join-Path $root 'build\bin'
$distRoot   = Join-Path $root 'dist'
$distApp    = Join-Path $distRoot 'XTranslate'
$echoSrc    = Join-Path $root 'examples\plugin-echo'

# ---------------------------------------------------------------------------
# 1. build (default)
# ---------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "[deploy] building (Release) ..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
}

$exe = Join-Path $buildBin 'XTranslate.exe'
if (-not (Test-Path $exe)) { throw "exe not found: $exe" }

# ---------------------------------------------------------------------------
# 2. clean dist -> copy artifacts
# ---------------------------------------------------------------------------
if (Test-Path $distRoot) {
    Write-Host "[deploy] cleaning $distRoot"
    Remove-Item $distRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $distApp -Force | Out-Null

Write-Host "[deploy] copying build\bin\* -> $distApp"
# build\bin contains XTranslate.exe, windeployqt-deployed Qt dlls,
# sqldrivers / imageformats / platforms subdirs, onnxruntime.dll, etc.
# Copy everything to keep runtime integrity.
Copy-Item -Path (Join-Path $buildBin '*') -Destination $distApp -Recurse -Force

# ---------------------------------------------------------------------------
# 3. verify OCR models (already copied by build.ps1 post-build + step 2)
# ---------------------------------------------------------------------------
$distModels = Join-Path $distApp 'models\paddleocr\pp-ocrv6-small'
if (-not (Test-Path (Join-Path $distModels 'PP-OCRv6_small_det.onnx'))) {
    Write-Warning "[deploy] models not found under $distModels"
} else {
    Write-Host "[deploy] models OK: $distModels"
}

# ---------------------------------------------------------------------------
# 4. copy plugin example
# ---------------------------------------------------------------------------
if (Test-Path $echoSrc) {
    $echoDst = Join-Path $distApp 'examples\plugin-echo'
    New-Item -ItemType Directory -Path $echoDst -Force | Out-Null
    Write-Host "[deploy] copying examples\plugin-echo -> $echoDst"
    Copy-Item -Path (Join-Path $echoSrc '*') -Destination $echoDst -Recurse -Force
}

# ---------------------------------------------------------------------------
# 5. copy LICENSE / THIRD-PARTY-NOTICES.txt / README.txt
#    Phase 9: LICENSE.txt 已改名为 THIRD-PARTY-NOTICES.txt，根 LICENSE 是 MIT
# ---------------------------------------------------------------------------
foreach ($f in @('LICENSE', 'THIRD-PARTY-NOTICES.txt', 'README.txt')) {
    $src = Join-Path $root $f
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $distApp -Force
        Write-Host "[deploy] copied $f"
    } else {
        Write-Warning "[deploy] $f not found at $src"
    }
}

# ---------------------------------------------------------------------------
# 6. 15 selftest inside dist (must all be 0)
# ---------------------------------------------------------------------------
$distExe = Join-Path $distApp 'XTranslate.exe'
if (-not (Test-Path $distExe)) { throw "dist exe not found: $distExe" }

Write-Host ""
Write-Host "[deploy] running 15 selftest in dist ..."
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\phase5_run_selftests.ps1') -Exe $distExe
$stCode = $LASTEXITCODE
if ($stCode -ne 0) {
    throw "dist selftest failed (exit $stCode)"
}

# ---------------------------------------------------------------------------
# 7. print dist tree
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "[deploy] dist tree:"
Get-ChildItem -Path $distApp -Recurse |
    ForEach-Object {
        $rel = $_.FullName.Substring($distApp.Length + 1)
        if ($_.PSIsContainer) {
            Write-Host "  [DIR]  $rel"
        } else {
            Write-Host ("  {0,10:N0}  {1}" -f $_.Length, $rel)
        }
    }

Write-Host ""
Write-Host "DEPLOY_OK -> $distApp"
exit 0

#requires -version 5
# ===========================================================================
#  Phase 5 - batch selftest runner.
#  Runs every --selftest mode, captures stdout (one JSON line expected),
#  prints a summary table; any non-zero exit fails the script.
#  Used for the WIN32/AttachConsole baseline check and per-step regression.
# ===========================================================================
[CmdletBinding()]
param(
    [string]$Exe = 'e:\Transform\XTranslate\build\bin\XTranslate.exe',
    # 默认列表随 Phase 5 推进扩充；第 6 步起 15 项全跑。
    [string[]]$Modes = @(
        'env','ocr','translate','capture','overlay',
        'hotkey','tts','selection','config','db','providers','theme',
        'replace','systemocr','plugin'
    )
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { throw "exe not found: $Exe" }

$results = @()
foreach ($m in $Modes) {
    # 一步捕获：& exe 2>&1 | Out-String 避免 PowerShell 数组+合并流导致的空输出问题
    $line = (& $exe --selftest $m 2>&1 | Out-String).Trim()
    $code = $LASTEXITCODE
    $results += [pscustomobject]@{
        Mode   = $m
        Exit   = $code
        Output = $line
    }
    Write-Host ("[{0,-12}] exit={1}  out={2}" -f $m, $code, $line)
}

Write-Host ""
Write-Host "=== Phase 5 selftest summary ==="
$results | Format-Table Mode, Exit -AutoSize | Out-String | Write-Host

$failed = $results | Where-Object { $_.Exit -ne 0 }
if ($failed) {
    Write-Host "FAILED: $($failed.Mode -join ', ')"
    exit 1
}
Write-Host "ALL_PASS ($($results.Count) modes)"
exit 0

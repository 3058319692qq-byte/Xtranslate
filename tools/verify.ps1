# Phase 0 acceptance checks for XTranslate.
#  1) --selftest env : valid JSON, exit 0, models_found = true
#  2) GUI launch     : top-level window titled "X翻译"
# "X翻译" is built from code points so this script stays pure ASCII on disk.
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$exe = 'e:\Transform\XTranslate\build\bin\XTranslate.exe'
$expected = [string]::new([char[]]@([char]0x58, [char]0x7FFB, [char]0x8BD1))  # X + 翻 + 译

if (-not (Test-Path $exe)) { Write-Host "MISSING_EXE $exe"; exit 3 }

# ---- 1) environment self-test ----
$out  = & $exe --selftest env 2>&1 | Out-String
$code = $LASTEXITCODE
Write-Host "=== selftest env stdout ==="
Write-Host $out.Trim()
Write-Host "=== selftest exit code: $code ==="

$obj = $null
try { $obj = $out | ConvertFrom-Json } catch { Write-Host "JSON_PARSE_ERROR: $_" }

$selftestPass = $false
if ($null -ne $obj) {
    Write-Host ("parsed: app={0} display_ok={1} qt={2} onnxruntime={3} opencv={4} models_found={5}" -f `
        $obj.app, ($obj.display_name -eq $expected), $obj.qt, $obj.onnxruntime, $obj.opencv, $obj.models_found)
    if (($code -eq 0) -and ($obj.models_found -eq $true) -and `
        ($obj.app -eq 'XTranslate') -and ($obj.display_name -eq $expected)) {
        $selftestPass = $true
    }
}
Write-Host ("SELFTEST_" + $(if ($selftestPass) { 'PASS' } else { 'FAIL' }))
Write-Host ""

# ---- 2) window launch ----
$p = Start-Process $exe -PassThru
$title = ''
for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Milliseconds 400
    $p.Refresh()
    if ($p.HasExited) { break }
    if ($p.MainWindowTitle) { $title = $p.MainWindowTitle; break }
}
Write-Host "WINDOW_TITLE=$title"
$windowPass = ($title -eq $expected)
Write-Host ("WINDOW_" + $(if ($windowPass) { 'PASS' } else { 'FAIL' }))
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }

Write-Host ""
if ($selftestPass -and $windowPass) { Write-Host "ALL_PASS"; exit 0 }
Write-Host "SOME_FAIL"; exit 1

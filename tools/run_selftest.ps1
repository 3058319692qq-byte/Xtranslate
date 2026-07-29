# Runs one --selftest mode and prints the exit code.
param(
    [Parameter(Mandatory=$true)][string]$Mode,
    [string[]]$Extra = @()
)
# from script location: tools/ -> root -> build/bin/XTranslate.exe
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptDir
$exe = Join-Path $root 'build\bin\XTranslate.exe'
& $exe --selftest $Mode @Extra
Write-Host ("SELFTEST_EXIT=" + $LASTEXITCODE)
exit $LASTEXITCODE

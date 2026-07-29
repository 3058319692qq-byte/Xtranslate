# Qt SDK bootstrap installer for XTranslate.
# Tries 6.11.1 on mirrors first (official download.qt.io is unreachable here),
# then falls back to 6.8.3. Records the winning version/base and Qt root.
$ErrorActionPreference = 'Continue'

$py      = 'E:\miniconda3\python.exe'
$out     = 'e:\Transform\XTranslate\third_party\Qt'
$log     = 'e:\Transform\XTranslate\reports\qt_install.log'
$result  = 'e:\Transform\XTranslate\reports\qt_install_result.txt'
$modules = @('qtmultimedia', 'qtspeech', 'qtimageformats')

$attempts = @(
    @{ ver = '6.11.1'; base = 'https://mirror.nju.edu.cn/qt' },
    @{ ver = '6.11.1'; base = 'https://mirrors.ustc.edu.cn/qtproject' },
    @{ ver = '6.8.3';  base = 'https://mirrors.tuna.tsinghua.edu.cn/qt' },
    @{ ver = '6.8.3';  base = 'https://mirror.nju.edu.cn/qt' },
    @{ ver = '6.8.3';  base = 'https://mirrors.ustc.edu.cn/qtproject' }
)

"=== Qt install run $(Get-Date -Format o) ===" | Out-File -FilePath $log -Encoding utf8

$success = $false
$chosenVer = $null
$chosenBase = $null
$qtRoot = $null

foreach ($a in $attempts) {
    $header = "`n--- Attempt ver=$($a.ver) base=$($a.base) $(Get-Date -Format HH:mm:ss) ---"
    Write-Host $header
    $header | Out-File -FilePath $log -Append -Encoding utf8

    $output = & $py -m aqt install-qt windows desktop $($a.ver) win64_msvc2022_64 -m $modules -O $out -b $($a.base) 2>&1
    $code = $LASTEXITCODE
    $output | Out-File -FilePath $log -Append -Encoding utf8
    "exit code = $code" | Out-File -FilePath $log -Append -Encoding utf8

    $candidate = Join-Path $out (Join-Path $($a.ver) 'msvc2022_64')
    $qmake = Join-Path $candidate 'bin\qmake.exe'
    if (($code -eq 0) -and (Test-Path $qmake)) {
        $success = $true
        $chosenVer = $($a.ver)
        $chosenBase = $($a.base)
        $qtRoot = $candidate
        break
    } else {
        "WARN: attempt failed (code=$code, qmake exists=$(Test-Path $qmake))" | Out-File -FilePath $log -Append -Encoding utf8
    }
}

if ($success) {
    $line = "QT_INSTALL_SUCCESS ver=$chosenVer base=$chosenBase root=$qtRoot"
    Write-Host $line
    $line | Out-File -FilePath $result -Encoding utf8
    exit 0
} else {
    $line = 'QT_INSTALL_FAILED_ALL'
    Write-Host $line
    $line | Out-File -FilePath $result -Encoding utf8
    exit 1
}

#Requires -Version 5
# ===========================================================================
#  fetch_models.ps1 - 下载 PP-OCRv6 small 官方 ONNX 模型
#
#  用途: 克隆仓库后，build 前必跑此脚本下载 OCR 模型（models/ 已被
#        .gitignore 排除，不会随仓库分发）。
#
#  来源: modelscope RapidAI/RapidOCR 仓库 (Apache-2.0)
#        https://modelscope.cn/models/RapidAI/RapidOCR
#
#  目标: <repo>/models/paddleocr/pp-ocrv6-small/
#    PP-OCRv6_small_det.onnx   检测模型
#    PP-OCRv6_small_rec.onnx   识别模型
#    ppocrv6_dict.txt          字典文件
#
#  校验: 下载后做 SHA256 校验，与官方发布版本一致才视为成功。
# ===========================================================================
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# 从脚本位置推算工程根目录（tools/fetch_models.ps1 -> 上一级）
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptDir
$dstDir = Join-Path $root 'models\paddleocr\pp-ocrv6-small'

# modelscope RapidAI/RapidOCR 仓库 - PP-OCRv6 small 官方 ONNX
$base = 'https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/master/'

# 文件映射: 仓库路径 -> 本地文件名 (PaddleOcrEngine.cpp 硬编码名)
$files = @(
  @{ src = 'onnx/PP-OCRv6/det/PP-OCRv6_det_small.onnx';           dst = 'PP-OCRv6_small_det.onnx'; sha = '090F04ABCD9D9A7498BC4EBF677E4CB9BDCE1FE4197DDB7E529F1EF44E1FF94F' },
  @{ src = 'onnx/PP-OCRv6/rec/PP-OCRv6_rec_small.onnx';           dst = 'PP-OCRv6_small_rec.onnx'; sha = '6F327246B50388F3C176AE304BD95767EA6DC0C9AE92153EF8CBE210B3C14884' },
  @{ src = 'paddle/PP-OCRv6/rec/PP-OCRv6_rec_small/ppocrv6_dict.txt'; dst = 'ppocrv6_dict.txt'; sha = 'B5F2BFE2BDD9448429E3E82B51C789775D9B42F2403D082B00662EB77E401C5D' }
)

Write-Host '============================================================'
Write-Host '  XTranslate - fetch PP-OCRv6 small ONNX models'
Write-Host '  Source : modelscope RapidAI/RapidOCR (Apache-2.0)'
Write-Host "  Target : $dstDir"
Write-Host '============================================================'
Write-Host ''

New-Item -ItemType Directory -Path $dstDir -Force | Out-Null

$allOk = $true
foreach ($f in $files) {
  $url  = $base + $f.src
  $out  = Join-Path $dstDir $f.dst
  Write-Host "[$($f.dst)]"
  Write-Host "  downloading from modelscope ..."
  try {
    Invoke-WebRequest -Uri $url -OutFile $out -TimeoutSec 180 -UseBasicParsing
  } catch {
    Write-Host "  FAIL: download failed - $($_.Exception.Message)"
    Write-Host "  hint : check network / proxy, or manually download from"
    Write-Host "         $url"
    $allOk = $false
    continue
  }

  $size = (Get-Item $out).Length
  $sha  = (Get-FileHash -Algorithm SHA256 -Path $out).Hash
  Write-Host "  size  : $size bytes"
  Write-Host "  sha256: $sha"

  if ($sha -ne $f.sha) {
    Write-Host "  FAIL: SHA256 mismatch (expected $($f.sha))"
    Write-Host "  hint : file may be corrupted or upstream updated; remove $out and retry"
    $allOk = $false
  } else {
    Write-Host "  OK   : SHA256 verified"
  }
  Write-Host ''
}

if (-not $allOk) {
  Write-Host '============================================================'
  Write-Host '  RESULT: FAILED - some files failed verification'
  Write-Host '  Build will fail until models are correctly downloaded.'
  Write-Host '============================================================'
  exit 1
}

Write-Host '============================================================'
Write-Host '  RESULT: OK - all 3 models downloaded and verified'
Write-Host '  Now run build.ps1 to compile XTranslate.'
Write-Host '============================================================'

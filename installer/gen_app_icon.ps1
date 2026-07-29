#requires -version 5
# ===========================================================================
#  Phase 8 - 生成 app.ico（安装程序/快捷方式/exe 资源用）
#
#  视觉对齐 TrayManager::paintIconPixmap（src/ui/tray/TrayManager.cpp）：
#    - 深灰 #202020 圆角方块（radius = size * 0.22）
#    - 白色「译」字（Microsoft YaHei Bold，pixelSize = size * 0.62）
#  避免引入另一套视觉语言；与托盘图标保持一致。
#
#  多尺寸 16/32/48/256 写入单个 .ico（ICO 容器格式）。
#  输出: installer\app.ico
# ===========================================================================
[CmdletBinding()]
param(
    [string]$OutPath = ''
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot 在某些 -File 调用场景下为空，用 $MyInvocation 兜底
if (-not $OutPath) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $OutPath = Join-Path $scriptDir 'app.ico'
}

Add-Type -AssemblyName System.Drawing

# 与 TrayManager 一致的颜色/比例
$bgColor = [System.Drawing.Color]::FromArgb(0x20, 0x20, 0x20)
$fgColor = [System.Drawing.Color]::White
$sizes = @(16, 32, 48, 256)

# ICO 容器格式：
#   ICONDIR (6 bytes)  : reserved(2)=0, type(2)=1, count(2)=N
#   ICONDIRENTRY[N]    : each 16 bytes
#   位图数据[N]         : PNG 或 BMP 数据
# 这里用 PNG（现代 ICO 支持，文件更小，alpha 支持完整）。

$pngBlobs = New-Object System.Collections.Generic.List[byte[]]
foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    try {
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.SmoothingMode    = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

        # 透明背景
        $g.Clear([System.Drawing.Color]::Transparent)

        # 圆角方块：radius = size * 0.22（与 TrayManager 一致）
        $radius = [int]([math]::Round($size * 0.22))
        $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddArc($rect.X, $rect.Y, $radius * 2, $radius * 2, 180, 90)
        $path.AddArc($rect.Right - $radius * 2, $rect.Y, $radius * 2, $radius * 2, 270, 90)
        $path.AddArc($rect.Right - $radius * 2, $rect.Bottom - $radius * 2, $radius * 2, $radius * 2, 0, 90)
        $path.AddArc($rect.X, $rect.Bottom - $radius * 2, $radius * 2, $radius * 2, 90, 90)
        $path.CloseFigure()

        $brush = New-Object System.Drawing.SolidBrush $bgColor
        $g.FillPath($brush, $path)
        $brush.Dispose()
        $path.Dispose()

        # 「译」字：Microsoft YaHei Bold，pixelSize = size * 0.62
        $fontSize = [int]([math]::Round($size * 0.62))
        if ($fontSize -lt 6) { $fontSize = 6 }
        $font = New-Object System.Drawing.Font 'Microsoft YaHei', $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)

        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
        # 与 TrayManager::drawText 的 -size*0.02 偏移对齐（视觉居中补偿）
        $textRect = New-Object System.Drawing.RectangleF 0, (-$size * 0.02), $size, $size

        $textBrush = New-Object System.Drawing.SolidBrush $fgColor
        $g.DrawString('译', $font, $textBrush, $textRect, $sf)
        $textBrush.Dispose()
        $font.Dispose()
        $sf.Dispose()
        $g.Dispose()

        # PNG 编码（保留 alpha 通道）
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $pngBlobs.Add($ms.ToArray())
        $ms.Dispose()
    } finally {
        $bmp.Dispose()
    }
}

# 写 ICO 容器
$fs = [System.IO.File]::Create($OutPath)
try {
    $bw = New-Object System.IO.BinaryWriter $fs

    # ICONDIR
    $bw.Write([uint16]0)                          # reserved
    $bw.Write([uint16]1)                          # type = 1 (icon)
    $bw.Write([uint16]$pngBlobs.Count)             # count

    # ICONDIRENTRY（每个 16 bytes）
    #  w(1) h(1) colors(1) reserved(1) planes(2) bitcount(2) bytes(4) offset(4)
    $dataOffset = 6 + ($pngBlobs.Count * 16)
    for ($i = 0; $i -lt $pngBlobs.Count; $i++) {
        $sz = $sizes[$i]
        $blob = $pngBlobs[$i]
        $w = if ($sz -ge 256) { 0 } else { $sz }   # 256 -> 0（约定）
        $h = $w
        $bw.Write([byte]$w)
        $bw.Write([byte]$h)
        $bw.Write([byte]0)                          # colors (0 = >=256)
        $bw.Write([byte]0)                          # reserved
        $bw.Write([uint16]1)                        # planes
        $bw.Write([uint16]32)                       # bitcount
        $bw.Write([uint32]$blob.Length)
        $bw.Write([uint32]$dataOffset)
        $dataOffset += $blob.Length
    }

    # 位图数据
    foreach ($blob in $pngBlobs) {
        $bw.Write($blob)
    }
    $bw.Flush()
} finally {
    $fs.Dispose()
}

# 校验
$info = Get-Item $OutPath
$sizesStr = $sizes -join ','
Write-Host "[gen_icon] wrote $($info.FullName) ($($info.Length) bytes, $($sizes.Count) sizes: $sizesStr)"

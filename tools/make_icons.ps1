# =============================================================================
# @file make_icons.ps1
# @brief 由 Taiphoon 標誌產生程式圖示：Windows .ico、macOS .icns 與視窗圖示 PNG
# @author Taiphoon GCS Team
# @date 2026-10-12
#
# 只取鳥形標誌（TAIPHON 字樣在 16～32 px 無法辨識），置於白色圓角底板上。
# 目前來源僅 168x168，取得高解析度或向量標誌後以相同參數重新產生即可。
# 用法（專案根目錄）：
#   .\tools\make_icons.ps1 -Source resources\taiphoon-logo.png `
#       -IcoOut packaging\windows\Taiphoon_QGCS.ico -IcnsOut packaging\macos\Taiphoon_QGCS.icns `
#       -PngOut resources\taiphoon-icon.png -PreviewOut $env:TEMP\icon-preview.png
# =============================================================================
param(
    [Parameter(Mandatory)] [string]$Source,
    [Parameter(Mandatory)] [string]$IcoOut,
    [Parameter(Mandatory)] [string]$IcnsOut,
    [Parameter(Mandatory)] [string]$PngOut,
    [Parameter(Mandatory)] [string]$PreviewOut
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = [System.Drawing.Bitmap]::FromFile($Source)

# 鳥形標誌範圍（排除下方 TAIPHON 字樣）
$minX = 999; $maxX = -1; $minY = 999; $maxY = -1
for ($y = 0; $y -lt 122; $y++) {
    for ($x = 0; $x -lt $src.Width; $x++) {
        if ($src.GetPixel($x, $y).A -gt 20) {
            if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
        }
    }
}
$mark = New-Object System.Drawing.Rectangle($minX, $minY, ($maxX - $minX + 1), ($maxY - $minY + 1))
"mark bbox: $mark"

function Render([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    # 白色圓角底板：深色工作列上也能辨識深藍色標誌
    $inset = [Math]::Max(0.5, $s * 0.02)
    $r = $s * 0.22
    $w = $s - 2 * $inset
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($inset, $inset, 2 * $r, 2 * $r, 180, 90)
    $path.AddArc($inset + $w - 2 * $r, $inset, 2 * $r, 2 * $r, 270, 90)
    $path.AddArc($inset + $w - 2 * $r, $inset + $w - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $path.AddArc($inset, $inset + $w - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $path.CloseFigure()
    $g.FillPath([System.Drawing.Brushes]::White, $path)
    if ($s -ge 32) {
        $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 0xC8, 0xCE, 0xD8), [Math]::Max(1, $s / 128))
        $g.DrawPath($pen, $path)
        $pen.Dispose()
    }

    # 標誌置中，保留邊距
    $pad = $s * $(if ($s -le 24) { 0.08 } else { 0.13 })
    $box = $s - 2 * $pad
    $scale = [Math]::Min($box / $mark.Width, $box / $mark.Height)
    $dw = $mark.Width * $scale; $dh = $mark.Height * $scale
    $dest = New-Object System.Drawing.RectangleF((($s - $dw) / 2), (($s - $dh) / 2), $dw, $dh)
    $attr = New-Object System.Drawing.Imaging.ImageAttributes
    $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
    $g.DrawImage($src, [System.Drawing.Rectangle]::Round($dest), $mark.X, $mark.Y, $mark.Width, $mark.Height, [System.Drawing.GraphicsUnit]::Pixel, $attr)
    $g.Dispose()
    return $bmp
}

function PngBytes($bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return , $ms.ToArray()
}

# ---- ICO（各尺寸皆以 PNG 內嵌，Windows Vista 以後支援）----
$icoSizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = @()
foreach ($s in $icoSizes) { $b = Render $s; $images += , (PngBytes $b); $b.Dispose() }
$fs = [System.IO.File]::Create($IcoOut)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$icoSizes.Count)
$offset = 6 + 16 * $icoSizes.Count
for ($i = 0; $i -lt $icoSizes.Count; $i++) {
    $s = $icoSizes[$i]; $dim = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$images[$i].Length); $bw.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $bw.Write($img) }
$bw.Close()

# ---- ICNS（PNG 項目，macOS 10.7 以後支援；大端序）----
function BE32([int]$v) { $a = [BitConverter]::GetBytes([int32]$v); [Array]::Reverse($a); return , $a }
$icnsTypes = [ordered]@{ 'icp4' = 16; 'icp5' = 32; 'icp6' = 64; 'ic07' = 128; 'ic08' = 256; 'ic09' = 512 }
$body = New-Object System.IO.MemoryStream
foreach ($t in $icnsTypes.Keys) {
    $b = Render $icnsTypes[$t]; $png = PngBytes $b; $b.Dispose()
    $tb = [Text.Encoding]::ASCII.GetBytes($t)
    $body.Write($tb, 0, 4); $len = BE32 ($png.Length + 8); $body.Write($len, 0, 4); $body.Write($png, 0, $png.Length)
}
$all = $body.ToArray()
$fs = [System.IO.File]::Create($IcnsOut)
$h = [Text.Encoding]::ASCII.GetBytes('icns'); $fs.Write($h, 0, 4)
$len = BE32 ($all.Length + 8); $fs.Write($len, 0, 4); $fs.Write($all, 0, $all.Length)
$fs.Close()

# ---- 視窗圖示 PNG ----
$b = Render 256; $b.Save($PngOut, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()

# ---- 預覽（深色與淺色背景，各尺寸並排）----
$prev = New-Object System.Drawing.Bitmap(720, 300)
$g = [System.Drawing.Graphics]::FromImage($prev)
$g.FillRectangle((New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 32, 32, 32))), 0, 0, 720, 150)
$g.FillRectangle([System.Drawing.Brushes]::WhiteSmoke, 0, 150, 720, 150)
$x = 10
foreach ($s in 16, 24, 32, 48, 64, 128) {
    $b = Render $s
    $g.DrawImage($b, $x, (75 - $s / 2)); $g.DrawImage($b, $x, (225 - $s / 2))
    $b.Dispose(); $x += $s + 24
}
$g.Dispose(); $prev.Save($PreviewOut, [System.Drawing.Imaging.ImageFormat]::Png); $prev.Dispose()
$src.Dispose()
"ico=$((Get-Item $IcoOut).Length) icns=$((Get-Item $IcnsOut).Length) png=$((Get-Item $PngOut).Length)"



Add-Type -AssemblyName System.Drawing

function New-RoundRect([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $d = $r * 2
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc(($x + $w - $d), $y, $d, $d, 270, 90)
    $path.AddArc(($x + $w - $d), ($y + $h - $d), $d, $d, 0, 90)
    $path.AddArc($x, ($y + $h - $d), $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-AudioPassBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::Transparent)

    $margin = [Math]::Max(1.0, $size * 0.015)
    $side = $size - (2.0 * $margin)
    $radius = $side * 0.22
    $tile = New-RoundRect $margin $margin $side $side $radius
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (
        (New-Object System.Drawing.PointF 0, 0),
        (New-Object System.Drawing.PointF $size, $size),
        ([System.Drawing.Color]::FromArgb(255, 22, 78, 214)),
        ([System.Drawing.Color]::FromArgb(255, 56, 176, 255)))
    $g.FillPath($brush, $tile)
    $tile.Dispose()
    $brush.Dispose()

    $wave = New-Object 'System.Drawing.PointF[]' 7
    $wave[0] = New-Object System.Drawing.PointF ($size * 0.14), ($size * 0.50)
    $wave[1] = New-Object System.Drawing.PointF ($size * 0.23), ($size * 0.42)
    $wave[2] = New-Object System.Drawing.PointF ($size * 0.31), ($size * 0.52)
    $wave[3] = New-Object System.Drawing.PointF ($size * 0.40), ($size * 0.27)
    $wave[4] = New-Object System.Drawing.PointF ($size * 0.50), ($size * 0.75)
    $wave[5] = New-Object System.Drawing.PointF ($size * 0.60), ($size * 0.40)
    $wave[6] = New-Object System.Drawing.PointF ($size * 0.66), ($size * 0.50)
    $wavePen = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ([Math]::Max(2.0, $size * 0.068))
    $wavePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $wavePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $wavePen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawCurve($wavePen, $wave, 0.55)
    $wavePen.Dispose()

    $chevron = New-Object 'System.Drawing.PointF[]' 3
    $chevron[0] = New-Object System.Drawing.PointF ($size * 0.74), ($size * 0.40)
    $chevron[1] = New-Object System.Drawing.PointF ($size * 0.86), ($size * 0.50)
    $chevron[2] = New-Object System.Drawing.PointF ($size * 0.74), ($size * 0.60)
    $chevronPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 103, 232, 249)), ([Math]::Max(2.0, $size * 0.052))
    $chevronPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $chevronPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $chevronPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawLines($chevronPen, $chevron)
    $chevronPen.Dispose()

    $g.Dispose()
    return $bmp
}

function Save-Png([System.Drawing.Bitmap]$bmp, [string]$path) {
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
}

function New-Ico([System.Drawing.Bitmap[]]$bitmaps, [string]$path) {
    $images = foreach ($bmp in $bitmaps) {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bytes = $ms.ToArray()
        $ms.Dispose()
        [pscustomobject]@{ Width = $bmp.Width; Height = $bmp.Height; Bytes = $bytes }
    }
    $out = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $out
    $bw.Write([uint16]0)
    $bw.Write([uint16]1)
    $bw.Write([uint16]$images.Count)
    $offset = 6 + (16 * $images.Count)
    foreach ($image in $images) {
        $w = if ($image.Width -ge 256) { 0 } else { $image.Width }
        $h = if ($image.Height -ge 256) { 0 } else { $image.Height }
        $bw.Write([byte]$w)
        $bw.Write([byte]$h)
        $bw.Write([byte]0)
        $bw.Write([byte]0)
        $bw.Write([uint16]1)
        $bw.Write([uint16]32)
        $bw.Write([uint32]$image.Bytes.Length)
        $bw.Write([uint32]$offset)
        $offset += $image.Bytes.Length
    }
    foreach ($image in $images) {
        $bw.Write($image.Bytes)
    }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes($path, $out.ToArray())
    $bw.Dispose()
    $out.Dispose()
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256
$bitmaps = foreach ($size in $sizes) { New-AudioPassBitmap $size }
$master = New-AudioPassBitmap 1024
Save-Png $master (Join-Path $root "AudioPass.png")
New-Ico $bitmaps (Join-Path $root "AudioPass.ico")
$master.Dispose()
foreach ($bmp in $bitmaps) { $bmp.Dispose() }
Write-Output "wrote AudioPass.ico and AudioPass.png"

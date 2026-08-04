# Generuje res\launcher.ico - wielorozmiarowa ikone aplikacji.
#
# Kompozycja w rodzinie ikon KKr: zaokraglony kafelek z gradientem po przekatnej,
# plaski glif i bursztynowy akcent.
#
# Glif celowo NIE odwzorowuje znaku Claude. Po pierwsze jest to znak towarowy
# Anthropic i podobna ikona sugerowalaby powiazanie z producentem. Po drugie -
# i to wazniejsze na co dzien - ikona podobna do oryginalnej bylaby nie do
# odroznienia od samej aplikacji na pasku zadan. Zamiast tego rysujemy dwa
# nachodzace okna, czyli to, co ten program faktycznie robi.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root   = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$target = Join-Path $root 'res\launcher.ico'
$sizes  = @(16, 24, 32, 48, 64, 128, 256)

# Paleta Claude: terakota rozjasniona w lewym gornym rogu, przygaszona w prawym dolnym.
$gradientFrom = [System.Drawing.Color]::FromArgb(255, 232, 138, 99)
$gradientTo   = [System.Drawing.Color]::FromArgb(255, 178, 78,  42)
$glyphColor   = [System.Drawing.Color]::FromArgb(255, 253, 252, 251)
$accentColor  = [System.Drawing.Color]::FromArgb(255, 245, 179, 60)

function New-RoundedPath([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function Fill-RoundedRect($g, [single]$x, [single]$y, [single]$side, [single]$radius, $color) {
    $path = New-RoundedPath $x $y $side $side $radius
    $brush = New-Object System.Drawing.SolidBrush $color
    $g.FillPath($brush, $path)
    $brush.Dispose()
    $path.Dispose()
}

function New-Frame([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    # Kafelek
    $inset = [single]($size * 0.02)
    $side  = [single]($size - $inset * 2)
    $tile  = New-RoundedPath $inset $inset $side $side ([single]($size * 0.22))
    $gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point 0, 0),
        (New-Object System.Drawing.Point $size, $size),
        $gradientFrom, $gradientTo)
    $g.FillPath($gradient, $tile)
    $gradient.Dispose()
    $tile.Dispose()

    # Dwa nachodzace okna. Tylne bursztynowe, przednie biale - kontrast miedzy nimi
    # niesie caly przekaz, wiec czytelnosc nie zalezy od cieni ani obrysu.
    $cardSide   = [single]($size * 0.40)
    $cardRadius = [single]($size * 0.07)

    # Ponizej 32 px cienki prezwit miedzy oknami znika, wiec je lekko rozsuwamy.
    $spread = if ($size -ge 32) { 0.17 } else { 0.20 }

    Fill-RoundedRect $g ([single]($size * 0.22)) ([single]($size * 0.20)) `
                     $cardSide $cardRadius $accentColor

    # Przerwa pod przednim oknem: rysujemy je najpierw w kolorze tla kafelka,
    # nieco wieksze, zeby odcielo sie od bursztynowego sasiada.
    $gapSide = [single]($cardSide + $size * 0.05)
    $gapPath = New-RoundedPath ([single]($size * (0.22 + $spread) - $size * 0.025)) `
                               ([single]($size * (0.20 + $spread) - $size * 0.025)) `
                               $gapSide $gapSide ([single]($cardRadius * 1.15))
    $gapBrush = New-Object System.Drawing.SolidBrush $gradientTo
    $g.FillPath($gapBrush, $gapPath)
    $gapBrush.Dispose()
    $gapPath.Dispose()

    Fill-RoundedRect $g ([single]($size * (0.22 + $spread))) ([single]($size * (0.20 + $spread))) `
                     $cardSide $cardRadius $glyphColor

    $g.Dispose()
    return $bmp
}

# --- zlozenie pliku ICO (kazda klatka zapisana jako PNG) ---
$frames = @()
foreach ($size in $sizes) {
    $bmp = New-Frame $size
    $stream = New-Object System.IO.MemoryStream
    $bmp.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += , @{ Size = $size; Bytes = $stream.ToArray() }
    $stream.Dispose()
    $bmp.Dispose()
}

$output = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $output

$writer.Write([UInt16]0)               # reserved
$writer.Write([UInt16]1)               # typ: ikona
$writer.Write([UInt16]$frames.Count)

$offset = 6 + 16 * $frames.Count
foreach ($frame in $frames) {
    $dimension = if ($frame.Size -ge 256) { 0 } else { $frame.Size }
    $writer.Write([Byte]$dimension)    # szerokosc
    $writer.Write([Byte]$dimension)    # wysokosc
    $writer.Write([Byte]0)             # paleta
    $writer.Write([Byte]0)             # reserved
    $writer.Write([UInt16]1)           # plaszczyzny
    $writer.Write([UInt16]32)          # bitow na piksel
    $writer.Write([UInt32]$frame.Bytes.Length)
    $writer.Write([UInt32]$offset)
    $offset += $frame.Bytes.Length
}
foreach ($frame in $frames) {
    $writer.Write($frame.Bytes)
}

$writer.Flush()
[System.IO.File]::WriteAllBytes($target, $output.ToArray())
$writer.Dispose()
$output.Dispose()

Write-Host "Zapisano $target ($($frames.Count) klatek: $($sizes -join ', ') px)"

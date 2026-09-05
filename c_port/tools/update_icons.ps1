# QuickBTTray Icon Update Script
# Converts bt_icon_on.svg and bt_icon_off.svg to multi-resolution .ico files

param(
    [string]$ResDir = "$PSScriptRoot\..\res",
    [string]$AppAssetsDir = "$PSScriptRoot\..\..\QuickBTTrayApp\Views\Assets",
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "QuickBTTray Icon Generator" -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan

# 1. Locate Inkscape
$cmdCom = Get-Command inkscape.com -ErrorAction SilentlyContinue
$cmdExe = Get-Command inkscape.exe -ErrorAction SilentlyContinue
$pathCom = if ($cmdCom) { $cmdCom.Source } else { $null }
$pathExe = if ($cmdExe) { $cmdExe.Source } else { $null }

$inkscapePaths = @(
    $pathCom,
    $pathExe,
    "D:\Program Files\Inkscape\bin\inkscape.com",
    "D:\Program Files\Inkscape\bin\inkscape.exe",
    "C:\Program Files\Inkscape\bin\inkscape.com",
    "C:\Program Files\Inkscape\bin\inkscape.exe",
    "$env:ProgramFiles\Inkscape\bin\inkscape.com",
    "$env:ProgramFiles\Inkscape\bin\inkscape.exe",
    "${env:ProgramFiles(x86)}\Inkscape\bin\inkscape.com",
    "${env:ProgramFiles(x86)}\Inkscape\bin\inkscape.exe"
)

$inkscape = $null
foreach ($path in $inkscapePaths) {
    if ($path -and (Test-Path $path)) {
        $inkscape = $path
        break
    }
}

if (-not $inkscape) {
    Write-Error "Inkscape was not found. Please install Inkscape or ensure it is in your PATH."
    exit 1
}

Write-Host "Using Inkscape: $inkscape" -ForegroundColor Green

# 2. Paths
$svgOn  = Join-Path $ResDir "bt_icon_on.svg"
$svgOff = Join-Path $ResDir "bt_icon_off.svg"

if (-not (Test-Path $svgOn))  { Write-Error "Missing $svgOn"; exit 1 }
if (-not (Test-Path $svgOff)) { Write-Error "Missing $svgOff"; exit 1 }

$sizes = @(16, 20, 24, 32, 48, 64, 128, 256)
$tempFiles = @()

Add-Type -AssemblyName System.Drawing

function Convert-PngsToIco {
    param(
        [string[]]$PngPaths,
        [string]$OutIcoPath
    )

    $images = @()
    foreach ($p in $PngPaths) {
        $bytes = [System.IO.File]::ReadAllBytes($p)
        $img = [System.Drawing.Image]::FromFile($p)
        $w = $img.Width
        $h = $img.Height
        $img.Dispose()
        $images += [PSCustomObject]@{
            Width = $w
            Height = $h
            Bytes = $bytes
        }
    }

    $fs = [System.IO.File]::Create($OutIcoPath)
    $bw = New-Object System.IO.BinaryWriter($fs)

    # ICONDIR header: idReserved=0, idType=1, idCount=N
    $bw.Write([uint16]0)
    $bw.Write([uint16]1)
    $bw.Write([uint16]$images.Count)

    # ICONDIRENTRY list
    $offset = 6 + ($images.Count * 16)
    foreach ($img in $images) {
        $bw.Write([byte]($img.Width -band 0xFF))
        $bw.Write([byte]($img.Height -band 0xFF))
        $bw.Write([byte]0)   # bColorCount
        $bw.Write([byte]0)   # bReserved
        $bw.Write([uint16]1)  # wPlanes
        $bw.Write([uint16]32) # wBitCount
        $bw.Write([uint32]$img.Bytes.Length)
        $bw.Write([uint32]$offset)
        $offset += $img.Bytes.Length
    }

    # Image byte blocks
    foreach ($img in $images) {
        $bw.Write($img.Bytes)
    }

    $bw.Close()
    $fs.Close()

    $len = (Get-Item $OutIcoPath).Length
    Write-Host "Created $OutIcoPath ($($images.Count) sizes, $len bytes)" -ForegroundColor Green
}

# 3. Export PNGs at each resolution
Write-Host "Rendering resolutions from SVGs: $($sizes -join ', ')..."
$onPngs = @()
$offPngs = @()

foreach ($s in $sizes) {
    $onPng  = Join-Path $ResDir "temp_on_$s.png"
    $offPng = Join-Path $ResDir "temp_off_$s.png"
    $tempFiles += $onPng
    $tempFiles += $offPng

    & $inkscape $svgOn  -w $s -h $s --export-filename=$onPng
    & $inkscape $svgOff -w $s -h $s --export-filename=$offPng

    $onPngs  += $onPng
    $offPngs += $offPng
}

# 4. Pack into .ico
$icoOn  = Join-Path $ResDir "icon.ico"
$icoOff = Join-Path $ResDir "icon-connecting.ico"

Write-Host "Assembling multi-resolution .ico files..."
Convert-PngsToIco -PngPaths $onPngs  -OutIcoPath $icoOn
Convert-PngsToIco -PngPaths $offPngs -OutIcoPath $icoOff

# 5. Clean up temp PNGs
foreach ($f in $tempFiles) {
    if (Test-Path $f) { Remove-Item $f -Force }
}

# 6. Copy to C# WPF app assets if directory exists
if (Test-Path $AppAssetsDir) {
    Copy-Item $icoOn  (Join-Path $AppAssetsDir "icon.ico") -Force
    Copy-Item $icoOff (Join-Path $AppAssetsDir "icon-connecting.ico") -Force
    Write-Host "Synced icons to QuickBTTrayApp assets: $AppAssetsDir" -ForegroundColor Cyan
}

# 7. Refresh Windows Shell Icon Cache
try {
    Add-Type -TypeDefinition @"
    using System;
    using System.Runtime.InteropServices;
    public class ShellNotifier {
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(int wEventId, uint uFlags, IntPtr dwItem1, IntPtr dwItem2);
    }
"@ -ErrorAction SilentlyContinue
    [ShellNotifier]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero) # SHCNE_ASSOCCHANGED
    Write-Host "Notified Windows Shell of icon update." -ForegroundColor Cyan
} catch {
    # Non-critical if P/Invoke type was already defined
}

# 8. Rebuild C port if requested
if ($Rebuild) {
    $buildBat = Join-Path $PSScriptRoot "..\build.bat"
    if (Test-Path $buildBat) {
        Write-Host "Rebuilding QuickBTTray.exe..." -ForegroundColor Yellow
        cmd.exe /c "call `"$buildBat`""
    }
}

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "Icon update complete!" -ForegroundColor Green
Write-Host "===================================================" -ForegroundColor Cyan

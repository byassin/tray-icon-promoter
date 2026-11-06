[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string]$Architecture = 'x64',
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build-local'
$distDirectory = Join-Path $projectRoot 'dist'
$executable = Join-Path $distDirectory 'TrayIconPromoter.exe'
$version = & (Join-Path $PSScriptRoot 'test-version.ps1')

$gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
$windres = Get-Command windres.exe -ErrorAction SilentlyContinue

if (-not $gcc -or -not $windres) {
    throw 'gcc.exe and windres.exe are required. Use w64devkit or build with CMake and Visual Studio.'
}

New-Item -ItemType Directory -Force -Path $buildDirectory, $distDirectory | Out-Null

$resourceObject = Join-Path $buildDirectory 'version.o'
& $windres.Source `
    (Join-Path $projectRoot 'resources\version.rc') `
    '--include-dir' (Join-Path $projectRoot 'resources') `
    '-O' 'coff' `
    '-o' $resourceObject
if ($LASTEXITCODE -ne 0) { throw "windres failed with exit code $LASTEXITCODE" }

& $gcc.Source `
    (Join-Path $projectRoot 'src\tray_icon_promoter.c') `
    $resourceObject `
    '-o' $executable `
    '-std=c11' `
    '-DUNICODE' `
    '-D_UNICODE' `
    '-DWIN32_LEAN_AND_MEAN' `
    '-D_WIN32_WINNT=0x0A00' `
    '-Os' `
    '-s' `
    '-Wall' `
    '-Wextra' `
    '-Wpedantic' `
    '-Werror' `
    '-ffunction-sections' `
    '-fdata-sections' `
    '-municode' `
    '-mwindows' `
    '-static-libgcc' `
    '-Wl,--gc-sections' `
    '-Wl,--dynamicbase' `
    '-Wl,--nxcompat' `
    '-Wl,--high-entropy-va' `
    '-ladvapi32' `
    '-lversion'
if ($LASTEXITCODE -ne 0) { throw "gcc failed with exit code $LASTEXITCODE" }

$checksumTargets = @($executable)

if ($Package) {
    $packageRoot = Join-Path $buildDirectory 'package'
    $packagePath = Join-Path $distDirectory "TrayIconPromoter-$Architecture.zip"
    Remove-Item -LiteralPath $packageRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $packagePath -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
    Copy-Item -LiteralPath $executable,(Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'LICENSE') -Destination $packageRoot
    Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $packagePath -CompressionLevel Optimal
    $checksumTargets += $packagePath
}

$hashes = @($checksumTargets | ForEach-Object { Get-FileHash -LiteralPath $_ -Algorithm SHA256 })
$hashLines = @($hashes | ForEach-Object { "$($_.Hash.ToLowerInvariant())  $(Split-Path -Leaf $_.Path)" })
Set-Content -LiteralPath (Join-Path $distDirectory 'SHA256SUMS.txt') -Value $hashLines -Encoding ascii

Write-Output "Built Tray Icon Promoter $version ($Architecture)."
Get-Item -LiteralPath $executable | Select-Object FullName, Length, LastWriteTime
$hashes | Select-Object Algorithm, Hash, Path

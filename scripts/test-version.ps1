[CmdletBinding()]
param(
    [string]$ReleaseTag
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Get-MatchedVersion {
    param([string]$RelativePath, [string]$Pattern)

    $content = Get-Content -LiteralPath (Join-Path $projectRoot $RelativePath) -Raw
    $match = [regex]::Match($content, $Pattern)
    if (-not $match.Success) {
        throw "Could not read version metadata in $RelativePath."
    }
    return $match.Groups[1].Value
}

$version = Get-MatchedVersion 'CMakeLists.txt' '(?m)^project\(TrayIconPromoter VERSION (\d+\.\d+\.\d+) LANGUAGES C RC\)'
$numericVersion = $version.Replace('.', ',') + ',0'
$expected = @(
    @{ Path = 'src/tray_icon_promoter.c'; Pattern = '(?m)^#define APP_VERSION L"([^"]+)"'; Value = $version },
    @{ Path = 'resources/app.manifest'; Pattern = '(?s)<assemblyIdentity\b[^>]*\bversion="([^"]+)"'; Value = "$version.0" },
    @{ Path = 'resources/version.rc'; Pattern = '(?m)^\s*FILEVERSION\s+([^\r\n]+)'; Value = $numericVersion },
    @{ Path = 'resources/version.rc'; Pattern = '(?m)^\s*PRODUCTVERSION\s+([^\r\n]+)'; Value = $numericVersion },
    @{ Path = 'resources/version.rc'; Pattern = 'VALUE "FileVersion", "([^"\\]+)\\0"'; Value = $version },
    @{ Path = 'resources/version.rc'; Pattern = 'VALUE "ProductVersion", "([^"\\]+)\\0"'; Value = $version }
)

foreach ($entry in $expected) {
    $actual = Get-MatchedVersion $entry.Path $entry.Pattern
    if ($actual -cne $entry.Value) {
        throw "Version mismatch in $($entry.Path): expected $($entry.Value), found $actual."
    }
}

if ($PSBoundParameters.ContainsKey('ReleaseTag')) {
    if ($ReleaseTag -cnotmatch '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$' -or $ReleaseTag -cne "v$version") {
        throw "Release tag '$ReleaseTag' must exactly match project version v$version."
    }
}

# Keep stdout machine-readable for the build and GitHub Actions workflows.
Write-Output $version

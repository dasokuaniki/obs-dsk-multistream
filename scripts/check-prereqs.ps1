param(
    [string]$ObsPrefix = "",
    [string]$ObsDepsPrefix = "",
    [string]$QtPrefix = ""
)

$ErrorActionPreference = "Stop"

function Find-FirstFile {
    param(
        [string[]]$Roots,
        [string]$Filter
    )

    foreach ($root in $Roots) {
        if ([string]::IsNullOrWhiteSpace($root) -or -not (Test-Path -LiteralPath $root)) {
            continue
        }

        $found = Get-ChildItem -LiteralPath $root -Recurse -Filter $Filter -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    }

    return $null
}

function Add-Result {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Detail
    )

    [PSCustomObject]@{
        Check = $Name
        OK = $Ok
        Detail = $Detail
    }
}

$vsRoot = "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) {
    $cmakePath = Find-FirstFile -Roots @($vsRoot) -Filter "cmake.exe"
    if ($cmakePath) {
        $cmake = [PSCustomObject]@{ Source = $cmakePath }
    }
}

$ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue
if (-not $ninja) {
    $ninjaPath = Find-FirstFile -Roots @($vsRoot) -Filter "ninja.exe"
    if ($ninjaPath) {
        $ninja = [PSCustomObject]@{ Source = $ninjaPath }
    }
}

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) {
    $clPath = Find-FirstFile -Roots @($vsRoot) -Filter "cl.exe"
    if ($clPath) {
        $cl = [PSCustomObject]@{ Source = $clPath }
    }
}

$libobsRoots = @($ObsPrefix, $env:OBS_PREFIX, "C:\obs-studio", "C:\obs-sdk")
$obsDepsRoots = @($ObsDepsPrefix, $env:OBS_DEPS_PREFIX, "deps\obs-studio-32.1.2\.deps\obs-deps-2025-08-23-x64")
$qtRoots = @($QtPrefix, $env:QT_PREFIX, "C:\Qt")

$libobs = Find-FirstFile -Roots $libobsRoots -Filter "libobsConfig.cmake"
$frontend = Find-FirstFile -Roots $libobsRoots -Filter "obs-frontend-apiConfig.cmake"
$simde = Find-FirstFile -Roots $obsDepsRoots -Filter "simde-common.h"
$curl = Find-FirstFile -Roots $obsDepsRoots -Filter "CURLConfig.cmake"
$qt = Find-FirstFile -Roots $qtRoots -Filter "Qt6Config.cmake"

$results = @()
$results += Add-Result "CMake" ([bool]$cmake) ($(if ($cmake) { $cmake.Source } else { "missing" }))
$results += Add-Result "Ninja" ([bool]$ninja) ($(if ($ninja) { $ninja.Source } else { "missing" }))
$results += Add-Result "MSVC cl.exe" ([bool]$cl) ($(if ($cl) { $cl.Source } else { "missing" }))
$results += Add-Result "libobs CMake package" ([bool]$libobs) ($(if ($libobs) { $libobs } else { "missing; set -ObsPrefix or OBS_PREFIX" }))
$results += Add-Result "obs-frontend-api CMake package" ([bool]$frontend) ($(if ($frontend) { $frontend } else { "missing; set -ObsPrefix or OBS_PREFIX" }))
$results += Add-Result "obs-deps SIMDe headers" ([bool]$simde) ($(if ($simde) { $simde } else { "missing; set -ObsDepsPrefix or OBS_DEPS_PREFIX" }))
$results += Add-Result "obs-deps CURL CMake package" ([bool]$curl) ($(if ($curl) { $curl } else { "missing; set -ObsDepsPrefix or OBS_DEPS_PREFIX" }))
$results += Add-Result "Qt6 CMake package" ([bool]$qt) ($(if ($qt) { $qt } else { "missing; set -QtPrefix or QT_PREFIX" }))

$results | Format-Table -AutoSize

if ($results.OK -contains $false) {
    exit 1
}

param(
    [string]$ObsPrefix = $env:OBS_PREFIX,
    [string]$ObsDepsPrefix = $env:OBS_DEPS_PREFIX,
    [string]$QtPrefix = $env:QT_PREFIX,
    [string]$TestRuntimeDir = "C:\Program Files\obs-studio\bin\64bit",
    [string]$OAuthAppConfig = $env:DSK_OAUTH_APP_CONFIG,
    [string]$BuildDir = "build\windows-x64-sdk3",
    [string]$Configuration = "RelWithDebInfo",
    [switch]$DisableBundledOAuth,
    [switch]$DisableObsCanvasApi,
    [switch]$EnableE2eHooks,
    [switch]$PublisherRelease
)

$ErrorActionPreference = "Stop"

function Normalize-ProcessPath {
    $pathValue = [System.Environment]::GetEnvironmentVariable("Path", "Process")
    if ([string]::IsNullOrEmpty($pathValue)) {
        $pathValue = [System.Environment]::GetEnvironmentVariable("PATH", "Process")
    }

    [System.Environment]::SetEnvironmentVariable("PATH", $null, "Process")
    [System.Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
}

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

function Require-File {
    param(
        [string]$Name,
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) {
        throw "$Name was not found. Install OBS/Qt development files or pass the correct prefix."
    }
}

$vsRoot = "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
$cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Find-FirstFile -Roots @($vsRoot) -Filter "cmake.exe"
}
Require-File "CMake" $cmake

$ninja = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
if (-not $ninja) {
    $ninja = Find-FirstFile -Roots @($vsRoot) -Filter "ninja.exe"
}
Require-File "Ninja" $ninja

$vcvars = Find-FirstFile -Roots @($vsRoot) -Filter "vcvars64.bat"
Require-File "vcvars64.bat" $vcvars

$repoRoot = Split-Path -Parent $PSScriptRoot
$repoObsSdkRoots = @(
    (Join-Path $repoRoot "deps\obs-sdk"),
    (Join-Path $repoRoot ".deps\obs-sdk")
)
$repoObsDepsRoot = Join-Path $repoRoot "deps\obs-studio-32.1.2\.deps\obs-deps-2025-08-23-x64"
$repoQtRoot = Join-Path $repoRoot "deps\obs-studio-32.1.2\.deps\obs-deps-qt6-2025-08-23-x64"
$obsSdkSearchRoots = @($ObsPrefix) + $repoObsSdkRoots + @("C:\obs-studio", "C:\obs-sdk")

$libobsConfig = Find-FirstFile -Roots $obsSdkSearchRoots -Filter "libobsConfig.cmake"
$frontendConfig = Find-FirstFile -Roots $obsSdkSearchRoots -Filter "obs-frontend-apiConfig.cmake"
$qtConfig = Find-FirstFile -Roots @($QtPrefix, $repoQtRoot, "C:\Qt") -Filter "Qt6Config.cmake"
$simdeHeader = Find-FirstFile -Roots @($ObsDepsPrefix, $repoObsDepsRoot) -Filter "simde-common.h"
$curlConfig = Find-FirstFile -Roots @($ObsDepsPrefix, $repoObsDepsRoot) -Filter "CURLConfig.cmake"

Require-File "libobsConfig.cmake" $libobsConfig
Require-File "obs-frontend-apiConfig.cmake" $frontendConfig
Require-File "Qt6Config.cmake" $qtConfig
Require-File "obs-deps SIMDe headers" $simdeHeader
Require-File "CURLConfig.cmake" $curlConfig

if ($PublisherRelease -and [string]::IsNullOrWhiteSpace($OAuthAppConfig)) {
    throw "PublisherRelease requires OAuthAppConfig."
}
if ($PublisherRelease -and $DisableBundledOAuth) {
    throw "PublisherRelease cannot be combined with DisableBundledOAuth."
}
if ($PublisherRelease -and $EnableE2eHooks) {
    throw "PublisherRelease cannot be combined with EnableE2eHooks."
}
if ($DisableBundledOAuth -and -not [string]::IsNullOrWhiteSpace($OAuthAppConfig)) {
    throw "DisableBundledOAuth cannot be combined with OAuthAppConfig."
}
if (-not $DisableBundledOAuth -and -not [string]::IsNullOrWhiteSpace($OAuthAppConfig)) {
    Require-File "Publisher OAuth application config" $OAuthAppConfig
    $OAuthAppConfig = (Resolve-Path -LiteralPath $OAuthAppConfig).Path
}

$libobsDir = Split-Path -Parent $libobsConfig
$frontendDir = Split-Path -Parent $frontendConfig
$qtDir = Split-Path -Parent $qtConfig
$curlDir = Split-Path -Parent $curlConfig
$resolvedObsDepsPrefix = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $simdeHeader))
$resolvedQtPrefix = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtDir))
$prefixPath = @(
    (Resolve-Path -LiteralPath (Split-Path -Parent $libobsDir)).Path,
    (Resolve-Path -LiteralPath $resolvedObsDepsPrefix).Path,
    (Resolve-Path -LiteralPath $resolvedQtPrefix).Path
) -join ";"
$canvas = if ($DisableObsCanvasApi) { "OFF" } else { "ON" }
$e2eHooks = if ($EnableE2eHooks) { "ON" } else { "OFF" }
$publisherReleaseFlag = if ($PublisherRelease) { "ON" } else { "OFF" }

Normalize-ProcessPath

$cmakeArgs = @(
    "-S .",
    "-B `"$BuildDir`"",
    "-G Ninja",
    "-DCMAKE_MAKE_PROGRAM=`"$ninja`"",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-Dlibobs_DIR=`"$libobsDir`"",
    "-Dobs-frontend-api_DIR=`"$frontendDir`"",
    "-DQt6_DIR=`"$qtDir`"",
    "-DCURL_DIR=`"$curlDir`"",
    "-DCMAKE_PREFIX_PATH=`"$prefixPath`"",
    "-DDSK_ENABLE_OBS_CANVAS_API=$canvas",
    "-DDSK_INCLUDE_E2E_HOOKS=$e2eHooks",
    "-DDSK_PUBLISHER_RELEASE=$publisherReleaseFlag"
)
if (-not [string]::IsNullOrWhiteSpace($TestRuntimeDir) -and
    (Test-Path -LiteralPath $TestRuntimeDir -PathType Container)) {
    $resolvedTestRuntime = (Resolve-Path -LiteralPath $TestRuntimeDir).Path
    $cmakeArgs += "-DDSK_TEST_RUNTIME_DIR=`"$resolvedTestRuntime`""
}
if (-not $DisableBundledOAuth -and -not [string]::IsNullOrWhiteSpace($OAuthAppConfig)) {
    $cmakeArgs += "-DDSK_OAUTH_APP_CONFIG=`"$OAuthAppConfig`""
} else {
    $cmakeArgs += "-DDSK_OAUTH_APP_CONFIG="
}
$cmakeArgs = $cmakeArgs -join " "

$cmd = "call `"$vcvars`" && `"$cmake`" $cmakeArgs"

cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

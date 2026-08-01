param(
    [string]$BuildDir = "build\windows-x64-sdk3",
    [string]$Configuration = "RelWithDebInfo",
    [string]$OutputDir = "release",
    [string]$IsccPath = "",
    [switch]$RequireValidSignature,
    [switch]$RequireValidPluginSignature,
    [switch]$RequireValidInstallerSignature,
    [switch]$InstallerE2E,
    [switch]$KeepStage
)

$ErrorActionPreference = "Stop"
$requirePluginSignature = $RequireValidSignature -or $RequireValidPluginSignature
$requireInstallerSignature = $RequireValidSignature -or $RequireValidInstallerSignature
$ExpectedIsccVersion = "6.7.3"

function Get-AbsolutePath {
    param([string]$Path, [string]$BasePath)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Resolve-IsccPath {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        $resolved = Get-AbsolutePath -Path $RequestedPath -BasePath (Get-Location).Path
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "ISCC.exe was not found: $resolved"
        }
        return $resolved
    }

    $candidates = @(
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe"
    )
    $found = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
    if (-not $found) {
        $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) {
            $found = $command.Source
        }
    }
    if (-not $found) {
        throw "Inno Setup compiler (ISCC.exe) was not found. Install Inno Setup $ExpectedIsccVersion."
    }
    return [IO.Path]::GetFullPath($found)
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = Get-AbsolutePath -Path $BuildDir -BasePath $repoRoot
$outputRoot = Get-AbsolutePath -Path $OutputDir -BasePath $repoRoot
$buildSpecPath = Join-Path $repoRoot "buildspec.json"
$cmakePath = Join-Path $repoRoot "CMakeLists.txt"
$installerDefinition = Join-Path $repoRoot "installer\dsk-multistream.iss"
$licensePath = Join-Path $repoRoot "LICENSE"
$privacyPath = Join-Path $repoRoot "docs\privacy.md"
$betaGuidePath = Join-Path $repoRoot "docs\beta-distribution.md"
$thirdPartyNoticesPath = Join-Path $repoRoot "docs\third-party-notices.md"
$simulcastGuidelinesPath = Join-Path $repoRoot "docs\simulcast-guidelines.md"
$removalScriptPath = Join-Path $repoRoot "scripts\remove-user-data.ps1"

foreach ($requiredPath in @(
    $buildSpecPath, $cmakePath, $installerDefinition, $licensePath, $privacyPath, $betaGuidePath,
    $thirdPartyNoticesPath, $simulcastGuidelinesPath, $removalScriptPath
)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required packaging input was not found: $requiredPath"
    }
}

$buildSpec = Get-Content -LiteralPath $buildSpecPath -Raw -Encoding UTF8 | ConvertFrom-Json
$version = [string]$buildSpec.version
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "buildspec.json contains an invalid semantic version: $version"
}
$cmakeText = Get-Content -LiteralPath $cmakePath -Raw -Encoding UTF8
$cmakeVersionMatch = [regex]::Match($cmakeText, 'project\(obs-dsk-multistream\s+VERSION\s+(\d+\.\d+\.\d+)')
if (-not $cmakeVersionMatch.Success -or $cmakeVersionMatch.Groups[1].Value -ne $version) {
    throw "CMakeLists.txt and buildspec.json must contain the same version."
}

$appId = [string]$buildSpec.uuids.windowsApp
if ($appId -notmatch '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$') {
    throw "buildspec.json contains an invalid Windows AppId: $appId"
}
$effectiveAppId = if ($InstallerE2E) { "54f34b89-0ba8-47cb-bef4-823bad6887a7" } else { $appId }

$dllCandidates = @(
    (Join-Path $buildRoot "$Configuration\obs-dsk-multistream.dll"),
    (Join-Path $buildRoot "obs-dsk-multistream.dll")
)
$builtDll = $dllCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $builtDll) {
    throw "obs-dsk-multistream.dll was not found under $buildRoot. Build the plugin first."
}
$builtDll = (Resolve-Path -LiteralPath $builtDll).Path

$cmakeCachePath = Join-Path $buildRoot "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cmakeCachePath -PathType Leaf)) {
    throw "CMakeCache.txt was not found under $buildRoot. Configure and rebuild with E2E hooks disabled before packaging."
}
$cmakeCacheText = Get-Content -LiteralPath $cmakeCachePath -Raw -Encoding UTF8
if ($cmakeCacheText -notmatch '(?m)^DSK_INCLUDE_E2E_HOOKS:BOOL=OFF\s*$') {
    throw "Distribution packaging requires DSK_INCLUDE_E2E_HOOKS=OFF. Reconfigure and rebuild without -EnableE2eHooks."
}

$dllAscii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($builtDll))
if ($dllAscii.Contains("DSK_E2E_AUTORUN") -or $dllAscii.Contains("DSK_E2E_VERTICAL_UI_STRESS")) {
    throw "The built plugin DLL contains E2E automation hooks. Rebuild it with DSK_INCLUDE_E2E_HOOKS=OFF."
}

$dllSignature = Get-AuthenticodeSignature -LiteralPath $builtDll
if ($requirePluginSignature -and $dllSignature.Status -ne [Management.Automation.SignatureStatus]::Valid) {
    throw "The built plugin DLL must have a valid Authenticode signature. Status=$($dllSignature.Status)"
}

$payloadSources = [ordered]@{
    "bin/64bit/obs-dsk-multistream.dll" = $builtDll
    "data/locale/en-US.ini" = (Join-Path $repoRoot "data\locale\en-US.ini")
    "data/locale/ja-JP.ini" = (Join-Path $repoRoot "data\locale\ja-JP.ini")
    "data/presets/platforms.json" = (Join-Path $repoRoot "data\presets\platforms.json")
    "docs/beta-distribution.md" = $betaGuidePath
    "docs/privacy.md" = $privacyPath
    "docs/third-party-notices.md" = $thirdPartyNoticesPath
    "docs/simulcast-guidelines.md" = $simulcastGuidelinesPath
    "tools/remove-user-data.ps1" = $removalScriptPath
    "LICENSE" = $licensePath
}
$uiSourceRoot = Join-Path $repoRoot "data\ui"
if (-not (Test-Path -LiteralPath $uiSourceRoot -PathType Container)) {
    throw "Required installer UI asset directory was not found: $uiSourceRoot"
}
$uiFiles = @(Get-ChildItem -LiteralPath $uiSourceRoot -Recurse -File)
if ($uiFiles.Count -eq 0) {
    throw "The installer UI asset directory is empty: $uiSourceRoot"
}
foreach ($uiFile in $uiFiles) {
    $uiRelativePath = $uiFile.FullName.Substring($uiSourceRoot.TrimEnd('\').Length + 1).Replace('\', '/')
    $payloadSources["data/ui/$uiRelativePath"] = $uiFile.FullName
}
foreach ($source in $payloadSources.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required installer payload was not found: $source"
    }
}

$iscc = Resolve-IsccPath -RequestedPath $IsccPath
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$stageRoot = Join-Path (Join-Path $repoRoot "build") ("installer-stage-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

try {
    $manifestEntries = @()
    foreach ($entry in $payloadSources.GetEnumerator()) {
        $relativeWindowsPath = $entry.Key.Replace('/', [IO.Path]::DirectorySeparatorChar)
        $destination = Join-Path $stageRoot $relativeWindowsPath
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $entry.Value -Destination $destination -Force
        $file = Get-Item -LiteralPath $destination
        $manifestEntries += [ordered]@{
            path = $entry.Key
            size = $file.Length
            sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        }
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        name = "obs-dsk-multistream"
        version = $version
        architecture = "windows-x64"
        minimumObsVersion = "32.0.0"
        files = $manifestEntries
    }
    $manifestJson = $manifest | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Join-Path $stageRoot "dsk-package-manifest.json"), $manifestJson,
        (New-Object Text.UTF8Encoding($false)))

    $validator = Join-Path $PSScriptRoot "validate-installer-layout.ps1"
    & $validator -PackageRoot $stageRoot -ExpectedVersion $version -RequireValidSignature:$requirePluginSignature

    $outputName = if ($InstallerE2E) {
        "DSK-Multistream-$version-Windows-x64-E2E-Setup"
    } else {
        "DSK-Multistream-$version-Windows-x64-Setup"
    }
    $isccArguments = @(
        "/DSourceRoot=$stageRoot",
        "/DOutputDir=$outputRoot",
        "/DAppVersion=$version",
        "/DAppId=$effectiveAppId",
        "/DLicenseFile=$licensePath",
        "/DPrivacyFile=$privacyPath",
        $installerDefinition
    )
    if ($InstallerE2E) {
        $isccArguments = @("/DTestMode=1") + $isccArguments
    }
    $isccOutput = @(& $iscc @isccArguments 2>&1)
    $isccExitCode = $LASTEXITCODE
    $isccOutput | ForEach-Object { Write-Host $_ }
    if ($isccExitCode -ne 0) {
        throw "Inno Setup compiler failed with exit code $isccExitCode."
    }
    $isccVersionMatch = [regex]::Match(($isccOutput | Out-String), 'Compiler engine version: Inno Setup (\d+\.\d+\.\d+)')
    if (-not $isccVersionMatch.Success -or $isccVersionMatch.Groups[1].Value -ne $ExpectedIsccVersion) {
        $actualVersion = if ($isccVersionMatch.Success) { $isccVersionMatch.Groups[1].Value } else { "unknown" }
        throw "Unexpected Inno Setup compiler version: $actualVersion. Release packaging requires $ExpectedIsccVersion."
    }
    $isccVersion = $isccVersionMatch.Groups[1].Value
    if (($isccOutput | Out-String) -match '(?m)^Warning:') {
        throw "Inno Setup emitted a compiler warning. Release packaging requires a warning-free compile."
    }

    $installerPath = Join-Path $outputRoot "$outputName.exe"
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
        throw "The installer compiler completed without creating the expected file: $installerPath"
    }

    $installerHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
    $hashFile = "$installerPath.sha256"
    [IO.File]::WriteAllText($hashFile, "$installerHash *$([IO.Path]::GetFileName($installerPath))`r`n",
        (New-Object Text.ASCIIEncoding))
    $installerSignature = Get-AuthenticodeSignature -LiteralPath $installerPath
    if ($requireInstallerSignature -and $installerSignature.Status -ne [Management.Automation.SignatureStatus]::Valid) {
        throw "The compiled installer must have a valid Authenticode signature. Status=$($installerSignature.Status)"
    }

    [PSCustomObject]@{
        Installer = $installerPath
        Version = $version
        AppId = $effectiveAppId
        E2ETestBuild = [bool]$InstallerE2E
        Size = (Get-Item -LiteralPath $installerPath).Length
        Sha256 = $installerHash
        InstallerSignature = [string]$installerSignature.Status
        PluginSignature = [string]$dllSignature.Status
        InstallerCompilerVersion = $isccVersion
        HashFile = $hashFile
    } | Format-List
} finally {
    if (-not $KeepStage -and (Test-Path -LiteralPath $stageRoot)) {
        $expectedStageParent = [IO.Path]::GetFullPath((Join-Path $repoRoot "build")).TrimEnd('\') + '\'
        $stageFull = [IO.Path]::GetFullPath($stageRoot)
        if (-not $stageFull.StartsWith($expectedStageParent, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove a staging directory outside the project build directory: $stageFull"
        }
        Remove-Item -LiteralPath $stageFull -Recurse -Force
    }
}

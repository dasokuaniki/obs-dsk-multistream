[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("StagePlugin", "PrepareUninstaller", "BuildInstaller", "VerifyRelease")]
    [string]$Phase,
    [string]$BuildDir = "build\windows-x64-sdk3",
    [string]$WorkDir = "build\windows-external-signing",
    [string]$OutputDir = "release\signed",
    [string]$Configuration = "RelWithDebInfo",
    [string]$IsccPath = "",
    [switch]$Force,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$ExpectedSignerThumbprint
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "dsk-release-validation.ps1")
$expectedSignerThumbprint = $ExpectedSignerThumbprint.ToUpperInvariant()

function Get-AbsolutePath {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$BasePath)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Test-IsChildPath {
    param([Parameter(Mandatory = $true)][string]$Child, [Parameter(Mandatory = $true)][string]$Parent)

    $parentPrefix = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    return [IO.Path]::GetFullPath($Child).StartsWith($parentPrefix, [StringComparison]::OrdinalIgnoreCase)
}

function Get-VerifiedSignature {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Label)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label was not found: $Path"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    Assert-ReleaseSignature -Signature $signature -ExpectedSignerThumbprint $ExpectedSignerThumbprint -Label $Label
    return $signature
}

function Get-SingleExternalUninstaller {
    param([Parameter(Mandatory = $true)][string]$Directory)

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        throw "The external signed-uninstaller directory does not exist: $Directory"
    }
    $artifacts = @(Get-ChildItem -LiteralPath $Directory -File -Filter '*.e32')
    if ($artifacts.Count -ne 1) {
        throw "Expected exactly one external Inno uninstaller artifact, found $($artifacts.Count)."
    }
    return $artifacts[0].FullName
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceBuildRoot = Get-AbsolutePath -Path $BuildDir -BasePath $repoRoot
$workRoot = Get-AbsolutePath -Path $WorkDir -BasePath $repoRoot
$outputRoot = Get-AbsolutePath -Path $OutputDir -BasePath $repoRoot
$allowedBuildRoot = Join-Path $repoRoot "build"
$allowedReleaseRoot = Join-Path $repoRoot "release"
if (-not (Test-IsChildPath -Child $workRoot -Parent $allowedBuildRoot)) {
    throw "WorkDir must remain under the repository build directory."
}
if (-not (Test-IsChildPath -Child $outputRoot -Parent $allowedBuildRoot) -and
    -not (Test-IsChildPath -Child $outputRoot -Parent $allowedReleaseRoot)) {
    throw "OutputDir must remain under the repository build or release directory."
}

$buildSpec = Get-Content -LiteralPath (Join-Path $repoRoot "buildspec.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$version = [string]$buildSpec.version
$signedBuildRoot = Join-Path $workRoot "signed-build"
$externalUninstallerRoot = Join-Path $workRoot "signed-uninstaller"
$installerOutputRoot = Join-Path $workRoot "installer"
$signedDll = Join-Path $signedBuildRoot "obs-dsk-multistream.dll"
$installerName = "DSK-Multistream-$version-Windows-x64-Setup.exe"
$workingInstaller = Join-Path $installerOutputRoot $installerName
$statePath = Join-Path $workRoot "external-signing-state.json"

switch ($Phase) {
    "StagePlugin" {
        $sourceDll = @(
            (Join-Path $sourceBuildRoot "$Configuration\obs-dsk-multistream.dll"),
            (Join-Path $sourceBuildRoot "obs-dsk-multistream.dll")
        ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if (-not $sourceDll) {
            throw "The clean release DLL was not found under $sourceBuildRoot."
        }
        $sourceCache = Join-Path $sourceBuildRoot "CMakeCache.txt"
        if (-not (Test-Path -LiteralPath $sourceCache -PathType Leaf)) {
            throw "CMakeCache.txt was not found under the clean release build."
        }
        $oauthHeaderPath = Join-Path $sourceBuildRoot "generated\oauth-publisher-config.hpp"
        Assert-ReleaseBuildConfiguration -CachePath $sourceCache -OAuthHeaderPath $oauthHeaderPath -RequirePublisherRelease
        $sourceVersion = (Get-Item -LiteralPath $sourceDll).VersionInfo
        $expectedFileVersion = "$version.0"
        if ($sourceVersion.ProductName -ne "DSK Multistream" -or
            $sourceVersion.ProductVersion -ne $expectedFileVersion -or
            $sourceVersion.FileVersion -ne $expectedFileVersion -or
            $sourceVersion.OriginalFilename -ne "obs-dsk-multistream.dll") {
            throw "The clean DLL metadata does not match release version $version."
        }
        $sourceSignature = Get-AuthenticodeSignature -LiteralPath $sourceDll
        if ($sourceSignature.Status -ne [Management.Automation.SignatureStatus]::NotSigned) {
            throw "The clean release DLL must be unsigned before the controlled external-signing copy is created. Status=$($sourceSignature.Status)"
        }
        if (Test-Path -LiteralPath $workRoot) {
            if (-not $Force) {
                throw "The external signing work directory already exists. Use a new path, or review it and pass -Force."
            }
            if (-not (Test-IsChildPath -Child $workRoot -Parent $allowedBuildRoot)) {
                throw "Refusing to replace an external signing directory outside the project build directory."
            }
            Remove-Item -LiteralPath $workRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Force -Path $signedBuildRoot, $externalUninstallerRoot, $installerOutputRoot | Out-Null
        Copy-Item -LiteralPath $sourceDll -Destination $signedDll -Force
        Copy-Item -LiteralPath $sourceCache -Destination (Join-Path $signedBuildRoot "CMakeCache.txt") -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $signedBuildRoot "generated") | Out-Null
        Copy-Item -LiteralPath $oauthHeaderPath -Destination (Join-Path $signedBuildRoot "generated\oauth-publisher-config.hpp") -Force
        $state = [ordered]@{
            schemaVersion = 1
            product = "DSK Multistream"
            version = $version
            phase = "PluginStaged"
            cleanDllSha256 = (Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash
            stagedDllSha256 = (Get-FileHash -LiteralPath $signedDll -Algorithm SHA256).Hash
        }
        [IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false))
        [PSCustomObject]@{
            Phase = "StagePlugin"
            Version = $version
            FileToSign = $signedDll
            Sha256 = $state.stagedDllSha256
            NextPhase = "PrepareUninstaller"
        } | Format-List
    }
    "PrepareUninstaller" {
        $pluginSignature = Get-VerifiedSignature -Path $signedDll -Label "Staged plugin DLL"
        if (@(Get-ChildItem -LiteralPath $externalUninstallerRoot -File -Filter '*.e32').Count -ne 0) {
            throw "The external signed-uninstaller directory is not empty. Use a new signing work directory."
        }
        & (Join-Path $PSScriptRoot "build-windows-installer.ps1") `
            -BuildDir $signedBuildRoot -Configuration $Configuration -OutputDir $installerOutputRoot `
            -IsccPath $IsccPath -RequireValidPluginSignature -RequirePublisherRelease `
            -ExternalSignedUninstallerDir $externalUninstallerRoot -PrepareExternalSignedUninstaller
        $uninstallerArtifact = Get-SingleExternalUninstaller -Directory $externalUninstallerRoot
        $uninstallerSignature = Get-AuthenticodeSignature -LiteralPath $uninstallerArtifact
        if ($uninstallerSignature.Status -ne [Management.Automation.SignatureStatus]::NotSigned) {
            throw "The prepared external uninstaller must still be unsigned before the server signing step."
        }
        [PSCustomObject]@{
            Phase = "PrepareUninstaller"
            Version = $version
            FileToSign = $uninstallerArtifact
            Sha256 = (Get-FileHash -LiteralPath $uninstallerArtifact -Algorithm SHA256).Hash
            PluginSignature = [string]$pluginSignature.Status
            NextPhase = "BuildInstaller"
        } | Format-List
    }
    "BuildInstaller" {
        $pluginSignature = Get-VerifiedSignature -Path $signedDll -Label "Staged plugin DLL"
        $uninstallerArtifact = Get-SingleExternalUninstaller -Directory $externalUninstallerRoot
        $uninstallerSignature = Get-VerifiedSignature -Path $uninstallerArtifact -Label "External Inno uninstaller artifact"
        if ($uninstallerSignature.SignerCertificate.Thumbprint -ne $pluginSignature.SignerCertificate.Thumbprint) {
            throw "The plugin DLL and external Inno uninstaller artifact must have the same signer."
        }
        if (Test-Path -LiteralPath $workingInstaller -PathType Leaf) {
            throw "The working installer already exists. Review the work directory instead of overwriting it."
        }
        & (Join-Path $PSScriptRoot "build-windows-installer.ps1") `
            -BuildDir $signedBuildRoot -Configuration $Configuration -OutputDir $installerOutputRoot `
            -IsccPath $IsccPath -RequireValidPluginSignature -RequirePublisherRelease -RequireValidUninstallerSignature `
            -ExternalSignedUninstallerDir $externalUninstallerRoot
        if (-not (Test-Path -LiteralPath $workingInstaller -PathType Leaf)) {
            throw "The installer was not built after external uninstaller signing."
        }
        $installerSignature = Get-AuthenticodeSignature -LiteralPath $workingInstaller
        if ($installerSignature.Status -ne [Management.Automation.SignatureStatus]::NotSigned) {
            throw "The working installer must be unsigned before the final server signing step. Status=$($installerSignature.Status)"
        }
        [PSCustomObject]@{
            Phase = "BuildInstaller"
            Version = $version
            FileToSign = $workingInstaller
            Sha256 = (Get-FileHash -LiteralPath $workingInstaller -Algorithm SHA256).Hash
            NextPhase = "VerifyRelease"
        } | Format-List
    }
    "VerifyRelease" {
        $pluginSignature = Get-VerifiedSignature -Path $signedDll -Label "Staged plugin DLL"
        $uninstallerArtifact = Get-SingleExternalUninstaller -Directory $externalUninstallerRoot
        $uninstallerSignature = Get-VerifiedSignature -Path $uninstallerArtifact -Label "External Inno uninstaller artifact"
        $installerSignature = Get-VerifiedSignature -Path $workingInstaller -Label "Final installer"
        $expectedSigner = $expectedSignerThumbprint
        if ($uninstallerSignature.SignerCertificate.Thumbprint -ne $expectedSigner -or
            $installerSignature.SignerCertificate.Thumbprint -ne $expectedSigner) {
            throw "The plugin DLL, external uninstaller artifact, and installer must have the same signer."
        }
        New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
        $finalInstaller = Join-Path $outputRoot $installerName
        $finalHashFile = "$finalInstaller.sha256"
        if (-not $Force -and ((Test-Path -LiteralPath $finalInstaller) -or (Test-Path -LiteralPath $finalHashFile))) {
            throw "A verified release output already exists. Use a new output directory, or review it and pass -Force."
        }
        Copy-Item -LiteralPath $workingInstaller -Destination $finalInstaller -Force
        $finalSignature = Get-VerifiedSignature -Path $finalInstaller -Label "Copied release installer"
        if ($finalSignature.SignerCertificate.Thumbprint -ne $expectedSigner) {
            throw "The copied release installer signer changed unexpectedly."
        }
        $hash = (Get-FileHash -LiteralPath $finalInstaller -Algorithm SHA256).Hash
        [IO.File]::WriteAllText($finalHashFile, "$hash *$installerName`r`n", [Text.ASCIIEncoding]::new())
        [PSCustomObject]@{
            Phase = "VerifyRelease"
            Version = $version
            PluginSignature = [string]$pluginSignature.Status
            UninstallerSignature = [string]$uninstallerSignature.Status
            InstallerSignature = [string]$installerSignature.Status
            SignerMatches = $true
            TrustedTimestamps = $true
            Installer = $finalInstaller
            Sha256 = $hash
            HashFile = $finalHashFile
        } | Format-List
    }
}

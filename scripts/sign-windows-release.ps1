[CmdletBinding()]
param(
    [string]$BuildDir = "build\windows-x64-sdk3",
    [string]$SignedBuildDir = "build\windows-signing",
    [string]$OutputDir = "release\signed",
    [string]$Configuration = "RelWithDebInfo",
    [string]$CertificateThumbprint = "",
    [string]$TimestampUrl = "http://ts.ssl.com",
    [string]$SignToolPath = "",
    [switch]$Force,
    [switch]$PlanOnly
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "signing-certificate-utils.ps1")

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

function Resolve-SignTool {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        $resolved = Get-AbsolutePath -Path $RequestedPath -BasePath (Get-Location).Path
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "SignTool was not found: $resolved"
        }
        return $resolved
    }

    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    $kitsRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    $candidates = if (Test-Path -LiteralPath $kitsRoot -PathType Container) {
        @(Get-ChildItem -LiteralPath $kitsRoot -Directory | Sort-Object Name -Descending | ForEach-Object {
            Join-Path $_.FullName "x86\signtool.exe"
        })
    } else {
        @()
    }
    $found = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $found) {
        throw "SignTool was not found. Install the Windows SDK signing tools."
    }
    return $found
}

function Get-CodeSigningCertificate {
    param([Parameter(Mandatory = $true)][string]$Thumbprint)

    $clean = ($Thumbprint -replace '\s', '').ToUpperInvariant()
    if ($clean -notmatch '^[0-9A-F]{40}$') {
        throw "CertificateThumbprint must be a 40-character SHA-1 certificate thumbprint."
    }
    $certificate = Get-ChildItem -LiteralPath "Cert:\CurrentUser\My\$clean" -ErrorAction SilentlyContinue
    if (-not $certificate) {
        throw "The requested code-signing certificate is not loaded in CurrentUser\\My."
    }
    if (-not $certificate.HasPrivateKey) {
        throw "The requested certificate has no accessible private key. Load the eSigner CKA credential first."
    }
    $now = Get-Date
    if ($certificate.NotBefore -gt $now -or $certificate.NotAfter -le $now) {
        throw "The requested code-signing certificate is not currently valid."
    }
    $codeSigningOid = "1.3.6.1.5.5.7.3.3"
    if (-not (Test-CertificateHasEnhancedKeyUsage -Certificate $certificate -Oid $codeSigningOid)) {
        throw "The requested certificate is not valid for code signing."
    }
    return $certificate
}

function Invoke-AuthenticodeSigning {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string]$Thumbprint,
        [Parameter(Mandatory = $true)][string]$ToolPath,
        [Parameter(Mandatory = $true)][string]$Rfc3161TimestampUrl
    )

    & $ToolPath sign /fd SHA256 /tr $Rfc3161TimestampUrl /td SHA256 /sha1 $Thumbprint $FilePath
    if ($LASTEXITCODE -ne 0) {
        throw "SignTool failed for $FilePath with exit code $LASTEXITCODE."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $FilePath
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid) {
        throw "Authenticode verification failed for $FilePath. Status=$($signature.Status)"
    }
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $Thumbprint) {
        throw "The signed file was not signed by the requested certificate."
    }
    if (-not $signature.TimeStamperCertificate) {
        throw "The signed file does not contain a trusted timestamp."
    }
    return $signature
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceBuildRoot = Get-AbsolutePath -Path $BuildDir -BasePath $repoRoot
$signedBuildRoot = Get-AbsolutePath -Path $SignedBuildDir -BasePath $repoRoot
$outputRoot = Get-AbsolutePath -Path $OutputDir -BasePath $repoRoot
$signTool = Resolve-SignTool -RequestedPath $SignToolPath
$buildSpec = Get-Content -LiteralPath (Join-Path $repoRoot "buildspec.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$version = [string]$buildSpec.version

$timestamp = [Uri]$TimestampUrl
if (-not $timestamp.IsAbsoluteUri -or ($timestamp.Scheme -ne "http" -and $timestamp.Scheme -ne "https") -or
    [string]::IsNullOrWhiteSpace($timestamp.Host)) {
    throw "TimestampUrl must be an absolute HTTP or HTTPS URL."
}
$allowedBuildParent = Join-Path $repoRoot "build"
$allowedReleaseParent = Join-Path $repoRoot "release"
if (-not (Test-IsChildPath -Child $signedBuildRoot -Parent $allowedBuildParent)) {
    throw "SignedBuildDir must remain under the repository build directory."
}
if (-not (Test-IsChildPath -Child $outputRoot -Parent $allowedBuildParent) -and
    -not (Test-IsChildPath -Child $outputRoot -Parent $allowedReleaseParent)) {
    throw "OutputDir must remain under the repository build or release directory."
}

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
$cacheText = Get-Content -LiteralPath $sourceCache -Raw -Encoding UTF8
if ($cacheText -notmatch '(?m)^DSK_INCLUDE_E2E_HOOKS:BOOL=OFF\s*$') {
    throw "Release signing refuses a build with E2E hooks enabled."
}
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
    throw "The clean release DLL must be unsigned before the controlled signing copy is created. Status=$($sourceSignature.Status)"
}

if ($PlanOnly) {
    [PSCustomObject]@{
        ReadyForCertificateEnrollment = $true
        Version = $version
        CleanDll = (Resolve-Path -LiteralPath $sourceDll).Path
        CleanDllSha256 = (Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash
        SignTool = $signTool
        TimestampUrl = $TimestampUrl
        RequiredOrder = "plugin DLL, generated uninstaller, installer EXE"
    } | ConvertTo-Json -Depth 3
    return
}

$certificate = Get-CodeSigningCertificate -Thumbprint $CertificateThumbprint
$thumbprint = $certificate.Thumbprint.ToUpperInvariant()
$signedDll = Join-Path $signedBuildRoot "obs-dsk-multistream.dll"
$installer = Join-Path $outputRoot "DSK-Multistream-$version-Windows-x64-Setup.exe"
$hashPath = "$installer.sha256"
$existingOutputs = @(@($signedDll, $installer, $hashPath) | Where-Object { Test-Path -LiteralPath $_ })
if (-not $Force -and $existingOutputs.Count -gt 0) {
    throw "A signing output already exists. Use a new output directory, or review it and pass -Force to replace the fixed release files."
}
New-Item -ItemType Directory -Force -Path $signedBuildRoot, $outputRoot | Out-Null
Copy-Item -LiteralPath $sourceCache -Destination (Join-Path $signedBuildRoot "CMakeCache.txt") -Force
Copy-Item -LiteralPath $sourceDll -Destination $signedDll -Force

$pluginSignature = Invoke-AuthenticodeSigning -FilePath $signedDll -Thumbprint $thumbprint `
    -ToolPath $signTool -Rfc3161TimestampUrl $TimestampUrl

$innoSignToolCommand = '"{0}" sign /fd SHA256 /tr "{1}" /td SHA256 /sha1 {2} $f' -f `
    $signTool, $TimestampUrl, $thumbprint

& (Join-Path $repoRoot "scripts\build-windows-installer.ps1") `
    -BuildDir $signedBuildRoot -Configuration $Configuration -OutputDir $outputRoot `
    -RequireValidPluginSignature -RequireValidInstallerSignature `
    -RequireValidUninstallerSignature -InnoSignToolCommand $innoSignToolCommand
if ($LASTEXITCODE -ne 0) {
    throw "Installer packaging failed after the plugin was signed."
}

if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "The expected installer was not created: $installer"
}
$installerSignature = Get-AuthenticodeSignature -LiteralPath $installer
if ($installerSignature.Status -ne [Management.Automation.SignatureStatus]::Valid) {
    throw "Authenticode verification failed for $installer. Status=$($installerSignature.Status)"
}
if (-not $installerSignature.SignerCertificate -or
    $installerSignature.SignerCertificate.Thumbprint -ne $thumbprint) {
    throw "The installer was not signed by the requested certificate."
}
if (-not $installerSignature.TimeStamperCertificate) {
    throw "The installer does not contain a trusted timestamp."
}

$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
[IO.File]::WriteAllText($hashPath, "$hash *$([IO.Path]::GetFileName($installer))`r`n", [Text.ASCIIEncoding]::new())

[PSCustomObject]@{
    Version = $version
    SignerSubject = $certificate.Subject
    SignerThumbprint = $thumbprint
    Plugin = $signedDll
    PluginSignature = [string]$pluginSignature.Status
    Installer = $installer
    InstallerSignature = [string]$installerSignature.Status
    InstallerSha256 = $hash
    HashFile = $hashPath
    TimestampUrl = $TimestampUrl
} | ConvertTo-Json -Depth 3

param(
    [string]$BuildDir = "build\windows-x64-sdk3",
    [string]$ObsPluginRoot = "$env:ProgramData\obs-studio\plugins\obs-dsk-multistream",
    [string]$ObsPluginScanRoot = "",
    [string]$ObsRuntimeDir = "$env:ProgramFiles\obs-studio\bin\64bit",
    [string]$InstalledDllName = "obs-dsk-multistream.dll",
    [switch]$RequireValidSignature
)

$ErrorActionPreference = "Stop"

if ($InstalledDllName -notmatch "^[A-Za-z0-9._-]+\.dll$") {
    throw "InstalledDllName must be a plain DLL file name: $InstalledDllName"
}

function Require-File {
    param(
        [string]$Path,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Name was not found: $Path"
    }
}

function Read-LocaleKeys {
    param([string]$Path)

    $keys = @{}
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
            continue
        }

        $parts = $line.Split("=", 2)
        if ($parts.Length -ne 2 -or [string]::IsNullOrWhiteSpace($parts[0])) {
            throw "Invalid locale line in ${Path}: $line"
        }

        if ($keys.ContainsKey($parts[0])) {
            throw "Duplicate locale key in ${Path}: $($parts[0])"
        }

        $keys[$parts[0]] = $true
    }

    return $keys
}

function Get-NormalizedPeHash {
    param([string]$Path)

    [byte[]]$bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path).Path)
    if ($bytes.Length -lt 256) {
        throw "PE file is too small: $Path"
    }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    $optionalHeader = $peOffset + 24
    if ($peOffset -lt 0 -or $optionalHeader + 160 -gt $bytes.Length -or
        $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45) {
        throw "Invalid PE header: $Path"
    }

    $magic = [BitConverter]::ToUInt16($bytes, $optionalHeader)
    if ($magic -eq 0x20b) {
        $dataDirectory = $optionalHeader + 112
    } elseif ($magic -eq 0x10b) {
        $dataDirectory = $optionalHeader + 96
    } else {
        throw "Unsupported PE optional header: $Path"
    }

    $checksumOffset = $optionalHeader + 64
    $securityDirectory = $dataDirectory + (4 * 8)
    $certificateOffset = [BitConverter]::ToInt32($bytes, $securityDirectory)
    $certificateSize = [BitConverter]::ToInt32($bytes, $securityDirectory + 4)

    [Array]::Clear($bytes, $checksumOffset, 4)
    [Array]::Clear($bytes, $securityDirectory, 8)

    [byte[]]$normalized = $bytes
    if ($certificateOffset -gt 0 -and $certificateSize -gt 0 -and
        $certificateOffset + $certificateSize -le $bytes.Length) {
        $normalized = New-Object byte[] ($bytes.Length - $certificateSize)
        [Array]::Copy($bytes, 0, $normalized, 0, $certificateOffset)
        $tailLength = $bytes.Length - ($certificateOffset + $certificateSize)
        if ($tailLength -gt 0) {
            [Array]::Copy($bytes, $certificateOffset + $certificateSize, $normalized, $certificateOffset, $tailLength)
        }
    }

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($normalized))).Replace("-", "")
    } finally {
        $sha.Dispose()
    }
}

$dllCandidates = @(
    (Join-Path $BuildDir "obs-dsk-multistream.dll"),
    (Join-Path $BuildDir "RelWithDebInfo\obs-dsk-multistream.dll")
)
$builtDll = $dllCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $builtDll) {
    throw "Built plugin DLL was not found under $BuildDir"
}

$platformsPath = "data\presets\platforms.json"
Require-File $platformsPath "Platform preset file"
$platformsJson = Get-Content -LiteralPath $platformsPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($platformsJson.schemaVersion -ne 1) {
    throw "Unexpected platform preset schemaVersion: $($platformsJson.schemaVersion)"
}

$platformIds = @{}
foreach ($platform in $platformsJson.platforms) {
    foreach ($field in @("id", "name", "defaultServer", "verticalCommon")) {
        if (-not $platform.PSObject.Properties[$field]) {
            throw "Platform preset is missing field '$field'"
        }
    }
    if ($platformIds.ContainsKey($platform.id)) {
        throw "Duplicate platform id: $($platform.id)"
    }
    $platformIds[$platform.id] = $true
}

foreach ($required in @("twitch", "youtube", "kick", "custom")) {
    if (-not $platformIds.ContainsKey($required)) {
        throw "Missing required platform preset: $required"
    }
}

$enKeys = Read-LocaleKeys "data\locale\en-US.ini"
$jaKeys = Read-LocaleKeys "data\locale\ja-JP.ini"
foreach ($key in $enKeys.Keys) {
    if (-not $jaKeys.ContainsKey($key)) {
        throw "ja-JP locale is missing key: $key"
    }
}
foreach ($key in $jaKeys.Keys) {
    if (-not $enKeys.ContainsKey($key)) {
        throw "en-US locale is missing key: $key"
    }
}

$installedDll = Join-Path (Join-Path $ObsPluginRoot "bin\64bit") $InstalledDllName
$installedEn = Join-Path $ObsPluginRoot "data\locale\en-US.ini"
$installedJa = Join-Path $ObsPluginRoot "data\locale\ja-JP.ini"
$installedPresets = Join-Path $ObsPluginRoot "data\presets\platforms.json"
$installedTlsDir = Join-Path (Join-Path $ObsPluginRoot "bin\64bit") "tls"
$obsLibcurl = Join-Path $ObsRuntimeDir "libcurl.dll"

Require-File $installedDll "Installed plugin DLL"
Require-File $installedEn "Installed en-US locale"
Require-File $installedJa "Installed ja-JP locale"
Require-File $installedPresets "Installed platform presets"
Require-File $obsLibcurl "OBS signed libcurl runtime"

$obsoleteTlsPlugins = @()
if (Test-Path -LiteralPath $installedTlsDir -PathType Container) {
    $obsoleteTlsPlugins = @(Get-ChildItem -LiteralPath $installedTlsDir -Filter "*.dll" -File -ErrorAction SilentlyContinue)
}
if ($obsoleteTlsPlugins.Count -gt 0) {
    throw "The plugin package contains obsolete Qt TLS plugins. DSK HTTPS uses OBS' signed libcurl runtime."
}

$builtHash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
$installedHash = (Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash
$builtContentHash = Get-NormalizedPeHash $builtDll
$installedContentHash = Get-NormalizedPeHash $installedDll
$builtSignature = Get-AuthenticodeSignature -LiteralPath $builtDll
$installedSignature = Get-AuthenticodeSignature -LiteralPath $installedDll
$httpRuntimeSignature = Get-AuthenticodeSignature -LiteralPath $obsLibcurl
if ($builtContentHash -ne $installedContentHash) {
    throw "Installed plugin DLL does not match the built DLL."
}
if ($httpRuntimeSignature.Status -ne [Management.Automation.SignatureStatus]::Valid) {
    throw "OBS libcurl runtime must have a valid Authenticode signature. Status=$($httpRuntimeSignature.Status)"
}
if ($RequireValidSignature -and
    ($builtSignature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
     $installedSignature.Status -ne [Management.Automation.SignatureStatus]::Valid)) {
    throw "A valid Authenticode signature is required for this package. Built=$($builtSignature.Status), installed=$($installedSignature.Status)"
}

$scanRoot = $ObsPluginScanRoot
if ([string]::IsNullOrWhiteSpace($scanRoot)) {
    $scanRoot = Split-Path -Parent $ObsPluginRoot
}

$duplicateDlls = @()
if ($scanRoot -and (Test-Path -LiteralPath $scanRoot)) {
    $duplicateDlls = @(Get-ChildItem -LiteralPath $scanRoot -Recurse -Filter $InstalledDllName -ErrorAction SilentlyContinue)
    if ($duplicateDlls.Count -ne 1) {
        $paths = ($duplicateDlls | Select-Object -ExpandProperty FullName) -join [Environment]::NewLine
        throw "Expected exactly one $InstalledDllName under OBS plugin scan root '$scanRoot', found $($duplicateDlls.Count):$([Environment]::NewLine)$paths"
    }

    $installedResolved = (Resolve-Path -LiteralPath $installedDll).Path
    if ($duplicateDlls[0].FullName -ne $installedResolved) {
        throw "The only DSK DLL under OBS plugin scan root is not the installed DLL: $($duplicateDlls[0].FullName)"
    }
}

[PSCustomObject]@{
    BuiltDll = (Resolve-Path -LiteralPath $builtDll).Path
    InstalledDll = (Resolve-Path -LiteralPath $installedDll).Path
    ScanRoot = $scanRoot
    DskDllsInScanRoot = $duplicateDlls.Count
    Platforms = $platformsJson.platforms.Count
    LocaleKeys = $enKeys.Count
    BundledTlsPlugins = $obsoleteTlsPlugins.Count
    HttpRuntime = (Resolve-Path -LiteralPath $obsLibcurl).Path
    HttpRuntimeSignature = [string]$httpRuntimeSignature.Status
    BuiltSha256 = $builtHash
    InstalledSha256 = $installedHash
    NormalizedPeSha256 = $builtContentHash
    BuiltSignature = [string]$builtSignature.Status
    InstalledSignature = [string]$installedSignature.Status
    SignatureRequired = [bool]$RequireValidSignature
} | Format-List

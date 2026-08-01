param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,
    [string]$ExpectedVersion = "",
    [switch]$RequireValidSignature,
    [switch]$AllowInnoUninstaller
)

$ErrorActionPreference = "Stop"

$root = [IO.Path]::GetFullPath($PackageRoot)
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "Installer package root was not found: $root"
}

$requiredFiles = @(
    "bin\64bit\obs-dsk-multistream.dll",
    "data\locale\en-US.ini",
    "data\locale\ja-JP.ini",
    "data\presets\platforms.json",
    "data\ui\settings-button.png",
    "docs\beta-distribution.md",
    "docs\privacy.md",
    "docs\third-party-notices.md",
    "docs\simulcast-guidelines.md",
    "tools\remove-user-data.ps1",
    "LICENSE",
    "dsk-package-manifest.json"
)
foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Installer package is missing '$relativePath': $path"
    }
}

$forbiddenFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @(".pdb", ".ilk", ".lib", ".exp", ".obj") -or
    $_.Name -match '(^|-)tests?\.exe$'
})
if ($forbiddenFiles.Count -gt 0) {
    $paths = ($forbiddenFiles | Select-Object -ExpandProperty FullName) -join [Environment]::NewLine
    throw "Installer package contains development-only files:$([Environment]::NewLine)$paths"
}

$manifestPath = Join-Path $root "dsk-package-manifest.json"
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    throw "Unexpected installer manifest schemaVersion: $($manifest.schemaVersion)"
}
if ($manifest.name -ne "obs-dsk-multistream") {
    throw "Unexpected installer manifest name: $($manifest.name)"
}
if ($manifest.architecture -ne "windows-x64") {
    throw "Unexpected installer architecture: $($manifest.architecture)"
}
if ($ExpectedVersion -and $manifest.version -ne $ExpectedVersion) {
    throw "Installer manifest version '$($manifest.version)' does not match expected version '$ExpectedVersion'."
}

$manifestPaths = @{}
foreach ($entry in $manifest.files) {
    $relativePath = ([string]$entry.path).Replace('/', [IO.Path]::DirectorySeparatorChar)
    if ([string]::IsNullOrWhiteSpace($relativePath) -or [IO.Path]::IsPathRooted($relativePath) -or
        $relativePath.Split([IO.Path]::DirectorySeparatorChar) -contains "..") {
        throw "Installer manifest contains an unsafe path: $($entry.path)"
    }
    if ($manifestPaths.ContainsKey($relativePath)) {
        throw "Installer manifest contains a duplicate path: $relativePath"
    }
    $manifestPaths[$relativePath] = $true

    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Installer manifest references a missing file: $relativePath"
    }
    $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actualHash -ne [string]$entry.sha256) {
        throw "Installer manifest hash mismatch for '$relativePath'."
    }
    if ((Get-Item -LiteralPath $path).Length -ne [long]$entry.size) {
        throw "Installer manifest size mismatch for '$relativePath'."
    }
}

$payloadFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $isInnoUninstallerFile = $AllowInnoUninstaller -and $_.Name -match '^unins\d{3}\.(exe|dat|msg)$'
    $_.FullName -ne $manifestPath -and -not $isInnoUninstallerFile
})
foreach ($file in $payloadFiles) {
    $relativePath = $file.FullName.Substring($root.TrimEnd('\').Length + 1)
    if (-not $manifestPaths.ContainsKey($relativePath)) {
        throw "Installer payload file is not covered by the manifest: $relativePath"
    }
}
if ($manifestPaths.Count -ne $payloadFiles.Count) {
    throw "Installer manifest file count does not match the staged payload."
}

$dll = Join-Path $root "bin\64bit\obs-dsk-multistream.dll"
$signature = Get-AuthenticodeSignature -LiteralPath $dll
if ($RequireValidSignature -and $signature.Status -ne [Management.Automation.SignatureStatus]::Valid) {
    throw "The staged plugin DLL must have a valid Authenticode signature. Status=$($signature.Status)"
}

[PSCustomObject]@{
    PackageRoot = $root
    Version = [string]$manifest.version
    PayloadFiles = $payloadFiles.Count
    DllSha256 = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
    DllSignature = [string]$signature.Status
    UserSettingsIncluded = $false
} | Format-List

param(
    [string]$BuildDir = "build\windows-x64-sdk3",
    [string]$Configuration = "RelWithDebInfo",
    [string]$ObsPluginRoot = "$env:ProgramData\obs-studio\plugins\obs-dsk-multistream",
    [string]$InstalledDllName = "obs-dsk-multistream.dll",
    [switch]$AllowRunningObs
)

$ErrorActionPreference = "Stop"

function Get-AbsolutePath {
    param([string]$Path, [string]$BasePath)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Assert-ChildPath {
    param([string]$Path, [string]$Parent, [string]$Label)

    $parentFull = [IO.Path]::GetFullPath($Parent).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $pathFull = [IO.Path]::GetFullPath($Path)
    $prefix = $parentFull + [IO.Path]::DirectorySeparatorChar
    if (-not $pathFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escaped the intended plugin parent directory: $pathFull"
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = Get-AbsolutePath -Path $BuildDir -BasePath $repoRoot
$targetRoot = Get-AbsolutePath -Path $ObsPluginRoot -BasePath $repoRoot
$defaultInstalledRoot = [IO.Path]::GetFullPath("$env:ProgramData\obs-studio\plugins\obs-dsk-multistream")
$obsRunning = [bool](Get-Process -Name "obs64" -ErrorAction SilentlyContinue)

if ($InstalledDllName -notmatch "^[A-Za-z0-9._-]+\.dll$") {
    throw "InstalledDllName must be a plain DLL file name: $InstalledDllName"
}

if ($obsRunning) {
    if (-not $AllowRunningObs) {
        throw "OBS is running. Close OBS before installing the plugin, or use -AllowRunningObs only for an isolated staging directory."
    }
    if ($targetRoot.Equals($defaultInstalledRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace the installed plugin while OBS is running. -AllowRunningObs is valid only with an isolated -ObsPluginRoot."
    }
}

$candidates = @(
    (Join-Path $buildRoot "$Configuration\obs-dsk-multistream.dll"),
    (Join-Path $buildRoot "obs-dsk-multistream.dll")
)
$dll = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $dll) {
    throw "obs-dsk-multistream.dll was not found under $buildRoot. Build the plugin first."
}
$dll = (Resolve-Path -LiteralPath $dll).Path

$localeSource = Join-Path $repoRoot "data\locale"
$presetsSource = Join-Path $repoRoot "data\presets"
$uiSource = Join-Path $repoRoot "data\ui"
foreach ($requiredDirectory in @($localeSource, $presetsSource, $uiSource)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required plugin data directory was not found: $requiredDirectory"
    }
}

$cmakeProject = Get-Content -LiteralPath (Join-Path $repoRoot "CMakeLists.txt") -Raw -Encoding UTF8
if ($cmakeProject -notmatch 'project\(obs-dsk-multistream\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    throw "Could not determine the plugin version from CMakeLists.txt."
}
$pluginVersion = $Matches[1]

$pluginParent = Split-Path -Parent $targetRoot
$pluginLeaf = Split-Path -Leaf $targetRoot
if ([string]::IsNullOrWhiteSpace($pluginLeaf)) {
    throw "Invalid OBS plugin destination: $targetRoot"
}

New-Item -ItemType Directory -Force -Path $pluginParent | Out-Null
$transactionId = "$PID-$([Guid]::NewGuid().ToString('N'))"
$stageRoot = Join-Path $pluginParent ".$pluginLeaf.stage-$transactionId"
$backupRoot = Join-Path $pluginParent ".$pluginLeaf.backup-$transactionId"
Assert-ChildPath -Path $stageRoot -Parent $pluginParent -Label "Staging path"
Assert-ChildPath -Path $backupRoot -Parent $pluginParent -Label "Backup path"

$stageExists = $false
$oldMoved = $false
try {
    $stageBinDir = Join-Path $stageRoot "bin\64bit"
    $stageDataDir = Join-Path $stageRoot "data"
    New-Item -ItemType Directory -Force -Path $stageBinDir | Out-Null
    New-Item -ItemType Directory -Force -Path $stageDataDir | Out-Null
    $stageExists = $true

    Copy-Item -LiteralPath $dll -Destination (Join-Path $stageBinDir $InstalledDllName) -Force
    Copy-Item -LiteralPath $localeSource -Destination $stageDataDir -Recurse -Force
    Copy-Item -LiteralPath $presetsSource -Destination $stageDataDir -Recurse -Force
    Copy-Item -LiteralPath $uiSource -Destination $stageDataDir -Recurse -Force

    # Developer installs must not remove an existing distribution installer's
    # uninstall registration files when refreshing only the plugin payload.
    $preservedInstallerFiles = @("unins000.exe", "unins000.dat")
    if (Test-Path -LiteralPath $targetRoot -PathType Container) {
        foreach ($fileName in $preservedInstallerFiles) {
            $existingFile = Join-Path $targetRoot $fileName
            if (Test-Path -LiteralPath $existingFile -PathType Leaf) {
                Copy-Item -LiteralPath $existingFile -Destination (Join-Path $stageRoot $fileName) -Force
            }
        }
    }

    $manifestEntries = Get-ChildItem -LiteralPath $stageRoot -File -Recurse |
        Where-Object { $_.Name -notin $preservedInstallerFiles -and $_.Name -ne "dsk-package-manifest.json" } |
        Sort-Object FullName |
        ForEach-Object {
            [ordered]@{
                path = $_.FullName.Substring($stageRoot.Length + 1).Replace('\', '/')
                size = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        }
    $manifest = [ordered]@{
        schemaVersion = 1
        name = "obs-dsk-multistream"
        version = $pluginVersion
        architecture = "windows-x64"
        minimumObsVersion = "32.0.0"
        files = @($manifestEntries)
    }
    [IO.File]::WriteAllText(
        (Join-Path $stageRoot "dsk-package-manifest.json"),
        ($manifest | ConvertTo-Json -Depth 5),
        (New-Object Text.UTF8Encoding($false)))

    if (Test-Path -LiteralPath $targetRoot) {
        Move-Item -LiteralPath $targetRoot -Destination $backupRoot
        $oldMoved = $true
    }

    try {
        Move-Item -LiteralPath $stageRoot -Destination $targetRoot
        $stageExists = $false
    } catch {
        if ($oldMoved -and -not (Test-Path -LiteralPath $targetRoot) -and (Test-Path -LiteralPath $backupRoot)) {
            Move-Item -LiteralPath $backupRoot -Destination $targetRoot
            $oldMoved = $false
        }
        throw
    }
} catch {
    if ($stageExists -and (Test-Path -LiteralPath $stageRoot)) {
        Assert-ChildPath -Path $stageRoot -Parent $pluginParent -Label "Staging cleanup path"
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    if ($oldMoved -and -not (Test-Path -LiteralPath $targetRoot) -and (Test-Path -LiteralPath $backupRoot)) {
        Move-Item -LiteralPath $backupRoot -Destination $targetRoot
    }
    throw "DSK plugin installation failed without leaving a partial package. Detail: $($_.Exception.Message)"
}

if ($oldMoved -and (Test-Path -LiteralPath $backupRoot)) {
    try {
        Assert-ChildPath -Path $backupRoot -Parent $pluginParent -Label "Backup cleanup path"
        Remove-Item -LiteralPath $backupRoot -Recurse -Force
    } catch {
        Write-Warning "The plugin was installed, but its transaction backup could not be removed: $backupRoot"
    }
}

Write-Host "Installed DSK Multistream atomically to $targetRoot"

param(
    [string]$BuildDir = "build\windows-x64-sdk3",
    [string]$Configuration = "RelWithDebInfo",
    [string]$ObsPluginRoot = "$env:ProgramData\obs-studio\plugins\obs-dsk-multistream",
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
foreach ($requiredDirectory in @($localeSource, $presetsSource)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required plugin data directory was not found: $requiredDirectory"
    }
}

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

    Copy-Item -LiteralPath $dll -Destination (Join-Path $stageBinDir "obs-dsk-multistream.dll") -Force
    Copy-Item -LiteralPath $localeSource -Destination $stageDataDir -Recurse -Force
    Copy-Item -LiteralPath $presetsSource -Destination $stageDataDir -Recurse -Force
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

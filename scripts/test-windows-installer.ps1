param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,
    [string]$TestRoot = "build\installer-e2e",
    [string]$DisplayName = "DSK Multistream for OBS (Installer E2E)"
)

$ErrorActionPreference = "Stop"

function Get-AbsolutePath {
    param([string]$Path, [string]$BasePath)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Get-DskUninstallEntries {
    $registryPaths = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKCU:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*"
    )
    return @(Get-ItemProperty -Path $registryPaths -ErrorAction SilentlyContinue | Where-Object {
        $_.DisplayName -eq $DisplayName
    })
}

function Invoke-CheckedProcess {
    param([string]$FilePath, [string[]]$ArgumentList)

    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Process failed with exit code $($process.ExitCode): $FilePath $($ArgumentList -join ' ')"
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$installer = Get-AbsolutePath -Path $InstallerPath -BasePath $repoRoot
$testBase = Get-AbsolutePath -Path $TestRoot -BasePath $repoRoot
$buildParent = [IO.Path]::GetFullPath((Join-Path $repoRoot "build")).TrimEnd('\') + '\'
if (-not $testBase.StartsWith($buildParent, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Installer E2E test root must stay inside the project build directory: $testBase"
}
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Installer was not found: $installer"
}

$installRoot = Join-Path $testBase "obs-dsk-multistream"
$settingsSentinel = Join-Path $testBase "user-settings-preserved.txt"
$existingEntries = @(Get-DskUninstallEntries)
$foreignEntries = @($existingEntries | Where-Object {
    $_.InstallLocation -and -not ([IO.Path]::GetFullPath($_.InstallLocation).Equals($installRoot, [StringComparison]::OrdinalIgnoreCase))
})
if ($foreignEntries.Count -gt 0) {
    throw "A non-test DSK installer registration already exists. Refusing to replace it during E2E testing."
}

if (Test-Path -LiteralPath $testBase) {
    Remove-Item -LiteralPath $testBase -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $testBase | Out-Null
[IO.File]::WriteAllText($settingsSentinel, "preserve-me", (New-Object Text.UTF8Encoding($false)))
$sentinelHash = (Get-FileHash -LiteralPath $settingsSentinel -Algorithm SHA256).Hash

$installLog = Join-Path $testBase "install.log"
$upgradeLog = Join-Path $testBase "upgrade.log"
$uninstallLog = Join-Path $testBase "uninstall.log"
$commonSetupArguments = @(
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART",
    "/NOCLOSEAPPLICATIONS",
    "/DIR=$installRoot"
)

try {
    Invoke-CheckedProcess -FilePath $installer -ArgumentList ($commonSetupArguments + "/LOG=$installLog")
    & (Join-Path $PSScriptRoot "validate-installer-layout.ps1") -PackageRoot $installRoot -AllowInnoUninstaller

    $entriesAfterInstall = @(Get-DskUninstallEntries)
    if ($entriesAfterInstall.Count -ne 1) {
        throw "Expected one Windows uninstall registration after install, found $($entriesAfterInstall.Count)."
    }

    Invoke-CheckedProcess -FilePath $installer -ArgumentList ($commonSetupArguments + "/LOG=$upgradeLog")
    & (Join-Path $PSScriptRoot "validate-installer-layout.ps1") -PackageRoot $installRoot -AllowInnoUninstaller
    $entriesAfterUpgrade = @(Get-DskUninstallEntries)
    if ($entriesAfterUpgrade.Count -ne 1) {
        throw "Expected one Windows uninstall registration after upgrade, found $($entriesAfterUpgrade.Count)."
    }

    $uninstaller = Join-Path $installRoot "unins000.exe"
    if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        throw "Inno Setup uninstaller was not created: $uninstaller"
    }
    Invoke-CheckedProcess -FilePath $uninstaller -ArgumentList @(
        "/VERYSILENT",
        "/SUPPRESSMSGBOXES",
        "/NORESTART",
        "/LOG=$uninstallLog"
    )

    if (Test-Path -LiteralPath (Join-Path $installRoot "bin\64bit\obs-dsk-multistream.dll")) {
        throw "The plugin DLL remained after uninstall."
    }
    if (Test-Path -LiteralPath $installRoot) {
        $remainingFiles = @(Get-ChildItem -LiteralPath $installRoot -Recurse -Force)
        if ($remainingFiles.Count -gt 0) {
            $remainingPaths = ($remainingFiles | Select-Object -ExpandProperty FullName) -join [Environment]::NewLine
            throw "The dedicated plugin directory was not fully removed:$([Environment]::NewLine)$remainingPaths"
        }
        throw "The empty dedicated plugin directory remained after uninstall: $installRoot"
    }
    if (@(Get-DskUninstallEntries).Count -ne 0) {
        throw "The Windows uninstall registration remained after uninstall."
    }
    if (-not (Test-Path -LiteralPath $settingsSentinel -PathType Leaf) -or
        (Get-FileHash -LiteralPath $settingsSentinel -Algorithm SHA256).Hash -ne $sentinelHash) {
        throw "The uninstall test modified data outside the dedicated plugin directory."
    }

    [PSCustomObject]@{
        Installer = $installer
        InstallRoot = $installRoot
        FreshInstall = "Passed"
        InPlaceUpgrade = "Passed"
        Uninstall = "Passed"
        UserDataPreserved = "Passed"
        InstallLog = $installLog
        UpgradeLog = $upgradeLog
        UninstallLog = $uninstallLog
    } | Format-List
} catch {
    $uninstaller = Join-Path $installRoot "unins000.exe"
    if (Test-Path -LiteralPath $uninstaller -PathType Leaf) {
        try {
            Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") -Wait | Out-Null
        } catch {
            Write-Warning "Failed to clean up the E2E installer registration: $($_.Exception.Message)"
        }
    }
    throw
}

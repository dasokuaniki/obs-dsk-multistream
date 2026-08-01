[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "High")]
param(
    [string]$ProfilesRoot = "$env:APPDATA\obs-studio\basic\profiles",
    [string]$ModuleConfigRoot = "$env:APPDATA\obs-studio\plugin_config\obs-dsk-multistream",
    [string]$CredentialPrefix = "DSK Multistream/",
    [switch]$Force,
    [switch]$TestMode,
    [string]$TestRoot = ""
)

$ErrorActionPreference = "Stop"
if ($Force) {
    $ConfirmPreference = "None"
}

function Get-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($Path)).TrimEnd('\')
}

function Test-IsChildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Child,
        [Parameter(Mandatory = $true)][string]$Parent
    )

    $parentPrefix = $Parent.TrimEnd('\') + '\'
    return $Child.StartsWith($parentPrefix, [StringComparison]::OrdinalIgnoreCase)
}

$profilesPath = Get-NormalizedPath $ProfilesRoot
$moduleConfigPath = Get-NormalizedPath $ModuleConfigRoot
$repoRoot = Get-NormalizedPath (Join-Path $PSScriptRoot "..")

if ($TestMode) {
    if ([string]::IsNullOrWhiteSpace($TestRoot)) {
        throw "Refusing test-mode removal without an explicit TestRoot."
    }
    $testPath = Get-NormalizedPath $TestRoot
    $buildPath = Get-NormalizedPath (Join-Path $repoRoot "build")
    $scriptPath = Get-NormalizedPath $PSScriptRoot
    $sourceTreeTest = Test-IsChildPath -Child $testPath -Parent $buildPath
    $installedE2eTest = Test-IsChildPath -Child $scriptPath -Parent $testPath
    if (-not $sourceTreeTest -and -not $installedE2eTest) {
        throw "Refusing test-mode removal outside an isolated source-build or installed E2E root: $testPath"
    }
    if (-not (Test-IsChildPath -Child $profilesPath -Parent $testPath) -or
        -not (Test-IsChildPath -Child $moduleConfigPath -Parent $testPath)) {
        throw "Refusing test-mode removal because the requested data roots are outside TestRoot."
    }
    if ($CredentialPrefix -cne "DSK Multistream E2E/") {
        throw "Refusing test-mode removal for a non-E2E credential prefix."
    }
}
else {
    $expectedProfiles = Get-NormalizedPath "$env:APPDATA\obs-studio\basic\profiles"
    $expectedModuleConfig = Get-NormalizedPath "$env:APPDATA\obs-studio\plugin_config\obs-dsk-multistream"
    if (-not $profilesPath.Equals($expectedProfiles, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing removal from a non-standard OBS profiles directory: $profilesPath"
    }
    if (-not $moduleConfigPath.Equals($expectedModuleConfig, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing removal from a directory not owned by obs-dsk-multistream: $moduleConfigPath"
    }
    if ($CredentialPrefix -cne "DSK Multistream/") {
        throw "Refusing removal for a credential prefix not owned by DSK Multistream."
    }
}

if (-not ("DskRemovalCredentialNative" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class DskRemovalCredentialNative
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct CREDENTIAL
    {
        public UInt32 Flags;
        public UInt32 Type;
        public IntPtr TargetName;
        public IntPtr Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public UInt32 CredentialBlobSize;
        public IntPtr CredentialBlob;
        public UInt32 Persist;
        public UInt32 AttributeCount;
        public IntPtr Attributes;
        public IntPtr TargetAlias;
        public IntPtr UserName;
    }

    [DllImport("advapi32.dll", EntryPoint = "CredEnumerateW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredEnumerate(string filter, UInt32 flags, out UInt32 count, out IntPtr credentials);

    [DllImport("advapi32.dll", EntryPoint = "CredDeleteW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredDelete(string target, UInt32 type, UInt32 flags);

    [DllImport("advapi32.dll", SetLastError = false)]
    private static extern void CredFree(IntPtr buffer);

    public static string[] Enumerate(string filter)
    {
        UInt32 count;
        IntPtr credentials;
        if (!CredEnumerate(filter, 0, out count, out credentials)) {
            int error = Marshal.GetLastWin32Error();
            if (error == 1168)
                return new string[0];
            throw new System.ComponentModel.Win32Exception(error);
        }

        var result = new List<string>();
        try {
            for (int index = 0; index < count; ++index) {
                IntPtr pointer = Marshal.ReadIntPtr(credentials, index * IntPtr.Size);
                CREDENTIAL credential = Marshal.PtrToStructure<CREDENTIAL>(pointer);
                string target = Marshal.PtrToStringUni(credential.TargetName);
                if (!String.IsNullOrEmpty(target))
                    result.Add(target);
            }
        }
        finally {
            CredFree(credentials);
        }
        return result.ToArray();
    }

    public static void Delete(string target)
    {
        const UInt32 GenericCredential = 1;
        if (!CredDelete(target, GenericCredential, 0)) {
            int error = Marshal.GetLastWin32Error();
            if (error != 1168)
                throw new System.ComponentModel.Win32Exception(error);
        }
    }
}
"@
}

$settingsFiles = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
if (Test-Path -LiteralPath $profilesPath -PathType Container) {
    $profilesRootItem = Get-Item -LiteralPath $profilesPath -Force
    if (($profilesRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to traverse a reparse-point OBS profiles root."
    }
    foreach ($profileDirectory in @(Get-ChildItem -LiteralPath $profilesPath -Directory -Force)) {
        if (($profileDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to traverse a reparse-point OBS profile directory."
        }
        foreach ($file in @(Get-ChildItem -LiteralPath $profileDirectory.FullName -File -Force)) {
            if ($file.Name -eq "dsk-multistream.json" -or
                $file.Name -eq "dsk-multistream.json.bak" -or
                $file.Name -match '^dsk-multistream\.json\.corrupt-[0-9-]+\.json$') {
                $settingsFiles.Add($file)
            }
        }
    }
}

if (Test-Path -LiteralPath $moduleConfigPath) {
    $moduleItem = Get-Item -LiteralPath $moduleConfigPath -Force
    if (-not $moduleItem.PSIsContainer) {
        throw "Refusing removal because the DSK module configuration path is not a directory."
    }
    $reparseItems = @($moduleItem) + @(Get-ChildItem -LiteralPath $moduleConfigPath -Recurse -Force)
    if (@($reparseItems | Where-Object {
        ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    }).Count -gt 0) {
        throw "Refusing to recursively remove a DSK module configuration tree containing reparse points."
    }
}

$credentials = @([DskRemovalCredentialNative]::Enumerate("$CredentialPrefix*") | Where-Object {
    $_.StartsWith($CredentialPrefix, [StringComparison]::OrdinalIgnoreCase)
} | Sort-Object -Unique)

$summary = "Remove $($settingsFiles.Count) DSK settings file(s), the DSK module config directory, and $($credentials.Count) DSK credential(s)"
if (-not $PSCmdlet.ShouldProcess("the current Windows user's DSK Multistream data", $summary)) {
    return
}

foreach ($file in $settingsFiles) {
    Remove-Item -LiteralPath $file.FullName -Force
}
if (Test-Path -LiteralPath $moduleConfigPath) {
    Remove-Item -LiteralPath $moduleConfigPath -Recurse -Force
}
foreach ($credential in $credentials) {
    [DskRemovalCredentialNative]::Delete($credential)
}

[PSCustomObject]@{
    RemovedSettingsFiles = $settingsFiles.Count
    RemovedModuleConfig = -not (Test-Path -LiteralPath $moduleConfigPath)
    RemovedCredentials = $credentials.Count
    CredentialPrefix = $CredentialPrefix
} | ConvertTo-Json -Depth 2

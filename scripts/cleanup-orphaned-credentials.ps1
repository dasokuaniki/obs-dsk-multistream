param(
    [string]$ProfilesRoot = "$env:APPDATA\obs-studio\basic\profiles",
    [switch]$Apply,
    [switch]$DeleteRecoveryOnly,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

if (-not ("DskCredentialNative" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class DskCredentialNative
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

    public static string[] EnumerateDskTargets()
    {
        UInt32 count;
        IntPtr credentials;
        if (!CredEnumerate(null, 0, out count, out credentials)) {
            int error = Marshal.GetLastWin32Error();
            if (error == 1168)
                return new string[0];
            throw new System.ComponentModel.Win32Exception(error);
        }

        var result = new List<string>();
        try {
            for (int index = 0; index < count; ++index) {
                IntPtr credentialPointer = Marshal.ReadIntPtr(credentials, index * IntPtr.Size);
                CREDENTIAL credential = Marshal.PtrToStructure<CREDENTIAL>(credentialPointer);
                string target = Marshal.PtrToStringUni(credential.TargetName);
                if (!String.IsNullOrEmpty(target) && target.StartsWith("DSK Multistream/", StringComparison.OrdinalIgnoreCase))
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

function Add-TargetCredentialRefs {
    param(
        [object]$Target,
        [System.Collections.Generic.HashSet[string]]$Refs
    )

    foreach ($property in @("authCredentialRef", "oauthClientSecretRef", "oauthRefreshTokenRef")) {
        $value = [string]$Target.$property
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            [void]$Refs.Add($value.Trim())
        }
    }

    $id = [string]$Target.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        return
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Target.streamKey) -and
        [string]::IsNullOrWhiteSpace([string]$Target.authCredentialRef)) {
        [void]$Refs.Add("DSK Multistream/stream-key/$id")
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Target.oauthClientSecret) -and
        [string]::IsNullOrWhiteSpace([string]$Target.oauthClientSecretRef)) {
        [void]$Refs.Add("DSK Multistream/oauth-client-secret/$id")
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Target.oauthRefreshToken) -and
        [string]::IsNullOrWhiteSpace([string]$Target.oauthRefreshTokenRef)) {
        [void]$Refs.Add("DSK Multistream/oauth-refresh-token/$id")
    }
}

function Read-SettingsCredentialRefs {
    param(
        [System.IO.FileInfo[]]$Files,
        [System.Collections.Generic.HashSet[string]]$Refs,
        [System.Collections.Generic.List[string]]$InvalidFiles
    )

    foreach ($file in $Files) {
        try {
            $settings = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
            foreach ($target in @($settings.targets)) {
                if ($null -ne $target) {
                    Add-TargetCredentialRefs -Target $target -Refs $Refs
                }
            }
        }
        catch {
            $InvalidFiles.Add($file.FullName)
        }
    }
}

$activeRefs = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$recoveryRefs = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$invalidActiveFiles = [System.Collections.Generic.List[string]]::new()
$invalidRecoveryFiles = [System.Collections.Generic.List[string]]::new()

$activeFiles = @()
$recoveryFiles = @()
if (Test-Path -LiteralPath $ProfilesRoot) {
    $allSettingsFiles = @(Get-ChildItem -LiteralPath $ProfilesRoot -Recurse -File -ErrorAction Stop |
        Where-Object { $_.Name -like "dsk-multistream.json*" })
    $activeFiles = @($allSettingsFiles | Where-Object { $_.Name -eq "dsk-multistream.json" })
    $recoveryFiles = @($allSettingsFiles | Where-Object { $_.Name -ne "dsk-multistream.json" })
}

Read-SettingsCredentialRefs -Files $activeFiles -Refs $activeRefs -InvalidFiles $invalidActiveFiles
Read-SettingsCredentialRefs -Files $recoveryFiles -Refs $recoveryRefs -InvalidFiles $invalidRecoveryFiles

$credentials = @([DskCredentialNative]::EnumerateDskTargets() | Sort-Object -Unique)
$report = foreach ($credential in $credentials) {
    $status = if ($activeRefs.Contains($credential)) {
        "Active"
    }
    elseif ($recoveryRefs.Contains($credential)) {
        "RecoveryOnly"
    }
    else {
        "Orphan"
    }
    [PSCustomObject]@{
        Credential = $credential
        Status = $status
        Deleted = $false
    }
}

if ($Apply) {
    if ($invalidActiveFiles.Count -gt 0) {
        throw "Credential deletion refused because active DSK settings could not be parsed: $($invalidActiveFiles -join ', ')"
    }
    if ($activeFiles.Count -eq 0 -and -not $Force) {
        throw "Credential deletion refused because no active dsk-multistream.json files were found. Review the report first, then pass -Force only if all listed credentials are intentionally orphaned."
    }

    foreach ($entry in $report) {
        $deletable = $entry.Status -eq "Orphan" -or ($DeleteRecoveryOnly -and $entry.Status -eq "RecoveryOnly")
        if (-not $deletable) {
            continue
        }
        [DskCredentialNative]::Delete($entry.Credential)
        $entry.Deleted = $true
    }
}

[PSCustomObject]@{
    ProfilesRoot = $ProfilesRoot
    ActiveSettingsFiles = $activeFiles.Count
    RecoverySettingsFiles = $recoveryFiles.Count
    InvalidActiveSettingsFiles = @($invalidActiveFiles)
    InvalidRecoverySettingsFiles = @($invalidRecoveryFiles)
    CredentialCount = $credentials.Count
    ActiveCredentialCount = @($report | Where-Object Status -eq "Active").Count
    RecoveryOnlyCredentialCount = @($report | Where-Object Status -eq "RecoveryOnly").Count
    OrphanCredentialCount = @($report | Where-Object Status -eq "Orphan").Count
    DeletedCredentialCount = @($report | Where-Object Deleted).Count
    Credentials = @($report)
} | ConvertTo-Json -Depth 5

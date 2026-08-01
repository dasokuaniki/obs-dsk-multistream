param(
    [Parameter(Mandatory = $true)]
    [string]$RemovalScript,
    [Parameter(Mandatory = $true)]
    [string]$TestRoot
)

$ErrorActionPreference = "Stop"

if (-not ("DskRemovalTestCredential" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class DskRemovalTestCredential
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct CREDENTIAL
    {
        public UInt32 Flags;
        public UInt32 Type;
        public string TargetName;
        public string Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public UInt32 CredentialBlobSize;
        public IntPtr CredentialBlob;
        public UInt32 Persist;
        public UInt32 AttributeCount;
        public IntPtr Attributes;
        public string TargetAlias;
        public string UserName;
    }

    [DllImport("advapi32.dll", EntryPoint = "CredWriteW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredWrite(ref CREDENTIAL credential, UInt32 flags);

    [DllImport("advapi32.dll", EntryPoint = "CredReadW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredRead(string target, UInt32 type, UInt32 flags, out IntPtr credential);

    [DllImport("advapi32.dll", EntryPoint = "CredDeleteW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredDelete(string target, UInt32 type, UInt32 flags);

    [DllImport("advapi32.dll", SetLastError = false)]
    private static extern void CredFree(IntPtr buffer);

    public static void Write(string target)
    {
        CREDENTIAL credential = new CREDENTIAL();
        credential.Type = 1;
        credential.TargetName = target;
        credential.Persist = 2;
        credential.UserName = "DSK removal E2E";
        if (!CredWrite(ref credential, 0))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    }

    public static bool Exists(string target)
    {
        IntPtr credential;
        if (!CredRead(target, 1, 0, out credential)) {
            int error = Marshal.GetLastWin32Error();
            if (error == 1168)
                return false;
            throw new System.ComponentModel.Win32Exception(error);
        }
        CredFree(credential);
        return true;
    }

    public static void Delete(string target)
    {
        if (!CredDelete(target, 1, 0)) {
            int error = Marshal.GetLastWin32Error();
            if (error != 1168)
                throw new System.ComponentModel.Win32Exception(error);
        }
    }
}
"@
}

$root = [IO.Path]::GetFullPath($TestRoot)
$profiles = Join-Path $root "profiles"
$profile = Join-Path $profiles "Profile A"
$moduleConfig = Join-Path $root "module-config"
$outsideSentinel = Join-Path $root "other-product-settings.json"
$ownedCredential = "DSK Multistream E2E/remove-$([Guid]::NewGuid().ToString('N'))"
$foreignCredential = "DSK Other Product E2E/remove-$([Guid]::NewGuid().ToString('N'))"

try {
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $profile, $moduleConfig | Out-Null
    [IO.File]::WriteAllText((Join-Path $profile "dsk-multistream.json"), "{}")
    [IO.File]::WriteAllText((Join-Path $profile "dsk-multistream.json.bak"), "{}")
    [IO.File]::WriteAllText((Join-Path $profile "dsk-multistream.json.corrupt-20260731.json"), "{}")
    [IO.File]::WriteAllText((Join-Path $profile "keep-other-product.txt"), "keep")
    [IO.File]::WriteAllText((Join-Path $moduleConfig "settings.json"), "{}")
    [IO.File]::WriteAllText($outsideSentinel, "keep")
    [DskRemovalTestCredential]::Write($ownedCredential)
    [DskRemovalTestCredential]::Write($foreignCredential)

    & $RemovalScript -TestMode -TestRoot $root -ProfilesRoot $profiles `
        -ModuleConfigRoot $moduleConfig -CredentialPrefix "DSK Multistream E2E/" -Force | Out-Null

    foreach ($ownedFile in @(
        (Join-Path $profile "dsk-multistream.json"),
        (Join-Path $profile "dsk-multistream.json.bak"),
        (Join-Path $profile "dsk-multistream.json.corrupt-20260731.json")
    )) {
        if (Test-Path -LiteralPath $ownedFile) {
            throw "Complete removal left DSK-owned settings: $ownedFile"
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $profile "keep-other-product.txt"))) {
        throw "Complete removal deleted a non-DSK file inside an OBS profile."
    }
    if (Test-Path -LiteralPath $moduleConfig) {
        throw "Complete removal left the exact DSK module configuration directory."
    }
    if (-not (Test-Path -LiteralPath $outsideSentinel)) {
        throw "Complete removal deleted a sibling product sentinel."
    }
    if ([DskRemovalTestCredential]::Exists($ownedCredential)) {
        throw "Complete removal left a DSK Multistream E2E credential."
    }
    if (-not [DskRemovalTestCredential]::Exists($foreignCredential)) {
        throw "Complete removal deleted another product's credential."
    }
}
finally {
    [DskRemovalTestCredential]::Delete($ownedCredential)
    [DskRemovalTestCredential]::Delete($foreignCredential)
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}

Write-Host "DSK complete-removal ownership guards passed."

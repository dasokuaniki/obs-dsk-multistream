param(
    [Parameter(Mandatory = $true)]
    [string]$PluginDll
)

$ErrorActionPreference = "Stop"
$resolved = (Resolve-Path -LiteralPath $PluginDll).Path
$bytes = [IO.File]::ReadAllBytes($resolved)
if ($bytes.Length -lt 512) {
    throw "Plugin DLL is too small to contain a valid PE header."
}
if ($bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    throw "Plugin DLL does not have an MZ header."
}

$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ($peOffset -lt 0x40 -or $peOffset + 96 -gt $bytes.Length) {
    throw "Plugin DLL has an invalid PE header offset."
}
if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
    $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
    throw "Plugin DLL does not have a PE signature."
}

$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
if ($machine -ne 0x8664) {
    throw ("Plugin DLL is not x64. Machine=0x{0:X4}" -f $machine)
}

$optionalHeader = $peOffset + 24
$magic = [BitConverter]::ToUInt16($bytes, $optionalHeader)
if ($magic -ne 0x20B) {
    throw ("Plugin DLL is not PE32+. Magic=0x{0:X4}" -f $magic)
}

$dllCharacteristics = [BitConverter]::ToUInt16($bytes, $optionalHeader + 0x46)
$required = [ordered]@{
    HighEntropyVa = 0x0020
    DynamicBase = 0x0040
    NxCompatible = 0x0100
    ControlFlowGuard = 0x4000
}
foreach ($entry in $required.GetEnumerator()) {
    if (($dllCharacteristics -band $entry.Value) -ne $entry.Value) {
        throw ("Plugin DLL is missing {0}. DllCharacteristics=0x{1:X4}" -f $entry.Key, $dllCharacteristics)
    }
}

[PSCustomObject]@{
    Plugin = $resolved
    Machine = "x64"
    DllCharacteristics = ("0x{0:X4}" -f $dllCharacteristics)
    HighEntropyVa = $true
    DynamicBase = $true
    NxCompatible = $true
    ControlFlowGuard = $true
} | ConvertTo-Json -Compress

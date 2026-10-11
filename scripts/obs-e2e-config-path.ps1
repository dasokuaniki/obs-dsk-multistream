function Resolve-ObsE2eConfigRoot {
    param(
        [string]$ObsPath,
        [string]$BuildRoot,
        [string]$AppDataPath = $env:APPDATA,
        [switch]$IsolatedPortable
    )

    if (-not $IsolatedPortable) {
        if (-not $AppDataPath) { throw 'APPDATA is unavailable.' }
        return Join-Path $AppDataPath 'obs-studio'
    }
    if (-not $BuildRoot) { throw 'A build-local boundary is required for portable E2E.' }
    $exe = [IO.Path]::GetFullPath($ObsPath)
    $boundary = [IO.Path]::GetFullPath($BuildRoot).TrimEnd('\', '/')
    if (-not $exe.EndsWith('\bin\64bit\obs64.exe', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Portable E2E requires an OBS bin/64bit/obs64.exe tree.'
    }
    $root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $exe))
    if (-not $root.StartsWith($boundary + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Portable E2E must stay inside the repository build directory.'
    }
    # Reject existing junctions/symlinks that could redirect a build-local write.
    for ($ancestor = $root; $ancestor; $ancestor = Split-Path -Parent $ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Portable E2E does not accept redirected directories.'
            }
        }
    }
    $config = Join-Path $root 'config\obs-studio'
    if (Test-Path -LiteralPath (Join-Path $root 'config')) {
        if ((Get-Item -LiteralPath (Join-Path $root 'config') -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Portable E2E does not accept a redirected config directory.'
        }
    }
    if (Test-Path -LiteralPath $config) {
        if ((Get-Item -LiteralPath $config -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Portable E2E does not accept a redirected OBS config directory.'
        }
    }
    return $config
}

param([string]$SourceRoot = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
$helper = Join-Path $SourceRoot 'scripts\obs-e2e-config-path.ps1'
if (-not (Test-Path -LiteralPath $helper)) {
    throw 'The RTMP harness does not yet support a build-local portable config root.'
}
. $helper

$failures = 0
function Assert-Equal($Actual, $Expected, [string]$Label) {
    if ($Actual -ne $Expected) {
        $script:failures++
        Write-Error -ErrorAction Continue "$Label expected '$Expected', got '$Actual'"
    }
}
function Assert-Rejected([scriptblock]$Action, [string]$Label) {
    try { & $Action | Out-Null } catch { return }
    $script:failures++
    Write-Error -ErrorAction Continue "$Label was not rejected"
}

$build = 'C:\test-workspace\build'
$portableExe = 'C:\test-workspace\build\canvas-canary\obs\bin\64bit\obs64.exe'
$appData = 'C:\test-user\AppData\Roaming'
Assert-Equal (Resolve-ObsE2eConfigRoot -ObsPath $portableExe -BuildRoot $build -AppDataPath $appData -IsolatedPortable) `
    'C:\test-workspace\build\canvas-canary\obs\config\obs-studio' 'Portable configuration'
Assert-Equal (Resolve-ObsE2eConfigRoot -ObsPath $portableExe -BuildRoot $build -AppDataPath $appData) `
    'C:\test-user\AppData\Roaming\obs-studio' 'Legacy non-portable configuration'
Assert-Rejected { Resolve-ObsE2eConfigRoot -ObsPath 'C:\Program Files\obs-studio\bin\64bit\obs64.exe' -BuildRoot $build -AppDataPath $appData -IsolatedPortable } 'Production executable'
Assert-Rejected { Resolve-ObsE2eConfigRoot -ObsPath 'C:\test-workspace\build-other\obs\bin\64bit\obs64.exe' -BuildRoot $build -IsolatedPortable } 'Prefix-only sibling'
Assert-Rejected { Resolve-ObsE2eConfigRoot -ObsPath 'C:\test-workspace\build\..\production\bin\64bit\obs64.exe' -BuildRoot $build -IsolatedPortable } 'Traversal outside build'
Assert-Rejected { Resolve-ObsE2eConfigRoot -ObsPath 'C:\test-workspace\build\obs64.exe' -BuildRoot $build -IsolatedPortable } 'Invalid portable tree'
Assert-Rejected { Resolve-ObsE2eConfigRoot -ObsPath $portableExe -BuildRoot '' -IsolatedPortable } 'Missing build boundary'
Assert-Equal (Resolve-ObsE2eConfigRoot -ObsPath $portableExe.ToUpperInvariant() -BuildRoot $build -IsolatedPortable) `
    'C:\TEST-WORKSPACE\BUILD\CANVAS-CANARY\OBS\config\obs-studio' 'Case-insensitive Windows boundary'
if ($failures) { throw "$failures isolation check(s) failed" }
Write-Output '8 OBS E2E path isolation checks passed.'

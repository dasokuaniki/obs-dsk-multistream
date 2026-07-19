param(
    [int]$Iterations = 6,
    [int]$BasePort = 19500,
    [int]$TimeoutSeconds = 110,
    [int]$RunMs = 2500
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$e2e = Join-Path $root "scripts\run-obs-rtmp-e2e.ps1"

if (-not (Test-Path -LiteralPath $e2e)) {
    throw "E2E script was not found: $e2e"
}

$cases = @(
    @{ Name = "horizontal"; EncoderGroup = "dsk-horizontal" },
    @{ Name = "vertical"; EncoderGroup = "dsk-vertical" },
    @{ Name = "dual-horizontal"; EncoderGroup = "dsk-horizontal"; SecondEncoderGroup = "dsk-horizontal" },
    @{ Name = "horizontal-vertical"; EncoderGroup = "dsk-horizontal"; SecondEncoderGroup = "dsk-vertical" },
    @{ Name = "vertical-horizontal"; EncoderGroup = "dsk-vertical"; SecondEncoderGroup = "dsk-horizontal" }
)

for ($i = 0; $i -lt $Iterations; ++$i) {
    $case = $cases[$i % $cases.Count]
    $port = $BasePort + ($i * 2)
    $caseParams = @{
        Port = $port
        EncoderGroup = $case.EncoderGroup
        TimeoutSeconds = $TimeoutSeconds
        RunMs = $RunMs
        StartDelayMs = 0
        CleanupE2eConfig = $true
    }

    if ($case.ContainsKey("SecondEncoderGroup")) {
        $caseParams.SecondPort = $port + 1
        $caseParams.SecondEncoderGroup = $case.SecondEncoderGroup
    }

    Write-Host "[$($i + 1)/$Iterations] $($case.Name) on port $port"
    & $e2e @caseParams
}

Write-Host "DSK loop E2E passed for $Iterations iterations."

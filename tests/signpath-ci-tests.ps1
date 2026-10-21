param(
    [string]$HelperScript = (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\signpath-ci.ps1'),
    [string]$WorkflowPath = (Join-Path (Split-Path -Parent $PSScriptRoot) '.github\workflows\windows-release.yml')
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $HelperScript -PathType Leaf)) {
    throw 'SignPath CI helper was not found.'
}
. $HelperScript

$script:checkCount = 0
function Assert-Equal {
    param($Actual, $Expected, [string]$Label)
    if ($Actual -cne $Expected) { throw "Unexpected result: $Label" }
    $script:checkCount++
}
function Expect-Rejection {
    param([scriptblock]$Action, [string]$Label)
    $rejected = $false
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -match 'fixture-sensitive-') {
            throw 'A fixture credential appeared in an exception.'
        }
        $rejected = $true
    }
    if (-not $rejected) { throw "Invalid fixture accepted: $Label" }
    $script:checkCount++
}

$pin = 'A' * 40
$validInputs = @{
    ApiToken = 'fixture-sensitive-token'
    OrganizationId = 'fixture-sensitive-organization'
    ProjectSlug = 'fixture-sensitive-project'
    SigningPolicySlug = 'fixture-sensitive-policy'
    PluginConfigurationSlug = 'fixture-sensitive-plugin'
    UninstallerConfigurationSlug = 'fixture-sensitive-uninstaller'
    InstallerConfigurationSlug = 'fixture-sensitive-installer'
    ExpectedSignerThumbprint = $pin
    ReleaseSigningApproved = 'true'
}
$mode = Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs $validInputs -PublisherOAuthConfigured $true
Assert-Equal $mode.Enabled $true 'complete approved tag'
Assert-Equal $mode.UnsignedArtifactLabel 'unsigned-beta' 'tag unsigned label'
foreach ($ref in @('refs/heads/main', 'refs/pull/12/merge', 'refs/tags/test', 'refs/tags/V1.2.3')) {
    $mode = Get-SignPathSigningMode -Ref $ref -Inputs $validInputs -PublisherOAuthConfigured $false
    Assert-Equal $mode.Enabled $false 'nonrelease ref cannot sign'
    Assert-Equal $mode.UnsignedArtifactLabel 'unsigned-development' 'nonrelease unsigned label'
}
$mode = Get-SignPathSigningMode -Ref 'refs/heads/main' -Inputs @{ ReleaseSigningApproved = 'true' } -PublisherOAuthConfigured $false
Assert-Equal $mode.Enabled $false 'approved branch ignores missing release inputs'
foreach ($approval in @('', $null, 'false')) {
    $inputs = @{ ReleaseSigningApproved = $approval }
    $mode = Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs $inputs -PublisherOAuthConfigured $false
    Assert-Equal $mode.Enabled $false 'unapproved tag remains dormant'
    Assert-Equal $mode.UnsignedArtifactLabel 'unsigned-beta' 'unapproved tag unsigned label'
}
$mode = Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs @{} -PublisherOAuthConfigured $false
Assert-Equal $mode.Enabled $false 'missing approval remains dormant'
foreach ($approval in @('yes', 'TRUE', 'False', ' true ', 'fixture-sensitive-invalid-approval')) {
    $inputs = $validInputs.Clone()
    $inputs.ReleaseSigningApproved = $approval
    Expect-Rejection { Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs $inputs -PublisherOAuthConfigured $true } 'invalid approval'
}
foreach ($name in @('ApiToken', 'OrganizationId', 'ProjectSlug', 'SigningPolicySlug',
    'PluginConfigurationSlug', 'UninstallerConfigurationSlug', 'InstallerConfigurationSlug', 'ExpectedSignerThumbprint')) {
    foreach ($badValue in @($null, '', '  ')) {
        $inputs = $validInputs.Clone()
        $inputs[$name] = $badValue
        Expect-Rejection { Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs $inputs -PublisherOAuthConfigured $true } "missing $name"
    }
    $inputs = $validInputs.Clone()
    $inputs.Remove($name)
    Expect-Rejection { Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs $inputs -PublisherOAuthConfigured $true } "absent $name"
}
foreach ($badPin in @('not-a-pin', ('A' * 39), ('A' * 41), ('G' * 40), 'fixture-sensitive-invalid-pin')) {
    $inputs = $validInputs.Clone()
    $inputs.ExpectedSignerThumbprint = $badPin
    Expect-Rejection { Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs $inputs -PublisherOAuthConfigured $true } 'malformed signer pin'
}
$inputs = $validInputs.Clone()
$inputs.ExpectedSignerThumbprint = $pin.ToLowerInvariant()
$mode = Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs $inputs -PublisherOAuthConfigured $true
Assert-Equal $mode.Enabled $true 'lowercase approved pin'
Expect-Rejection { Get-SignPathSigningMode -Ref 'refs/tags/v1.2.3' -Inputs $validInputs -PublisherOAuthConfigured $false } 'publisher OAuth required'

# Only the operating-system signature read is replaced. The helper must still
# invoke the production Assert-ReleaseSignature validation with this result.
$script:fixtureSignature = [pscustomobject]@{
    Status = 'Valid'
    SignerCertificate = [pscustomobject]@{ Thumbprint = $pin }
    TimeStamperCertificate = [pscustomobject]@{ Present = $true }
}
function Get-AuthenticodeSignature {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)
    return $script:fixtureSignature
}
$fixtureParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $fixtureParent ('dsk-signpath-ci-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
try {
    $fileName = 'obs-dsk-multistream.dll'
    Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint $pin } 'missing signed file'
    Expect-Rejection { Get-SignPathSignedFile -Directory (Join-Path $fixtureRoot 'absent') -FileName $fileName -ExpectedSignerThumbprint $pin } 'missing signed directory'
    foreach ($badName in @('..\escape.dll', '../escape.dll', 'C:\escape.dll', 'C:escape.dll', 'folder/file.dll', '*.dll', '?file.dll', '.', '..')) {
        Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $badName -ExpectedSignerThumbprint $pin } 'filename must be bare'
    }
    $nested = Join-Path $fixtureRoot 'nested'
    New-Item -ItemType Directory -Path $nested | Out-Null
    $signedFile = Join-Path $nested $fileName
    [IO.File]::WriteAllText($signedFile, 'Synthetic test data; not executable code.')
    $actual = Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint $pin
    Assert-Equal $actual ([IO.Path]::GetFullPath($signedFile)) 'nested signed file returned'
    Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint ('B' * 40) } 'different expected signer'
    $script:fixtureSignature.SignerCertificate.Thumbprint = 'B' * 40
    Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint $pin } 'wrong returned signer'
    $script:fixtureSignature.SignerCertificate.Thumbprint = $pin
    $script:fixtureSignature.TimeStamperCertificate = $null
    Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint $pin } 'missing timestamp'
    $script:fixtureSignature.TimeStamperCertificate = [pscustomobject]@{ Present = $true }
    $script:fixtureSignature.Status = 'NotSigned'
    Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint $pin } 'unsigned returned file'
    $script:fixtureSignature.Status = 'HashMismatch'
    Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint $pin } 'invalid returned signature'
    $script:fixtureSignature.Status = 'Valid'
    $duplicate = Join-Path $fixtureRoot $fileName
    [IO.File]::WriteAllText($duplicate, 'Synthetic duplicate.')
    Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint $pin } 'duplicate signed files'
    Remove-Item -LiteralPath $duplicate -Force
    $literalFileName = 'obs-[fixture].dll'
    $literalFile = Join-Path $nested $literalFileName
    [IO.File]::WriteAllText($literalFile, 'Synthetic literal-name fixture.')
    $actual = Get-SignPathSignedFile -Directory $fixtureRoot -FileName $literalFileName -ExpectedSignerThumbprint $pin
    Assert-Equal $actual ([IO.Path]::GetFullPath($literalFile)) 'filename is matched literally'
    $junctionPath = Join-Path $fixtureRoot 'returned-junction'
    New-Item -ItemType Junction -Path $junctionPath -Target $nested | Out-Null
    try {
        Expect-Rejection { Get-SignPathSignedFile -Directory $junctionPath -FileName $fileName -ExpectedSignerThumbprint $pin } 'returned directory reparse point'
        Expect-Rejection { Get-SignPathSignedFile -Directory $fixtureRoot -FileName $fileName -ExpectedSignerThumbprint $pin } 'nested returned reparse point'
    } finally {
        # Delete only this junction entry; never recurse through its target.
        [IO.Directory]::Delete($junctionPath)
    }
    # Read Windows PE fixtures without executing or modifying system files.
    # All writes below remain in this disposable test directory.
    $peInput = Join-Path $PSHOME 'powershell.exe'
    $differentPeInput = Join-Path ([Environment]::SystemDirectory) 'cmd.exe'
    if (-not (Test-Path -LiteralPath $peInput -PathType Leaf) -or
        -not (Test-Path -LiteralPath $differentPeInput -PathType Leaf)) {
        throw 'Windows PowerShell and cmd PE fixtures are required.'
    }
    $copySourceRoot = Join-Path $fixtureRoot 'copy-returned'
    $copyDestinationRoot = Join-Path $fixtureRoot 'copy-staged'
    New-Item -ItemType Directory -Path $copySourceRoot, $copyDestinationRoot | Out-Null
    $copyFileName = 'fixture-pe.exe'
    $copySource = Join-Path $copySourceRoot $copyFileName
    $copyDestination = Join-Path $copyDestinationRoot $copyFileName
    [IO.File]::Copy($peInput, $copySource)
    [IO.File]::Copy($peInput, $copyDestination)
    [IO.File]::AppendAllText($copyDestination, 'Synthetic pre-copy marker.')
    $sourceHash = (Get-FileHash -LiteralPath $copySource -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $copyDestination -Algorithm SHA256).Hash -eq $sourceHash) {
        throw 'The normal-copy fixture must start with different bytes.'
    }
    $actual = Copy-SignPathSignedFile -Directory $copySourceRoot -FileName $copyFileName -Destination $copyDestination -ExpectedSignerThumbprint $pin
    Assert-Equal $actual ([IO.Path]::GetFullPath($copyDestination)) 'copied signed path returned'
    Assert-Equal (Get-FileHash -LiteralPath $copyDestination -Algorithm SHA256).Hash $sourceHash 'copy preserves returned bytes'
    [IO.File]::Copy($differentPeInput, $copyDestination, $true)
    $originalDestinationHash = (Get-FileHash -LiteralPath $copyDestination -Algorithm SHA256).Hash
    Expect-Rejection { Copy-SignPathSignedFile -Directory $copySourceRoot -FileName $copyFileName -Destination $copyDestination -ExpectedSignerThumbprint $pin } 'returned PE metadata differs from staged input'
    Assert-Equal (Get-FileHash -LiteralPath $copyDestination -Algorithm SHA256).Hash $originalDestinationHash 'metadata mismatch preserves staged input'
    Expect-Rejection { Copy-SignPathSignedFile -Directory $copySourceRoot -FileName $copyFileName -Destination (Join-Path $copyDestinationRoot 'absent.exe') -ExpectedSignerThumbprint $pin } 'copy destination must exist'
    Expect-Rejection { Copy-SignPathSignedFile -Directory $copySourceRoot -FileName $copyFileName -Destination $copyDestinationRoot -ExpectedSignerThumbprint $pin } 'copy destination must be a file'
    $textDestination = Join-Path $copyDestinationRoot 'metadata-empty.dll'
    [IO.File]::WriteAllText($textDestination, 'Synthetic staged file without PE metadata.')
    Expect-Rejection { Copy-SignPathSignedFile -Directory $nested -FileName $literalFileName -Destination $textDestination -ExpectedSignerThumbprint $pin } 'empty PE metadata rejected'
    # Exercise the actual run block with the GitHub PowerShell exit-code wrapper.
    # The only executable handoff is a local stub, modeling Inno's intentional
    # first-pass native exit 1 after creating a usable unsigned .e32 artifact.
    $workflowText = Get-Content -LiteralPath $WorkflowPath -Raw -Encoding UTF8
    $stepMatch = [regex]::Match($workflowText, '(?ms)^      - name: Prepare unsigned uninstaller\r?\n(?<step>.*?)(?=^      - name:|\z)')
    if (-not $stepMatch.Success) { throw 'The uninstaller preparation workflow step was not found.' }
    $runMatch = [regex]::Match($stepMatch.Groups['step'].Value, '(?ms)^        run: \|\r?\n(?<run>.*)$')
    if (-not $runMatch.Success) { throw 'The uninstaller preparation run block was not found.' }
    $runBlock = [regex]::Replace($runMatch.Groups['run'].Value, '(?m)^          ', '')
    $resetPattern = '(?m)^[ \t]*\$global:LASTEXITCODE[ \t]*=[ \t]*0[ \t]*\r?\n?'
    if (-not [regex]::IsMatch($runBlock, $resetPattern)) {
        throw 'The uninstaller preparation step must clear its validated native exit code.'
    }
    $stubPhase = @(
        'param([string]$Phase, [string]$WorkDir, [string]$IsccPath, [string]$ExpectedSignerThumbprint)',
        'if ($Phase -cne ''PrepareUninstaller'') { throw ''Unexpected synthetic phase.'' }',
        '$handoffRoot = Join-Path $WorkDir ''signed-uninstaller''',
        'New-Item -ItemType Directory -Path $handoffRoot -Force | Out-Null',
        '[IO.File]::WriteAllText((Join-Path (Get-Location).Path (Join-Path $handoffRoot ''uninst-6.7.3-fixture.e32'')), ''Synthetic Inno handoff.'')',
        '& $env:ComSpec /d /c ''exit 1''',
        'return'
    ) -join [Environment]::NewLine
    foreach ($scenario in @(
        [pscustomobject]@{ Name = 'without-reset'; Run = [regex]::Replace($runBlock, $resetPattern, ''); ExitCode = 1 },
        [pscustomobject]@{ Name = 'actual-workflow'; Run = $runBlock; ExitCode = 0 }
    )) {
        $scenarioRoot = Join-Path $fixtureRoot $scenario.Name
        $stubScripts = Join-Path $scenarioRoot 'scripts'
        New-Item -ItemType Directory -Path $stubScripts | Out-Null
        [IO.File]::WriteAllText((Join-Path $stubScripts 'external-windows-signing.ps1'), $stubPhase)
        $workflowOutput = Join-Path $scenarioRoot 'github-output.txt'
        $scenarioLiteral = $scenarioRoot.Replace("'", "''")
        $outputLiteral = $workflowOutput.Replace("'", "''")
        $childPreamble = @"
`$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath '$scenarioLiteral'
`$env:GITHUB_WORKSPACE = '$scenarioLiteral'
`$env:GITHUB_OUTPUT = '$outputLiteral'
`$env:SIGNPATH_EXPECTED_SIGNER_THUMBPRINT = '$pin'
"@
        $childWrapper = 'if (Test-Path variable:\LASTEXITCODE) { exit $LASTEXITCODE }'
        $childPath = Join-Path $scenarioRoot 'workflow-step.ps1'
        [IO.File]::WriteAllText($childPath, ($childPreamble + "`r`n" + $scenario.Run + "`r`n" + $childWrapper))
        $childOutput = @(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $childPath 2>&1)
        $childExitCode = $LASTEXITCODE
        if ($childExitCode -ne $scenario.ExitCode) {
            $childOutput | ForEach-Object { Write-Output $_ }
            throw "Unexpected synthetic workflow exit: $($scenario.Name)"
        }
        $script:checkCount++
        Assert-Equal (Get-Content -LiteralPath $workflowOutput -Raw).Trim() 'name=uninst-6.7.3-fixture.e32' 'preparation completed before wrapper exit'
        Assert-Equal (Test-Path -LiteralPath (Join-Path $scenarioRoot 'build\signing-input\uninstaller\uninst-6.7.3-fixture.e32') -PathType Leaf) $true 'synthetic uninstaller was staged'
    }
    Write-Output "SignPath CI behavior tests passed: $script:checkCount"
} finally {
    $fixtureFullPath = [IO.Path]::GetFullPath($fixtureRoot)
    $fixturePrefix = $fixtureParent.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $fixtureFullPath.StartsWith($fixturePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $fixtureFullPath) -notlike 'dsk-signpath-ci-test-*') {
        throw 'Refusing to remove fixtures outside the dedicated temporary directory.'
    }
    Remove-Item -LiteralPath $fixtureFullPath -Recurse -Force
}

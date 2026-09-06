param([Parameter(Mandatory = $true)][string]$ValidationScript)
$ErrorActionPreference = 'Stop'
. $ValidationScript
$root = Join-Path ([IO.Path]::GetTempPath()) ('dsk-release-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$header = Join-Path $root 'oauth.hpp'
$cache = Join-Path $root 'CMakeCache.txt'
$goodHeader = 'inline constexpr char BundledYouTubeClientId[] = "fixture-id";' + "`n" +
    'inline constexpr char BundledYouTubeClientSecret[] = "fixture-value";'
$goodCache = "DSK_INCLUDE_E2E_HOOKS:BOOL=OFF`nDSK_PUBLISHER_RELEASE:BOOL=ON`n"
$count = 0
function Expect-Rejection {
    param([scriptblock]$Action)
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid fixture was accepted.' }
}
function Check-Build {
    Assert-ReleaseBuildConfiguration -CachePath $cache -OAuthHeaderPath $header -RequirePublisherRelease
}
try {
    [IO.File]::WriteAllText($header, $goodHeader)
    [IO.File]::WriteAllText($cache, $goodCache)
    Check-Build
    $count++
    foreach ($badHeader in @(
        $goodHeader.Replace('fixture-id', ''),
        $goodHeader.Replace('fixture-value', '  '),
        $goodHeader.Replace('BundledYouTubeClientSecret[]', 'Other[]'),
        $goodHeader.Replace('fixture-id', '@DSK_BUNDLED_YOUTUBE_CLIENT_ID@'),
        ($goodHeader + "`n" + $goodHeader),
        ("/*`n" + $goodHeader + "`n*/"),
        ($goodHeader -replace '(?m)^', '// ')
    )) {
        [IO.File]::WriteAllText($header, $badHeader)
        Expect-Rejection { Check-Build }
        $count++
    }
    [IO.File]::WriteAllText($header, $goodHeader)
    foreach ($badCache in @(
        $goodCache.Replace('DSK_PUBLISHER_RELEASE:BOOL=ON', 'DSK_PUBLISHER_RELEASE:BOOL=OFF'),
        $goodCache.Replace('DSK_INCLUDE_E2E_HOOKS:BOOL=OFF', 'DSK_INCLUDE_E2E_HOOKS:BOOL=ON'),
        ($goodCache + "DSK_PUBLISHER_RELEASE:BOOL=ON`n"),
        '# DSK_INCLUDE_E2E_HOOKS:BOOL=OFF'
    )) {
        [IO.File]::WriteAllText($cache, $badCache)
        Expect-Rejection { Check-Build }
        $count++
    }
    [IO.File]::WriteAllText($cache, "DSK_INCLUDE_E2E_HOOKS:BOOL=OFF`n")
    Assert-ReleaseBuildConfiguration -CachePath $cache -OAuthHeaderPath $header
    $count++
    $pin = 'A' * 40
    $signature = [pscustomobject]@{ Status = 'Valid'; SignerCertificate = [pscustomobject]@{ Thumbprint = $pin }; TimeStamperCertificate = [pscustomobject]@{ Present = $true } }
    Assert-ReleaseSignature -Signature $signature -ExpectedSignerThumbprint $pin.ToLowerInvariant()
    $count++
    foreach ($badPin in @('', 'not-a-pin', ('B' * 40))) {
        Expect-Rejection { Assert-ReleaseSignature -Signature $signature -ExpectedSignerThumbprint $badPin }
        $count++
    }
    $signature.TimeStamperCertificate = $null
    Expect-Rejection { Assert-ReleaseSignature -Signature $signature -ExpectedSignerThumbprint $pin }
    $count++
    $signature.TimeStamperCertificate = [pscustomobject]@{ Present = $true }
    $signature.Status = 'NotSigned'
    Expect-Rejection { Assert-ReleaseSignature -Signature $signature -ExpectedSignerThumbprint $pin }
    $count++
    Write-Output "Release validation behavior tests passed: $count"
} finally {
    if (([IO.Path]::GetFullPath($root)).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}

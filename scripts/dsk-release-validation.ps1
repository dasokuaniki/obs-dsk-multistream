function Assert-ReleaseOAuthHeader {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw 'Generated publisher OAuth header is missing.'
    }
    $header = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ($header -match '/\*|\*/') {
        throw 'Generated publisher OAuth header has an unexpected block comment.'
    }
    foreach ($name in @('BundledYouTubeClientId', 'BundledYouTubeClientSecret')) {
        $pattern = '(?m)^[ \t]*inline[ \t]+constexpr[ \t]+char[ \t]+' +
            [regex]::Escape($name) + '[ \t]*\[\][ \t]*=[ \t]*"([^"\r\n]*)"[ \t]*;[ \t]*\r?$'
        $declarations = [regex]::Matches($header, $pattern)
        if ($declarations.Count -ne 1) {
            throw 'Generated publisher OAuth declarations are missing, duplicated, or malformed.'
        }
        $value = $declarations[0].Groups[1].Value
        if ([string]::IsNullOrWhiteSpace($value) -or $value.Contains('@DSK_')) {
            throw 'Generated publisher OAuth credentials are empty or unresolved.'
        }
    }
}

function Assert-ReleaseBuildConfiguration {
    param(
        [Parameter(Mandatory = $true)][string]$CachePath,
        [Parameter(Mandatory = $true)][string]$OAuthHeaderPath,
        [switch]$RequirePublisherRelease
    )

    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
        throw 'Release CMakeCache.txt is missing.'
    }
    $cacheText = Get-Content -LiteralPath $CachePath -Raw -Encoding UTF8
    $flags = @{ DSK_INCLUDE_E2E_HOOKS = 'OFF' }
    if ($RequirePublisherRelease) { $flags.DSK_PUBLISHER_RELEASE = 'ON' }
    foreach ($name in $flags.Keys) {
        $pattern = '(?m)^' + [regex]::Escape($name) + ':BOOL=([^\r\n]*)\r?$'
        $entries = [regex]::Matches($cacheText, $pattern)
        if ($entries.Count -ne 1 -or $entries[0].Groups[1].Value -cne $flags[$name]) {
            throw "Release cache requires exactly one $name=$($flags[$name]) entry."
        }
    }
    if ($RequirePublisherRelease) {
        Assert-ReleaseOAuthHeader -Path $OAuthHeaderPath
    }
}

function Assert-ReleaseSignature {
    param(
        [Parameter(Mandatory = $true)]$Signature,
        [Parameter(Mandatory = $true)][string]$ExpectedSignerThumbprint,
        [string]$Label = 'Release file'
    )

    if ($ExpectedSignerThumbprint -notmatch '^[0-9A-Fa-f]{40}$') {
        throw 'An approved 40-hex certificate thumbprint is required.'
    }
    if ([string]$Signature.Status -ne 'Valid' -or
        -not $Signature.SignerCertificate -or -not $Signature.TimeStamperCertificate) {
        throw "$Label requires a valid Authenticode signature and trusted timestamp."
    }
    if ([string]$Signature.SignerCertificate.Thumbprint -ine $ExpectedSignerThumbprint) {
        throw "$Label signer does not match the approved release signer."
    }
}

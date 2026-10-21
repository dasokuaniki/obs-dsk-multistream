. (Join-Path $PSScriptRoot 'dsk-release-validation.ps1')

function Get-SignPathSigningMode {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Ref,
        [Parameter(Mandatory = $true)][hashtable]$Inputs,
        [Parameter(Mandatory = $true)][bool]$PublisherOAuthConfigured
    )

    $isReleaseTag = $Ref.StartsWith('refs/tags/v', [StringComparison]::Ordinal)
    $unsignedLabel = if ($isReleaseTag) { 'unsigned-beta' } else { 'unsigned-development' }
    $approval = [string]$Inputs.ReleaseSigningApproved
    if (-not [string]::IsNullOrWhiteSpace($approval) -and
        $approval -cne 'false' -and $approval -cne 'true') {
        throw 'Release signing approval must be unset, false, or true.'
    }
    $enabled = $isReleaseTag -and $approval -ceq 'true'
    if ($enabled) {
        foreach ($name in @('ApiToken', 'OrganizationId', 'ProjectSlug', 'SigningPolicySlug',
            'PluginConfigurationSlug', 'UninstallerConfigurationSlug', 'InstallerConfigurationSlug', 'ExpectedSignerThumbprint')) {
            if ([string]::IsNullOrWhiteSpace([string]$Inputs[$name])) {
                throw "Approved release signing requires the configured $name input."
            }
        }
        if ([string]$Inputs.ExpectedSignerThumbprint -notmatch '^[0-9A-Fa-f]{40}$') {
            throw 'Approved release signing requires a 40-hex signer certificate thumbprint.'
        }
        if (-not $PublisherOAuthConfigured) {
            throw 'Approved release signing requires publisher OAuth configuration.'
        }
    }
    return [pscustomobject]@{
        Enabled = [bool]$enabled
        UnsignedArtifactLabel = $unsignedLabel
    }
}

function Get-SignPathSignedFile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string]$FileName,
        [Parameter(Mandatory = $true)][string]$ExpectedSignerThumbprint
    )

    if ([string]::IsNullOrWhiteSpace($FileName) -or $FileName -in @('.', '..') -or
        $FileName.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
        $FileName -match '[/\\:*?]' -or $FileName.Trim() -cne $FileName -or
        $FileName.EndsWith('.', [StringComparison]::Ordinal)) {
        throw 'The signed artifact name must be a bare file name.'
    }
    if ($ExpectedSignerThumbprint -notmatch '^[0-9A-Fa-f]{40}$') {
        throw 'An approved 40-hex certificate thumbprint is required.'
    }
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        throw 'The signed artifact directory was not found.'
    }
    $directoryInfo = Get-Item -LiteralPath $Directory -Force
    if ($directoryInfo -isnot [IO.DirectoryInfo] -or
        ($directoryInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The signed artifact directory must be a regular file-system directory.'
    }
    $pending = New-Object 'System.Collections.Generic.Queue[System.IO.DirectoryInfo]'
    $pending.Enqueue($directoryInfo)
    $matches = @()
    while ($pending.Count -gt 0) {
        $current = $pending.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $current.FullName -Force -ErrorAction Stop)) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'The signed artifact tree must not contain reparse points.'
            }
            if ($item -is [IO.DirectoryInfo]) {
                $pending.Enqueue($item)
            } elseif ($item -is [IO.FileInfo] -and
                [string]::Equals($item.Name, $FileName, [StringComparison]::OrdinalIgnoreCase)) {
                $matches += $item.FullName
            }
        }
    }
    if ($matches.Count -ne 1) {
        throw 'Expected exactly one matching signed artifact file.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $matches[0]
    Assert-ReleaseSignature -Signature $signature -ExpectedSignerThumbprint $ExpectedSignerThumbprint -Label 'Returned SignPath artifact'
    return $matches[0]
}

function Copy-SignPathSignedFile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string]$FileName,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$ExpectedSignerThumbprint
    )

    $signedPath = Get-SignPathSignedFile -Directory $Directory -FileName $FileName -ExpectedSignerThumbprint $ExpectedSignerThumbprint
    if (-not (Test-Path -LiteralPath $Destination -PathType Leaf)) {
        throw 'The staged signing destination must already exist as a file.'
    }
    $destinationInfo = Get-Item -LiteralPath $Destination -Force
    if ($destinationInfo -isnot [IO.FileInfo] -or
        ($destinationInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The staged signing destination must be a regular file.'
    }
    $signedVersion = (Get-Item -LiteralPath $signedPath -Force).VersionInfo
    $stagedVersion = $destinationInfo.VersionInfo
    foreach ($name in @('ProductName', 'ProductVersion', 'FileVersion', 'OriginalFilename', 'CompanyName')) {
        $returnedValue = [string]$signedVersion.$name
        $stagedValue = [string]$stagedVersion.$name
        if ([string]::IsNullOrWhiteSpace($returnedValue) -or [string]::IsNullOrWhiteSpace($stagedValue) -or
            $returnedValue.Trim() -cne $stagedValue.Trim()) {
            throw "Returned SignPath artifact does not match staged $name metadata."
        }
    }
    $returnedHash = (Get-FileHash -LiteralPath $signedPath -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $signedPath -Destination $destinationInfo.FullName -Force -ErrorAction Stop
    $copiedHash = (Get-FileHash -LiteralPath $destinationInfo.FullName -Algorithm SHA256).Hash
    if ($copiedHash -cne $returnedHash) {
        throw 'Copied SignPath artifact does not match the returned artifact SHA-256.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $destinationInfo.FullName
    Assert-ReleaseSignature -Signature $signature -ExpectedSignerThumbprint $ExpectedSignerThumbprint -Label 'Copied SignPath artifact'
    return $destinationInfo.FullName
}

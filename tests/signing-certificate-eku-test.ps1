[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SigningCertificateUtils
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SigningCertificateUtils -PathType Leaf)) {
    throw "Signing certificate utility was not found: $SigningCertificateUtils"
}

. $SigningCertificateUtils

$codeSigningOid = "1.3.6.1.5.5.7.3.3"

$windowsPowerShellShape = [PSCustomObject]@{
    EnhancedKeyUsageList = @(
        [PSCustomObject]@{ ObjectId = $codeSigningOid }
    )
}
if (-not (Test-CertificateHasEnhancedKeyUsage -Certificate $windowsPowerShellShape -Oid $codeSigningOid)) {
    throw "Windows PowerShell string ObjectId shape was not recognized."
}

$powershellCoreShape = [PSCustomObject]@{
    EnhancedKeyUsageList = @(
        [PSCustomObject]@{
            ObjectId = [PSCustomObject]@{ Value = $codeSigningOid }
        }
    )
}
if (-not (Test-CertificateHasEnhancedKeyUsage -Certificate $powershellCoreShape -Oid $codeSigningOid)) {
    throw "PowerShell object ObjectId shape was not recognized."
}

$wrongUsageShape = [PSCustomObject]@{
    EnhancedKeyUsageList = @(
        [PSCustomObject]@{ ObjectId = "1.3.6.1.5.5.7.3.1" }
    )
}
if (Test-CertificateHasEnhancedKeyUsage -Certificate $wrongUsageShape -Oid $codeSigningOid) {
    throw "A non-code-signing EKU was accepted."
}

$missingUsageShape = [PSCustomObject]@{ EnhancedKeyUsageList = @() }
if (Test-CertificateHasEnhancedKeyUsage -Certificate $missingUsageShape -Oid $codeSigningOid) {
    throw "A certificate without EKUs was accepted."
}

Write-Output "Signing certificate EKU compatibility checks passed."

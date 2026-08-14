function Get-EnhancedKeyUsageObjectId {
    param([Parameter(Mandatory = $true)]$EnhancedKeyUsage)

    $objectId = $EnhancedKeyUsage.ObjectId
    if ($null -eq $objectId) {
        return ""
    }
    if ($objectId -is [string]) {
        return $objectId
    }

    $valueProperty = $objectId.PSObject.Properties["Value"]
    if ($null -ne $valueProperty) {
        return [string]$valueProperty.Value
    }
    return [string]$objectId
}

function Test-CertificateHasEnhancedKeyUsage {
    param(
        [Parameter(Mandatory = $true)]$Certificate,
        [Parameter(Mandatory = $true)][string]$Oid
    )

    foreach ($enhancedKeyUsage in @($Certificate.EnhancedKeyUsageList)) {
        if ((Get-EnhancedKeyUsageObjectId -EnhancedKeyUsage $enhancedKeyUsage) -eq $Oid) {
            return $true
        }
    }
    return $false
}

param(
    [string]$OutputPath = "",
    [string]$RelayUrl = "https://45-77-181-113.sslip.io",
    [int]$CodeIntegrityLookbackDays = 7
)

$ErrorActionPreference = "Stop"

function Get-PropertyString {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return ""
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return ""
    }
    return [string]$property.Value
}

function Get-SafeSignature {
    param([string]$Path)

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    return [ordered]@{
        status = [string]$signature.Status
        signer = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { "" }
    }
}

function Protect-LogLine {
    param([string]$Line)

    $safe = $Line
    $safe = $safe -replace '(?i)(access_token|refresh_token|client_secret|stream_key|authorization)(\s*[=:]\s*)[^\s,}&]+', '$1$2[redacted]'
    $safe = $safe -replace '(?i)([?&](?:key|token)=)[^&\s]+', '$1[redacted]'
    return $safe
}

function Resolve-AbsoluteOutputPath {
    param([string]$Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

$checks = [Collections.Generic.List[object]]::new()
function Add-Check {
    param(
        [string]$Name,
        [ValidateSet("ok", "warning", "error", "info")]
        [string]$Status,
        [string]$Detail,
        [object]$Data = $null
    )

    $checks.Add([pscustomobject][ordered]@{
        name = $Name
        status = $Status
        detail = $Detail
        data = $Data
    })
}

$obsProcesses = @(Get-Process -Name "obs64" -ErrorAction SilentlyContinue)
Add-Check -Name "obs-process" -Status "info" -Detail $(
    if ($obsProcesses.Count -gt 0) { "OBS is running; this diagnostic remains read-only." } else { "OBS is not running." }
) -Data ([ordered]@{ count = $obsProcesses.Count })

$obsExeCandidates = @(
    "$env:ProgramFiles\obs-studio\bin\64bit\obs64.exe",
    "${env:ProgramFiles(x86)}\obs-studio\bin\64bit\obs64.exe"
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$obsExe = $obsExeCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ($obsExe) {
    $version = (Get-Item -LiteralPath $obsExe).VersionInfo.FileVersion
    Add-Check -Name "obs-install" -Status "ok" -Detail "OBS executable found." -Data ([ordered]@{
        path = $obsExe
        version = $version
        signature = Get-SafeSignature -Path $obsExe
    })
    $obsLibcurl = Join-Path (Split-Path -Parent $obsExe) "libcurl.dll"
    if (Test-Path -LiteralPath $obsLibcurl -PathType Leaf) {
        $libcurlSignature = Get-SafeSignature -Path $obsLibcurl
        Add-Check -Name "signed-http-runtime" -Status $(
            if ($libcurlSignature.status -eq "Valid") { "ok" } else { "error" }
        ) -Detail $(
            if ($libcurlSignature.status -eq "Valid") { "OBS signed libcurl runtime is available for OAuth and platform APIs." }
            else { "OBS libcurl runtime is not signed by a trusted publisher." }
        ) -Data ([ordered]@{ path = $obsLibcurl; signature = $libcurlSignature })
    } else {
        Add-Check -Name "signed-http-runtime" -Status "error" -Detail "OBS libcurl runtime was not found."
    }
} else {
    Add-Check -Name "obs-install" -Status "error" -Detail "OBS executable was not found in the standard install locations."
}

$pluginCandidates = @(
    "$env:ProgramData\obs-studio\plugins\obs-dsk-multistream\bin\64bit\obs-dsk-multistream.dll",
    "$env:APPDATA\obs-studio\plugins\obs-dsk-multistream\bin\64bit\obs-dsk-multistream.dll",
    "$env:APPDATA\obs-studio\plugins\obs-dsk-multistream.dll",
    "$env:ProgramFiles\obs-studio\obs-plugins\64bit\obs-dsk-multistream.dll"
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$pluginDlls = @($pluginCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -Unique)
$pluginDetails = @(
    foreach ($dll in $pluginDlls) {
        [ordered]@{
            path = $dll
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash
            signature = Get-SafeSignature -Path $dll
        }
    }
)
$unsignedPluginCount = @($pluginDetails | Where-Object { $_.signature.status -ne "Valid" }).Count
if ($pluginDlls.Count -eq 0) {
    Add-Check -Name "plugin-install" -Status "warning" -Detail "No installed DSK plugin DLL was found." -Data $pluginDetails
} elseif ($pluginDlls.Count -eq 1 -and $unsignedPluginCount -eq 0) {
    Add-Check -Name "plugin-install" -Status "ok" -Detail "One installed DSK plugin DLL was found." -Data $pluginDetails
} elseif ($pluginDlls.Count -eq 1) {
    Add-Check -Name "plugin-install" -Status "warning" -Detail "The installed DSK plugin is not Authenticode signed and may be blocked by Application Control." -Data $pluginDetails
} else {
    Add-Check -Name "plugin-install" -Status "warning" -Detail "Multiple DSK plugin DLLs were found; OBS may load an unintended copy." -Data $pluginDetails
}

if ($pluginDlls.Count -gt 0) {
    $pluginRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $pluginDlls[0]))
    $requiredFiles = @(
        (Join-Path $pluginRoot "data\locale\en-US.ini"),
        (Join-Path $pluginRoot "data\presets\platforms.json")
    )
    $missingFiles = @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    Add-Check -Name "plugin-runtime-files" -Status $(
        if ($missingFiles.Count -eq 0) { "ok" } else { "error" }
    ) -Detail $(
        if ($missingFiles.Count -eq 0) { "Plugin data files are present." } else { "Required plugin runtime files are missing." }
    ) -Data ([ordered]@{ missing = $missingFiles })

    $obsoleteTlsPlugins = @(
        (Join-Path $pluginRoot "bin\64bit\tls\qcertonlybackend.dll"),
        (Join-Path $pluginRoot "bin\64bit\tls\qschannelbackend.dll")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
    Add-Check -Name "obsolete-qt-tls-plugins" -Status $(
        if ($obsoleteTlsPlugins.Count -eq 0) { "ok" } else { "warning" }
    ) -Detail $(
        if ($obsoleteTlsPlugins.Count -eq 0) { "No obsolete unsigned Qt TLS plugins are bundled." }
        else { "Obsolete Qt TLS plugins remain in the DSK package and can be blocked by Application Control." }
    ) -Data ([ordered]@{ paths = $obsoleteTlsPlugins })
}

$settingsRoots = @(
    "$env:APPDATA\obs-studio\basic\profiles",
    "$env:APPDATA\obs-studio\plugin_config"
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$settingsFiles = @(
    foreach ($root in $settingsRoots) {
        Get-ChildItem -LiteralPath $root -Recurse -Filter "dsk-multistream.json" -File -ErrorAction SilentlyContinue
    }
)
$settingsDetails = @()
$settingsErrors = 0
$plaintextSecretCount = 0
$foreignCredentialRefCount = 0
foreach ($file in $settingsFiles) {
    try {
        $settings = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
        $targets = @($settings.targets)
        $filePlaintext = 0
        $fileRefs = 0
        $fileForeignRefs = 0
        foreach ($target in $targets) {
            foreach ($secretName in @("streamKey", "oauthClientSecret", "oauthRefreshToken")) {
                if (-not [string]::IsNullOrWhiteSpace((Get-PropertyString -Object $target -Name $secretName))) {
                    $filePlaintext++
                }
            }
            foreach ($refName in @("authCredentialRef", "oauthClientSecretRef", "oauthRefreshTokenRef")) {
                $reference = (Get-PropertyString -Object $target -Name $refName).Trim()
                if ([string]::IsNullOrWhiteSpace($reference)) {
                    continue
                }
                $fileRefs++
                if (-not $reference.StartsWith("DSK Multistream/", [StringComparison]::OrdinalIgnoreCase)) {
                    $fileForeignRefs++
                }
            }
        }
        $plaintextSecretCount += $filePlaintext
        $foreignCredentialRefCount += $fileForeignRefs
        $settingsDetails += [ordered]@{
            path = $file.FullName
            parsed = $true
            targetCount = $targets.Count
            plaintextSecretFieldCount = $filePlaintext
            credentialReferenceCount = $fileRefs
            foreignCredentialReferenceCount = $fileForeignRefs
        }
    } catch {
        $settingsErrors++
        $settingsDetails += [ordered]@{
            path = $file.FullName
            parsed = $false
            error = $_.Exception.Message
        }
    }
}
$settingsStatus = if ($settingsErrors -gt 0 -or $plaintextSecretCount -gt 0 -or $foreignCredentialRefCount -gt 0) { "error" } elseif ($settingsFiles.Count -eq 0) { "info" } else { "ok" }
$settingsDetail = if ($settingsErrors -gt 0) {
    "One or more DSK settings files could not be parsed."
} elseif ($plaintextSecretCount -gt 0) {
    "Plaintext secret fields remain in DSK settings."
} elseif ($foreignCredentialRefCount -gt 0) {
    "Credential references outside the DSK namespace were found."
} elseif ($settingsFiles.Count -eq 0) {
    "No profile DSK settings files were found."
} else {
    "DSK settings parsed without plaintext secrets or foreign credential references."
}
Add-Check -Name "settings" -Status $settingsStatus -Detail $settingsDetail -Data $settingsDetails

$listener17371 = @([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() | Where-Object { $_.Port -eq 17371 })
Add-Check -Name "oauth-callback-port" -Status $(
    if ($listener17371.Count -gt 0) { "warning" } else { "ok" }
) -Detail $(
    if ($listener17371.Count -gt 0) { "Port 17371 is already listening; a new OAuth callback may not be able to bind." } else { "Port 17371 is available." }
) -Data ([ordered]@{ listenerCount = $listener17371.Count })

foreach ($relayPlatform in @("twitch", "kick")) {
    try {
        $readyUri = "$($RelayUrl.TrimEnd('/'))/v1/ready?platform=$relayPlatform&profile=multistream"
        $ready = Invoke-RestMethod -Method Get -Uri $readyUri -TimeoutSec 8 -Headers @{ Accept = "application/json" }
        $readyOk = [bool]$ready.ok -and $ready.platform -eq $relayPlatform -and $ready.profile -eq "multistream"
        $displayPlatform = (Get-Culture).TextInfo.ToTitleCase($relayPlatform)
        Add-Check -Name "oauth-relay-$relayPlatform" -Status $(
            if ($readyOk) { "ok" } else { "error" }
        ) -Detail $(
            if ($readyOk) { "$displayPlatform publisher login relay is ready." } else { "OAuth relay returned unexpected readiness metadata." }
        ) -Data ([ordered]@{ uri = $readyUri; service = [string]$ready.service })
    } catch {
        Add-Check -Name "oauth-relay-$relayPlatform" -Status "error" -Detail "$relayPlatform publisher login relay could not be reached." -Data ([ordered]@{
            error = $_.Exception.Message
        })
    }
}

$logDirectory = "$env:APPDATA\obs-studio\logs"
$latestLog = $null
if (Test-Path -LiteralPath $logDirectory -PathType Container) {
    $latestLog = Get-ChildItem -LiteralPath $logDirectory -Filter "*.txt" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}
if ($latestLog) {
    $dskLines = @(Select-String -LiteralPath $latestLog.FullName -Pattern '\[DSK Multistream\]|obs-dsk-multistream' -ErrorAction SilentlyContinue |
        Select-Object -Last 30 |
        ForEach-Object { Protect-LogLine -Line $_.Line })
    $errorLines = @($dskLines | Where-Object { $_ -match '(?i)error|failed|crash|exception' })
    Add-Check -Name "latest-obs-log" -Status $(
        if ($errorLines.Count -gt 0) { "warning" } else { "info" }
    ) -Detail "Latest OBS log was inspected without launching OBS." -Data ([ordered]@{
        path = $latestLog.FullName
        dskLineCount = $dskLines.Count
        dskErrorLineCount = $errorLines.Count
        recentDskLines = $dskLines
    })
} else {
    Add-Check -Name "latest-obs-log" -Status "info" -Detail "No OBS log was found."
}

try {
    $since = (Get-Date).AddDays(-[Math]::Abs($CodeIntegrityLookbackDays))
    $ciEvents = @(Get-WinEvent -FilterHashtable @{
        LogName = "Microsoft-Windows-CodeIntegrity/Operational"
        Id = 3033, 3077
        StartTime = $since
    } -ErrorAction Stop | Where-Object {
        $_.Message -match 'obs-dsk-multistream|dsk-(?:ui-)?smoke-tests|dsk-settings-store-tests'
    })
    $ciDetails = @($ciEvents | Select-Object -First 20 | ForEach-Object {
        [ordered]@{
            time = $_.TimeCreated.ToString("o")
            id = $_.Id
        }
    })
    Add-Check -Name "code-integrity" -Status $(
        if ($ciEvents.Count -gt 0) { "warning" } else { "ok" }
    ) -Detail $(
        if ($ciEvents.Count -gt 0) { "Windows Application Control blocked one or more DSK development artifacts." } else { "No recent DSK Code Integrity blocks were found." }
    ) -Data ([ordered]@{ eventCount = $ciEvents.Count; recentEvents = $ciDetails })
} catch {
    Add-Check -Name "code-integrity" -Status "info" -Detail "Code Integrity events could not be read." -Data ([ordered]@{
        error = $_.Exception.Message
    })
}

$summary = [ordered]@{
    ok = @($checks | Where-Object { $_.status -eq "ok" }).Count
    warning = @($checks | Where-Object { $_.status -eq "warning" }).Count
    error = @($checks | Where-Object { $_.status -eq "error" }).Count
    info = @($checks | Where-Object { $_.status -eq "info" }).Count
}
$report = [ordered]@{
    schemaVersion = 1
    generatedAt = (Get-Date).ToString("o")
    readOnly = $true
    secretsIncluded = $false
    machine = [ordered]@{
        os = [Environment]::OSVersion.VersionString
        is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem
        powershell = $PSVersionTable.PSVersion.ToString()
    }
    summary = $summary
    checks = $checks
}
$json = $report | ConvertTo-Json -Depth 8
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $resolvedOutput = Resolve-AbsoluteOutputPath -Path $OutputPath
    $parent = Split-Path -Parent $resolvedOutput
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    [IO.File]::WriteAllText($resolvedOutput, $json, [Text.UTF8Encoding]::new($false))
    Write-Host "Wrote read-only DSK diagnostic report to $resolvedOutput"
} else {
    $json
}

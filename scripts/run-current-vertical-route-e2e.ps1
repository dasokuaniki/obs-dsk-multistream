param(
    [Parameter(Mandatory = $true)]
    [string]$ProfileName,
    [Parameter(Mandatory = $true)]
    [string]$CollectionName,
    [string]$FfmpegPath = "",
    [string]$ObsPath = "C:\Program Files\obs-studio\bin\64bit\obs64.exe",
    [string]$LogDir = "build\obs-current-vertical-e2e",
    [int]$Port = 19355,
    [int]$StartDelayMs = 8000,
    [int]$RunMs = 10000,
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"

function Resolve-Ffmpeg {
    param([string]$RequestedPath)

    if ($RequestedPath -and (Test-Path -LiteralPath $RequestedPath)) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $portable = Get-ChildItem -LiteralPath "deps\ffmpeg-portable" -Recurse -Filter ffmpeg.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($portable) {
        return $portable.FullName
    }

    $command = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "ffmpeg.exe was not found. Pass -FfmpegPath or place a portable build under deps\ffmpeg-portable."
}

function Stop-TestProcess {
    param([System.Diagnostics.Process]$Process)

    if ($Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        $Process.WaitForExit(10000) | Out-Null
    }
}

function Start-ObsWithEnvironment {
    param(
        [string]$FilePath,
        [string]$WorkingDirectory,
        [hashtable]$Environment,
        [string]$Arguments
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $startInfo.Arguments = $Arguments
    foreach ($key in $Environment.Keys) {
        $startInfo.Environment[$key] = [string]$Environment[$key]
    }

    return [System.Diagnostics.Process]::Start($startInfo)
}

$existingObs = Get-Process obs64 -ErrorAction SilentlyContinue
if ($existingObs) {
    $ids = ($existingObs | Select-Object -ExpandProperty Id) -join ", "
    throw "OBS is already running. Close it before this route test. Running process id(s): $ids"
}

$ffmpeg = Resolve-Ffmpeg $FfmpegPath
if (-not (Test-Path -LiteralPath $ObsPath)) {
    throw "OBS executable was not found: $ObsPath"
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$server = "rtmp://127.0.0.1:$Port/live"
$key = "dsk-current-vertical-$stamp"
$rtmpUrl = "$server/$key"
$capturePath = Join-Path $LogDir "received-$stamp.flv"
$framePath = Join-Path $LogDir "received-$stamp.png"
$listenerOut = Join-Path $LogDir "listener-$stamp.out.log"
$listenerErr = Join-Path $LogDir "listener-$stamp.err.log"

$settingsPath = Join-Path $env:APPDATA "obs-studio\basic\profiles\$ProfileName\dsk-multistream.json"
if (-not (Test-Path -LiteralPath $settingsPath)) {
    throw "DSK settings were not found for profile '$ProfileName': $settingsPath"
}
$settingsBackup = "$settingsPath.route-e2e-$stamp.bak"
Copy-Item -LiteralPath $settingsPath -Destination $settingsBackup
$settingsHashBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $settingsPath).Hash

$obsLogRoot = Join-Path $env:APPDATA "obs-studio\logs"
$existingLogs = @{}
if (Test-Path -LiteralPath $obsLogRoot) {
    foreach ($log in Get-ChildItem -LiteralPath $obsLogRoot -Filter *.txt -ErrorAction SilentlyContinue) {
        $existingLogs[$log.FullName] = $true
    }
}
$crashRoot = Join-Path $env:APPDATA "obs-studio\crashes"
$latestCrashBefore = Get-ChildItem -LiteralPath $crashRoot -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1

$listenerArgs = @(
    "-hide_banner",
    "-loglevel", "info",
    "-listen", "1",
    "-i", $rtmpUrl,
    "-t", "$([Math]::Ceiling($RunMs / 1000) + 5)",
    "-map", "0:v:0",
    "-map", "0:a?",
    "-c", "copy",
    "-y", $capturePath
)

$listener = $null
$obs = $null
try {
    $listener = Start-Process -WindowStyle Hidden -FilePath $ffmpeg -ArgumentList $listenerArgs `
        -RedirectStandardOutput $listenerOut -RedirectStandardError $listenerErr -PassThru
    Start-Sleep -Seconds 2

    $environment = @{
        DSK_E2E_AUTORUN = "1"
        DSK_E2E_SERVER = $server
        DSK_E2E_KEY = $key
        DSK_E2E_ENCODER_GROUP = "dsk-vertical"
        DSK_E2E_USE_CURRENT_VERTICAL_LAYOUT = "1"
        DSK_E2E_START_DELAY_MS = "$StartDelayMs"
        DSK_E2E_RUN_MS = "$RunMs"
        DSK_E2E_QUIT_OBS = "1"
        DSK_E2E_DISABLE_RECONNECT = "1"
    }
    $obsArguments = "--disable-shutdown-check --disable-missing-files-check --disable-updater --collection `"$CollectionName`" --profile `"$ProfileName`""
    $obs = Start-ObsWithEnvironment -FilePath $ObsPath -WorkingDirectory (Split-Path -Parent $ObsPath) `
        -Arguments $obsArguments -Environment $environment

    if (-not $obs.WaitForExit($TimeoutSeconds * 1000)) {
        throw "OBS route test timed out after $TimeoutSeconds seconds."
    }
    if (-not $listener.WaitForExit(15000)) {
        throw "RTMP listener did not exit after OBS completed."
    }
    $listener.WaitForExit()
    $listener.Refresh()
    if ($null -ne $listener.ExitCode -and $listener.ExitCode -ne 0) {
        throw "RTMP listener failed with exit code $($listener.ExitCode). See $listenerErr"
    }
    if (-not (Test-Path -LiteralPath $capturePath) -or (Get-Item -LiteralPath $capturePath).Length -le 0) {
        throw "RTMP listener did not create a media capture. See $listenerErr"
    }

    $listenerText = Get-Content -LiteralPath $listenerErr -Raw
    if ($listenerText -notmatch "1080x1920" -or $listenerText -notmatch "frame=") {
        throw "Received media did not contain 1080x1920 video frames. See $listenerErr"
    }

    & $ffmpeg -hide_banner -loglevel error -ss 2 -i $capturePath -frames:v 1 -y $framePath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $framePath)) {
        throw "Could not extract a verification frame from $capturePath"
    }

    $newObsLog = Get-ChildItem -LiteralPath $obsLogRoot -Filter *.txt -ErrorAction SilentlyContinue |
        Where-Object { -not $existingLogs.ContainsKey($_.FullName) } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $newObsLog) {
        throw "OBS did not create a new log for the route test."
    }
    $obsLogText = Get-Content -LiteralPath $newObsLog.FullName -Raw
    if ($obsLogText -notmatch "E2E using current vertical layout 1080x1920") {
        throw "OBS did not confirm use of the current vertical layout. See $($newObsLog.FullName)"
    }
    if ($obsLogText -notmatch "E2E auto-run complete") {
        throw "OBS did not complete the route test. See $($newObsLog.FullName)"
    }

    $settingsHashAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $settingsPath).Hash
    $latestCrashAfter = Get-ChildItem -LiteralPath $crashRoot -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $newCrash = $latestCrashAfter -and (
        -not $latestCrashBefore -or $latestCrashAfter.FullName -ne $latestCrashBefore.FullName
    )
    if ($newCrash) {
        throw "OBS created a new crash report: $($latestCrashAfter.FullName)"
    }

    [pscustomobject]@{
        Result = "PASS"
        RtmpUrl = $rtmpUrl
        Resolution = "1080x1920"
        Capture = (Resolve-Path -LiteralPath $capturePath).Path
        Frame = (Resolve-Path -LiteralPath $framePath).Path
        ObsLog = $newObsLog.FullName
        ListenerLog = (Resolve-Path -LiteralPath $listenerErr).Path
        SettingsBackup = (Resolve-Path -LiteralPath $settingsBackup).Path
        SettingsUnchanged = $settingsHashBefore -eq $settingsHashAfter
        NewCrash = $false
    } | Format-List
} finally {
    Stop-TestProcess $obs
    Stop-TestProcess $listener
}

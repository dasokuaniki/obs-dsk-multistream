param(
    [string]$FfmpegPath = "",
    [string]$ObsPath = "C:\Program Files\obs-studio\bin\64bit\obs64.exe",
    [string]$LogDir = "build\obs-rtmp-e2e",
    [int]$Port = 19354,
    [int]$SecondPort = 0,
    [int]$StartDelayMs = 7000,
    [int]$RunMs = 8000,
    [int]$TimeoutSeconds = 45,
    [string]$EncoderGroup = "dsk-horizontal",
    [string]$SecondEncoderGroup = "",
    [ValidateSet("direct", "initial-disabled", "toggle")]
    [string]$RouteMode = "direct",
    [switch]$VerticalUiStress,
    [ValidateRange(1, 1000)]
    [int]$VerticalUiStressIterations = 100,
    [switch]$CleanupE2eConfig,
    [switch]$ExpectStartFailure
)

$ErrorActionPreference = "Stop"

if ($VerticalUiStress) {
    # Leave enough time for each save/rebuild cycle to reach the graphics thread.
    $minimumStressRunMs = 10000 + ($VerticalUiStressIterations * 40)
    $RunMs = [Math]::Max($RunMs, $minimumStressRunMs)
    $minimumTimeoutSeconds = [Math]::Ceiling(($StartDelayMs + $RunMs) / 1000) + 30
    $TimeoutSeconds = [Math]::Max($TimeoutSeconds, $minimumTimeoutSeconds)
}

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

function Stop-ProcessIfRunning {
    param([System.Diagnostics.Process]$Process)

    if ($Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        $Process.WaitForExit(10000) | Out-Null
    }
}

function Assert-ListenerReceived {
    param(
        [System.Diagnostics.Process]$ListenerProcess,
        [string]$ListenerErrPath,
        [string]$ExpectedEncoderGroup
    )

    $ListenerProcess.Refresh()
    if ($null -ne $ListenerProcess.ExitCode -and $ListenerProcess.ExitCode -ne 0) {
        throw "RTMP listener failed with exit code $($ListenerProcess.ExitCode). See $ListenerErrPath"
    }

    $listenerLog = Get-Content -LiteralPath $ListenerErrPath -Raw
    if ($listenerLog -match "Conversion failed") {
        throw "RTMP listener conversion failed. See $ListenerErrPath"
    }
    if ($listenerLog -notmatch "Input #0" -or $listenerLog -notmatch "Stream #0" -or $listenerLog -notmatch "frame=") {
        throw "RTMP listener did not show received OBS media frames. See $ListenerErrPath"
    }
    if ($ExpectedEncoderGroup -eq "dsk-horizontal" -and $listenerLog -notmatch "1920x1080") {
        throw "RTMP listener did not show expected 1920x1080 horizontal video. See $ListenerErrPath"
    }
    if ($ExpectedEncoderGroup -eq "dsk-vertical" -and $listenerLog -notmatch "1080x1920") {
        throw "RTMP listener did not show expected 1080x1920 vertical video. See $ListenerErrPath"
    }
}

function Assert-ListenerDidNotReceive {
    param(
        [System.Diagnostics.Process]$ListenerProcess,
        [string]$ListenerErrPath
    )

    if (-not $ListenerProcess) {
        return
    }

    $ListenerProcess.Refresh()
    if (-not $ListenerProcess.HasExited) {
        Stop-ProcessIfRunning $ListenerProcess
    }

    $listenerLog = ""
    if (Test-Path -LiteralPath $ListenerErrPath) {
        $listenerLog = Get-Content -LiteralPath $ListenerErrPath -Raw
    }
    if ($listenerLog -match "Input #0" -or $listenerLog -match "Stream #0" -or $listenerLog -match "frame=") {
        throw "RTMP listener received media even though the route was disabled. See $ListenerErrPath"
    }
}

function Backup-ObsUserState {
    $userIni = Join-Path $env:APPDATA "obs-studio\user.ini"
    if (-not (Test-Path -LiteralPath $userIni)) {
        return @{
            Path = $userIni
            Exists = $false
            Bytes = $null
        }
    }

    return @{
        Path = $userIni
        Exists = $true
        Bytes = [System.IO.File]::ReadAllBytes($userIni)
    }
}

function Restore-ObsUserState {
    param($State)

    if (-not $State) {
        return
    }

    if ($State.Exists) {
        $parent = Split-Path -Parent $State.Path
        if ($parent) {
            New-Item -ItemType Directory -Force -Path $parent | Out-Null
        }
        [System.IO.File]::WriteAllBytes($State.Path, [byte[]]$State.Bytes)
    } elseif (Test-Path -LiteralPath $State.Path) {
        Remove-Item -LiteralPath $State.Path -Force -ErrorAction SilentlyContinue
    }
}

function Remove-E2eObsConfig {
    param([string]$CollectionName)

    $obsRoot = Join-Path $env:APPDATA "obs-studio"
    $scenePath = Join-Path $obsRoot "basic\scenes\$CollectionName.json"
    $sceneBackupPath = "$scenePath.bak"
    $profileDir = Join-Path $obsRoot "basic\profiles\$CollectionName"

    Remove-Item -LiteralPath $scenePath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $sceneBackupPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $profileDir -Recurse -Force -ErrorAction SilentlyContinue
}

function New-ObsSourceBase {
    param(
        [string]$Name,
        [string]$Id,
        [hashtable]$Settings
    )

    return [ordered]@{
        prev_ver = 536936450
        name = $Name
        uuid = [guid]::NewGuid().ToString()
        id = $Id
        versioned_id = $Id
        settings = $Settings
        mixers = 0
        sync = 0
        flags = 0
        volume = 1.0
        balance = 0.5
        enabled = $true
        muted = $false
        "push-to-mute" = $false
        "push-to-mute-delay" = 0
        "push-to-talk" = $false
        "push-to-talk-delay" = 0
        hotkeys = [ordered]@{}
        deinterlace_mode = 0
        deinterlace_field_order = 0
        monitoring_type = 0
        private_settings = [ordered]@{}
    }
}

function Ensure-E2eObsConfig {
    $collectionName = "DSK E2E"
    $sceneName = "DSK E2E Scene"
    $colorSourceName = "DSK E2E Horizontal Source"
    $mainCanvasUuid = "6c69626f-6273-4c00-9d88-c5136d61696e"
    $obsRoot = Join-Path $env:APPDATA "obs-studio"
    $scenesDir = Join-Path $obsRoot "basic\scenes"
    $profileDir = Join-Path $obsRoot "basic\profiles\$collectionName"

    New-Item -ItemType Directory -Force -Path $scenesDir | Out-Null
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null

    $colorSource = New-ObsSourceBase -Name $colorSourceName -Id "color_source" -Settings ([ordered]@{
        color = 4281171199
        width = 1920
        height = 1080
    })

    $sceneItem = [ordered]@{
        name = $colorSourceName
        source_uuid = $colorSource.uuid
        visible = $true
        locked = $false
        rot = 0.0
        scale_ref = [ordered]@{
            x = 1920.0
            y = 1080.0
        }
        align = 5
        bounds_type = 0
        bounds_align = 0
        bounds_crop = $false
        crop_left = 0
        crop_top = 0
        crop_right = 0
        crop_bottom = 0
        id = 1
        group_item_backup = $false
        pos = [ordered]@{
            x = 0.0
            y = 0.0
        }
        pos_rel = [ordered]@{
            x = -1.7777777910232544
            y = -1.0
        }
        scale = [ordered]@{
            x = 1.0
            y = 1.0
        }
        scale_rel = [ordered]@{
            x = 1.0
            y = 1.0
        }
        bounds = [ordered]@{
            x = 0.0
            y = 0.0
        }
        bounds_rel = [ordered]@{
            x = 0.0
            y = 0.0
        }
        scale_filter = "disable"
        blend_method = "default"
        blend_type = "normal"
        show_transition = [ordered]@{ duration = 0 }
        hide_transition = [ordered]@{ duration = 0 }
        private_settings = [ordered]@{}
    }

    $sceneSource = New-ObsSourceBase -Name $sceneName -Id "scene" -Settings ([ordered]@{
        id_counter = 1
        custom_size = $false
        items = @($sceneItem)
    })
    $sceneSource.hotkeys = [ordered]@{
        "libobs.show_scene_item.1" = @()
        "libobs.hide_scene_item.1" = @()
    }
    $sceneSource.canvas_uuid = $mainCanvasUuid

    $sceneCollection = [ordered]@{
        name = $collectionName
        sources = @($colorSource, $sceneSource)
        groups = @()
        scene_order = @([ordered]@{ name = $sceneName })
        current_scene = $sceneName
        current_program_scene = $sceneName
        canvases = @(
            [ordered]@{
                info = [ordered]@{
                    name = "Aitum Vertical"
                    uuid = "45bc8df7-8f4a-41a7-b9d1-0ff92878e799"
                    private = $false
                    flags = 14
                }
            },
            [ordered]@{
                info = [ordered]@{
                    name = "DSK Vertical"
                    uuid = "5e1d9549-4831-4ef8-bae1-584d3d865425"
                    private = $false
                    flags = 14
                }
            }
        )
        current_transition = "Cut"
        transition_duration = 300
        transitions = @()
        quick_transitions = @(
            [ordered]@{
                name = "Cut"
                duration = 300
                hotkeys = @()
                id = 1
                fade_to_black = $false
            }
        )
        saved_projectors = @()
        preview_locked = $false
        scaling_enabled = $false
        scaling_level = 0
        scaling_off_x = 0.0
        scaling_off_y = 0.0
        modules = [ordered]@{
            "scripts-tool" = @()
            "output-timer" = [ordered]@{
                streamTimerHours = 0
                streamTimerMinutes = 0
                streamTimerSeconds = 30
                recordTimerHours = 0
                recordTimerMinutes = 0
                recordTimerSeconds = 30
                autoStartStreamTimer = $false
                autoStartRecordTimer = $false
                pauseRecordTimer = $true
            }
            "auto-scene-switcher" = [ordered]@{
                interval = 300
                non_matching_scene = ""
                switch_if_not_matching = $false
                active = $false
                switches = @()
            }
        }
        resolution = [ordered]@{
            x = 1920
            y = 1080
        }
        version = 2
    }

    $scenePath = Join-Path $scenesDir "$collectionName.json"
    $sceneCollection | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $scenePath -Encoding UTF8

    $basicIni = @"
[General]
Name=$collectionName

[Output]
Mode=Simple
Reconnect=true
RetryDelay=2
MaxRetries=25
BindIP=default
IPFamily=IPv4+IPv6

[SimpleOutput]
FilePath=$env:USERPROFILE\Videos
RecFormat2=hybrid_mp4
VBitrate=6000
ABitrate=160
Preset=veryfast
StreamAudioEncoder=aac
RecAudioEncoder=aac
RecTracks=1
StreamEncoder=x264
RecEncoder=x264
UseAdvanced=false

[AdvOut]
TrackIndex=1
VodTrackIndex=2
Encoder=obs_x264
AudioEncoder=ffmpeg_aac
Track1Bitrate=160

[Video]
BaseCX=1920
BaseCY=1080
OutputCX=1920
OutputCY=1080
FPSType=0
FPSCommon=60
FPSInt=30
FPSNum=30
FPSDen=1
ScaleType=bicubic
ColorFormat=NV12
ColorSpace=709
ColorRange=Partial
SdrWhiteLevel=300
HdrNominalPeakLevel=1000

[Audio]
MonitoringDeviceId=default
MonitoringDeviceName=Default
SampleRate=48000
ChannelSetup=Stereo
MeterDecayRate=23.53
PeakMeterType=0
"@
    Set-Content -LiteralPath (Join-Path $profileDir "basic.ini") -Value $basicIni -Encoding UTF8

    $service = [ordered]@{
        type = "rtmp_custom"
        settings = [ordered]@{
            server = "rtmp://127.0.0.1/live"
            key = ""
        }
    }
    $service | ConvertTo-Json -Depth 5 -Compress | Set-Content -LiteralPath (Join-Path $profileDir "service.json") -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $profileDir "streamEncoder.json") -Value "{}" -Encoding UTF8

    return @{
        Collection = $collectionName
        Profile = $collectionName
    }
}

function Suspend-ObsPluginManagerModules {
    param([string[]]$ModuleNames)

    $modulesPath = Join-Path $env:APPDATA "obs-studio\plugin_manager\modules.json"
    $backupPath = "$modulesPath.dsk-e2e.bak"
    if (Test-Path -LiteralPath $backupPath) {
        [System.IO.File]::WriteAllBytes($modulesPath, [System.IO.File]::ReadAllBytes($backupPath))
        Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
    }

    if (-not (Test-Path -LiteralPath $modulesPath)) {
        return $null
    }

    $raw = Get-Content -LiteralPath $modulesPath -Raw
    $originalBytes = [System.IO.File]::ReadAllBytes($modulesPath)
    $modules = $raw | ConvertFrom-Json
    $changed = $false
    foreach ($module in $modules) {
        if ($ModuleNames -contains $module.module_name -and $module.enabled) {
            $module.enabled = $false
            $changed = $true
        }
    }

    if (-not $changed) {
        return $null
    }

    [System.IO.File]::WriteAllBytes($backupPath, $originalBytes)
    $modules | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $modulesPath -Encoding UTF8
    return @{
        Path = $modulesPath
        BackupPath = $backupPath
        Bytes = $originalBytes
    }
}

function Restore-ObsPluginManagerModules {
    param($SuspendedState)

    if (-not $SuspendedState) {
        return
    }

    [System.IO.File]::WriteAllBytes($SuspendedState.Path, [byte[]]$SuspendedState.Bytes)
    if (Test-Path -LiteralPath $SuspendedState.BackupPath) {
        Remove-Item -LiteralPath $SuspendedState.BackupPath -Force -ErrorAction SilentlyContinue
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

$ffmpeg = Resolve-Ffmpeg $FfmpegPath
if (-not (Test-Path -LiteralPath $ObsPath)) {
    throw "OBS executable was not found: $ObsPath"
}
$obsUserState = Backup-ObsUserState
$e2eObsConfig = Ensure-E2eObsConfig
$obsArguments = "--disable-shutdown-check --disable-missing-files-check --disable-updater --collection `"$($e2eObsConfig.Collection)`" --profile `"$($e2eObsConfig.Profile)`""

$existingObs = Get-Process obs64 -ErrorAction SilentlyContinue
if ($existingObs) {
    $ids = ($existingObs | Select-Object -ExpandProperty Id) -join ", "
    throw "OBS is already running. Close it before running this E2E test. Running obs64 process id(s): $ids"
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$listenerErr = Join-Path $LogDir "listener-$stamp.err.log"
$listenerOut = Join-Path $LogDir "listener-$stamp.out.log"
$server = "rtmp://127.0.0.1:$Port/live"
$key = "dsk-e2e-$stamp"
$rtmpUrl = "$server/$key"
$secondListenerErr = Join-Path $LogDir "listener-$stamp-second.err.log"
$secondListenerOut = Join-Path $LogDir "listener-$stamp-second.out.log"
$secondServer = ""
$secondKey = ""
$secondRtmpUrl = ""
$secondListenerShouldReceive = $SecondPort -gt 0 -and $RouteMode -ne "initial-disabled"
if ($SecondPort -gt 0) {
    if (-not $SecondEncoderGroup) {
        $SecondEncoderGroup = $EncoderGroup
    }
    $secondServer = "rtmp://127.0.0.1:$SecondPort/live"
    $secondKey = "dsk-e2e-second-$stamp"
    $secondRtmpUrl = "$secondServer/$secondKey"
}
$obsLogRoot = Join-Path $env:APPDATA "obs-studio\logs"
$sentinelRoot = Join-Path $env:APPDATA "obs-studio\.sentinel"
if (Test-Path -LiteralPath $sentinelRoot) {
    Get-ChildItem -LiteralPath $sentinelRoot -Filter "run_*" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

$existingLogs = @{}
if (Test-Path -LiteralPath $obsLogRoot) {
    foreach ($log in Get-ChildItem -LiteralPath $obsLogRoot -Filter *.txt -ErrorAction SilentlyContinue) {
        $existingLogs[$log.FullName] = $true
    }
}

$listenerArgs = @(
    "-hide_banner",
    "-loglevel", "info",
    "-listen", "1",
    "-i", $rtmpUrl,
    "-t", "$([Math]::Ceiling(($StartDelayMs + $RunMs) / 1000) + 15)",
    "-f", "null",
    "NUL"
)

$secondListenerArgs = @()
if ($SecondPort -gt 0) {
    $secondListenerArgs = @(
        "-hide_banner",
        "-loglevel", "info",
        "-listen", "1",
        "-i", $secondRtmpUrl,
        "-t", "$([Math]::Ceiling(($StartDelayMs + $RunMs) / 1000) + 15)",
        "-f", "null",
        "NUL"
    )
}

$listener = $null
$secondListener = $null
$obs = $null
$suspendedPlugins = $null
try {
    if (-not $ExpectStartFailure) {
        $listener = Start-Process -WindowStyle Hidden -FilePath $ffmpeg -ArgumentList $listenerArgs `
            -RedirectStandardOutput $listenerOut -RedirectStandardError $listenerErr -PassThru
        if ($SecondPort -gt 0) {
            $secondListener = Start-Process -WindowStyle Hidden -FilePath $ffmpeg -ArgumentList $secondListenerArgs `
                -RedirectStandardOutput $secondListenerOut -RedirectStandardError $secondListenerErr -PassThru
        }
        Start-Sleep -Seconds 2
    } else {
        Write-Host "Running expected start-failure E2E without local RTMP listeners."
    }

    $suspendedPlugins = Suspend-ObsPluginManagerModules -ModuleNames @(
        "vertical-canvas",
        "aitum-multistream",
        "obs-asio",
        "StreamDeckPlugin",
        "AVerMediaCenter"
    )

    $obsEnvironment = @{
        DSK_E2E_AUTORUN = "1"
        DSK_E2E_SERVER = $server
        DSK_E2E_KEY = $key
        DSK_E2E_ENCODER_GROUP = $EncoderGroup
        DSK_E2E_START_DELAY_MS = $StartDelayMs
        DSK_E2E_RUN_MS = $RunMs
        DSK_E2E_QUIT_OBS = "1"
    }
    if ($ExpectStartFailure) {
        $obsEnvironment["DSK_E2E_DISABLE_RECONNECT"] = "1"
    }
    if ($SecondPort -gt 0) {
        $obsEnvironment["DSK_E2E_SECOND_SERVER"] = $secondServer
        $obsEnvironment["DSK_E2E_SECOND_KEY"] = $secondKey
        $obsEnvironment["DSK_E2E_SECOND_ENCODER_GROUP"] = $SecondEncoderGroup
    }
    if ($RouteMode -ne "direct") {
        $obsEnvironment["DSK_E2E_ROUTE_MODE"] = $RouteMode
    }
    if ($VerticalUiStress) {
        $obsEnvironment["DSK_E2E_VERTICAL_UI_STRESS"] = "1"
        $obsEnvironment["DSK_E2E_VERTICAL_UI_STRESS_ITERATIONS"] = [string][Math]::Max(1, $VerticalUiStressIterations)
        $obsEnvironment["DSK_E2E_VERTICAL_SOURCE"] = "DSK E2E Horizontal Source"
    }
    $obs = Start-ObsWithEnvironment -FilePath $ObsPath -WorkingDirectory (Split-Path -Parent $ObsPath) -Arguments $obsArguments -Environment $obsEnvironment

    if ($listener -and $RouteMode -eq "initial-disabled") {
        if (-not $obs.WaitForExit($TimeoutSeconds * 1000)) {
            throw "OBS timed out after $TimeoutSeconds seconds."
        }
        if (-not $listener.WaitForExit(20000)) {
            throw "RTMP listener timed out after OBS exited."
        }
    } elseif ($listener) {
        if (-not $listener.WaitForExit($TimeoutSeconds * 1000)) {
            throw "RTMP listener timed out after $TimeoutSeconds seconds."
        }
        if ($secondListenerShouldReceive -and -not $secondListener.WaitForExit($TimeoutSeconds * 1000)) {
            throw "Second RTMP listener timed out after $TimeoutSeconds seconds."
        }
    } elseif (-not $obs.WaitForExit($TimeoutSeconds * 1000)) {
        throw "OBS timed out after $TimeoutSeconds seconds."
    }

    if (-not $obs.WaitForExit(20000)) {
        Stop-ProcessIfRunning $obs
        throw "OBS did not exit cleanly within 20 seconds after the E2E run."
    }
    $obs.Refresh()
    if ($obs.ExitCode -ne 0) {
        throw "OBS exited with code $($obs.ExitCode)."
    }

    if ($listener) {
        Assert-ListenerReceived -ListenerProcess $listener -ListenerErrPath $listenerErr -ExpectedEncoderGroup $EncoderGroup
        if ($secondListenerShouldReceive) {
            Assert-ListenerReceived -ListenerProcess $secondListener -ListenerErrPath $secondListenerErr -ExpectedEncoderGroup $SecondEncoderGroup
        } elseif ($secondListener) {
            Assert-ListenerDidNotReceive -ListenerProcess $secondListener -ListenerErrPath $secondListenerErr
        }
    }

    $newLogs = Get-ChildItem -LiteralPath $obsLogRoot -Filter *.txt |
        Where-Object { -not $existingLogs.ContainsKey($_.FullName) } |
        Sort-Object LastWriteTime -Descending
    $obsLog = $newLogs | Select-Object -First 1
    if (-not $obsLog) {
        $obsLog = Get-ChildItem -LiteralPath $obsLogRoot -Filter *.txt | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    }
    if (-not $obsLog) {
        throw "OBS log was not found under $obsLogRoot"
    }

    $obsLogText = Get-Content -LiteralPath $obsLog.FullName -Raw
    $shutdownOffset = $obsLogText.IndexOf("==== Shutting down", [StringComparison]::Ordinal)
    if ($shutdownOffset -ge 0 -and
        $obsLogText.IndexOf("[DSK Multistream] Loading DSK Vertical Layout editor", $shutdownOffset, [StringComparison]::Ordinal) -ge 0) {
        throw "DSK Vertical Layout loaded after OBS shutdown began. See $($obsLog.FullName)"
    }
    $requiredPatterns = @(
        "\[DSK Multistream\] Loaded",
        "\[DSK Multistream\] E2E auto-run enabled",
        "\[DSK Multistream\] E2E start requested"
    )
    if (-not $ExpectStartFailure) {
        $requiredPatterns += @(
            "\[DSK Multistream\] Started DSK E2E",
            "\[DSK Multistream\] E2E stop requested",
            "\[DSK Multistream\] E2E auto-run complete"
        )
    }
    foreach ($pattern in $requiredPatterns) {
        if ($obsLogText -notmatch $pattern) {
            throw "OBS log is missing expected pattern '$pattern'. See $($obsLog.FullName)"
        }
    }
    if ($ExpectStartFailure) {
        if ($obsLogText -notmatch "Connection to .* failed" -and
            $obsLogText -notmatch "Failed to connect" -and
            $obsLogText -notmatch "\[DSK Multistream\] E2E start failed" -and
            $obsLogText -notmatch "\[DSK Multistream\] E2E auto-run failed") {
            throw "OBS log is missing the expected RTMP start failure. See $($obsLog.FullName)"
        }
    } elseif ($RouteMode -eq "initial-disabled") {
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E route secondary target intentionally disabled") {
            throw "OBS log is missing the route-disabled marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -match "\[DSK Multistream\] Started DSK E2E Secondary") {
            throw "OBS log shows the disabled secondary route started. See $($obsLog.FullName)"
        }
    } elseif ($RouteMode -eq "toggle") {
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E route toggle: secondary on, primary off") {
            throw "OBS log is missing the route toggle marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] Started DSK E2E Secondary") {
            throw "OBS log is missing the toggled secondary start. See $($obsLog.FullName)"
        }
    } elseif ($obsLogText -match "\[DSK Multistream\] E2E start failed" -or $obsLogText -match "\[DSK Multistream\] E2E auto-run failed") {
        throw "OBS log shows a DSK E2E failure. See $($obsLog.FullName)"
    }
    if ($VerticalUiStress) {
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical UI stress scheduled: $VerticalUiStressIterations scene switches\.") {
            throw "OBS log is missing the scheduled vertical UI stress marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical UI stress started: $VerticalUiStressIterations scene switches\.") {
            throw "OBS log is missing the started vertical UI stress marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical UI stress complete: $VerticalUiStressIterations scene switches\.") {
            throw "OBS log is missing the completed vertical UI stress marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical preview rendered: callbacks=[1-9][0-9]*, snapshots=[1-9][0-9]*, generations=([2-9]|[1-9][0-9]+)\.") {
            throw "OBS log does not prove that the vertical preview rendered multiple layout generations. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical preview scene dimensions: 1080x1920\.") {
            throw "OBS log does not prove that the vertical preview scene has a non-zero 9:16 canvas. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical preview canvas replacement complete\.") {
            throw "OBS log is missing the preview canvas replacement marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical source visibility toggle complete\.") {
            throw "OBS log is missing the source visibility toggle marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical setup visibility toggle complete\.") {
            throw "OBS log is missing the setup visibility toggle marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -notmatch "\[DSK Multistream\] E2E vertical scene CRUD complete\.") {
            throw "OBS log is missing the vertical scene CRUD marker. See $($obsLog.FullName)"
        }
        if ($obsLogText -match "Attempted to add Scene without specifying a canvas") {
            throw "OBS log shows a DSK preview scene was created without an explicit canvas. See $($obsLog.FullName)"
        }
    }

    if ($ExpectStartFailure) {
        Write-Host "OBS RTMP expected-failure E2E test passed."
    } else {
        Write-Host "OBS RTMP E2E test passed."
    }
    Write-Host "ffmpeg: $ffmpeg"
    Write-Host "obs: $ObsPath"
    Write-Host "obs arguments: $obsArguments"
    Write-Host "encoderGroup: $EncoderGroup"
    Write-Host "routeMode: $RouteMode"
    Write-Host "url: $rtmpUrl"
    if ($SecondPort -gt 0) {
        Write-Host "second encoderGroup: $SecondEncoderGroup"
        Write-Host "second url: $secondRtmpUrl"
    }
    if ($listener) {
        Write-Host "listener log: $listenerErr"
    }
    if ($secondListener) {
        Write-Host "second listener log: $secondListenerErr"
    }
    Write-Host "obs log: $($obsLog.FullName)"
}
finally {
    Stop-ProcessIfRunning $obs
    Stop-ProcessIfRunning $secondListener
    Stop-ProcessIfRunning $listener
    Restore-ObsPluginManagerModules $suspendedPlugins
    Restore-ObsUserState $obsUserState
    if ($CleanupE2eConfig) {
        Remove-E2eObsConfig $e2eObsConfig.Collection
    }
}

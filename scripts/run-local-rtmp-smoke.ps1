param(
    [string]$FfmpegPath = "",
    [string]$LogDir = "build\rtmp-smoke",
    [int]$Port = 19350,
    [int]$DurationSeconds = 5,
    [int]$TimeoutSeconds = 30
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

    throw "ffmpeg.exe was not found. Pass -FfmpegPath or install the portable package under deps\ffmpeg-portable."
}

function Stop-ProcessIfRunning {
    param([System.Diagnostics.Process]$Process)

    if ($Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    }
}

$ffmpeg = Resolve-Ffmpeg $FfmpegPath
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$listenerErr = Join-Path $LogDir "listener-$stamp.err.log"
$listenerOut = Join-Path $LogDir "listener-$stamp.out.log"
$publisherErr = Join-Path $LogDir "publisher-$stamp.err.log"
$publisherOut = Join-Path $LogDir "publisher-$stamp.out.log"
$rtmpUrl = "rtmp://127.0.0.1:$Port/live/dsk-smoke"

$listenerArgs = @(
    "-hide_banner",
    "-loglevel", "info",
    "-listen", "1",
    "-i", $rtmpUrl,
    "-t", "$($DurationSeconds + 2)",
    "-f", "null",
    "NUL"
)

$publisherArgs = @(
    "-hide_banner",
    "-loglevel", "info",
    "-re",
    "-f", "lavfi",
    "-i", "testsrc2=size=1280x720:rate=30",
    "-f", "lavfi",
    "-i", "sine=frequency=1000:sample_rate=48000",
    "-t", "$DurationSeconds",
    "-c:v", "libx264",
    "-preset", "ultrafast",
    "-tune", "zerolatency",
    "-pix_fmt", "yuv420p",
    "-c:a", "aac",
    "-ar", "48000",
    "-b:a", "128k",
    "-f", "flv",
    $rtmpUrl
)

$listener = $null
$publisher = $null
try {
    $listener = Start-Process -WindowStyle Hidden -FilePath $ffmpeg -ArgumentList $listenerArgs `
        -RedirectStandardOutput $listenerOut -RedirectStandardError $listenerErr -PassThru
    Start-Sleep -Seconds 2

    $publisher = Start-Process -WindowStyle Hidden -FilePath $ffmpeg -ArgumentList $publisherArgs `
        -RedirectStandardOutput $publisherOut -RedirectStandardError $publisherErr -PassThru

    if (-not $publisher.WaitForExit($TimeoutSeconds * 1000)) {
        throw "RTMP publisher timed out after $TimeoutSeconds seconds."
    }

    if (-not $listener.WaitForExit($TimeoutSeconds * 1000)) {
        throw "RTMP listener did not exit after the publisher finished."
    }

    $publisher.Refresh()
    $listener.Refresh()

    if ($null -ne $publisher.ExitCode -and $publisher.ExitCode -ne 0) {
        throw "RTMP publisher failed with exit code $($publisher.ExitCode). See $publisherErr"
    }
    if ($null -ne $listener.ExitCode -and $listener.ExitCode -ne 0) {
        throw "RTMP listener failed with exit code $($listener.ExitCode). See $listenerErr"
    }

    $listenerLog = Get-Content -LiteralPath $listenerErr -Raw
    $publisherLog = Get-Content -LiteralPath $publisherErr -Raw
    if ($listenerLog -match "Conversion failed" -or $publisherLog -match "Conversion failed") {
        throw "RTMP smoke test conversion failed. See $listenerErr and $publisherErr"
    }
    if ($listenerLog -notmatch "Input #0" -or $listenerLog -notmatch "Stream #0") {
        throw "RTMP listener log did not show an accepted media stream. See $listenerErr"
    }
    if ($publisherLog -notmatch "Output #0" -or $publisherLog -notmatch "frame=") {
        throw "RTMP publisher log did not show transmitted frames. See $publisherErr"
    }

    Write-Host "Local RTMP smoke test passed."
    Write-Host "ffmpeg: $ffmpeg"
    Write-Host "url: $rtmpUrl"
    Write-Host "listener log: $listenerErr"
    Write-Host "publisher log: $publisherErr"
}
finally {
    Stop-ProcessIfRunning $publisher
    Stop-ProcessIfRunning $listener
}

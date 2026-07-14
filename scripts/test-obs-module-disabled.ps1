param(
    [Parameter(Mandatory = $true)]
    [string]$ModuleName
)

$ErrorActionPreference = "Stop"

$modulesPath = Join-Path $env:APPDATA "obs-studio\plugin_manager\modules.json"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupPath = "$modulesPath.dsk-isolation-backup-$stamp"
$obsExe = "C:\Program Files\obs-studio\bin\64bit\obs64.exe"
$obsCwd = "C:\Program Files\obs-studio\bin\64bit"
$logsDir = Join-Path $env:APPDATA "obs-studio\logs"

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    throw "OBS is already running. Close OBS before running the isolation test."
}

Copy-Item -LiteralPath $modulesPath -Destination $backupPath

try {
    $modules = Get-Content -LiteralPath $modulesPath -Raw | ConvertFrom-Json
    $found = $false
    foreach ($module in $modules) {
        if ($module.module_name -eq $ModuleName) {
            $module.enabled = $false
            $found = $true
        }
    }
    if (-not $found) {
        $modules += [pscustomobject]@{
            display_name = ""
            enabled = $false
            encoders = @()
            id = ""
            module_name = $ModuleName
            outputs = @()
            services = @()
            sources = @()
            version = ""
        }
    }

    $modules | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $modulesPath -Encoding UTF8

    $process = Start-Process -FilePath $obsExe -WorkingDirectory $obsCwd -PassThru
    Start-Sleep -Seconds 16

    $running = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
    if ($running) {
        [void]$running.CloseMainWindow()
        Start-Sleep -Seconds 20
    }

    $running = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
    if ($running) {
        Stop-Process -Id $process.Id -Force
        Start-Sleep -Seconds 2
    }

    $latest = Get-ChildItem -LiteralPath $logsDir -Filter "*.txt" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $matches = Select-String -Path $latest.FullName -Pattern "Number of memory leaks|Not all sources|Attempted to add Scene|obs-dsk-multistream|Skipping module '$ModuleName'|Crash|crash|failed|Failed|error|Error|warning|Warning"

    [pscustomobject]@{
        disabledModule = $ModuleName
        log = $latest.FullName
        matches = @($matches | ForEach-Object { "$($_.LineNumber): $($_.Line)" })
    } | ConvertTo-Json -Depth 5
}
finally {
    Copy-Item -LiteralPath $backupPath -Destination $modulesPath -Force
}

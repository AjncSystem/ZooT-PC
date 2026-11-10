# ZooT-PC stutter bench: cold caches (Lichii + NVIDIA driver), title screen + attract demo, no input.
# Usage: powershell -File zoot\bench.ps1 -Label base -Seconds 240 [-Exe Z:\build\bin\eden-cli.exe] [-Env "A=1,B=2"]
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [int]$Seconds = 240,
    [string]$Exe = 'Z:\build\bin\eden-cli.exe',
    [string]$Env = '',
    [string]$Ini = '',  # "key=value,..." set in the run's copy of sdl2-config.ini
    [switch]$Keep,  # keep the caches of an earlier run with this label (second boot)
    [string]$Seed = ''  # title cache folder to start from (pipeline caches only, no warm files)
)
$ErrorActionPreference = 'Stop'
$game = (Get-ChildItem -LiteralPath 'E:\Zelda Remaster switch 2\The Legend of Zelda Ocarina of Time [0400486024DEC000][v131072]' -Filter *.nup).FullName
$dir = "Z:\bench\$Label"
Get-Process eden-cli -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
if (-not $Keep -and (Test-Path $dir)) { Remove-Item -Recurse -Force $dir }
New-Item -ItemType Directory -Force "$dir\user\config", "$dir\nvcache" | Out-Null
Copy-Item -Force $Exe "$dir\eden-cli.exe"
Copy-Item -Force 'Z:\build\bin\nx2_switches.txt' "$dir\"
if (-not $Keep) {
    # Same settings as the play install (precache on), no saves: the title screen needs none.
    Copy-Item -Force 'Z:\build\bin\user\config\sdl2-config.ini' "$dir\user\config\"
    $iniPath = "$dir\user\config\sdl2-config.ini"
    $text = [IO.File]::ReadAllText($iniPath)
    foreach ($kv in ($Ini -split ',' | Where-Object { $_ })) {
        $k, $v = $kv -split '=', 2
        $k = $k.Trim(); $v = $v.Trim()
        $text = $text -replace "(?m)^$([regex]::Escape($k))\\default=true", "$k\default=false"
        if ($text -match "(?m)^$([regex]::Escape($k))=") {
            $text = $text -replace "(?m)^$([regex]::Escape($k))=.*$", "$k=$v"
        } else {
            $text = $text -replace '(?m)^\[Renderer\]\r?$', "[Renderer]`r`n$k\default=false`r`n$k=$v"
        }
    }
    [IO.File]::WriteAllText($iniPath, $text, (New-Object Text.UTF8Encoding($false)))
    if ($Seed) {
        $tc = "$dir\user\cache\shader\0400486024dec000"
        New-Item -ItemType Directory -Force $tc | Out-Null
        Get-ChildItem $Seed -File | Where-Object { $_.Name -notmatch '^(warm-|precache-)|\.lock$' } |
            Copy-Item -Destination $tc -Force
    }
}
$log = "$dir\user\log\eden_log.txt"
if (Test-Path $log) { Remove-Item -Force $log }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "$dir\eden-cli.exe"
$psi.Arguments = "-g `"$game`""
$psi.WorkingDirectory = $dir
$psi.UseShellExecute = $false
$psi.EnvironmentVariables['EDEN_NX2_STUTTER_TRACE'] = '1'
$psi.EnvironmentVariables['__GL_SHADER_DISK_CACHE_PATH'] = "$dir\nvcache"
foreach ($kv in ($Env -split ',' | Where-Object { $_ })) {
    $k, $v = $kv -split '=', 2
    $psi.EnvironmentVariables[$k.Trim()] = $v.Trim()
}
$p = [System.Diagnostics.Process]::Start($psi)
$start = Get-Date
# Wait for the first frame (precache and warm-up happen before it), at most 20 minutes.
while (-not $p.HasExited) {
    if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'event=first_frame' -Quiet)) { break }
    if (((Get-Date) - $start).TotalMinutes -gt 20) { break }
    Start-Sleep 2
}
$boot = ((Get-Date) - $start).TotalSeconds
if (-not $p.HasExited) { Start-Sleep $Seconds }
$crashed = $p.HasExited
if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
"label=$Label boot_s={0:N0} measured_s=$Seconds crashed=$crashed" -f $boot

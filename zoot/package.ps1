# ZooT-PC package: freezes the launcher (ZooT.exe) and assembles dist\ZooT-PC with eden-cli,
# the Visual C++ runtime (app-local, redistributable), the licences and an empty user folder.
# Nothing from the game or from the player's own folder goes in: no game files, keys, saves,
# shader caches or the player's cover or icon.
# Usage: powershell -File zoot\package.ps1 [-Version 0.10]
param([string]$Version = '0.10')
$ErrorActionPreference = 'Stop'

$src = Split-Path -Parent $PSScriptRoot          # ...\ZooT-PC\src
$root = Split-Path -Parent $src                   # ...\ZooT-PC
$work = Join-Path $root 'pkgbuild'
$dist = Join-Path $root 'dist'
$out = Join-Path $dist 'ZooT-PC'
$py = 'C:\PyBuild312\python.exe'
$crt = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Redist\MSVC\14.50.35710\x64\Microsoft.VC145.CRT'
if (-not (Test-Path $crt)) {
    $crt = (Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Redist\MSVC\14.50.*\x64\Microsoft.VC14*.CRT' -Directory | Select-Object -First 1).FullName
}

# 1. Launcher, windowed, as a folder (ZooT.exe + launcher\): a one-file build would unpack itself
#    into %TEMP% on every start, and the port keeps everything next to the executable.
$launcher = Join-Path $src 'zoot\launcher'
& $py -m PyInstaller --noconfirm --clean --onedir --contents-directory launcher --windowed --name ZooT `
    --icon (Join-Path $launcher 'assets\zoot.ico') `
    --add-data "$(Join-Path $launcher 'assets');assets" `
    --paths $launcher `
    --distpath (Join-Path $work 'dist') --workpath (Join-Path $work 'build') --specpath $work `
    (Join-Path $launcher 'zoot_launcher.py')
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller failed' }

# 2. Package folder.
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out, "$out\user", "$out\licenses" | Out-Null
Copy-Item (Join-Path $work 'dist\ZooT\ZooT.exe') $out
Copy-Item -Recurse (Join-Path $work 'dist\ZooT\launcher') "$out\launcher"
$bin = Join-Path $root 'build\bin'
Copy-Item (Join-Path $bin 'eden-cli.exe') $out
Copy-Item (Join-Path $src 'dist\nx2_switches.txt') $out -ErrorAction SilentlyContinue
if (-not (Test-Path "$out\nx2_switches.txt")) { Copy-Item (Join-Path $bin 'nx2_switches.txt') $out }
foreach ($dll in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll',
                 'msvcp140_codecvt_ids.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-Item (Join-Path $crt $dll) $out
}
Copy-Item (Join-Path $src 'LICENSE.txt') "$out\licenses\ZooT-PC-GPL-3.0.txt"
Copy-Item (Join-Path $launcher 'assets\fonts\Cinzel-OFL.txt') "$out\licenses\"
Copy-Item (Join-Path $launcher 'assets\fonts\Inter-LICENSE.txt') "$out\licenses\"
if (Test-Path (Join-Path $src 'LICENSES')) {
    Copy-Item -Recurse (Join-Path $src 'LICENSES') "$out\licenses\third-party"
}
Copy-Item (Join-Path $src 'zoot\README-ZooT.txt') $out

# 3. Safety: nothing but our own files.
$bad = Get-ChildItem $out -Recurse -File | Where-Object {
    $_.Extension -in '.nup', '.nx', '.nsp', '.xci', '.nca', '.keys', '.sav', '.jpg' -or
    ($_.Extension -eq '.bin' -and $_.FullName -notmatch '\\launcher\\') -or
    $_.Name -match '^(capa|game-icon|icon)\.(png|jpg)$' -or $_.FullName -match '\\user\\.+'
}
if ($bad) { $bad | ForEach-Object { $_.FullName }; throw 'Package holds files it must not ship' }

# 4. Zip.
$zip = Join-Path $dist "ZooT-PC-v$Version.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $out -DestinationPath $zip
Get-ChildItem $out -Recurse -File | Select-Object @{n = 'Arquivo'; e = { $_.FullName.Substring($out.Length + 1) } },
    @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } } | Format-Table -AutoSize
"zip: {0} ({1:N0} MB)" -f $zip, ((Get-Item $zip).Length / 1MB)

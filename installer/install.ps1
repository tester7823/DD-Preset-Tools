# DD Preset Tools installer
#  1) puts the loader (SDL2.dll) next to Darkest.exe  (original SDL2.dll -> SDL2_orig.dll)
#  2) (only when dd_preset_tools.dll sits next to this script, e.g. a GitHub build)
#     copies it into the DD Preset Tools mod folder. The Workshop version already contains it.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Test-Loader($path) {
    if (-not (Test-Path $path)) { return $false }
    $bytes = [IO.File]::ReadAllBytes($path)
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    return $text.Contains('SDL2_orig')   # any proxy (ours) forwards to SDL2_orig; the game's own SDL2.dll never does
}

function Find-Game {
    $roots = @()
    foreach ($k in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        try { $p = (Get-ItemProperty $k -ErrorAction Stop); if ($p.SteamPath) { $roots += $p.SteamPath }; if ($p.InstallPath) { $roots += $p.InstallPath } } catch {}
    }
    $libs = @()
    foreach ($r in ($roots | Select-Object -Unique)) {
        $r = $r -replace '/', '\'
        $libs += $r
        $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) { $libs += ($m.Groups[1].Value -replace '\\\\', '\') }
        }
    }
    foreach ($l in ($libs | Select-Object -Unique)) {
        $g = Join-Path $l 'steamapps\common\DarkestDungeon'
        if (Test-Path (Join-Path $g '_windows\win64\Darkest.exe')) { return $g }
    }
    return $null
}

$game = Find-Game
if (-not $game) {
    $game = Read-Host 'Darkest Dungeon folder not found. Paste the game folder path (the one that contains _windows)'
}
$win64 = Join-Path $game '_windows\win64'
if (-not (Test-Path (Join-Path $win64 'Darkest.exe'))) { throw "Darkest.exe not found in $win64" }
Write-Host "Game folder: $game"

if (Get-Process -Name 'Darkest' -ErrorAction SilentlyContinue) { throw 'Please close Darkest Dungeon first.' }

# 1) loader
$sdl = Join-Path $win64 'SDL2.dll'
$orig = Join-Path $win64 'SDL2_orig.dll'
if (-not (Test-Loader $sdl)) {
    # current SDL2.dll is the game's original (fresh install, or restored by a game update / file verify)
    Copy-Item $sdl $orig -Force
    Write-Host 'Original SDL2.dll saved as SDL2_orig.dll'
}
if (-not (Test-Path $orig)) { throw 'SDL2_orig.dll is missing. Verify the game files in Steam and run install again.' }
Copy-Item (Join-Path $here 'SDL2.dll') $sdl -Force
Write-Host 'Loader installed.'

# 2) tools dll into every DD Preset Tools mod folder (only if a separate build is next to this script)
$dll = Join-Path $here 'dd_preset_tools.dll'
if (Test-Path $dll) {
$steamapps = Split-Path -Parent (Split-Path -Parent $game)
$candidates = @()
foreach ($base in @((Join-Path $game 'mods'), (Join-Path $steamapps 'workshop\content\262060'))) {
    if (Test-Path $base) { $candidates += Get-ChildItem $base -Directory | ForEach-Object { Join-Path $_.FullName 'dd_preset_tools' } | Where-Object { Test-Path (Join-Path $_ 'autobuy.txt') } }
}
if ($candidates.Count -eq 0) {
    Write-Warning 'The DD Preset Tools mod folder was not found. Subscribe to the mod on the Steam Workshop (or put it in the mods folder), then run install again.'
} else {
    foreach ($c in $candidates) { Copy-Item $dll (Join-Path $c 'dd_preset_tools.dll') -Force; Write-Host "Tools installed: $c" }
}
}
Write-Host ''
Write-Host 'Done. Enable the mod in the in-game mod list (blue hammer) for your save.'

# DD Preset Tools uninstaller - restores the original SDL2.dll (the loader). Unsubscribe the mod to remove the rest.
$ErrorActionPreference = 'Stop'
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
        if (Test-Path $vdf) { foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) { $libs += ($m.Groups[1].Value -replace '\\\\', '\') } }
    }
    foreach ($l in ($libs | Select-Object -Unique)) {
        $g = Join-Path $l 'steamapps\common\DarkestDungeon'
        if (Test-Path (Join-Path $g '_windows\win64\Darkest.exe')) { return $g }
    }
    return $null
}
$game = Find-Game
if (-not $game) { $game = Read-Host 'Paste the Darkest Dungeon game folder path' }
if (Get-Process -Name 'Darkest' -ErrorAction SilentlyContinue) { throw 'Please close Darkest Dungeon first.' }
$win64 = Join-Path $game '_windows\win64'
$sdl = Join-Path $win64 'SDL2.dll'; $orig = Join-Path $win64 'SDL2_orig.dll'
if (Test-Path $orig) { Move-Item $orig $sdl -Force; Write-Host 'Original SDL2.dll restored.' }
else { Write-Host 'SDL2_orig.dll not found - nothing to restore. (Use "Verify integrity of game files" in Steam if the game does not start.)' }
Write-Host 'Loader removed. Unsubscribe the mod (or delete its folder) to remove the rest. Your saved trinket sets and lineups are kept in _windows\win64.'

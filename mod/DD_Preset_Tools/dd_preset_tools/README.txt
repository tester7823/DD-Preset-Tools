Trinket Sets / Party Lineups / Auto Buy   (Darkest Dungeon 1)
==============================================================

Adds new buttons and windows to the town UI:

  * Trinket Sets   - A button in the Trinket bag. Save up to 10 two-trinket sets
                     and equip them from the hero screen in one click.
                     Class-exclusive trinkets are checked (mod classes too).
  * Party Lineups  - The Caretaker button on the Estate Map. Save up to 10
                     four-hero lineups and load them into the party.
                     Right-click a hero: hero screen / drag: swap / left-click: remove.
                     Dead heroes are removed automatically; missing or busy
                     heroes block the lineup until they are available.
  * Auto Buy       - A button on the Provision screen. Buys the list in
                     autobuy.txt in one go (only before you buy anything).
  * Sets and lineups are saved separately for each estate (save slot).
  * Enable / disable it in the in-game mod list like any other mod.

REQUIRED: loader install (once)
-------------------------------
The game cannot load code from the mods folder by itself, so a small loader
must be placed next to the game executable once.

  1. Close the game.
  2. Open this mod's folder and run  dd_preset_tools\install\install.bat
       - renames  Darkest Dungeon\_windows\win64\SDL2.dll  to  SDL2_orig.dll
       - copies the loader as  SDL2.dll
  3. Start the game and enable the mod in the mod list (blue hammer).
  To uninstall, run  install\uninstall.bat  (restores the original SDL2.dll).

  The loader only forwards SDL calls to the original file and loads
  dd_preset_tools.dll from THIS mod folder. Without this mod folder
  (or with the mod disabled) it does nothing.

  After "Verify integrity of game files" or a game update, run install.bat again.

Files in this folder
--------------------
  dd_preset_tools.dll             The mod's code (source: https://github.com/tester7823/DD-Preset-Tools)
  install\                        Loader installer (SDL2.dll, install.bat, uninstall.bat)
  autobuy.txt                     Auto Buy list - edit freely
  images\hero_icon.png            Party Lineups icon (Caretaker)
  images\hero_background.png      Party Lineups window background
  images\trinket_background.png   Trinket Sets window background
  images\autobuy_icon.png         Auto Buy button icon
  -> Replace any image with your own file of the same name.
  Images are original game art by Red Hook Studios.

Notes
-----
  * Some antivirus programs may flag the DLL files because they hook into the
    game. The full source code is available on GitHub:
    https://github.com/tester7823/DD-Preset-Tools
  * Translations: the text comes from the mod's localization folder
    (localization\dd_preset_tools.string_table.xml, keys ddpt_*). All 12 game
    languages are included: English, French, German, Spanish, Brazilian
    Portuguese, Russian, Polish, Czech, Italian, Simplified Chinese, Korean,
    Japanese. To fix a translation, edit tools/strings.py on GitHub and send
    a pull request.

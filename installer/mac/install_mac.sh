#!/bin/bash
# DD Preset Tools - macOS installer (BETA)
# How to run: open Terminal, type "sh " (with a space), drag this file into the window, press Enter.
# 실행 방법: 터미널을 열고 "sh " (띄어쓰기 포함) 입력 후 이 파일을 창에 끌어다 놓고 Enter.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
echo "== DD Preset Tools - macOS install (BETA) =="

find_app() {
  # 1) this file is inside steamapps/workshop/content/262060/<id>/mac_only  or  <game>/mods/<mod>/mac_only
  local c
  c="$HERE/../../../../../common/DarkestDungeon/_osx/Darkest.app"; [ -d "$c" ] && { echo "$c"; return; }
  c="$HERE/../../../_osx/Darkest.app";                         [ -d "$c" ] && { echo "$c"; return; }
  c="$HOME/Library/Application Support/Steam/steamapps/common/DarkestDungeon/_osx/Darkest.app"; [ -d "$c" ] && { echo "$c"; return; }
  echo ""
}
APP="$(find_app)"
if [ -z "$APP" ]; then
  echo "Could not find Darkest.app automatically."
  echo "게임(Darkest.app)을 찾지 못했습니다."
  echo "Drag Darkest.app here and press Enter (Steam > Darkest Dungeon > Manage > Browse local files > _osx):"
  echo "Darkest.app 을 이 창에 끌어다 놓고 Enter (Steam > 다키스트 던전 > 관리 > 로컬 파일 보기 > _osx):"
  read -r APP
  APP="${APP%/}"; APP="${APP//\\ / }"; APP="${APP%\'}"; APP="${APP#\'}"
fi
APP="$(cd "$APP" && pwd)"
FW="$APP/Contents/Frameworks/SDL2.framework/Versions/A"
if [ ! -f "$FW/SDL2" ]; then echo "SDL2 not found in $FW - aborting / SDL2 파일이 없어 중단합니다"; exit 1; fi
echo "Game: $APP"

if grep -q "dd_preset_tools.dylib" "$FW/SDL2" 2>/dev/null; then
  echo "Loader already installed - updating / 이미 설치됨 - 로더만 갱신"
else
  cp -f "$FW/SDL2" "$FW/SDL2_orig"          # keep the original SDL2 (원본 보관)
fi
cp -f "$HERE/SDL2_loader.dylib" "$FW/SDL2"
chmod 755 "$FW/SDL2" "$FW/SDL2_orig" "$HERE/dd_preset_tools.dylib" 2>/dev/null || true
xattr -d com.apple.quarantine "$FW/SDL2" "$HERE/dd_preset_tools.dylib" 2>/dev/null || true
echo ""
echo "Done! Start the game and enable 'DD Preset Tools' in the mod list."
echo "완료! 게임을 켜고 모드 목록에서 'DD Preset Tools' 를 켜 주세요."
echo "If something goes wrong, please post the LAST line of this file in the Workshop comments:"
echo "문제가 있으면 아래 파일의 마지막 줄을 창작마당 댓글로 남겨 주세요:"
echo "  $(cd "$APP/.." && pwd)/trinket_preset.log"

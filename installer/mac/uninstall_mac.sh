#!/bin/bash
# DD Preset Tools - macOS uninstaller. Run: Terminal → "sh " + drag this file → Enter
# 제거: 터미널 → "sh " 입력 후 이 파일을 끌어다 놓고 Enter
HERE="$(cd "$(dirname "$0")" && pwd)"
for c in "$HERE/../../../../../common/DarkestDungeon/_osx/Darkest.app" "$HERE/../../../_osx/Darkest.app" \
         "$HOME/Library/Application Support/Steam/steamapps/common/DarkestDungeon/_osx/Darkest.app"; do
  [ -d "$c" ] && { APP="$c"; break; }
done
if [ -z "$APP" ]; then echo "Drag Darkest.app here and press Enter / Darkest.app 을 끌어다 놓고 Enter:"; read -r APP; APP="${APP%/}"; APP="${APP//\\ / }"; fi
FW="$APP/Contents/Frameworks/SDL2.framework/Versions/A"
if [ -f "$FW/SDL2_orig" ]; then
  mv -f "$FW/SDL2_orig" "$FW/SDL2"
  echo "Removed. The game is back to the original. / 제거 완료 - 게임이 원래대로 돌아갔습니다."
else
  echo "Nothing to remove (SDL2_orig not found). / 제거할 것이 없습니다."
  echo "(Steam > Properties > Installed Files > Verify integrity also restores the original.)"
fi

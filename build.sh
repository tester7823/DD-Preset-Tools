#!/usr/bin/env bash
# llvm-mingw 로 Windows x64 SDL2.dll(프록시) 빌드
set -euo pipefail
cd "$(dirname "$0")"
[ -d third_party/imgui ] && [ -d third_party/minhook ] && [ -d third_party/SDL2 ] && [ -f third_party/stb_image.h ] || ./fetch_deps.sh
TC="${LLVM_MINGW:?set LLVM_MINGW to your llvm-mingw folder}/bin"
ARCH="${1:-x64}"
if [ "$ARCH" = x86 ]; then TRIPLE=i686; DLLM=i386; else TRIPLE=x86_64; DLLM=i386:x86-64; fi
CXX="$TC/$TRIPLE-w64-mingw32-g++"
DLLTOOL="$TC/llvm-dlltool"
OUT=build/$ARCH; mkdir -p "$OUT/obj"

python3 tools/gen_proxy_def.py src/sdl2_orig.def "$OUT/proxy.def"
"$DLLTOOL" -m $DLLM -d src/sdl2_orig.def -l "$OUT/libSDL2_orig.a"

IMGUI=third_party/imgui
CXXFLAGS="-std=c++17 -O2 -DNDEBUG -DUNICODE -D_UNICODE -DSDL_MAIN_HANDLED -Ithird_party -Ithird_party/SDL2 -I$IMGUI -Ithird_party/minhook/include -Wall -Wno-unused-function"
# 1) 로더: 게임 폴더의 SDL2.dll (SDL 함수 전달 + 모드 폴더의 dd_preset_tools.dll 불러오기)
"$CXX" -std=c++17 -O2 -DNDEBUG -DUNICODE -D_UNICODE -DSDL_MAIN_HANDLED -Ithird_party -Ithird_party/SDL2 -c src/loader.cpp -o "$OUT/obj/loader.o"
"$CXX" -shared -static -s -o "$OUT/SDL2.dll" "$OUT/obj/loader.o" "$OUT/proxy.def" -luser32
echo "빌드 완료: $OUT/SDL2.dll (로더)"

# 2) 기능 본체: mods\<모드>\dd_preset_tools\dd_preset_tools.dll
SRCS="src/main.cpp src/overlay.cpp src/presets.cpp src/gfx.cpp src/game.cpp src/ui_preset.cpp src/ui_heropreset.cpp src/ui_bulkbuy.cpp src/text.cpp src/hero_presets.cpp \
  $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp $IMGUI/imgui_demo.cpp \
  $IMGUI/backends/imgui_impl_sdl2.cpp $IMGUI/backends/imgui_impl_opengl2.cpp $IMGUI/backends/imgui_impl_opengl3.cpp"

MH=third_party/minhook/src
CSRCS="$MH/hook.c $MH/buffer.c $MH/trampoline.c $MH/hde/hde64.c $MH/hde/hde32.c"
CC="$TC/$TRIPLE-w64-mingw32-gcc"
OBJS=""
for s in $CSRCS; do
  o="$OUT/obj/mh_$(basename "${s%.c}").o"
  "$CC" -O2 -Ithird_party/minhook/include -c "$s" -o "$o"
  OBJS="$OBJS $o"
done
for s in $SRCS; do
  o="$OUT/obj/$(basename "${s%.cpp}").o"
  "$CXX" $CXXFLAGS -c "$s" -o "$o"
  OBJS="$OBJS $o"
done

"$CXX" -shared -static -s -o "$OUT/dd_preset_tools.dll" $OBJS \
  -L"$OUT" -lSDL2_orig -lopengl32 -lgdi32 -luser32 -limm32 -lshell32 -ldwmapi
echo "빌드 완료: $OUT/dd_preset_tools.dll (기능)"

#!/usr/bin/env bash
# macOS x86_64 빌드 (리눅스에서 clang + macOS SDK 로 교차 컴파일)
#  - build/mac/SDL2                    : 로더 (Darkest.app/Contents/Frameworks/SDL2.framework/Versions/A/SDL2 자리)
#  - build/mac/dd_preset_tools.dylib   : 기능 본체 (<모드>/mac_only/)
set -euo pipefail
cd "$(dirname "$0")"
SDK="${MACOS_SDK:?set MACOS_SDK to a macOS SDK folder (e.g. MacOSX12.3.sdk)}"
[ -d third_party/imgui ] || ./fetch_deps.sh
OUT=build/mac; mkdir -p "$OUT/obj"
T="-target x86_64-apple-macos10.13 -isysroot $SDK"
CC="clang $T"; CXX="clang++ $T -stdlib=libc++"
LD="-fuse-ld=lld -Wl,-adhoc_codesign -Wl,-platform_version,macos,10.13,12.3"

# 1) 로더
$CXX -std=c++17 -O2 -fvisibility=hidden -c src/mac/loader_mac.cpp -o $OUT/obj/loader_mac.o
$CXX -dynamiclib $LD -o $OUT/SDL2 $OUT/obj/loader_mac.o \
  -install_name @rpath/SDL2.framework/Versions/A/SDL2 -current_version 3001.10.0 -compatibility_version 3001.0.0 \
  -Wl,-reexport_library,src/mac/SDL2_orig.tbd
echo "빌드 완료: $OUT/SDL2 (로더)"

# 2) 기능 본체
IMGUI=third_party/imgui
CXXFLAGS="-std=c++17 -O2 -DNDEBUG -DGL_SILENCE_DEPRECATION -fvisibility=hidden -Isrc/mac -Isrc -Ithird_party -Ithird_party/SDL2 -I$IMGUI -Wall -Wno-unused-function -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-but-set-variable"
SRCS="src/mac/main_mac.cpp src/mac/game_mac.cpp src/mac/win_shim.cpp src/mac/hook.cpp \
  src/overlay.cpp src/presets.cpp src/gfx.cpp src/ui_preset.cpp src/ui_heropreset.cpp src/ui_bulkbuy.cpp src/text.cpp src/hero_presets.cpp \
  $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp $IMGUI/imgui_demo.cpp \
  $IMGUI/backends/imgui_impl_sdl2.cpp $IMGUI/backends/imgui_impl_opengl2.cpp $IMGUI/backends/imgui_impl_opengl3.cpp"
OBJS=""
for s in $SRCS; do
  o="$OUT/obj/$(basename "${s%.cpp}").o"
  $CXX $CXXFLAGS -c "$s" -o "$o" &
  OBJS="$OBJS $o"
done
wait
$CXX -dynamiclib $LD -o $OUT/dd_preset_tools.dylib $OBJS -install_name @rpath/dd_preset_tools.dylib \
  -framework OpenGL -framework CoreFoundation -Wl,-undefined,dynamic_lookup
echo "빌드 완료: $OUT/dd_preset_tools.dylib (기능)"

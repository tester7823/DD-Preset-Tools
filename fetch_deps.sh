#!/usr/bin/env bash
# Downloads the third-party code into third_party/ (exact versions used for the release build)
set -euo pipefail
cd "$(dirname "$0")"
T=third_party; mkdir -p $T; tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
[ -d $T/imgui ] || { git clone -q --depth 1 -b v1.91.5 https://github.com/ocornut/imgui "$tmp/imgui"; mkdir -p $T/imgui/backends
  cp "$tmp"/imgui/*.cpp "$tmp"/imgui/*.h "$tmp"/imgui/LICENSE.txt $T/imgui/
  cp "$tmp"/imgui/backends/imgui_impl_sdl2.* "$tmp"/imgui/backends/imgui_impl_opengl2.* "$tmp"/imgui/backends/imgui_impl_opengl3* $T/imgui/backends/; }
[ -d $T/minhook ] || { git clone -q https://github.com/TsudaKageyu/minhook "$tmp/minhook"; git -C "$tmp/minhook" checkout -q 8af6b4acae5a9388fd742b56fa79ece89d96f823
  mkdir -p $T/minhook; cp -r "$tmp"/minhook/src "$tmp"/minhook/include "$tmp"/minhook/LICENSE.txt $T/minhook/; }
[ -d $T/SDL2 ] || { git clone -q --depth 1 -b release-2.30.9 https://github.com/libsdl-org/SDL "$tmp/SDL"; cp -r "$tmp"/SDL/include $T/SDL2; }
[ -f $T/stb_image.h ] || { git clone -q https://github.com/nothings/stb "$tmp/stb"; git -C "$tmp/stb" checkout -q 013ac3beddff3dbffafd5177e7972067cd2b5083; cp "$tmp"/stb/stb_image.h $T/; }
echo "third_party ready"

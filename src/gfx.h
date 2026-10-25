#pragma once
// 게임 그림(PNG/TGA)과 게임 폰트(BMFont .fnt)를 OpenGL 텍스처로 불러와 ImGui 로 그리는 도우미
#include <string>
#include <vector>
#include <cstdint>
#include "imgui.h"

namespace Gfx {

struct Texture {
    unsigned id = 0;
    int w = 0, h = 0;
    bool ok() const { return id != 0; }
    ImTextureID tex() const { return (ImTextureID)(intptr_t)id; }
};

// 게임 루트 폴더 (Darkest.exe 기준 ..\..)
const std::wstring& GameRoot();
std::string ReadFile(const std::wstring& path);

// 게임 루트 기준 상대 경로 (예: "panels/icons_equip/trinket/rarity_rare.png"). 캐시됨.
const Texture& GameTex(const char* relPath);
// 게임 폴더 → 로컬 mods → 창작마당 순서로 찾음 (모드 장신구 아이콘용). 없으면 빈 텍스처
const Texture& ModTex(const char* relPath);
// 게임 데이터가 있는 모든 폴더 (DLC, DLC features, 로컬 mods, 창작마당) — 게임 루트 제외
const std::vector<std::wstring>& ModRoots();
// 이 모드의 데이터 폴더 (...\dd_preset_tools). 없으면 빈 문자열
const std::wstring& ToolRoot();
// 특정 폴더 기준 그림 (장신구가 정의된 모드 폴더에서 아이콘을 먼저 찾을 때)
const Texture& TexIn(const std::wstring& root, const char* relPath);
// 메모리 PNG (DLL 에 내장된 그림)
const Texture& MemTex(const char* key, const unsigned char* png, int size);

void ReleaseAll();   // GL 컨텍스트 파괴 시

// --- BMFont -----------------------------------------------------------------
enum class FontId { Title, Body };
void  SetFontLanguage(const std::string& lang);   // english/koreana/japanese/schinese/russian ... (글꼴 다시 불러옴)
bool  FontAvailable(FontId f);
float FontLineHeight(FontId f);
ImVec2 MeasureText(FontId f, const char* utf8, float scale);
void  DrawStr(ImDrawList* dl, FontId f, ImVec2 pos, float scale, ImU32 color, const char* utf8);
// 그림자 포함
void  DrawTextShadow(ImDrawList* dl, FontId f, ImVec2 pos, float scale, ImU32 color, const char* utf8);

} // namespace Gfx

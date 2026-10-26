#include <cfloat>
// 게임 그림/폰트 로더 + ImGui 그리기
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#include "stb_image.h"

#include "gfx.h"
#include "log.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

namespace Gfx {
namespace {

std::wstring Utf8ToW(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// 텍스처 업로드 (RGBA8). 게임의 GL 상태를 건드리지 않도록 바인딩/언팩 상태를 복원한다.
unsigned Upload(const unsigned char* rgba, int w, int h) {
    GLint prevTex = 0, prevAlign = 4, prevRow = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRow);
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRow);
    return id;
}

// asMask: 폰트 페이지 - 밝기/알파를 알파로 쓰고 색은 흰색으로
Texture Decode(const unsigned char* data, int size, bool asMask) {
    Texture t;
    int w, h, ch;
    unsigned char* px = stbi_load_from_memory(data, size, &w, &h, &ch, 4);
    if (!px) return t;
    if (asMask) {
        for (int i = 0; i < w * h; ++i) {
            unsigned char* p = px + i * 4;
            unsigned char a = (ch == 1 || ch == 3) ? p[0] : p[3];   // L/RGB → 밝기, RGBA → 알파
            p[0] = p[1] = p[2] = 255; p[3] = a;
        }
    }
    t.id = Upload(px, w, h); t.w = w; t.h = h;
    stbi_image_free(px);
    return t;
}

std::unordered_map<std::string, Texture> g_tex;

// --- BMFont ---
struct Glyph { float u0, v0, u1, v1; short w, h, xo, yo, adv; unsigned short page; };
struct Font {
    bool loaded = false, tried = false;
    float lineHeight = 0;
    std::vector<Texture> pages;                 // 전체 페이지 (여러 파일 합침)
    std::unordered_map<uint32_t, Glyph> glyphs;
};
Font g_fonts[2];
std::string g_fontLang = "koreana";   // 게임 언어 (fonts.darkest 의 font_language_override 와 같은 조합)

int KV(const char* line, const char* key, int def = 0) {
    char pat[32]; snprintf(pat, sizeof pat, " %s=", key);
    const char* p = strstr(line, pat);
    return p ? atoi(p + strlen(pat)) : def;
}
std::string KVs(const char* line, const char* key) {
    char pat[32]; snprintf(pat, sizeof pat, " %s=\"", key);
    const char* p = strstr(line, pat);
    if (!p) return {};
    p += strlen(pat);
    const char* e = strchr(p, '"');
    return e ? std::string(p, e) : std::string();
}

// fnt 파일 하나를 font 에 추가 (뒤에 오는 파일이 같은 글자를 덮어씀 = append_to_font)
bool AppendFnt(Font& f, const char* relFnt) {
    std::string txt = ReadFile(GameRoot() + L"\\" + Utf8ToW(relFnt));
    if (txt.empty()) { Log("폰트 파일 없음: %s", relFnt); return false; }
    std::string dir = relFnt; dir = dir.substr(0, dir.find_last_of('/') + 1);
    int pageBase = (int)f.pages.size();
    float sw = 1, sh = 1;
    struct Raw { uint32_t cp; int x, y; };
    std::vector<Raw> raws;     // 글자 위치(픽셀) — 페이지 그림의 실제 크기로 나중에 정규화
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t e = txt.find('\n', pos); if (e == std::string::npos) e = txt.size();
        std::string line = " " + txt.substr(pos, e - pos);
        pos = e + 1;
        if (line.compare(1, 7, "common ") == 0) {
            sw = (float)KV(line.c_str(), "scaleW", 1); sh = (float)KV(line.c_str(), "scaleH", 1);
            if (f.lineHeight == 0) f.lineHeight = (float)KV(line.c_str(), "lineHeight");
        } else if (line.compare(1, 5, "page ") == 0) {
            int id = KV(line.c_str(), "id");
            std::string file = dir + KVs(line.c_str(), "file");
            std::string bytes = ReadFile(GameRoot() + L"\\" + Utf8ToW(file));
            while ((int)f.pages.size() <= pageBase + id) f.pages.push_back(Texture{});
            if (!bytes.empty()) f.pages[pageBase + id] = Decode((const unsigned char*)bytes.data(), (int)bytes.size(), true);
        } else if (line.compare(1, 5, "char ") == 0) {
            Glyph g{};
            int x = KV(line.c_str(), "x"), y = KV(line.c_str(), "y");
            g.w = (short)KV(line.c_str(), "width"); g.h = (short)KV(line.c_str(), "height");
            g.xo = (short)KV(line.c_str(), "xoffset"); g.yo = (short)KV(line.c_str(), "yoffset");
            g.adv = (short)KV(line.c_str(), "xadvance");
            g.page = (unsigned short)(pageBase + KV(line.c_str(), "page"));
            uint32_t cp = (uint32_t)KV(line.c_str(), "id");
            f.glyphs[cp] = g;
            raws.push_back({ cp, x, y });
        }
    }
    // .fnt 의 scaleW/scaleH 가 실제 그림 크기와 다른 파일이 있음 (dwarvenaxe-l: scaleW=1024, 그림 512)
    // → 실제로 읽은 그림 크기를 우선 사용
    for (const Raw& r : raws) {
        Glyph& g = f.glyphs[r.cp];
        float w = sw, h = sh;
        if (g.page < f.pages.size() && f.pages[g.page].w > 0) { w = (float)f.pages[g.page].w; h = (float)f.pages[g.page].h; }
        g.u0 = r.x / w; g.v0 = r.y / h; g.u1 = (r.x + g.w) / w; g.v1 = (r.y + g.h) / h;
    }
    return true;
}

Font& GetFont(FontId id) {
    Font& f = g_fonts[(int)id];
    if (!f.tried) {
        f.tried = true;
        bool ok;
        const std::string& L = g_fontLang;
        if (id == FontId::Title) {
            if (L == "russian") ok = AppendFnt(f, "fonts/russian_dwarven_axe_l.fnt");
            else {
                ok = AppendFnt(f, "fonts/dwarvenaxe-l.fnt");
                if (L == "koreana")  ok &= AppendFnt(f, "fonts/korean_dwarven_axe_l.fnt");
                if (L == "japanese") ok &= AppendFnt(f, "fonts/japanese_dwarven_axe_l.fnt");
                if (L == "schinese") ok &= AppendFnt(f, "fonts/chinese_dwarven_axe_l.fnt");
            }
        } else {
            ok = AppendFnt(f, "fonts/ubuntu.fnt");
            if (L == "koreana")  ok &= AppendFnt(f, "fonts/korean_ubuntu.fnt");
            if (L == "japanese") ok &= AppendFnt(f, "fonts/japanese_ubuntu.fnt");
            if (L == "schinese") ok &= AppendFnt(f, "fonts/chinese_ubuntu.fnt");
        }
        f.loaded = ok && !f.glyphs.empty();
        Log("게임 폰트 %s: %s (글자 %zu개, 페이지 %zu장)", id == FontId::Title ? "제목" : "본문",
            f.loaded ? "OK" : "실패", f.glyphs.size(), f.pages.size());
    }
    return f;
}

uint32_t NextCp(const char*& s) {
    unsigned char c = (unsigned char)*s++;
    if (c < 0x80) return c;
    int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
    uint32_t cp = c & (0x3F >> n);
    while (n-- && *s) cp = (cp << 6) | ((unsigned char)*s++ & 0x3F);
    return cp;
}

} // namespace

const std::wstring& GameRoot() {
    static std::wstring root;
    if (root.empty()) {
        wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
        root = p;
        for (int i = 0; i < 3; ++i) root = root.substr(0, root.find_last_of(L"\\/"));   // exe → win64 → _windows → root
    }
    return root;
}

std::string ReadFile(const std::wstring& path) {
    std::string out;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0) { out.resize(n); if (fread(out.data(), 1, n, f) != (size_t)n) out.clear(); }
    fclose(f);
    return out;
}

// 모드 폴더(mods 또는 창작마당)의 dd_preset_tools\images 에 같은 이름의 그림이 있으면 그것을 씀
const char* OverrideName(const char* rel) {
    static const char* map[][2] = {
        { "campaign/town/hero_slot/caretaker_portrait.png",              "images/hero_icon.png" },
        { "campaign/town/glossary/glossary_character.png",               "images/hero_background.png" },
        { "campaign/town/buildings/nomad_wagon/nomad_wagon.character.png", "images/trinket_background.png" },
        { "campaign/town/provision/provision.icon.png",                  "images/autobuy_icon.png" },
    };
    for (auto& m : map) if (strcmp(m[0], rel) == 0) return m[1];
    return nullptr;
}

const Texture& GameTex(const char* rel) {
    auto it = g_tex.find(rel);
    if (it != g_tex.end()) return it->second;
    std::string bytes;
    if (const char* ov = OverrideName(rel)) {
        const std::wstring& tr = ToolRoot();
        if (!tr.empty()) bytes = ReadFile(tr + L"\\" + Utf8ToW(ov));
    }
    if (bytes.empty()) bytes = ReadFile(GameRoot() + L"\\" + Utf8ToW(rel));
    Texture t;
    if (!bytes.empty()) t = Decode((const unsigned char*)bytes.data(), (int)bytes.size(), false);
    if (!t.ok()) Log("그림 로드 실패: %s", rel);
    return g_tex[rel] = t;
}

#ifdef GFX_HARNESS
const std::wstring& ToolRoot() { static std::wstring s = getenv("DDPT_ROOT") ? std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(getenv("DDPT_ROOT")) : std::wstring(); return s; }
#else
// 이 모드의 데이터 폴더: mods\<아무 이름>\dd_preset_tools 또는 창작마당\<번호>\dd_preset_tools
const std::wstring& ToolRoot() {
    static std::wstring root; static bool done = false;
    if (done) return root;
    done = true;
    for (auto& r : ModRoots()) {
        std::wstring p = r + L"\\dd_preset_tools";
        DWORD a = GetFileAttributesW(p.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) { root = p; break; }
    }
    if (root.empty()) Log("모드 데이터 폴더(dd_preset_tools) 없음 - 기본값 사용");
    else { int n = WideCharToMultiByte(CP_UTF8, 0, root.c_str(), -1, nullptr, 0, nullptr, nullptr);
           std::string u(n, 0); WideCharToMultiByte(CP_UTF8, 0, root.c_str(), -1, u.data(), n, nullptr, nullptr);
           Log("모드 데이터 폴더: %s", u.c_str()); }
    return root;
}
#endif

#ifndef GFX_HARNESS
const std::vector<std::wstring>& ModRoots() {
    static std::vector<std::wstring> roots;
    static bool done = false;
    if (done) return roots;
    done = true;
    auto scan = [&](const std::wstring& dir) {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.')
                roots.push_back(dir + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    };
    // DLC: dlc\<id>\ 와 dlc\<id>\features\<이름>\ 아래에 panels\ 등이 있음
    size_t before = roots.size();
    scan(GameRoot() + L"\\dlc");
    for (size_t i = before, n = roots.size(); i < n; ++i) scan(roots[i] + L"\\features");
    scan(GameRoot() + L"\\mods");
    std::wstring steamapps = GameRoot().substr(0, GameRoot().find_last_of(L"\\/"));   // ...\common
    steamapps = steamapps.substr(0, steamapps.find_last_of(L"\\/"));                   // ...\steamapps
    scan(steamapps + L"\\workshop\\content\\262060");
    Log("모드 폴더 %zu개 확인", roots.size());
    return roots;
}
#endif

const Texture& ModTex(const char* rel) {
    std::string key = std::string("mod:") + rel;
    auto it = g_tex.find(key);
    if (it != g_tex.end()) return it->second;
    std::wstring wrel = Utf8ToW(rel);
    std::string bytes = ReadFile(GameRoot() + L"\\" + wrel);
#ifndef GFX_HARNESS
    if (bytes.empty()) for (auto& r : ModRoots()) { bytes = ReadFile(r + L"\\" + wrel); if (!bytes.empty()) break; }
#endif
    Texture t;
    if (!bytes.empty()) t = Decode((const unsigned char*)bytes.data(), (int)bytes.size(), false);
    return g_tex[key] = t;
}

const Texture& TexIn(const std::wstring& root, const char* rel) {
    std::string key = "in:" + std::string(rel) + "@" + std::to_string(std::hash<std::wstring>()(root));
    auto it = g_tex.find(key);
    if (it != g_tex.end()) return it->second;
    std::string bytes = ReadFile(root + L"\\" + Utf8ToW(rel));
    Texture t;
    if (!bytes.empty()) t = Decode((const unsigned char*)bytes.data(), (int)bytes.size(), false);
    return g_tex[key] = t;
}

const Texture& MemTex(const char* key, const unsigned char* png, int size) {
    auto it = g_tex.find(key);
    if (it != g_tex.end()) return it->second;
    return g_tex[key] = Decode(png, size, false);
}

void ReleaseAll() {
    for (auto& kv : g_tex) if (kv.second.id) { GLuint id = kv.second.id; glDeleteTextures(1, &id); }
    g_tex.clear();
    for (auto& f : g_fonts) {
        for (auto& p : f.pages) if (p.id) { GLuint id = p.id; glDeleteTextures(1, &id); }
        f = Font{};
    }
}

void SetFontLanguage(const std::string& lang) {
    if (lang.empty() || lang == g_fontLang) return;
    g_fontLang = lang;
    for (auto& f : g_fonts) {
        for (auto& p : f.pages) if (p.id) { GLuint id = p.id; glDeleteTextures(1, &id); }
        f = Font{};
    }
    Log("game language: %s (fonts reloaded)", lang.c_str());
}

bool FontAvailable(FontId id) { return GetFont(id).loaded; }
float FontLineHeight(FontId id) { return GetFont(id).lineHeight; }

// 게임 폰트에 없는 글자(예: 영어 게임에서 한글로 지은 이름)는 ImGui 폰트(맑은 고딕)로 대신 그림
static ImFont* FallbackFont(uint32_t cp) {
    ImFont* f = ImGui::GetFont();
    if (!f || cp > 0xFFFF) return nullptr;
    return f->FindGlyphNoFallback((ImWchar)cp) ? f : nullptr;
}
static void CpToUtf8(char* u, uint32_t c) {
    if (c < 0x80) { u[0] = (char)c; }
    else if (c < 0x800) { u[0] = (char)(0xC0 | (c >> 6)); u[1] = (char)(0x80 | (c & 0x3F)); }
    else { u[0] = (char)(0xE0 | (c >> 12)); u[1] = (char)(0x80 | ((c >> 6) & 0x3F)); u[2] = (char)(0x80 | (c & 0x3F)); }
}
static float FallbackSize(const Font& f, float scale) { return f.lineHeight * scale * 1.1f; }

ImVec2 MeasureText(FontId id, const char* s, float scale) {
    Font& f = GetFont(id);
    float x = 0;
    while (*s) {
        uint32_t cp = NextCp(s);
        auto it = f.glyphs.find(cp);
        if (it != f.glyphs.end()) x += it->second.adv;
        else if (cp == ' ') x += f.lineHeight * 0.3f;
        else if (ImFont* ff = FallbackFont(cp)) {
            ImWchar c = (ImWchar)cp;
            char u[5] = {}; CpToUtf8(u, c);
            x += ff->CalcTextSizeA(FallbackSize(f, 1.0f), FLT_MAX, 0, u).x;
        }
    }
    return ImVec2(x * scale, f.lineHeight * scale);
}

void DrawStr(ImDrawList* dl, FontId id, ImVec2 pos, float scale, ImU32 col, const char* s) {
    Font& f = GetFont(id);
    if (!f.loaded) {   // 게임 폰트가 없으면 ImGui 기본 폰트로 대체
        dl->AddText(nullptr, ImGui::GetFontSize() * scale, pos, col, s);
        return;
    }
    float x = pos.x;
    while (*s) {
        uint32_t cp = NextCp(s);
        auto it = f.glyphs.find(cp);
        if (it == f.glyphs.end()) {
            if (cp == ' ') { x += f.lineHeight * 0.3f * scale; continue; }
            if (ImFont* ff = FallbackFont(cp)) {
                char u[5] = {}; CpToUtf8(u, cp);
                float fs = FallbackSize(f, scale);
                dl->AddText(ff, fs, ImVec2(x, pos.y + (f.lineHeight * scale - fs) * 0.5f), col, u);
                x += ff->CalcTextSizeA(fs, FLT_MAX, 0, u).x;
            }
            continue;
        }
        const Glyph& g = it->second;
        if (g.w > 0 && g.page < f.pages.size() && f.pages[g.page].ok()) {
            ImVec2 p0(x + g.xo * scale, pos.y + g.yo * scale);
            ImVec2 p1(p0.x + g.w * scale, p0.y + g.h * scale);
            dl->AddImage(f.pages[g.page].tex(), p0, p1, ImVec2(g.u0, g.v0), ImVec2(g.u1, g.v1), col);
        }
        x += g.adv * scale;
    }
}

void DrawTextShadow(ImDrawList* dl, FontId id, ImVec2 pos, float scale, ImU32 col, const char* s) {
    DrawStr(dl, id, ImVec2(pos.x + 2 * scale, pos.y + 2 * scale), scale, IM_COL32(0, 0, 0, 200), s);
    DrawStr(dl, id, pos, scale, col, s);
}

} // namespace Gfx

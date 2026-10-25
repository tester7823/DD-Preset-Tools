#include <unordered_map>
#include <string>
#include <cstring>
#include "text.h"
#include "game.h"
#include "gfx.h"
#include "log.h"

namespace Txt {
namespace {
std::unordered_map<std::string, std::string> g_cache;
std::unordered_map<std::string, std::string> g_english;   // xml 영어 (예비)
bool g_englishLoaded = false;
int  g_frame = 0;
std::string g_lang;

void LoadEnglish() {
    if (g_englishLoaded) return;
    g_englishLoaded = true;
    const std::wstring& tr = Gfx::ToolRoot();             // ...\<모드>\dd_preset_tools
    if (tr.empty()) return;
    std::wstring root = tr.substr(0, tr.find_last_of(L"\\/"));
    std::string xml = Gfx::ReadFile(root + L"\\localization\\dd_preset_tools.string_table.xml");
    size_t ls = xml.find("<language id=\"english\">");
    if (ls == std::string::npos) return;
    size_t le = xml.find("</language>", ls);
    size_t p = ls;
    while (true) {
        p = xml.find("<entry id=\"", p);
        if (p == std::string::npos || p > le) break;
        p += 11;
        size_t q = xml.find('"', p); if (q == std::string::npos) break;
        std::string id = xml.substr(p, q - p);
        size_t c0 = xml.find("<![CDATA[", q); if (c0 == std::string::npos) break;
        c0 += 9;
        size_t c1 = xml.find("]]>", c0); if (c1 == std::string::npos) break;
        g_english[id] = xml.substr(c0, c1 - c0);
        p = c1;
    }
    Log("localization fallback (english xml): %zu strings", g_english.size());
}
} // namespace

const char* T(const char* key) {
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.c_str();
    std::string full = std::string("ddpt_") + key;
    std::string s = Game::Localize(full);
    if (s.empty()) {
        LoadEnglish();
        auto e = g_english.find(full);
        s = e != g_english.end() ? e->second : full;
    }
    return g_cache.emplace(key, s).first->second.c_str();
}

std::string F(const char* key, std::initializer_list<std::string> args) {
    std::string s = T(key);
    int i = 0;
    for (const std::string& a : args) {
        std::string tag = "{" + std::to_string(i++) + "}";
        for (size_t p = s.find(tag); p != std::string::npos; p = s.find(tag, p + a.size())) s.replace(p, tag.size(), a);
    }
    return s;
}

std::string Num(long long v) { return std::to_string(v); }

// 게임이 보여주는 성전사 이름의 글자 종류로 언어 추정
std::string Language() {
    std::string n;
    for (const char* k : { "hero_class_name_crusader", "hero_class_name_highwayman", "hero_class_name_plague_doctor", "hero_class_name_vestal" })
        n += Game::Localize(k);
    if (n.empty()) return g_lang;
    bool hangul = false, kana = false, han = false, cyr = false;
    const unsigned char* s = (const unsigned char*)n.c_str();
    while (*s) {
        unsigned cp = *s;
        int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
        if (extra) { cp &= (0x3F >> extra); for (int k = 0; k < extra && s[1]; ++k) cp = (cp << 6) | (*++s & 0x3F); }
        ++s;
        if (cp >= 0xAC00 && cp <= 0xD7A3) hangul = true;
        else if (cp >= 0x3040 && cp <= 0x30FF) kana = true;
        else if (cp >= 0x4E00 && cp <= 0x9FFF) han = true;
        else if (cp >= 0x0400 && cp <= 0x04FF) cyr = true;
    }
    g_lang = hangul ? "koreana" : kana ? "japanese" : han ? "schinese" : cyr ? "russian" : "english";
    return g_lang;
}

void NewFrame() {
    if (++g_frame % 180 == 0) {          // 약 3초마다: 언어/모드 변경 반영
        g_cache.clear();
        std::string l = Language();
        if (!l.empty()) Gfx::SetFontLanguage(l);
    } else if (g_frame == 2) {
        std::string l = Language();
        if (!l.empty()) Gfx::SetFontLanguage(l);
    }
}
} // namespace Txt

#include <unordered_map>
#include <string>
#include <vector>
#include <cstring>
#include "text.h"
#include "game.h"
#include "gfx.h"
#include "log.h"

namespace Txt {
namespace {
std::unordered_map<std::string, std::string> g_cache;
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> g_xml;   // 언어 → (키 → 문구)
bool g_xmlLoaded = false;
int  g_frame = 0;
std::string g_lang;

// 모드 폴더의 dd_preset_tools.string_table.xml (12개 언어 원본)
// 게임이 이 모드의 .loc2 를 못 읽었을 때(예: 창작마당 구독판) 여기서 직접 꺼내 씀
void LoadXml() {
    if (g_xmlLoaded) return;
    g_xmlLoaded = true;
    const std::wstring& tr = Gfx::ToolRoot();             // ...\<모드>\dd_preset_tools
    if (tr.empty()) return;
    std::wstring root = tr.substr(0, tr.find_last_of(L"\\/"));
    std::string xml = Gfx::ReadFile(root + L"\\localization\\dd_preset_tools.string_table.xml");
    size_t ls = 0, total = 0;
    while ((ls = xml.find("<language id=\"", ls)) != std::string::npos) {
        ls += 14;
        size_t lq = xml.find('"', ls); if (lq == std::string::npos) break;
        std::string lang = xml.substr(ls, lq - ls);
        size_t le = xml.find("</language>", lq); if (le == std::string::npos) le = xml.size();
        auto& m = g_xml[lang];
        size_t p = lq;
        while (true) {
            p = xml.find("<entry id=\"", p);
            if (p == std::string::npos || p > le) break;
            p += 11;
            size_t q = xml.find('"', p); if (q == std::string::npos) break;
            std::string id = xml.substr(p, q - p);
            size_t c0 = xml.find("<![CDATA[", q); if (c0 == std::string::npos || c0 > le) break;
            c0 += 9;
            size_t c1 = xml.find("]]>", c0); if (c1 == std::string::npos) break;
            m[id] = xml.substr(c0, c1 - c0);
            p = c1; ++total;
        }
        ls = le;
    }
    Log("번역 예비 (xml): 언어 %zu개, 문구 %zu개", g_xml.size(), total);
}

const std::string* XmlFind(const std::string& lang, const std::string& id) {
    auto l = g_xml.find(lang); if (l == g_xml.end()) return nullptr;
    auto e = l->second.find(id); return e == l->second.end() ? nullptr : &e->second;
}

// 게임 폴더의 영웅 이름표(12개 언어)로 게임이 지금 쓰는 언어를 알아냄
//  (게임 안 옵션 언어가 Steam 언어와 다를 수 있어서, 게임이 실제로 보여주는 이름과 비교)
const char* kLangKeys[] = { "hero_class_name_bounty_hunter", "hero_class_name_plague_doctor", "hero_class_name_grave_robber" };
std::unordered_map<std::string, std::vector<std::string>> g_heroNames;   // 언어 → 위 키 순서의 이름
bool g_heroLoaded = false;

void LoadHeroNames() {
    if (g_heroLoaded) return;
    g_heroLoaded = true;
    std::string xml = Gfx::ReadFile(Gfx::GameRoot() + L"\\localization\\heroes.string_table.xml");
    size_t ls = 0;
    while ((ls = xml.find("<language id=\"", ls)) != std::string::npos) {
        ls += 14;
        size_t lq = xml.find('"', ls); if (lq == std::string::npos) break;
        std::string lang = xml.substr(ls, lq - ls);
        size_t le = xml.find("</language>", lq); if (le == std::string::npos) le = xml.size();
        std::vector<std::string> v;
        for (const char* k : kLangKeys) {
            std::string tag = std::string("<entry id=\"") + k + "\"><![CDATA[";
            size_t p = xml.find(tag, lq), e;
            if (p == std::string::npos || p > le || (e = xml.find("]]>", p)) == std::string::npos) { v.push_back(""); continue; }
            p += tag.size();
            v.push_back(xml.substr(p, e - p));
        }
        g_heroNames[lang] = v;
        ls = le;
    }
    Log("게임 언어 판별용 영웅 이름: 언어 %zu개", g_heroNames.size());
}

std::string GameLanguage() {
    LoadHeroNames();
    std::string cur[3];
    for (int i = 0; i < 3; ++i) cur[i] = Game::Localize(kLangKeys[i]);
    if (cur[0].empty() && cur[1].empty() && cur[2].empty()) return {};
    std::string best; int bestN = 0;
    for (auto& [lang, v] : g_heroNames) {
        int n = 0;
        for (int i = 0; i < 3 && i < (int)v.size(); ++i) if (!cur[i].empty() && cur[i] == v[i]) ++n;
        if (n > bestN) { bestN = n; best = lang; }
    }
    return best;
}
} // namespace

const char* T(const char* key) {
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.c_str();
    std::string full = std::string("ddpt_") + key;
    std::string s = Game::Localize(full);
    if (s.empty()) {
        LoadXml();
        const std::string* x = XmlFind(Language(), full);
        if (!x) x = XmlFind("english", full);
        s = x ? *x : full;
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
    std::string gl = GameLanguage();
    if (!gl.empty()) return g_lang = gl;
    std::string sl = Game::SteamLanguage();
    if (!sl.empty()) {
        LoadXml();
        if (g_xml.count(sl)) return g_lang = sl;
    }
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

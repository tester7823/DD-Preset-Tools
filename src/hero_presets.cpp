#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "hero_presets.h"
#include "log.h"

namespace HeroPresets {
namespace {
    Preset g_p[kCount];
    bool   g_dirty = false;
    DWORD  g_since = 0;
    int    g_profile = -2;     // -2 = 아직 한 번도 설정 안 됨

    std::wstring FilePath() {
        if (g_profile < 0) return ModDir() + L"\\hero_presets_unknown.txt";
        return ModDir() + L"\\hero_presets_profile_" + std::to_wstring(g_profile) + L".txt";
    }
    int ReadNow() {
        for (auto& p : g_p) p = Preset{};
        FILE* f = _wfopen(FilePath().c_str(), L"rb");
        if (!f) return 0;
        char line[512]; int idx = 0;
        while (idx < kCount && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            Preset& p = g_p[idx++];
            char* tab = strchr(line, '\t');
            size_t n = tab ? (size_t)(tab - line) : strlen(line);
            if (n >= sizeof p.name) n = sizeof p.name - 1;
            memcpy(p.name, line, n); p.name[n] = 0;
            for (int i = 0; i < 4 && tab; ++i) {
                p.guid[i] = (int)strtol(tab + 1, nullptr, 10);
                tab = strchr(tab + 1, '\t');
            }
        }
        fclose(f);
        return idx;
    }
    void WriteNow() {
        g_dirty = false;
        if (g_profile < 0) return;      // 영지를 모를 때는 파일에 쓰지 않음 (다른 영지로 섞이지 않게)
        std::wstring tmp = FilePath() + L".tmp";
        FILE* f = _wfopen(tmp.c_str(), L"wb");
        if (!f) { Log("영웅 프리셋 저장 실패"); return; }
        for (auto& p : g_p) fprintf(f, "%s\t%d\t%d\t%d\t%d\n", p.name, p.guid[0], p.guid[1], p.guid[2], p.guid[3]);
        fclose(f);
        MoveFileExW(tmp.c_str(), FilePath().c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

Preset& Get(int i) { return g_p[i]; }
int Profile() { return g_profile < 0 ? -1 : g_profile; }
void MarkDirty() { if (!g_dirty) g_since = GetTickCount(); g_dirty = true; }

void SetProfile(int profile) {
    if (profile == g_profile) return;
    if (g_dirty) WriteNow();
    g_profile = profile;
    int n = ReadNow();
    Log("영지 profile_%d 영웅 프리셋 %d개 로드", profile, n);
}

void ResetProfile(int profile) {
    if (profile < 0) return;
    std::wstring path = ModDir() + L"\\hero_presets_profile_" + std::to_wstring(profile) + L".txt";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileExW(path.c_str(), (path + L".deleted").c_str(), MOVEFILE_REPLACE_EXISTING);
    if (profile == g_profile) { for (auto& p : g_p) p = Preset{}; g_dirty = false; }
    Log("profile_%d 원정대 편성 비움", profile);
}

void SaveIfDirty() {
    if (!g_dirty || GetTickCount() - g_since < 500) return;
    WriteNow();
}

} // namespace HeroPresets

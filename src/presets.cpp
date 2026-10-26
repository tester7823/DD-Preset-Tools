// 프리셋 저장/불러오기 — 영지(세이브 슬롯)마다 따로
//   게임 폴더\_windows\win64\trinket_presets_profile_N.txt  (UTF-8, 탭 구분)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "presets.h"
#include "log.h"

namespace Presets {
namespace {
    Preset g_presets[kCount];
    bool   g_dirty = false;
    DWORD  g_dirtySince = 0;
    int    g_profile = -1;          // -1 = 아직 모름

    std::wstring FilePath() {
        if (g_profile < 0) return ModDir() + L"\\trinket_presets_unknown.txt";
        return ModDir() + L"\\trinket_presets_profile_" + std::to_wstring(g_profile) + L".txt";
    }

    void CopyField(char* dst, size_t cap, const char* src, size_t len) {
        if (len >= cap) len = cap - 1;
        memcpy(dst, src, len); dst[len] = 0;
    }

    int ReadFile(const std::wstring& path) {
        for (auto& p : g_presets) p = Preset{};
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) return -1;
        char line[512]; int idx = 0;
        while (idx < kCount && fgets(line, sizeof line, f)) {
            size_t n = strcspn(line, "\r\n"); line[n] = 0;
            const char* fields[3] = { line, "", "" };
            size_t lens[3] = { 0, 0, 0 };
            int fi = 0; const char* start = line;
            for (char* p = line;; ++p) {
                if (*p == '\t' || *p == 0) {
                    if (fi < 3) { fields[fi] = start; lens[fi] = (size_t)(p - start); }
                    ++fi; if (*p == 0) break; start = p + 1;
                }
            }
            Preset& pr = g_presets[idx++];
            CopyField(pr.name, sizeof pr.name, fields[0], lens[0]);
            CopyField(pr.trinket[0], sizeof pr.trinket[0], fields[1], lens[1]);
            CopyField(pr.trinket[1], sizeof pr.trinket[1], fields[2], lens[2]);
        }
        fclose(f);
        return idx;
    }

    void WriteNow() {
        if (g_profile < 0) { g_dirty = false; return; }   // 영지를 모를 때는 파일에 쓰지 않음
        std::wstring tmp = FilePath() + L".tmp";
        FILE* f = _wfopen(tmp.c_str(), L"wb");
        if (!f) { Log("프리셋 저장 실패"); g_dirty = false; return; }
        for (auto& p : g_presets) fprintf(f, "%s\t%s\t%s\n", p.name, p.trinket[0], p.trinket[1]);
        fclose(f);
        MoveFileExW(tmp.c_str(), FilePath().c_str(), MOVEFILE_REPLACE_EXISTING);
        g_dirty = false;
    }
}

Preset& Get(int i) { return g_presets[i]; }
int Profile() { return g_profile; }

void MarkDirty() { if (!g_dirty) g_dirtySince = GetTickCount(); g_dirty = true; }

void Load() {
    // 영지를 알기 전에는 비어 있는 상태로 시작 (SetProfile 에서 불러옴)
    for (auto& p : g_presets) p = Preset{};
}

void SetProfile(int profile) {
    if (profile == g_profile) return;
    if (g_dirty) WriteNow();                    // 이전 영지 프리셋을 먼저 저장
    g_profile = profile;
    int n = ReadFile(FilePath());
    Log("영지 profile_%d 프리셋 %d개 로드", profile, n < 0 ? 0 : n);
}

void ResetProfile(int profile) {
    if (profile < 0) return;
    std::wstring path = ModDir() + L"\\trinket_presets_profile_" + std::to_wstring(profile) + L".txt";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileExW(path.c_str(), (path + L".deleted").c_str(), MOVEFILE_REPLACE_EXISTING);
    if (profile == g_profile) { for (auto& p : g_presets) p = Preset{}; g_dirty = false; }
    Log("profile_%d 장신구 조합 비움", profile);
}

void SaveIfDirty() {
    if (!g_dirty || GetTickCount() - g_dirtySince < 500) return;  // 타이핑 중엔 0.5초 모아서 저장
    WriteNow();
}

void Flush() { if (g_dirty) WriteNow(); }   // 게임 종료 직전: 0.5초 기다리지 않고 바로 저장

} // namespace Presets

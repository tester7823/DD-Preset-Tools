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

    std::wstring LegacyPath() { return ModDir() + L"\\trinket_presets.txt"; }
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
    std::wstring unk = ModDir() + L"\\trinket_presets_unknown.txt";
    if (n < 0 && profile >= 0 && GetFileAttributesW(unk.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // 영지 감지가 안 되던 동안(v0.6~0.7) 저장된 프리셋을 이 영지로 옮김
        n = ReadFile(unk);
        WriteNow();
        MoveFileExW(unk.c_str(), (unk + L".migrated").c_str(), MOVEFILE_REPLACE_EXISTING);
        Log("영지 미확인 프리셋을 profile_%d 로 옮김", profile);
    }
    if (n < 0 && profile >= 0 && GetFileAttributesW(LegacyPath().c_str()) != INVALID_FILE_ATTRIBUTES) {
        // v0.5 이전의 공용 파일은 처음 연 영지로 옮김 (한 번만)
        n = ReadFile(LegacyPath());
        WriteNow();
        MoveFileExW(LegacyPath().c_str(), (LegacyPath() + L".migrated").c_str(), MOVEFILE_REPLACE_EXISTING);
        Log("기존 공용 프리셋을 profile_%d 로 옮김", profile);
    }
    Log("영지 profile_%d 프리셋 %d개 로드", profile, n < 0 ? 0 : n);
}

void SaveIfDirty() {
    if (!g_dirty || GetTickCount() - g_dirtySince < 500) return;  // 타이핑 중엔 0.5초 모아서 저장
    WriteNow();
}

} // namespace Presets

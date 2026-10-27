#pragma once

namespace Presets {
    constexpr int kCount = 10;
    constexpr int kMaxSlots = 8;    // 장신구 칸 확장 모드(4칸·6칸·8칸)까지. 기본 게임은 2칸

    struct Preset {
        char name[64]               = {};
        char trinket[kMaxSlots][64] = {};   // 장신구 ID (예: "sun_ring"), 칸 순서대로
        bool Any(int n = kMaxSlots) const { for (int s = 0; s < n && s < kMaxSlots; ++s) if (trinket[s][0]) return true; return false; }
    };

    void    Load();
    void    SetProfile(int profile);   // 영지(세이브 슬롯 profile_N) 바뀔 때. 이전 것은 저장 후 새 파일 로드
    int     Profile();                 // -1 = 모름
    void    ResetProfile(int profile);   // 영지 삭제 시: 그 슬롯의 저장 파일을 .deleted 로 치우고 비움
    void    SaveIfDirty();
    void    Flush();
    void    MarkDirty();
    Preset& Get(int i);
}

#pragma once

namespace Presets {
    constexpr int kCount = 10;

    struct Preset {
        char name[64]       = {};
        char trinket[2][64] = {};   // 장신구 ID (예: "sun_ring"). 2단계에서 실제 아이템 식별자로 확장
    };

    void    Load();
    void    SetProfile(int profile);   // 영지(세이브 슬롯 profile_N) 바뀔 때. 이전 것은 저장 후 새 파일 로드
    int     Profile();                 // -1 = 모름
    void    SaveIfDirty();
    void    MarkDirty();
    Preset& Get(int i);
}

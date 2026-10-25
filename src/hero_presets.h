#pragma once
// 영웅(파티) 프리셋 저장 — 영지(세이브 슬롯)마다 따로
//   게임 폴더\_windows\win64\hero_presets_profile_N.txt  (UTF-8, 탭 구분: 이름 guid0 guid1 guid2 guid3)
namespace HeroPresets {
    constexpr int kCount = 10;
    struct Preset {
        char name[64] = {};
        int  guid[4]  = { -1, -1, -1, -1 };    // 파티 배열 인덱스 0~3 (-1 = 빈칸)
        bool Empty() const { return guid[0] < 0 && guid[1] < 0 && guid[2] < 0 && guid[3] < 0; }
    };
    void    SetProfile(int profile);
    int     Profile();
    void    ResetProfile(int profile);   // 영지 삭제 시: 그 슬롯의 저장 파일을 .deleted 로 치우고 비움
    void    SaveIfDirty();
    void    MarkDirty();
    Preset& Get(int i);
}

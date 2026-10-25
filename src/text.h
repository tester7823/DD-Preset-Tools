#pragma once
// 화면 문구 — 전부 모드 폴더의 localization 에서 가져옴 (DLL 에는 문구가 없음)
//   1) 게임의 번역 시스템 (mods\<모드>\localization\*_<언어>.loc2, 키 "ddpt_<키>")
//      → 다른 사람이 만든 번역 모드도 같은 키로 덮어쓸 수 있음
//   2) 없으면 localization\dd_preset_tools.string_table.xml 의 영어
//   3) 그래도 없으면 키 그대로
#include <string>
#include <initializer_list>

namespace Txt {
    const char* T(const char* key);                                           // 이번 프레임 동안 유효
    std::string F(const char* key, std::initializer_list<std::string> args); // {0} {1} 치환
    std::string Num(long long v);
    void NewFrame();                                                          // 가끔 캐시 비움 (언어/모드 변경 반영)
    std::string Language();                                                   // 게임 언어 추정 (글꼴 선택용)
}

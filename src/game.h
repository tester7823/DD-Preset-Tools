#pragma once
// 게임 내부 연결 (Darkest.exe 64비트, 2026-09-21 빌드 기준)
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "imgui.h"

namespace Game {

bool Install();             // 후킹 설치 (첫 프레임에 1회)
bool Supported();           // 게임 버전이 맞아 후킹이 설치됐는지

// 2D 변환 (게임 UI 좌표 → 화면)
struct Xform {
    float m[6] = {1,0,0,1,0,0};
    ImVec2 ToScreen(ImVec2 p) const { return ImVec2(m[0]*p.x + m[2]*p.y + m[4], m[1]*p.x + m[3]*p.y + m[5]); }
    float  Scale() const { return m[0]; }
};

// 장신구 가방(RealmInventory) 패널
struct BagState {
    bool   visible = false;
    Xform  xf;
    ImVec2 unequipPos;               // "장신구 해제" 버튼 위치 (패널 좌표)
    ImVec2 spacing;                  // 정렬 버튼 간격
    float  raw[16] = {};
    ImVec2 ToScreen(ImVec2 p) const { return xf.ToScreen(p); }
    float  Scale() const { return xf.Scale(); }
};

// 영웅 정보창(CharacterDisplay)
struct HeroPanelState {
    bool   visible = false;
    Xform  xf;
    ImVec2 anchor;                   // 색상(팔레트) 버튼 그림 위치 (패널 좌표)
    void*  hero = nullptr;
};

// 가방에서 끌고 있는 장신구
struct DragState {
    bool     active = false;
    uint32_t hash = 0;
    bool     isTrinket = false;
};

// 영웅 목록에서 끌고 있는 영웅
struct HeroDragState {
    bool active = false;
    int  guid = -1;
};

// 영지 지도(원정 준비) 화면의 파티 슬롯 4칸
struct PartyState {
    bool  visible = false;           // 영지 지도 화면이 보이는지
    bool  slotSeen[4] = {};          // 각 슬롯(파티 배열 인덱스)이 이번 프레임에 그려졌는지
    Xform slotXf[4];                 // 슬롯 좌표 → 화면 (슬롯 왼쪽 위가 0,0, 크기 85x85)
};

// 영웅 정보 (로스터에서 읽음)
struct HeroInfo {
    int guid = -1;
    std::string name, classId, portrait;   // portrait = 게임 상대 경로
    int status = 0;                  // 0 대기, 1 파티, 3 사망/제외, 4·8 행방불명, 그 외 활동 중
    bool Available() const { return status == 0 || status == 1; }
    bool Missing() const { return status == 4 || status == 8; }
    bool Dead() const { return status == 3; }       // 게임 코드: 사망 시 상태를 3 으로 설정
};

void BeginFrame();
const BagState&       Bag();
const HeroPanelState& HeroPanel();
const DragState&      Drag();
const HeroDragState&  HeroDrag();
const PartyState&     Party();
float SubtitleAlpha();
bool  UnderSubtitle();     // 지금 우리 창을 자막 아래(게임이 화면을 어둡게 하는 막 아래)에 그리는 중인지
float ScreenVisibility();   // 화면 전환 중 0 (검은 화면) ~ 1. 어두워지기 시작하면 빠르게 0
float ModalAmount();
bool  TownTransition();     // 마을 화면 전환 중 (검은 화면으로 넘어가는 중)
bool  QuestSelectVisible();
bool  TopLayerActive();
bool  TopMenuOpen();      // 확인 창·양초(ESC) 메뉴 등 맨 위 층이 떠 있는지
bool  TownFadingOut();
std::string Localize(const std::string& key);   // 게임 번역 (없으면 빈 문자열)
bool  ModEnabled();         // 모드 목록(파란 망치)에서 이 모드가 켜져 있는지. 꺼져 있으면 모든 기능 숨김
bool  ProvisionVisible();   // 원정 준비(상점) 화면이 보이는지
int   Gold();               // 현재 골드 (-1 = 모름)
struct BulkBuyResult { bool ok = false; int bought = 0, spent = 0; bool outOfGold = false, bagFull = false; std::string error; };
BulkBuyResult BulkBuyProvisions();   // 정해진 순서/개수로 자동 구매 (골드가 모자라면 그 시점에서 중단)
bool  AutoBuyAvailable();
std::string BuyPlanText();   // "식량 전부, 횃불 전부, 삽 4개, ..." (확인 창 표시용)   // 원정 준비 화면을 연 뒤 아직 아무것도 담지 않았는지 (기본 지급품 제외)
bool  ShowHeroInfo(int guid);   // 영웅 정보창 열기 (영웅 목록 우클릭과 같은 동작)   // 상점 왼쪽→오른쪽 순서로 골드 아이템을 전부 구매 (골드가 모자라면 그 시점에서 중단)      // 마을 화면이 검은 화면으로 넘어가는 중     // 게임이 맨 위층(확인 창/화면 전환)을 그리는 중에 오버레이를 그리고 있음 // 영지 지도(원정 준비 전 단계)가 이번 프레임에 보였는지        // 게임의 확인 창이 떠 있으면 0보다 큼
void  SetModalRenderCallback(void (*cb)());   // 게임이 확인 창/전환 층을 그리기 직전에 호출
void  SetLateRenderCallback(void (*cb)());    // 자막이 있을 때: 게임 요소를 다 그린 뒤, 자막 글자 직전
void  NewGameFrame();                         // 화면 넘기기(Swap) 때마다   // 확인 창을 그리기 직전에 호출      // 화면에 자막이 떠 있으면 0보다 큼 (버튼이 자막을 가리지 않도록)
bool InTown();
int  PopDeletedSlot();       // 게임에서 삭제된 영지 슬롯 번호 (없으면 -1). 한 번 꺼내면 지워짐
int  CurrentProfile();      // 게임이 불러온 세이브 슬롯 번호 (profile_N), 모르면 -1

// ---- 장신구 정보 ----
uint32_t Hash(const char* s);                       // 게임과 같은 문자열 해시
struct TrinketInfo {
    std::string id, rarity, name;
    std::vector<uint32_t> classes;    // 전용 직업(해시). 비어 있으면 모든 직업
    std::string classLabel;           // 예: "역병 의사 전용" (전용이 아니면 빈 문자열)
    std::wstring root;                // 이 장신구가 정의된 폴더 (아이콘을 같은 곳에서 찾음). 비면 게임 기본
    bool valid = false;
};
const TrinketInfo& Trinket(uint32_t hash);          // 라이브러리 조회 (캐시)
const TrinketInfo& TrinketById(const char* id);

// 직업 관련
uint32_t HeroClassHash(void* hero);                 // 0 = 알 수 없음
bool ClassesCompatible(const TrinketInfo& a, const TrinketInfo& b);   // 두 장신구를 한 영웅이 낄 수 있는지
bool CanUse(const TrinketInfo& t, uint32_t heroClass);

// 게임 효과음 (FMOD 이벤트 경로, 예: "/ui/town/button_click")
void PlaySound(const char* eventPath);

// 보유 개수 (가방 + 모든 영웅). ok=false 면 읽기 실패 → 자동 삭제하지 말 것
std::unordered_map<uint32_t, int> OwnedCounts(bool* ok);

// 영웅
bool GetHero(int guid, HeroInfo* out);              // 로스터에 없으면 false (사망/해고)
bool RosterReadable();                              // 로스터를 읽을 수 있는지 (자동 정리 안전장치)
std::string ClassName(const std::string& classId);  // 현지화된 직업 이름
// 영웅 프리셋 적용: 파티 슬롯 0~3 에 guid 배치 (-1 = 빈칸)
struct PartyResult { bool ok = false; std::string error; };
PartyResult LoadParty(const int guids[4]);

// 프리셋 적용: 가방에서 꺼내 영웅에게 장착 (빈 문자열 = 그 칸은 건드리지 않음)
struct EquipResult { int equipped = 0, already = 0; std::vector<std::string> missing; std::string error; };
EquipResult EquipPreset(void* hero, const char* id0, const char* id1);

} // namespace Game

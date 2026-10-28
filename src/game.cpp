// 게임 함수 후킹 + 게임 데이터 읽기/쓰기 (MinHook)
// 대상: Darkest.exe x64, TimeDateStamp 0x6AB13309 (2026-09-21 빌드)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include "MinHook.h"
#include "gfx.h"
#include "game.h"
#include "text.h"
#include "log.h"

namespace Game {
namespace {

// ---- 주소(RVA) --------------------------------------------------------------
constexpr DWORD kExpectedStamp = 0x6AB13309;
// 후킹 대상 함수
constexpr DWORD RVA_DrawUnequipButton  = 0x0069C900;  // TownUI::Panel::RealmInventory::DrawUnequipButton(vec2)
constexpr DWORD RVA_RIDragRender       = 0x007C4060;  // RealmInventoryItemDragObject::Render
constexpr DWORD RVA_PaletteButton      = 0x007F1B40;  // UI::Panel::CharacterDisplay::ShowCharacterPaletteButton(vec2, Hero*)
constexpr DWORD RVA_RosterPreShow      = 0x006A95A0;  // TownUI::Panel::RosterList::PreChildrenShowInternal
constexpr DWORD RVA_HeroSlotPreShow    = 0x00680F60;  // TownUI::Panel::HeroSlot::PreChildrenShowInternal (슬롯마다)
constexpr DWORD RVA_QuestSelectPreShow = 0x0068D120;  // TownUI::Panel::QuestSelect::PreChildrenShowInternal (영지 지도)
constexpr DWORD RVA_ProvisionPreShow   = 0x006890C0;  // ProvisionSelect::PreChildrenShowInternal (원정 준비 화면)
constexpr DWORD RVA_ProvisionBuyOne    = 0x0068B7C0;  // 원정 준비 상점 좌클릭 람다 _Do_call (캡처: +8 ProvisionSelect, +0x10 상점, +0x18 가방)
constexpr DWORD RVA_ItemGoldPrice      = 0x00571FD0;  // (unused, ItemClass*) → 골드 가격
constexpr DWORD RVA_ItemShardPrice     = 0x00572080;  // (unused, ItemClass*) → 파편 가격
constexpr DWORD RVA_ItemClassList      = 0x0117D978;  // vector<ItemClass*>* (+0x40 종류 해시, +0x84 ID 해시)
constexpr DWORD RVA_GoldHash           = 0x02BA44F0;  // 골드 통화 해시
constexpr DWORD RVA_ShardHash          = 0x02BA4360;  // 파편 통화 해시
constexpr DWORD RVA_InputMode          = 0x0117CD94;  // 0 마우스, 1 패드
constexpr DWORD RVA_InputModeFlag      = 0x01174ADA;
constexpr DWORD RVA_RosterShowChar     = 0x006A50B0;  // 영웅 목록 원소의 '정보창 열기' 람다 본체 (캡처 = 원소*)
constexpr DWORD RVA_GamePersist        = 0x02A5C288;  // persist.game (+0x208 적용된 모드 목록 vector, 원소 0xC0: 이름[0x80], 출처[0x40])
constexpr DWORD RVA_TownRender         = 0x00411BA0;  // 마을 그리기 (this+0x1F18 = 화면 전환 상태, 3/9 이면 전환 중 검은 화면)
constexpr DWORD RVA_RenderAll          = 0x00271F40;  // 지연 그리기 목록 전체를 그리는 함수 (자막 함수 바로 뒤에 호출됨)
constexpr DWORD RVA_DrawElement        = 0x002726F0;  // 지연 그리기 목록의 원소 하나 그리기 (원소+0 = 층. 5 일반 UI, 9 툴팁, 12 확인 창/전환)
constexpr DWORD RVA_ConfirmShow        = 0x007BA940;  // ConfirmDialog::System::Show (this+0x2C = 나타난 정도)
constexpr DWORD RVA_ModalRender        = 0x00799D20;  // ModalDialog::Render (확인 창). this+0x5C = 나타난 정도
constexpr DWORD RVA_GlobalFade         = 0x02A5D36C;  // 화면 전체 밝기 (1 = 보임, 0 = 검은 화면), +4 = 목표값
constexpr DWORD RVA_SubtitleRender     = 0x00610DC0;  // 자막 그리기 (shared/subtitles/subtitle_bg.png)
constexpr DWORD RVA_HeroDragRender     = 0x007C3320;  // HeroDragObject::Render
constexpr DWORD RVA_SetInParty         = 0x005817C0;  // Roster::System::SetInParty(entry, 0/1)
constexpr DWORD RVA_RefreshParty       = 0x0068BC00;  // QuestSelect::RefreshParty (파티 이름 갱신)
constexpr DWORD RVA_TownUIPtr          = 0x0117DBE0;  // TownUI* (EmbarkParty = +0x3640)
constexpr DWORD RVA_VT_QSlotIface      = 0x00ED1D60;  // QuestSelectHeroSlotInterface vtable
constexpr DWORD RVA_VT_HeroDragData    = 0x00ECF420;  // HeroDragData vtable
// 호출하는 게임 함수
constexpr DWORD RVA_InvSetItem         = 0x005D3EB0;  // Inventory::System::SetItem(slot, Item&) — 칸과 item 을 맞바꿈
constexpr DWORD RVA_ItemCopy           = 0x003CA5F0;  // Item 복사 생성
constexpr DWORD RVA_Dtor_242400        = 0x00242400;  // Item 소멸 단계들 (SetItem 이 임시 Item 에 쓰는 순서 그대로)
constexpr DWORD RVA_Dtor_232d70        = 0x00232D70;
constexpr DWORD RVA_Dtor_3839b0        = 0x003839B0;
constexpr DWORD RVA_HeroRefresh        = 0x0047CA10;  // 장비 변경 후 영웅 갱신 (장신구 효과 재계산)
constexpr DWORD RVA_Localize           = 0x0043C770;  // 문자열 테이블 조회
constexpr DWORD RVA_PlaySound          = 0x0022FAC0;  // 효과음 재생 (이벤트 경로, -, std::function*)
// 전역 데이터
constexpr DWORD RVA_MatrixCurrent      = 0x01174B78;  // 현재 UI 변환 행렬 float[16]
constexpr DWORD RVA_SortButtonSpacing  = 0x02B7CD90;  // realm_inventory_layout.sort_button_spacing
constexpr DWORD RVA_PaletteIconOffset  = 0x02BEED60;  // 캐릭터 화면 팔레트 버튼 그림 오프셋
constexpr DWORD RVA_CampaignPtr        = 0x0117D9B8;  // Campaign*
constexpr DWORD RVA_AltModeFlag        = 0x02A5C08B;  // 1 이면 다른 저장 슬롯(+0x68 이동) 사용
constexpr DWORD RVA_TrinketLibBegin    = 0x02C2AA20;  // vector<TrinketClass> (0x428 바이트)
constexpr DWORD RVA_RarityBegin        = 0x02C2AA38;  // vector<Rarity> (0x4C 바이트)
constexpr DWORD RVA_HeroClassBegin     = 0x02C2A7B8;  // vector<HeroClass*> (대체 모드: 0x2C2A798), 직업 해시 +0xD4

const unsigned char kSig_DrawUnequip[] = { 0x48,0x8B,0xC4, 0x48,0x89,0x58,0x08, 0x48,0x89,0x70,0x18, 0x48,0x89,0x50,0x10, 0x55,0x57 };
const unsigned char kSig_RIDragRender[] = { 0x48,0x89,0x5C,0x24,0x08, 0x57, 0x48,0x83,0xEC,0x30, 0x48,0x8B,0x79,0x08 };
const unsigned char kSig_Palette[] = { 0x48,0x89,0x74,0x24,0x08, 0x48,0x89,0x7C,0x24,0x20, 0x48,0x89,0x54,0x24,0x10, 0x55, 0x41,0x54 };
const unsigned char kSig_HeroSlotPre[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0x28,0xE9,0xFF,0xFF,0xB8,0xD8,0x17 };
const unsigned char kSig_QuestSelPre[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xF8,0x32,0xFF,0xFF,0xB8,0x08,0xCE };
const unsigned char kSig_Subtitle[] = { 0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0xA8,0xD8,0xF8,0xFF,0xFF,0x48,0x81,0xEC,0xF0 };
const unsigned char kSig_ProvPre[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0x28,0xF1,0xFF,0xFF,0x48,0x81,0xEC };
const unsigned char kSig_ProvBuy[] = { 0x8B,0x12,0x48,0x83,0xC1,0x08,0xE9,0x55,0xE5,0xFF,0xFF };
const unsigned char kSig_Price[]   = { 0x40,0x53,0x48,0x83,0xEC,0x30,0x80,0xBA,0x56,0x01,0x00,0x00,0x00,0x48,0x8B,0xDA };
const unsigned char kSig_TownRender[] = { 0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xA8,0x78,0xF3,0xFF,0xFF,0x48,0x81 };
const unsigned char kSig_RenderAll[] = { 0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0xB8,0xA8,0x10,0x00,0x00 };
const unsigned char kSig_DrawElem[] = { 0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x41,0x56,0x48,0x8D,0x6C,0x24,0xB9,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x48,0x8B,0x05 };
const unsigned char kSig_Confirm[] = { 0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x55,0x48,0x8D,0xAC,0x24,0xC0,0xFC,0xFF,0xFF,0x48,0x81,0xEC,0x40,0x04 };
const unsigned char kSig_Modal[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xD8,0xF7,0xFF,0xFF,0x48,0x81,0xEC,0x28,0x09,0x00,0x00 };
const unsigned char kSig_HeroDrag[] = { 0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xA8,0x98,0xF8,0xFF,0xFF,0x48,0x81 };
const unsigned char kSig_RosterPre[] = { 0x48,0x8B,0xC4, 0x55,0x53,0x56,0x57, 0x41,0x56, 0x41,0x57, 0x48,0x8D,0xA8,0xD8,0xF8,0xFF,0xFF };

constexpr size_t kItemSize = 0x5A8;
constexpr size_t kLibEntrySize = 0x428;
constexpr size_t kRaritySize = 0x4C;

BYTE* g_base = nullptr;
bool  g_ok = false;
uint32_t HashImpl(const char* s);
int   g_hashOk = -1;      // -1 미확인, 0 불일치(데이터 변경 기능 끔), 1 확인
uint32_t g_trinketType = 0;

template <class T> T At(DWORD rva) { return (T)(g_base + rva); }

// ---- 게임 함수 포인터 ----
using FnSetItem   = void  (__fastcall*)(void* inv, unsigned idx, void* item);
using FnItemCopy  = void  (__fastcall*)(void* dst, const void* src);
using FnVoidPtr   = void  (__fastcall*)(void*);
using FnVoidPtr2  = void  (__fastcall*)(void*, void*);
using FnLocalize  = void* (__fastcall*)(void* out, const char* key);
using FnPlaySound = void  (__fastcall*)(const char* path, unsigned long long unused, void* callback);

// ---- 후킹 원본 ----
using FnDrawUnequip = void (__fastcall*)(void* self, unsigned long long packedPos);
using FnSelfOnly    = void (__fastcall*)(void* self);
using FnPalette     = void (__fastcall*)(void* self, unsigned long long packedPos, void* hero);
FnDrawUnequip o_DrawUnequip = nullptr;
FnSelfOnly    o_RIDragRender = nullptr;
FnPalette     o_Palette = nullptr;
FnSelfOnly    o_RosterPre = nullptr;
FnSelfOnly    o_HeroSlotPre = nullptr;
FnSelfOnly    o_QuestSelPre = nullptr;
FnSelfOnly    o_HeroDrag = nullptr;

// 게임 스레드가 이번 프레임에 채우는 값
BagState g_bagPending, g_bag;            unsigned g_bagSeen = 0, g_bagLast = 0;
HeroPanelState g_heroPending, g_hero;    unsigned g_heroSeen = 0, g_heroLast = 0;
DragState g_dragPending, g_drag;         unsigned g_dragSeen = 0, g_dragLast = 0;
unsigned g_townSeen = 0, g_townLast = 0; bool g_town = false; int g_townMiss = 0;
HeroDragState g_hdragPending, g_hdrag;   unsigned g_hdragSeen = 0, g_hdragLast = 0;
PartyState g_partyPending, g_party;      unsigned g_qsSeen = 0, g_qsLast = 0;
void* g_questSelect = nullptr;
bool g_bulkOk = false;
float g_screenVis = 1.0f;
bool g_qsRecent = false;
volatile float g_subAlpha = 0; unsigned g_subSeen = 0, g_subLast = 0;
bool g_logHDrag = false;
bool g_logBag = false, g_logHero = false, g_logDrag = false;

float g_mapK = 1.0f, g_mapX = 0.0f, g_mapY = 0.0f; int g_mapVW = 0, g_mapVH = 0;

Xform CurrentXform(float raw[16] = nullptr) {
    const float* mat = At<const float*>(RVA_MatrixCurrent);
    if (raw) memcpy(raw, mat, 16 * sizeof(float));
    Xform x;
    if (std::fabs(mat[12]) + std::fabs(mat[13]) >= std::fabs(mat[3]) + std::fabs(mat[7])) {
        x.m[0] = mat[0]; x.m[1] = mat[1]; x.m[2] = mat[4]; x.m[3] = mat[5]; x.m[4] = mat[12]; x.m[5] = mat[13];
    } else {
        x.m[0] = mat[0]; x.m[1] = mat[4]; x.m[2] = mat[1]; x.m[3] = mat[5]; x.m[4] = mat[3]; x.m[5] = mat[7];
    }
    for (int i = 0; i < 4; ++i) x.m[i] *= g_mapK;
    x.m[4] = x.m[4] * g_mapK + g_mapX;
    x.m[5] = x.m[5] * g_mapK + g_mapY;
    return x;
}

ImVec2 Unpack(unsigned long long v) { float f[2]; memcpy(f, &v, 8); return ImVec2(f[0], f[1]); }

void __fastcall Hook_DrawUnequip(void* self, unsigned long long packedPos) {
    BagState s;
    s.visible = true;
    s.xf = CurrentXform(s.raw);
    s.unequipPos = Unpack(packedPos);
    const float* sp = At<const float*>(RVA_SortButtonSpacing);
    s.spacing = ImVec2(sp[0], sp[1]);
    g_bagPending = s; ++g_bagSeen;
    if (!g_logBag) {
        g_logBag = true;
        ImVec2 o = s.ToScreen(ImVec2(0, 0));
        Log("장신구 가방 감지: unequipPos=(%.1f,%.1f) spacing=(%.1f,%.1f) 원점=(%.1f,%.1f) 배율=%.3f",
            s.unequipPos.x, s.unequipPos.y, s.spacing.x, s.spacing.y, o.x, o.y, s.Scale());
    }
    o_DrawUnequip(self, packedPos);
}

void __fastcall Hook_RIDragRender(void* self) {
    BYTE* data = *(BYTE**)((BYTE*)self + 8);
    if (data) {
        BYTE* item = data + 8;
        DragState d;
        d.active = *(int*)item > 0;
        d.hash = *(uint32_t*)(item + 0x88);
        d.isTrinket = *(uint32_t*)(item + 0x44) == g_trinketType;
        g_dragPending = d; ++g_dragSeen;
        if (!g_logDrag) { g_logDrag = true; Log("가방 드래그 감지: hash=0x%08X trinket=%d", d.hash, d.isTrinket); }
    }
    o_RIDragRender(self);
}

void __fastcall Hook_Palette(void* self, unsigned long long packedPos, void* hero) {
    HeroPanelState h;
    h.visible = true;
    h.xf = CurrentXform();
    ImVec2 p = Unpack(packedPos);
    const float* off = At<const float*>(RVA_PaletteIconOffset);
    h.anchor = p;                 // 색상 버튼 그림 위치 = 인자 그대로 (오프셋을 더하면 오른쪽으로 56 밀림)
    h.hero = hero;
    g_heroPending = h; ++g_heroSeen;
    if (!g_logHero) {
        g_logHero = true;
        ImVec2 s = h.xf.ToScreen(h.anchor);
        Log("영웅 정보창 감지: pos=(%.1f,%.1f) off=(%.1f,%.1f) 화면=(%.1f,%.1f) 배율=%.3f hero=%p",
            p.x, p.y, off[0], off[1], s.x, s.y, h.xf.Scale(), hero);
    }
    o_Palette(self, packedPos, hero);
}

void* g_rosterList = nullptr;
bool g_showHeroOk = false;
void __fastcall Hook_RosterPre(void* self) { g_rosterList = self; ++g_townSeen; o_RosterPre(self); }

BYTE* EmbarkParty() { BYTE* ui = *At<BYTE**>(RVA_TownUIPtr); return ui ? ui + 0x3640 : nullptr; }

// 파티 슬롯(영지 지도 아래 4칸)이 그려질 때: 슬롯 위치 기록
void __fastcall Hook_HeroSlotPre(void* self) {
    BYTE* ep = EmbarkParty();
    if (ep) {
        BYTE** b = *(BYTE***)(ep + 0x88); BYTE** e = *(BYTE***)(ep + 0x90);
        if (b && e > b && e - b <= 8) {
            for (BYTE** p = b; p != e; ++p) {
                if (*p != (BYTE*)self) continue;
                BYTE* iface = *(BYTE**)((BYTE*)self + 0x158);
                if (!iface || *(void**)iface != (void*)(g_base + RVA_VT_QSlotIface)) break;
                unsigned idx = *(unsigned*)(iface + 0x230);
                if (idx < 4) {
                    g_partyPending.slotSeen[idx] = true;
                    g_partyPending.slotXf[idx] = CurrentXform();
                    static int logged = 0;
                    if (!(logged & (1 << idx))) {
                        logged |= 1 << idx;
                        ImVec2 o = g_partyPending.slotXf[idx].ToScreen(ImVec2(0, 0));
                        Log("파티 슬롯 %u 감지: 화면 (%.1f,%.1f) 배율 %.3f", idx, o.x, o.y, g_partyPending.slotXf[idx].Scale());
                    }
                }
                break;
            }
        }
    }
    o_HeroSlotPre(self);
}

// 확인 창(모달): 나타난 정도 기록, 그리기 직전에 오버레이를 먼저 그려서 창이 오버레이 위에 오도록
FnSelfOnly o_Modal = nullptr;
float g_modalPending = 0, g_modal = 0; unsigned g_modalSeen = 0, g_modalLast = 0;
void (*g_modalCb)() = nullptr;
void __fastcall Hook_Modal(void* self) {
    float d = self ? *(float*)((BYTE*)self + 0x5C) : 0.0f;
    if (d > 0.0f && d < 100.0f) {
        g_modalPending = std::max(g_modalPending, std::min(1.0f, d));
        ++g_modalSeen;
        static bool logged = false;
        if (!logged) { logged = true; Log("확인 창 감지 (%.2f)", d); }
    }
    o_Modal(self);
}
FnSelfOnly o_Confirm = nullptr;
void __fastcall Hook_Confirm(void* self) {
    float d = self ? *(float*)((BYTE*)self + 0x2C) : 0.0f;
    if (d > 0.0f && d < 100.0f) {
        g_modalPending = std::max(g_modalPending, std::min(1.0f, d));
        ++g_modalSeen;
        static bool logged = false;
        if (!logged) { logged = true; Log("확인 창(ConfirmDialog) 감지 (%.2f)", d); }
    }
    o_Confirm(self);
}

// 원정 준비 화면
FnSelfOnly o_ProvPre = nullptr;
void* g_provSelect = nullptr; unsigned g_provSeen = 0, g_provLast = 0; bool g_provVisible = false;
void __fastcall Hook_ProvPre(void* self) { g_provSelect = self; ++g_provSeen; o_ProvPre(self); }

// 마을 화면 전환 상태
FnSelfOnly o_TownRender = nullptr;
int g_townStatePending = -1, g_townState = -1; unsigned g_townRSeen = 0, g_townRLast = 0;
void __fastcall Hook_TownRender(void* self) {
    if (self) {
        int st = *(int*)((BYTE*)self + 0x1F18);
        if (st != g_townStatePending) Log("마을 전환 상태 %d → %d", g_townStatePending, st);
        g_townStatePending = st; ++g_townRSeen;
    }
    o_TownRender(self);
}

// 게임의 지연 그리기: 층(0~)별로 정렬돼 그려짐. 확인 창/화면 전환 층(12) 직전에 오버레이를 끼워 그림
constexpr int kOverlayLayer = 12;
FnSelfOnly o_DrawElem = nullptr;
bool g_inTopLayer = false;
bool g_townFadingOut = false;
bool g_layerDrawn = false;
unsigned long long g_layerMask = 0, g_layerMaskLogged = ~0ull, g_prevLayerMask = 0;
int g_drawDepth = 0;           // 게임의 층별 그리기 도중인지
// 게임은 그릴 요소를 배열(RVA 0x1202520, 0x358 바이트씩)에 쌓은 뒤(개수 RVA 0x2A868A4) 한꺼번에 그림.
// 자막 함수는 화면 UI 를 다 쌓은 뒤에 불리므로, 자막 함수가 쌓기 시작한 위치 직전에 우리 창을 그리면
// 패널 위·자막 아래가 됨.
constexpr DWORD RVA_ElemArray = 0x1202520, RVA_ElemCount = 0x2A868A4, kElemSize = 0x358;
int g_subStart = -1;           // 이번 프레임 자막 요소의 시작 번호 (-1 = 자막 없음)
bool g_afterSub = false;       // 이번 프레임에 자막 함수가 이미 불렸는지 (그 뒤로 그리는 것은 자막)
bool g_underSub = false;       // 이번 프레임에 우리 창을 자막(과 게임의 어두운 막) 아래에 그렸는지
bool g_underSubLast = false;   // 지난 프레임 값
// 버튼처럼 게임 패널 위에 얹히는 것은 "게임 요소를 다 그린 뒤, 자막 글자 직전"에 따로 그림.
// 그 위치(이번 프레임의 몇 번째 그리기인지)는 지난 프레임의 그리기 순서로 정함.
int g_seq = 0;
// 진단: 자막이 떠 있는 동안 1초에 한 번 그리기 순서를 로그에 남김
struct SubDiag { int nonSub[16] = {}, sub[16] = {}; int earlySeq = -1, earlyLayer = -1, lateSeq = -1, lateLayer = -1; bool lateAtEnd = false; } g_diag;
DWORD g_diagLast = 0;
constexpr int kTextLayer = 7;   // 게임의 글자 그리기(0x276AF0)가 쌓는 층
void (*g_lateCb)() = nullptr;
void (*g_frameCb)() = nullptr; bool g_frameCbDone = false;
void __fastcall Hook_DrawElem(void* elem) {
    if (!g_frameCbDone && g_frameCb) { g_frameCbDone = true; g_frameCb(); }   // 이 프레임 첫 요소: 게임 화면 크기 확인
    int layer = elem ? *(int*)elem : 0;
    g_layerMask |= 1ull << (layer < 0 ? 0 : layer > 63 ? 63 : layer);

    bool below = layer >= kOverlayLayer;                 // 확인 창(12층)보다 아래
    // 게임은 화면 전체를 한 번 그린 뒤 자막 함수를 부르고, 자막(어두운 막·글자)을 두 번째로 따로 그림.
    // → 자막 함수 뒤 첫 요소 = 화면을 다 그린 직후, 자막 직전. 우리 창·버튼을 모두 여기서 그림.
    bool isSub = g_afterSub && g_subAlpha > 0;
    if (isSub && !below) {
        below = true;
        static bool logged = false;
        if (!logged) { logged = true; Log("자막 직전(화면을 다 그린 뒤)에 오버레이 그림"); }
    }
    int seq = g_seq++;
    bool lateHere = false;
    if (g_subAlpha > 0 && layer >= 0 && layer < 16) (isSub ? g_diag.sub : g_diag.nonSub)[layer]++;
    if (!g_layerDrawn && below && g_modalCb) {
        g_underSub = layer < kOverlayLayer;
        if (g_subAlpha > 0) { g_diag.earlySeq = seq; g_diag.earlyLayer = layer; }
        g_layerDrawn = true; g_inTopLayer = true; g_modalCb(); g_inTopLayer = false;
    }
    if (lateHere && g_lateCb && g_diag.lateSeq < 0) { g_diag.lateSeq = seq; g_diag.lateLayer = layer; }
    if (lateHere && g_lateCb) g_lateCb();      // 이미 그렸으면 아무것도 안 함
    ++g_drawDepth;
    o_DrawElem(elem);
    --g_drawDepth;
}

// 지연 그리기 목록 전체 그리기 — 끝난 직후(게임 요소를 다 그린 뒤)에 떼어 둔 버튼을 그림.
// 자막이 떠 있고, 위에서 자막 글자 직전 자리를 못 찾았을 때만.
FnSelfOnly o_RenderAll = nullptr;
void __fastcall Hook_RenderAll(void* self) {
    o_RenderAll(self);
    if (g_lateCb && g_subAlpha > 0) { if (g_diag.lateSeq < 0) g_diag.lateAtEnd = true; g_lateCb(); }
}

// 자막(내레이션) 그리기: param[0] 상황 번호, param[0x83] 투명도
FnSelfOnly o_Subtitle = nullptr;
void __fastcall Hook_Subtitle(void* self) {
    int* p = (int*)self;
    g_subAlpha = (p && *p != 6 && p[0x83] != 0) ? 1.0f : 0.0f;
    static bool logged = false;
    if (g_subAlpha > 0 && !logged) { logged = true; Log("자막 감지 (상황 %d)", *p); }
    ++g_subSeen;
    g_subStart = g_subAlpha > 0 ? *(int*)(g_base + RVA_ElemCount) : -1;
    if (g_subAlpha > 0) g_afterSub = true;
    o_Subtitle(self);
}

void __fastcall Hook_QuestSelPre(void* self) { g_questSelect = self; ++g_qsSeen; o_QuestSelPre(self); }

void __fastcall Hook_HeroDrag(void* self) {
    BYTE* data = *(BYTE**)((BYTE*)self + 8);
    if (data && *(void**)data == (void*)(g_base + RVA_VT_HeroDragData)) {
        BYTE* hero = *(BYTE**)(data + 8);
        if (hero) {
            g_hdragPending.active = true;
            g_hdragPending.guid = *(int*)(hero + 0x130C);
            ++g_hdragSeen;
            if (!g_logHDrag) { g_logHDrag = true; Log("영웅 드래그 감지: guid=%d", g_hdragPending.guid); }
        }
    }
    o_HeroDrag(self);
}

// 세이브 슬롯 감지: 게임이 profile_N\persist.roster.json 을 열면 그 영지를 플레이 중
volatile LONG g_profile = -1;
// 세이브 파일은 Steam 클라우드(ISteamRemoteStorage)로 읽고 쓰므로 그쪽 함수도 후킹
void NoteSteamFile(const char* name) {
    if (!name || !strstr(name, "persist.roster.json")) return;
    const char* p = nullptr;
    for (const char* q = strstr(name, "profile_"); q; q = strstr(q + 1, "profile_"))
        if (q == name || q[-1] == '\\' || q[-1] == '/') p = q;
    if (p && p[8] >= '0' && p[8] <= '9') {
        LONG n = atoi(p + 8);
        if (InterlockedExchange(&g_profile, n) != n) Log("세이브 슬롯 감지: profile_%ld (Steam 클라우드)", n);
    }
}
// 영지 삭제 감지: 게임이 profile_N 의 persist.game.json 을 지우면 그 슬롯의 조합/편성도 비움 (프레임에서 처리)
volatile LONG g_deletedSlots = 0;   // 비트마스크 (슬롯 0~31)
template <class Ch> int SlotOfPath(const Ch* name, const Ch* key, const Ch* prof) {
    if (!name) return -1;
    auto find = [](const Ch* h, const Ch* n) -> const Ch* {
        for (; *h; ++h) { const Ch* a = h; const Ch* b = n; while (*a && *b && (*a | 0x20) == (*b | 0x20)) { ++a; ++b; } if (!*b) return h; }
        return nullptr; };
    if (!find(name, key)) return -1;
    const Ch* p = nullptr;
    for (const Ch* q = find(name, prof); q; q = find(q + 1, prof))
        if (q == name || q[-1] == '\\' || q[-1] == '/') p = q;
    if (!p || p[8] < '0' || p[8] > '9') return -1;
    int n = 0; for (const Ch* d = p + 8; *d >= '0' && *d <= '9'; ++d) n = n * 10 + (*d - '0');
    return n;
}
void NoteSlotDeleted(int n, const char* how) {
    if (n < 0 || n > 31) return;
    InterlockedOr(&g_deletedSlots, 1L << n);
    Log("영지 삭제 감지: profile_%d (%s) → 이 슬롯의 조합/편성을 비움", n, how);
}
using FnRSWrite = bool (__fastcall*)(void*, const char*, const void*, int);
using FnRSRead  = int (__fastcall*)(void*, const char*, void*, int);
using FnRSReadAsync = unsigned long long (__fastcall*)(void*, const char*, unsigned, unsigned);
using FnRSSize  = int (__fastcall*)(void*, const char*);
using FnRSDelete = bool (__fastcall*)(void*, const char*);
FnRSWrite o_RSWrite = nullptr; FnRSRead o_RSRead = nullptr; FnRSReadAsync o_RSReadAsync = nullptr; FnRSSize o_RSSize = nullptr; FnRSDelete o_RSDelete = nullptr;
bool __fastcall Hook_RSWrite(void* t, const char* n, const void* d, int c) { NoteSteamFile(n); return o_RSWrite(t, n, d, c); }
int  __fastcall Hook_RSRead(void* t, const char* n, void* d, int c) { NoteSteamFile(n); return o_RSRead(t, n, d, c); }
unsigned long long __fastcall Hook_RSReadAsync(void* t, const char* n, unsigned o, unsigned c) { NoteSteamFile(n); return o_RSReadAsync(t, n, o, c); }
int  __fastcall Hook_RSSize(void* t, const char* n) { NoteSteamFile(n); return o_RSSize(t, n); }
bool __fastcall Hook_RSDelete(void* t, const char* n) {
    bool r = o_RSDelete(t, n);
    if (r) NoteSlotDeleted(SlotOfPath<char>(n, "persist.game.json", "profile_"), "Steam");
    return r;
}

bool HookSteamStorage() {
    HMODULE sa = GetModuleHandleW(L"steam_api64.dll");
    if (!sa) return false;
    using FnGet = void* (__cdecl*)();
    FnGet get = (FnGet)GetProcAddress(sa, "SteamAPI_SteamRemoteStorage_v016");
    void* rs = get ? get() : nullptr;
    if (!rs) return false;
    void** vt = *(void***)rs;      // FileWrite 0, FileRead 1, FileReadAsync 3, FileDelete 6, GetFileSize 15
    struct { int i; void* d; void** o; } h[] = {
        { 0, (void*)&Hook_RSWrite, (void**)&o_RSWrite }, { 1, (void*)&Hook_RSRead, (void**)&o_RSRead },
        { 3, (void*)&Hook_RSReadAsync, (void**)&o_RSReadAsync }, { 15, (void*)&Hook_RSSize, (void**)&o_RSSize },
        { 6, (void*)&Hook_RSDelete, (void**)&o_RSDelete } };
    int ok = 0;
    for (auto& e : h)
        if (MH_CreateHook(vt[e.i], e.d, e.o) == MH_OK && MH_EnableHook(vt[e.i]) == MH_OK) ++ok;
    return ok >= 4;
}

void NoteFileOpen(LPCWSTR name) {
    if (!name || !wcsstr(name, L"persist.roster.json")) return;
    const wchar_t* p = nullptr;
    for (const wchar_t* q = wcsstr(name, L"profile_"); q; q = wcsstr(q + 1, L"profile_"))
        if (q > name && (q[-1] == L'\\' || q[-1] == L'/')) p = q;      // steam_profile_ 같은 것은 제외
    if (p && p[8] >= L'0' && p[8] <= L'9') {
        LONG n = _wtoi(p + 8);
        if (InterlockedExchange(&g_profile, n) != n) Log("세이브 슬롯 감지: profile_%ld", n);
    }
}
using FnCreateFileW = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using FnCreateFile2 = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, DWORD, void*);
FnCreateFileW o_CreateFileW = nullptr;
FnCreateFile2 o_CreateFile2 = nullptr;
HANDLE WINAPI Hook_CreateFileW(LPCWSTR name, DWORD a, DWORD sm, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fl, HANDLE t) {
    NoteFileOpen(name);
    return o_CreateFileW(name, a, sm, sa, cd, fl, t);
}
HANDLE WINAPI Hook_CreateFile2(LPCWSTR name, DWORD a, DWORD sm, DWORD cd, void* ex) {
    NoteFileOpen(name);
    return o_CreateFile2(name, a, sm, cd, ex);
}

using FnDeleteFileW = BOOL (WINAPI*)(LPCWSTR);
FnDeleteFileW o_DeleteFileW = nullptr;
BOOL WINAPI Hook_DeleteFileW(LPCWSTR name) {
    BOOL r = o_DeleteFileW(name);
    if (r) NoteSlotDeleted(SlotOfPath<wchar_t>(name, L"persist.game.json", L"profile_"), "local");
    return r;
}

bool HookOne(DWORD rva, const unsigned char* sig, size_t n, void* detour, void** orig, const char* name) {
    BYTE* t = g_base + rva;
    if (memcmp(t, sig, n) != 0) { Log("  %s: 함수 모양이 다름 - 건너뜀", name); return false; }
    if (MH_CreateHook(t, detour, orig) != MH_OK || MH_EnableHook(t) != MH_OK) { Log("  %s: 후킹 실패", name); return false; }
    Log("  %s: OK", name);
    return true;
}

// ---- 게임 데이터 ----
struct Inv { BYTE* base; };   // Inventory::System
BYTE* InvBegin(BYTE* inv) { return *(BYTE**)(inv + 8); }
BYTE* InvEnd(BYTE* inv)   { return *(BYTE**)(inv + 0x10); }
size_t InvSize(BYTE* inv) {
    BYTE* b = InvBegin(inv); BYTE* e = InvEnd(inv);
    if (!b || e < b || (size_t)(e - b) % kItemSize != 0) return 0;
    size_t n = (size_t)(e - b) / kItemSize;
    return n > 200000 ? 0 : n;      // 가방은 빈 칸이 많은 큰 배열일 수 있음 (예전 5000 제한이 가방을 통째로 무시하던 버그)
}
BYTE* InvItem(BYTE* inv, size_t i) { return InvBegin(inv) + i * kItemSize; }
int      ItemQty(BYTE* it)  { return *(int*)it; }
uint32_t ItemType(BYTE* it) { return *(uint32_t*)(it + 0x44); }
uint32_t ItemHash(BYTE* it) { return *(uint32_t*)(it + 0x88); }
bool IsTrinket(BYTE* it) { return ItemQty(it) > 0 && ItemType(it) == g_trinketType; }

BYTE* Campaign() { return *At<BYTE**>(RVA_CampaignPtr); }
bool  AltMode()  { return *At<unsigned char*>(RVA_AltModeFlag) != 0; }
BYTE* EstateInv() { BYTE* c = Campaign(); return c ? c + (AltMode() ? 0xEF0 : 0xE88) : nullptr; }
BYTE* HeroInv(void* hero) { return (BYTE*)hero + 0x1210; }

bool LooksLikeHeroInv(BYTE* inv) {
    BYTE* b = *(BYTE**)(inv + 8); BYTE* e = *(BYTE**)(inv + 0x10);
    if (!b || e <= b || (size_t)(e - b) % kItemSize != 0) return false;
    size_t n = (size_t)(e - b) / kItemSize;
    return n >= 1 && n <= 8;
}
// 영웅 목록의 각 영웅 인벤토리. 게임 코드는 원소+0x1218 을 쓰지만, 원소가 Hero* 자체인 경우(+0x1210)도 있어 둘 다 확인
template <class F> void ForEachRosterInv(F f) {
    BYTE* c = Campaign(); if (!c) return;
    BYTE* r = c + (AltMode() ? 0x728 : 0);
    BYTE** b = *(BYTE***)(r + 0x20); BYTE** e = *(BYTE***)(r + 0x28);
    if (!b || e < b || e - b > 2000) return;
    for (BYTE** p = b; p != e; ++p) {
        BYTE* entry = *p;
        if (!entry) continue;
        if (LooksLikeHeroInv(entry + 0x1218)) f(entry + 0x1218);
        else if (LooksLikeHeroInv(entry + 0x1210)) f(entry + 0x1210);
    }
}

// 임시 Item 소멸 (SetItem 이 자신의 임시 Item 에 하는 순서 그대로)
void DestroyItem(BYTE* it) {
    At<FnVoidPtr>(RVA_Dtor_242400)(it + 0x580);
    At<FnVoidPtr>(RVA_Dtor_242400)(it + 0xE0);
    At<FnVoidPtr>(RVA_Dtor_232d70)(it + 0xB8);
    At<FnVoidPtr>(RVA_Dtor_242400)(it + 0xA0);
    At<FnVoidPtr2>(RVA_Dtor_3839b0)(it + 0x90, it + 0x90);
}

// 장신구 라이브러리 / 등급
std::unordered_map<uint32_t, TrinketInfo> g_info;
int g_rarityOffset = -2;   // -2 = 아직 계산 안 함, -1 = 실패

void CalibrateRarity() {
    g_rarityOffset = -1;
    BYTE* lb = *At<BYTE**>(RVA_TrinketLibBegin); BYTE* le = *At<BYTE**>(RVA_TrinketLibBegin + 8);
    BYTE* rb = *At<BYTE**>(RVA_RarityBegin);     BYTE* re = *At<BYTE**>(RVA_RarityBegin + 8);
    if (!lb || !rb || le <= lb || re <= rb) return;
    size_t nl = (le - lb) / kLibEntrySize, nr = (re - rb) / kRaritySize;
    std::unordered_map<uint32_t, int> rh;
    for (size_t i = 0; i < nr; ++i) rh[*(uint32_t*)(rb + i * kRaritySize + 0x40)] = (int)i;
    for (int off = 0x44; off + 4 <= (int)kLibEntrySize; off += 4) {
        size_t hit = 0;
        for (size_t i = 0; i < nl; ++i) if (rh.count(*(uint32_t*)(lb + i * kLibEntrySize + off))) ++hit;
        if (hit == nl) { g_rarityOffset = off; break; }
    }
    Log("장신구 라이브러리 %zu개, 등급 %zu개, 등급 위치 0x%X", nl, nr, g_rarityOffset);
}

// 직업 제한(vector<uint32_t>) 위치를 라이브러리에서 찾아냄
int g_classOffset = -2;
bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (const BYTE*)p + n <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}
std::unordered_map<uint32_t, int> ClassSet() {
    std::unordered_map<uint32_t, int> set;
    DWORD rva = AltMode() ? 0x02C2A798 : RVA_HeroClassBegin;
    BYTE** b = *At<BYTE***>(rva); BYTE** e = *At<BYTE***>(rva + 8);
    if (!b || e <= b || e - b > 500) return set;
    for (BYTE** p = b; p != e; ++p) if (*p) set[*(uint32_t*)(*p + 0xD4)] = 1;
    return set;
}
void CalibrateClasses() {
    g_classOffset = -1;
    auto classes = ClassSet();
    BYTE* lb = *At<BYTE**>(RVA_TrinketLibBegin); BYTE* le = *At<BYTE**>(RVA_TrinketLibBegin + 8);
    if (classes.empty() || !lb || le <= lb) { Log("직업 목록/라이브러리 없음 - 직업 제한 확인 끔"); return; }
    size_t n = (le - lb) / kLibEntrySize;
    for (int off = 0x44; off + 24 <= (int)kLibEntrySize; off += 8) {
        size_t nonEmpty = 0; bool ok = true;
        for (size_t i = 0; i < n && ok; ++i) {
            BYTE* e = lb + i * kLibEntrySize + off;
            uintptr_t vb = *(uintptr_t*)e, ve = *(uintptr_t*)(e + 8), vc = *(uintptr_t*)(e + 16);
            if (vb == 0 && ve == 0 && vc == 0) continue;
            if (!(vb && vb <= ve && ve <= vc && (ve - vb) % 4 == 0 && (vc - vb) % 4 == 0 && ve - vb <= 4 * 40 && vb % 4 == 0)) { ok = false; break; }
            if (ve == vb) continue;
            if (!Readable((void*)vb, ve - vb)) { ok = false; break; }
            for (uintptr_t q = vb; q < ve; q += 4) if (!classes.count(*(uint32_t*)q)) { ok = false; break; }
            ++nonEmpty;
        }
        if (ok && nonEmpty >= 3) { g_classOffset = off; Log("직업 제한 위치 0x%X (전용 장신구 %zu개, 직업 %zu개)", off, nonEmpty, classes.size()); return; }
    }
    Log("직업 제한 위치를 찾지 못함 - 직업 제한 확인 끔");
}

// ---- 장신구 정의 파일(JSON)에서 직업 전용 정보 읽기 ----
// base / DLC / 모드 / 창작마당의 trinkets\*.entries.trinkets.json 의 "hero_class_requirements"
struct TrinketDef { std::vector<std::string> classes; std::wstring root; };
std::unordered_map<uint32_t, TrinketDef> g_defs;
bool g_defsLoaded = false;

void ParseTrinketJson(const std::string& t, const std::wstring& root, int& count) {
    auto readStr = [&](size_t& i) -> std::string {          // i 는 따옴표 위치
        size_t e = t.find('"', i + 1);
        if (e == std::string::npos) { i = t.size(); return {}; }
        std::string v = t.substr(i + 1, e - i - 1); i = e + 1; return v;
    };
    size_t i = 0;
    while ((i = t.find("\"id\"", i)) != std::string::npos) {
        i += 4;
        size_t q = t.find('"', t.find(':', i));
        if (q == std::string::npos) break;
        std::string id = readStr(q);
        size_t next = t.find("\"id\"", q);
        TrinketDef def; def.root = root;
        size_t h = t.find("\"hero_class_requirements\"", q);
        if (h != std::string::npos && (next == std::string::npos || h < next)) {
            size_t a = t.find('[', h), z = t.find(']', h);
            for (size_t k = t.find('"', a); k != std::string::npos && k < z; k = t.find('"', k)) {
                std::string c = readStr(k);
                if (!c.empty()) def.classes.push_back(c);
            }
        }
        if (!id.empty()) { g_defs[HashImpl(id.c_str())] = def; ++count; }   // 뒤에 읽은 모드가 덮어씀
        i = q;
    }
}

void LoadTrinketDefs() {
    g_defsLoaded = true;
    std::vector<std::wstring> roots;
    roots.push_back(Gfx::GameRoot());
    for (auto& r : Gfx::ModRoots()) roots.push_back(r);
    int files = 0, count = 0;
    for (auto& root : roots) {
        WIN32_FIND_DATAW fd;
        std::wstring dir = root + L"\\trinkets\\";
        HANDLE h = FindFirstFileW((dir + L"*.entries.trinkets.json").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do { ParseTrinketJson(Gfx::ReadFile(dir + fd.cFileName), root, count); ++files; } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    size_t excl = 0; for (auto& kv : g_defs) if (!kv.second.classes.empty()) ++excl;
    Log("장신구 정의 파일 %d개, 장신구 %d개, 직업 전용 %zu개", files, count, excl);
}

std::string LocalizeImpl(const std::string& key);
std::string ClassLabel(const std::vector<std::string>& cls) {
    std::string out;
    for (auto& c : cls) {
        std::string n = LocalizeImpl("hero_class_name_" + c);
        if (!out.empty()) out += ", ";
        out += n.empty() ? c : n;
    }
    return out.empty() ? out : Txt::F("class_only", { out });
}

std::string LocalizeImpl(const std::string& key) {
    struct Out { const char* s; uint32_t meta; uint32_t pad; } out{};
    At<FnLocalize>(RVA_Localize)(&out, key.c_str());
    if (!out.s || !out.s[0]) return {};
    std::string r(out.s, strnlen(out.s, 512));
    if (r == key) return {};
    if (r.find("[<" + key + ">]") != std::string::npos) return {};   // 게임의 "번역 없음" 표시 (<c>a$~~B[<키>]</c>)
    return r;
}

// Steam 게임 언어 ("koreana", "english", ...) — 다키스트 던전은 이 언어를 씀
std::string SteamLanguageImpl() {
    static std::string lang; static bool done = false;
    if (done) return lang;
    HMODULE sa = GetModuleHandleW(L"steam_api64.dll");
    if (!sa) return lang;
    using FnApps = void* (__cdecl*)();
    using FnLang = const char* (__cdecl*)(void*);
    FnApps apps = (FnApps)GetProcAddress(sa, "SteamAPI_SteamApps_v008");
    FnLang gl = (FnLang)GetProcAddress(sa, "SteamAPI_ISteamApps_GetCurrentGameLanguage");
    void* a = apps ? apps() : nullptr;
    if (!a || !gl) return lang;
    const char* l = gl(a);
    done = true;
    if (l) lang = l;
    Log("Steam 게임 언어: %s", lang.c_str());
    return lang;
}

uint32_t HashImpl(const char* s) { uint32_t h = 0; while (*s) h = h * 0x35 + (unsigned char)*s++; return h; }

// 우리 해시 함수가 게임과 같은지 라이브러리로 확인 (다르면 자동 삭제/장착을 하지 않음)
bool HashVerified() {
    if (g_hashOk >= 0) return g_hashOk == 1;
    BYTE* lb = *At<BYTE**>(RVA_TrinketLibBegin); BYTE* le = *At<BYTE**>(RVA_TrinketLibBegin + 8);
    if (!lb || le <= lb) return false;            // 아직 로드 전 → 다음에 다시
    size_t n = (le - lb) / kLibEntrySize, good = 0, checked = 0;
    for (size_t i = 0; i < n && checked < 50; ++i, ++checked) {
        BYTE* e = lb + i * kLibEntrySize;
        char id[65] = {}; memcpy(id, e, 64);
        if (HashImpl(id) == *(uint32_t*)(e + 0x40)) ++good;
    }
    g_hashOk = (checked > 0 && good == checked) ? 1 : 0;
    Log("해시 확인: %zu/%zu 일치 → %s", good, checked, g_hashOk ? "OK" : u8"불일치 - 자동 정리/불러오기 끔");
    return g_hashOk == 1;
}

} // namespace

uint32_t Hash(const char* s) { uint32_t h = 0; while (*s) h = h * 0x35 + (unsigned char)*s++; return h; }

bool Supported() { return g_ok; }

bool Install() {
#if !defined(_WIN64)
    Log("32비트 게임에서는 게임 연동 기능이 꺼집니다.");
    return false;
#else
    g_base = (BYTE*)GetModuleHandleW(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kExpectedStamp) {
        Log("게임 버전이 다릅니다 (stamp 0x%08lX). 게임 연동 기능을 끕니다.", nt->FileHeader.TimeDateStamp);
        return false;
    }
    g_trinketType = Hash("trinket");
    if (MH_Initialize() != MH_OK) { Log("MinHook 초기화 실패"); return false; }
    Log("게임 연동 후킹:");
    bool a = HookOne(RVA_DrawUnequipButton, kSig_DrawUnequip, sizeof kSig_DrawUnequip, (void*)&Hook_DrawUnequip, (void**)&o_DrawUnequip, "장신구 가방");
    HookOne(RVA_RIDragRender, kSig_RIDragRender, sizeof kSig_RIDragRender, (void*)&Hook_RIDragRender, (void**)&o_RIDragRender, "가방 드래그");
    HookOne(RVA_PaletteButton, kSig_Palette, sizeof kSig_Palette, (void*)&Hook_Palette, (void**)&o_Palette, "영웅 정보창");
    HookOne(RVA_RosterPreShow, kSig_RosterPre, sizeof kSig_RosterPre, (void*)&Hook_RosterPre, (void**)&o_RosterPre, "마을 영웅 목록");
    HookOne(RVA_HeroSlotPreShow, kSig_HeroSlotPre, sizeof kSig_HeroSlotPre, (void*)&Hook_HeroSlotPre, (void**)&o_HeroSlotPre, "파티 슬롯");
    HookOne(RVA_QuestSelectPreShow, kSig_QuestSelPre, sizeof kSig_QuestSelPre, (void*)&Hook_QuestSelPre, (void**)&o_QuestSelPre, "영지 지도");
    HookOne(RVA_ProvisionPreShow, kSig_ProvPre, sizeof kSig_ProvPre, (void*)&Hook_ProvPre, (void**)&o_ProvPre, "원정 준비");
    {
        bool ok = memcmp(g_base + RVA_ProvisionBuyOne, kSig_ProvBuy, sizeof kSig_ProvBuy) == 0 &&
                  memcmp(g_base + RVA_ItemGoldPrice, kSig_Price, sizeof kSig_Price) == 0 &&
                  memcmp(g_base + RVA_ItemShardPrice, kSig_Price, sizeof kSig_Price) == 0;
        g_bulkOk = ok;
        Log("  자동 구매 함수: %s", ok ? "OK" : u8"모양이 다름 - 사용 안 함");
        static const unsigned char kSig_ShowChar[] = { 0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x10,0x49,0x89,0x73,0x18,0x57,0x48,0x81,0xEC,0x10 };
        g_showHeroOk = memcmp(g_base + RVA_RosterShowChar, kSig_ShowChar, sizeof kSig_ShowChar) == 0;
        Log("  영웅 정보창 열기: %s", g_showHeroOk ? "OK" : u8"모양이 다름 - 사용 안 함");
    }
    HookOne(RVA_TownRender, kSig_TownRender, sizeof kSig_TownRender, (void*)&Hook_TownRender, (void**)&o_TownRender, "마을 전환");
    HookOne(RVA_RenderAll, kSig_RenderAll, sizeof kSig_RenderAll, (void*)&Hook_RenderAll, (void**)&o_RenderAll, "그리기 목록");
    HookOne(RVA_DrawElement, kSig_DrawElem, sizeof kSig_DrawElem, (void*)&Hook_DrawElem, (void**)&o_DrawElem, "그리기 층");
    HookOne(RVA_ConfirmShow, kSig_Confirm, sizeof kSig_Confirm, (void*)&Hook_Confirm, (void**)&o_Confirm, "확인 창 2");
    HookOne(RVA_ModalRender, kSig_Modal, sizeof kSig_Modal, (void*)&Hook_Modal, (void**)&o_Modal, "확인 창");
    HookOne(RVA_SubtitleRender, kSig_Subtitle, sizeof kSig_Subtitle, (void*)&Hook_Subtitle, (void**)&o_Subtitle, "자막");
    HookOne(RVA_HeroDragRender, kSig_HeroDrag, sizeof kSig_HeroDrag, (void*)&Hook_HeroDrag, (void**)&o_HeroDrag, "영웅 드래그");
    // UCRT 의 fopen 은 kernel32 를 거치지 않으므로 KernelBase 의 함수를 후킹
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    if (!kb) kb = GetModuleHandleW(L"kernel32.dll");
    void* cf = (void*)GetProcAddress(kb, "CreateFileW");
    void* cf2 = (void*)GetProcAddress(kb, "CreateFile2");
    bool okW = cf && MH_CreateHook(cf, (void*)&Hook_CreateFileW, (void**)&o_CreateFileW) == MH_OK && MH_EnableHook(cf) == MH_OK;
    bool ok2 = cf2 && MH_CreateHook(cf2, (void*)&Hook_CreateFile2, (void**)&o_CreateFile2) == MH_OK && MH_EnableHook(cf2) == MH_OK;
    void* df = (void*)GetProcAddress(kb, "DeleteFileW");
    if (df && MH_CreateHook(df, (void*)&Hook_DeleteFileW, (void**)&o_DeleteFileW) == MH_OK) MH_EnableHook(df);
    bool okS = HookSteamStorage();
    Log("  세이브 슬롯 감지: CreateFileW %s, CreateFile2 %s, Steam 클라우드 %s", okW ? "OK" : u8"실패", ok2 ? "OK" : u8"실패", okS ? "OK" : u8"실패");
    g_ok = a;
    return g_ok;
#endif
}

int PopDeletedSlot() {
    LONG m = g_deletedSlots;
    if (!m) return -1;
    for (int i = 0; i < 32; ++i)
        if (m & (1L << i)) { InterlockedAnd(&g_deletedSlots, ~(1L << i)); return i; }
    return -1;
}

void UpdateProvisionBaseline();
void SetScreenMap(float k, float ox, float oy, int vw, int vh) {
    static int logs = 0;
    if ((k != g_mapK || ox != g_mapX || oy != g_mapY || vw != g_mapVW || vh != g_mapVH) && logs < 30 && ++logs)
        Log("화면 맞춤: 게임 화면 %dx%d → 배율 %.3f, 여백 (%.1f, %.1f)", vw, vh, k, ox, oy);
    g_mapK = k; g_mapX = ox; g_mapY = oy; g_mapVW = vw; g_mapVH = vh;
}

void FullScreenXform1080(float& k, float& ox, float& oy) {
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (g_mapVW <= 0 || g_mapVH <= 0) { k = ds.y / 1080.0f; ox = ds.x * 0.5f - 960 * k; oy = 0; return; }
    float s = g_mapVH / 1080.0f;                       // 게임 화면 안에서 1080 기준 배율
    k  = g_mapK * s;
    ox = g_mapX + (g_mapVW * 0.5f - 960 * s) * g_mapK; // 게임 화면 가운데 맞춤
    oy = g_mapY;
}

void BeginFrame() {
    if (g_bagSeen != g_bagLast)   { g_bag = g_bagPending; g_bagLast = g_bagSeen; }    else g_bag.visible = false;
    if (g_heroSeen != g_heroLast) { g_hero = g_heroPending; g_heroLast = g_heroSeen; } else g_hero.visible = false;
    if (g_dragSeen != g_dragLast) { g_drag = g_dragPending; g_dragLast = g_dragSeen; } else g_drag.active = false;
    if (g_hdragSeen != g_hdragLast) { g_hdrag = g_hdragPending; g_hdragLast = g_hdragSeen; } else g_hdrag.active = false;
    // 파티 슬롯이 그려지는 화면이면 어디든 (영지 지도, 원정 준비 다음 화면 등)
    bool anySlot = false;
    for (bool b : g_partyPending.slotSeen) anySlot |= b;
    g_qsRecent = (g_qsSeen != g_qsLast); g_qsLast = g_qsSeen;
    if (anySlot) {
        g_party = g_partyPending; g_party.visible = true;
        static bool logged = false;
        if (!g_qsRecent && !logged) { logged = true; Log("영지 지도 밖 화면에서 파티 슬롯 표시"); }
    } else g_party.visible = false;
    for (bool& b : g_partyPending.slotSeen) b = false;
    if (g_subSeen != g_subLast) { g_subLast = g_subSeen; } else g_subAlpha = 0;
    g_provVisible = (g_provSeen != g_provLast); g_provLast = g_provSeen;
    UpdateProvisionBaseline();
    if (g_townRSeen != g_townRLast) { g_townState = g_townStatePending; g_townRLast = g_townRSeen; } else g_townState = -1;
    if (g_modalSeen != g_modalLast) { g_modal = g_modalPending; g_modalLast = g_modalSeen; } else g_modal = 0;
    g_modalPending = 0;
    // 화면 전환(검은 화면) 따라가기. 어두워지기 시작하면 오버레이는 먼저 빠르게 사라짐
    {
        static ULONGLONG last = GetTickCount64();
        ULONGLONG now = GetTickCount64();
        float dt = std::min(0.1f, (now - last) / 1000.0f); last = now;
        float cur = *At<float*>(RVA_GlobalFade), tgt = *At<float*>(RVA_GlobalFade + 4);
        if (!(cur >= 0.0f && cur <= 1.0f) || !(tgt >= 0.0f && tgt <= 1.0f)) g_screenVis = 1.0f;
        else if (tgt < cur - 0.001f || cur <= 0.001f) g_screenVis = std::max(0.0f, std::min(cur, g_screenVis - dt / 0.15f));
        else g_screenVis = std::min(cur, g_screenVis + dt / 0.3f);
        // 마을 화면 전환: TownUI+0xC = 검은 화면 정도 (0 보임 ~ 1 검음). 어두워지기 시작하면 즉시 숨김
        BYTE* tui = *At<BYTE**>(RVA_TownUIPtr);
        if (tui && g_town && Readable(tui, 0x10)) {
            static float prevB = 0;
            float b = *(float*)(tui + 0xC);
            if (b >= 0.0f && b <= 1.0f) {
                if (b > prevB + 0.0001f || b >= 0.999f) g_townFadingOut = true;
                else if (b < prevB - 0.0001f || b <= 0.0001f) g_townFadingOut = false;
                prevB = b;
                float tv = g_townFadingOut ? 0.0f : 1.0f - b;
                g_screenVis = std::min(g_screenVis, tv);
                static int lastLog = -1;
                int st = g_townFadingOut ? 1 : b > 0.0001f ? 2 : 0;
                if (st != lastLog) { lastLog = st; Log("마을 화면 %s (%.2f)", st == 1 ? u8"어두워짐" : st == 2 ? u8"밝아지는 중" : u8"보임", b); }
            }
        }
        static int lastLogged = -1;
        int bucket = g_screenVis > 0.99f ? 2 : g_screenVis < 0.01f ? 0 : 1;
        if (bucket != lastLogged && bucket != 1) { lastLogged = bucket; Log("화면 밝기 %s (cur %.2f tgt %.2f)", bucket ? u8"보임" : u8"검음", cur, tgt); }
    }
    if (g_townSeen != g_townLast) { g_town = true; g_townMiss = 0; g_townLast = g_townSeen; }
    else if (++g_townMiss > 30) g_town = false;      // 몇 프레임 연속 안 보이면 마을이 아님
}

const BagState& Bag() { return g_bag; }
const HeroPanelState& HeroPanel() { return g_hero; }
const DragState& Drag() { return g_drag; }
bool InTown() { return g_town; }
int  CurrentProfile() { return (int)g_profile; }
const HeroDragState& HeroDrag() { return g_hdrag; }
const PartyState& Party() { return g_party; }
float SubtitleAlpha() { return g_subAlpha; }
bool  UnderSubtitle() { return g_underSub; }
float ScreenVisibility() { return g_screenVis; }
bool TownTransition() {
    // 3/9 = 전환 중. 2초 넘게 계속되면 전환이 아니라고 보고 무시 (버튼이 영영 안 보이는 일 방지)
    static ULONGLONG since = 0;
    bool t = g_townState == 3 || g_townState == 9;
    if (!t) { since = 0; return false; }
    ULONGLONG now = GetTickCount64();
    if (!since) since = now;
    return now - since < 2000;
}
bool QuestSelectVisible() { return g_qsRecent; }
bool TopLayerActive() { return g_inTopLayer; }
bool TownFadingOut() { return g_townFadingOut; }

std::string Localize(const std::string& key) { return g_ok ? LocalizeImpl(key) : std::string(); }
std::string SteamLanguage() { return SteamLanguageImpl(); }

// 이 모드가 현재 영지(세이브)의 모드 목록에서 켜져 있는지
bool ModEnabled() {
    if (!g_ok) return false;
    const std::wstring& tr = Gfx::ToolRoot();
    if (tr.empty()) return false;                              // 모드 폴더가 없음
    // ...\<모드 폴더 이름>\dd_preset_tools → 모드 폴더 이름 (mods 폴더 이름 또는 창작마당 번호)
    std::wstring parent = tr.substr(0, tr.find_last_of(L"\\/"));
    std::wstring wname = parent.substr(parent.find_last_of(L"\\/") + 1);
    char name[260] = {};
    WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, name, sizeof name, nullptr, nullptr);
    // 게임은 적용된 모드를 project.xml 의 <Title> 로 저장함 → 제목도 비교
    static std::wstring titleFor; static std::string title;
    if (titleFor != parent) {
        titleFor = parent; title.clear();
        FILE* f = _wfopen((parent + L"\\project.xml").c_str(), L"rb");
        if (f) {
            std::string xml; char buf[4096]; size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0 && xml.size() < 65536) xml.append(buf, n);
            fclose(f);
            size_t s = xml.find("<Title>"), t = xml.find("</Title>");
            if (s != std::string::npos && t != std::string::npos && t > s + 7) {
                title = xml.substr(s + 7, t - s - 7);
                while (!title.empty() && isspace((unsigned char)title.back())) title.pop_back();
                while (!title.empty() && isspace((unsigned char)title.front())) title.erase(0, 1);
                // XML 엔티티 풀기 (&amp; → & 등)
                static const char* ents[][2] = { {"&amp;","&"}, {"&lt;","<"}, {"&gt;",">"}, {"&quot;","\""}, {"&apos;","'"} };
                std::string out;
                for (size_t i = 0; i < title.size();) {
                    bool hit = false;
                    for (auto& en : ents) {
                        size_t L = strlen(en[0]);
                        if (title.compare(i, L, en[0]) == 0) { out += en[1]; i += L; hit = true; break; }
                    }
                    if (!hit) out += title[i++];
                }
                title = out;
            }
        }
        Log("모드 제목: '%s'", title.c_str());
    }
    BYTE* gp = g_base + RVA_GamePersist;
    BYTE* b = *(BYTE**)(gp + 0x208); BYTE* e = *(BYTE**)(gp + 0x210);
    bool on = false;
    std::string list;
    if (b && e >= b && (e - b) / 0xC0 <= 1000) {
        for (BYTE* p = b; p < e; p += 0xC0) {
            const char* n = (const char*)p;
            if (_stricmp(n, name) == 0 || (!title.empty() && strncmp(n, title.c_str(), 0x7F) == 0)) on = true;
            if (list.size() < 600) { list += n; list += "("; list += (const char*)(p + 0x80); list += ") "; }
        }
    }
    static std::string lastList = "?"; static bool lastOn = false;
    if (list != lastList || on != lastOn) {
        lastList = list; lastOn = on;
        Log("모드 '%s' %s / 적용된 모드: %s", name, on ? u8"켜짐" : u8"꺼짐", list.empty() ? u8"(없음)" : list.c_str());
    }
    return on;
}
bool ProvisionVisible() { return g_provVisible; }

// ---- 원정 준비: 골드 아이템 일괄 구매 ----
int* WalletAmount(BYTE* c, int hash) {
    BYTE* b = *(BYTE**)(c + 0xE70); BYTE* e = *(BYTE**)(c + 0xE78);
    if (!b || e <= b || (e - b) / 0x48 > 64) return nullptr;
    for (BYTE* p = b; p < e; p += 0x48) if (*(int*)(p + 0x44) == hash) return (int*)p;
    return nullptr;
}
int Gold() {
    if (!g_ok) return -1;
    BYTE* c = Campaign(); if (!c) return -1;
    int* g = WalletAmount(c, *At<int*>(RVA_GoldHash));
    return g ? *g : -1;
}
BYTE* FindItemClass(int typeHash, int idHash) {
    BYTE** vec = *At<BYTE***>(RVA_ItemClassList);
    if (!vec) return nullptr;
    BYTE** b = (BYTE**)vec[0]; BYTE** e = (BYTE**)vec[1];
    if (!b || e < b || e - b > 100000) return nullptr;
    for (BYTE** p = b; p != e; ++p)
        if (*p && *(int*)(*p + 0x40) == typeHash && *(int*)(*p + 0x84) == idHash) return *p;
    return nullptr;
}

// 자동 구매 순서 (상점에 있는 것만, -1 = 전부). 모드 폴더의 dd_preset_tools\autobuy.txt 로 바꿀 수 있음
struct BuyTarget { std::string type, id, label; int count; };
struct ItemAlias { const char* name; const char* type; const char* id; };
const ItemAlias kAliases[] = {
    { u8"식량", "provision", "" },        { u8"횃불", "supply", "torch" },     { u8"삽", "supply", "shovel" },
    { u8"열쇠", "supply", "skeleton_key" }, { u8"해독제", "supply", "antivenom" }, { u8"붕대", "supply", "bandage" },
    { u8"약초", "supply", "medicinal_herbs" }, { u8"성수", "supply", "holy_water" }, { u8"아편", "supply", "laudanum" },
    { u8"장작", "supply", "firewood" },    { u8"개 간식", "supply", "dog_treats" },
};
// 영어 이름 (설정 파일용) → 같은 아이템
struct EnAlias { const char* en; const char* id; };
const EnAlias kEnAliases[] = {
    { "food", "" }, { "provisions", "" }, { "torch", "torch" }, { "torches", "torch" }, { "shovel", "shovel" }, { "shovels", "shovel" },
    { "key", "skeleton_key" }, { "keys", "skeleton_key" }, { "skeleton key", "skeleton_key" }, { "antivenom", "antivenom" },
    { "bandage", "bandage" }, { "bandages", "bandage" }, { "herbs", "medicinal_herbs" }, { "medicinal herbs", "medicinal_herbs" },
    { "holy water", "holy_water" }, { "laudanum", "laudanum" }, { "firewood", "firewood" }, { "dog treats", "dog_treats" },
};
std::vector<BuyTarget> DefaultPlan() {
    return { { "provision", "", u8"식량", -1 }, { "supply", "torch", u8"횃불", -1 }, { "supply", "shovel", u8"삽", 4 },
             { "supply", "skeleton_key", u8"열쇠", 6 }, { "supply", "antivenom", u8"해독제", 6 }, { "supply", "bandage", u8"붕대", 6 },
             { "supply", "medicinal_herbs", u8"약초", 6 }, { "supply", "holy_water", u8"성수", 3 }, { "supply", "laudanum", u8"아편", 3 } };
}
// 한 줄: "<이름> <개수>"  이름 = 한국어 이름 / 영어 ID / 종류:ID,  개수 = 숫자 또는 전부/all
std::vector<BuyTarget> LoadBuyPlan() {
    const std::wstring& tr = Gfx::ToolRoot();
    if (tr.empty()) return DefaultPlan();
    std::string txt = Gfx::ReadFile(tr + L"\\autobuy.txt");
    if (txt.empty()) return DefaultPlan();
    if (txt.size() >= 3 && (unsigned char)txt[0] == 0xEF) txt = txt.substr(3);   // UTF-8 BOM
    std::vector<BuyTarget> plan;
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t e = txt.find('\n', pos); if (e == std::string::npos) e = txt.size();
        std::string line = txt.substr(pos, e - pos); pos = e + 1;
        size_t h = line.find('#'); if (h != std::string::npos) line.resize(h);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t s0 = line.find_first_not_of(" \t"); if (s0 == std::string::npos) continue;
        line = line.substr(s0);
        size_t sp = line.find_last_of(" \t"); if (sp == std::string::npos) continue;
        std::string name = line.substr(0, sp), cnt = line.substr(sp + 1);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        std::string lc = cnt; for (auto& ch : lc) ch = (char)tolower((unsigned char)ch);
        int count = (cnt == u8"\uC804\uBD80" || lc == "all") ? -1 : atoi(cnt.c_str());
        if (count == 0) continue;
        BuyTarget t; t.count = count; t.label = name;
        bool found = false;
        std::string lname = name; for (auto& ch : lname) ch = (char)tolower((unsigned char)ch);
        for (auto& e : kEnAliases) if (lname == e.en) { name = e.id[0] ? e.id : "provision"; break; }
        for (auto& a : kAliases) if (name == a.name || name == a.id || (name == "provision" && !a.id[0] && std::string(a.type) == "provision")) {
            t.type = a.type; t.id = a.id; t.label = a.name; found = true; break; }
        if (!found) {
            size_t c = name.find(':');
            if (c != std::string::npos) { t.type = name.substr(0, c); t.id = name.substr(c + 1); }
            else { t.type = "supply"; t.id = name; }
        }
        plan.push_back(t);
    }
    if (plan.empty()) return DefaultPlan();
    return plan;
}

// 소지품 수량 (종류해시, ID해시) → 개수
std::unordered_map<unsigned long long, int> BagCounts() {
    std::unordered_map<unsigned long long, int> m;
    BYTE* c = Campaign(); if (!c) return m;
    BYTE* bag = c + 0x15B0;
    BYTE* b = *(BYTE**)(bag + 8); BYTE* e = *(BYTE**)(bag + 0x10);
    if (!b || e <= b || (e - b) / 0x5A8 > 200) return m;
    for (BYTE* p = b; p < e; p += 0x5A8) {
        int q = *(int*)p; if (q <= 0) continue;
        unsigned long long k = ((unsigned long long)*(uint32_t*)(p + 0x44) << 32) | *(uint32_t*)(p + 0x88);
        m[k] += q;
    }
    return m;
}
std::unordered_map<unsigned long long, int> g_bagBaseline;
bool g_bagBaselineSet = false, g_provWasVisible = false;

void UpdateProvisionBaseline() {
    // 원정 준비 화면이 열린 순간의 소지품 = 기본 지급품 (예: 성기사 성수). 이보다 늘어나면 '이미 담음'
    if (g_provVisible && !g_provWasVisible) { g_bagBaseline = BagCounts(); g_bagBaselineSet = true; }
    if (!g_provVisible) g_bagBaselineSet = false;
    g_provWasVisible = g_provVisible;
}

bool AutoBuyAvailable() {
    if (!g_ok || !g_bulkOk || !g_provVisible || !g_bagBaselineSet) return false;
    auto now = BagCounts();
    for (auto& kv : now) {
        auto it = g_bagBaseline.find(kv.first);
        if (kv.second > (it == g_bagBaseline.end() ? 0 : it->second)) return false;
    }
    return true;
}

BulkBuyResult BulkBuyProvisions() {
    BulkBuyResult r;
    if (!g_ok || !g_bulkOk) { r.error = Txt::T("err_version"); return r; }
    if (!g_provVisible || !g_provSelect) { r.error = Txt::T("err_provision_only"); return r; }
    BYTE* c = Campaign(); if (!c) { r.error = Txt::T("err_data"); return r; }
    BYTE* store = c + 0x1618; BYTE* bag = c + 0x15B0;
    int goldHash = *At<int*>(RVA_GoldHash);
    using FnPrice = int (__fastcall*)(void*, void*);
    using FnBuy = void (__fastcall*)(void*, unsigned*);
    auto goldPrice = At<FnPrice>(RVA_ItemGoldPrice);
    auto shardPrice = At<FnPrice>(RVA_ItemShardPrice);
    auto buyOne = At<FnBuy>(RVA_ProvisionBuyOne);
    void* lam[4] = { nullptr, g_provSelect, store, bag };     // 게임 람다와 같은 캡처 배치
    int mode = *At<int*>(RVA_InputMode);
    bool ordinal = (mode == 1) || (mode == 0 && *At<char*>(RVA_InputModeFlag) != 0);   // 패드 모드는 빈칸을 건너뛴 번호
    BYTE* b0 = *(BYTE**)(store + 8); BYTE* e0 = *(BYTE**)(store + 0x10);
    if (!b0 || e0 <= b0) { r.error = Txt::T("err_store_empty"); return r; }
    int slots = (int)((e0 - b0) / 0x5A8);
    if (slots > 200) { r.error = Txt::T("err_store"); return r; }
    r.ok = true;
    for (const BuyTarget& t : LoadBuyPlan()) {
        if (r.outOfGold || r.bagFull) break;
        uint32_t th = Hash(t.type.c_str());
        int want = t.count < 0 ? 1 << 30 : t.count;
        for (int i = 0; i < slots && want > 0 && !r.outOfGold && !r.bagFull; ++i) {
            BYTE* sb = *(BYTE**)(store + 8);
            BYTE* item = sb + (size_t)i * 0x5A8;
            int qty = *(int*)item;
            if (qty <= 0 || *(uint32_t*)(item + 0x44) != th) continue;
            if (strncmp((const char*)(item + 0x48), t.id.c_str(), 64) != 0) continue;
            BYTE* cls = FindItemClass(*(int*)(item + 0x44), *(int*)(item + 0x88));
            if (!cls || shardPrice(nullptr, cls) > 0) continue;      // 파편 아이템 제외
            int gp = goldPrice(nullptr, cls);
            while (qty > 0 && want > 0) {
                int* gold = WalletAmount(c, goldHash);
                if (!gold || *gold < gp) { r.outOfGold = true; break; }   // 골드가 모자란 시점에서 중단
                unsigned idx = (unsigned)i;
                if (ordinal) {
                    idx = 0;
                    for (int k = 0; k < i; ++k) if (*(int*)(sb + (size_t)k * 0x5A8) > 0) ++idx;
                }
                int before = *gold;
                buyOne(lam, &idx);
                sb = *(BYTE**)(store + 8); item = sb + (size_t)i * 0x5A8;
                int nq = *(int*)item;
                if (nq >= qty) { r.bagFull = true; break; }        // 소지품 가득 (게임이 알림을 띄움)
                r.bought += qty - nq; want -= qty - nq;
                gold = WalletAmount(c, goldHash);
                if (gold) r.spent += before - *gold;
                qty = nq;
            }
        }
    }
    Log("자동 구매: %d개, %d 골드%s%s", r.bought, r.spent, r.outOfGold ? u8" (골드 부족으로 중단)" : "", r.bagFull ? u8" (가방 가득)" : "");
    return r;
}

std::string BuyPlanText() {
    std::string s;
    for (auto& t : LoadBuyPlan()) {
        if (!s.empty()) s += ", ";
        std::string name = LocalizeImpl("str_inventory_title_" + t.type + t.id);   // 게임 언어의 아이템 이름
        if (name.empty()) name = t.label;
        s += name + " " + (t.count < 0 ? std::string(Txt::T("buy_all")) : Txt::F("buy_count", { std::to_string(t.count) }));
    }
    return s;
}

// 영웅 정보창 열기: 영웅 목록에서 그 영웅을 우클릭한 것과 같은 동작
bool ShowHeroInfo(int guid) {
    if (!g_ok || !g_rosterList || !g_town || !g_showHeroOk) return false;
    BYTE* rl = (BYTE*)g_rosterList;
    BYTE* b = *(BYTE**)(rl + 0xA0); BYTE* e = *(BYTE**)(rl + 0xA8);
    if (!b || e <= b || (e - b) / 0x40 > 2000) return false;
    for (BYTE* p = b; p < e; p += 0x40) {
        BYTE* elem = *(BYTE**)p;
        if (!elem || !Readable(elem, 0xA0) || *(BYTE**)(elem + 0x88) != rl) continue;
        int* entry = *(int**)(elem + 0x90);
        if (!entry || !Readable(entry, 4) || *entry != guid) continue;
        using FnShow = void (__fastcall*)(void*);
        void* cap = elem;
        At<FnShow>(RVA_RosterShowChar)(&cap);
        return true;
    }
    return false;
}
float ModalAmount() { return g_modal; }
void SetModalRenderCallback(void (*cb)()) { g_modalCb = cb; }
void SetLateRenderCallback(void (*cb)()) { g_lateCb = cb; }
void SetFrameStartCallback(void (*cb)()) { g_frameCb = cb; }
void NewGameFrame() {
    g_layerDrawn = false; g_frameCbDone = false;
    if (g_subAlpha > 0 && GetTickCount() - g_diagLast > 1000) {
        g_diagLast = GetTickCount();
        char b[400]; int n = snprintf(b, sizeof b, "[자막 진단] 시작번호 %d, 층별 일반/자막:", g_subStart);
        for (int i = 0; i < 16; ++i) if (g_diag.nonSub[i] || g_diag.sub[i]) n += snprintf(b + n, sizeof b - n, " %d층 %d/%d", i, g_diag.nonSub[i], g_diag.sub[i]);
        n += snprintf(b + n, sizeof b - n, " | 창: %d번째(%d층) | 버튼: %s %d(%d층) | 자막아래 %d",
                      g_diag.earlySeq, g_diag.earlyLayer, g_diag.lateAtEnd ? "목록 끝" : "자막 글자 앞", g_diag.lateSeq, g_diag.lateLayer, (int)g_underSub);
        Log("%s", b);
    }
    g_diag = SubDiag{};
    g_afterSub = false;
    g_underSubLast = g_underSub;
    g_underSub = false;
    g_seq = 0;
    if (g_layerMask && g_layerMask != g_layerMaskLogged) {
        g_layerMaskLogged = g_layerMask;
        char buf[200] = {}; int n = 0;
        for (int i = 0; i < 64; ++i) if (g_layerMask & (1ull << i)) n += snprintf(buf + n, sizeof buf - n, "%d ", i);
        Log("그리기 층: %s", buf);
    }
    g_prevLayerMask = g_layerMask;
    g_layerMask = 0;
}
// 확인 창·양초(ESC) 메뉴 같은 맨 위 층(12 이상)이 지난 프레임에 그려졌는지
bool TopMenuOpen() { return (g_prevLayerMask >> kOverlayLayer) != 0; }

// ---- 영웅 ----
BYTE* RosterSys() { BYTE* c = Campaign(); return c ? c + (AltMode() ? 0x728 : 0) : nullptr; }
BYTE* FindEntry(int guid) {
    BYTE* r = RosterSys(); if (!r) return nullptr;
    BYTE** b = *(BYTE***)(r + 0x20); BYTE** e = *(BYTE***)(r + 0x28);
    if (!b || e < b || e - b > 2000) return nullptr;
    for (BYTE** p = b; p != e; ++p) if (*p && *(int*)*p == guid) return *p;
    return nullptr;
}

bool RosterReadable() {
    if (!g_ok) return false;
    BYTE* r = RosterSys(); if (!r) return false;
    BYTE** b = *(BYTE***)(r + 0x20); BYTE** e = *(BYTE***)(r + 0x28);
    return b && e > b && e - b <= 2000;
}

std::string ClassName(const std::string& classId) {
    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(classId);
    if (it != cache.end()) return it->second;
    std::string n = g_ok ? LocalizeImpl("hero_class_name_" + classId) : std::string();
    return cache[classId] = n.empty() ? classId : n;
}

bool GetHero(int guid, HeroInfo* out) {
    if (!g_ok || guid < 0) return false;
    BYTE* entry = FindEntry(guid);
    if (!entry) return false;
    BYTE* hero = entry + 8;
    HeroInfo h;
    h.guid = guid;
    h.status = *(int*)(entry + 0x1540);
    h.name.assign((const char*)(hero + 8), strnlen((const char*)(hero + 8), 64));
    BYTE* cls = *(BYTE**)(hero + 0x12B0);
    if (cls && Readable(cls, 0x600)) {
        h.classId.assign((const char*)(cls + 8), strnlen((const char*)(cls + 8), 64));
        // 초상화: 직업의 외형 폴더 목록(0x200 바이트 문자열) 중 영웅의 외형 번호(+0xB68)
        BYTE* vb = *(BYTE**)(cls + 0x5E0); BYTE* ve = *(BYTE**)(cls + 0x5E8);
        int n = (vb && ve > vb) ? (int)((ve - vb) / 0x200) : 0;
        if (n > 0 && n < 64) {
            int idx = *(int*)(hero + 0xB68);
            if (idx < 0 || idx >= n) idx = 0;
            const char* folder = (const char*)(vb + idx * 0x200);
            h.portrait = std::string(folder, strnlen(folder, 0x200)) + h.classId + "_portrait_roster.png";
        } else {
            h.portrait = "heroes/" + h.classId + "/" + h.classId + "_portrait_roster.png";
        }
    }
    *out = h;
    return true;
}

PartyResult LoadParty(const int guids[4]) {
    PartyResult r;
    if (!g_ok)            { r.error = Txt::T("err_off"); return r; }
    if (!g_party.visible) { r.error = Txt::T("err_party_screen"); return r; }
    BYTE* roster = RosterSys();
    BYTE* ep = EmbarkParty();
    if (!roster || !ep) { r.error = Txt::T("err_data"); return r; }
    // 슬롯 인터페이스 4개 (파티 배열 인덱스 순)
    BYTE* iface[4] = {};
    BYTE** b = *(BYTE***)(ep + 0x88); BYTE** e = *(BYTE***)(ep + 0x90);
    if (!b || e <= b || e - b > 8) { r.error = Txt::T("err_party_slots"); return r; }
    for (BYTE** p = b; p != e; ++p) {
        BYTE* f = *p ? *(BYTE**)(*p + 0x158) : nullptr;
        if (!f || *(void**)f != (void*)(g_base + RVA_VT_QSlotIface)) continue;
        unsigned idx = *(unsigned*)(f + 0x230);
        if (idx < 4) iface[idx] = f;
    }
    for (int i = 0; i < 4; ++i) if (!iface[i]) { r.error = Txt::T("err_party_slots"); return r; }
    // 먼저 모두 확인 (중간에 실패해서 파티가 반쯤 바뀌지 않도록)
    BYTE* entries[4] = {};
    for (int i = 0; i < 4; ++i) {
        if (guids[i] < 0) continue;
        entries[i] = FindEntry(guids[i]);
        if (!entries[i]) { r.error = Txt::T("err_hero_missing"); return r; }
        int st = *(int*)(entries[i] + 0x1540);
        if (st != 0 && st != 1) { r.error = Txt::T("err_hero_unavail"); return r; }
    }
    using FnSetInParty = void (__fastcall*)(void* roster, void* entry, int inParty);
    using FnSetHero    = void (__fastcall*)(void* iface, void* hero);
    auto setInParty = At<FnSetInParty>(RVA_SetInParty);
    // 1) 현재 파티 비우기
    for (int i = 0; i < 4; ++i) {
        BYTE* hero = *(BYTE**)(iface[i] + 0x28);
        if (hero) setInParty(roster, hero - 8, 0);
        *(iface[i] + 0xC) = 0;
        (*(FnSetHero**)iface[i])[6](iface[i], nullptr);         // vtable +0x30
    }
    // 2) 프리셋 영웅 배치 (게임의 드롭 처리와 같은 순서)
    for (int i = 0; i < 4; ++i) {
        if (!entries[i]) continue;
        setInParty(roster, entries[i], 1);
        *(iface[i] + 0xC) = 1;
        (*(FnSetHero**)iface[i])[6](iface[i], entries[i] + 8);
    }
    // 3) 파티 이름/조합 갱신
    if (g_questSelect && g_qsRecent) At<FnVoidPtr>(RVA_RefreshParty)(g_questSelect);
    Log("영웅 프리셋 적용: %d %d %d %d", guids[0], guids[1], guids[2], guids[3]);
    r.ok = true;
    return r;
}

const TrinketInfo& Trinket(uint32_t hash) {
    auto it = g_info.find(hash);
    if (it != g_info.end()) return it->second;
    TrinketInfo info;
    if (g_ok) {
        if (g_rarityOffset == -2) CalibrateRarity();
        BYTE* lb = *At<BYTE**>(RVA_TrinketLibBegin); BYTE* le = *At<BYTE**>(RVA_TrinketLibBegin + 8);
        if (lb && le > lb) {
            for (BYTE* e = lb; e + kLibEntrySize <= le; e += kLibEntrySize) {
                if (*(uint32_t*)(e + 0x40) != hash) continue;
                info.valid = true;
                info.id.assign((const char*)e, strnlen((const char*)e, 0x40));
                if (g_rarityOffset > 0) {
                    uint32_t rh = *(uint32_t*)(e + g_rarityOffset);
                    BYTE* rb = *At<BYTE**>(RVA_RarityBegin); BYTE* re = *At<BYTE**>(RVA_RarityBegin + 8);
                    for (BYTE* r = rb; r && r + kRaritySize <= re; r += kRaritySize)
                        if (*(uint32_t*)(r + 0x40) == rh) { info.rarity.assign((const char*)r, strnlen((const char*)r, 0x40)); break; }
                }
                info.name = LocalizeImpl("str_inventory_title_trinket" + info.id);
                if (!g_defsLoaded) LoadTrinketDefs();
                auto d = g_defs.find(hash);
                if (d != g_defs.end()) {
                    for (auto& c : d->second.classes) info.classes.push_back(HashImpl(c.c_str()));
                    info.classLabel = ClassLabel(d->second.classes);
                    info.root = d->second.root;
                }
                break;
            }
        }
    }
    return g_info[hash] = info;
}

const TrinketInfo& TrinketById(const char* id) { return Trinket(Hash(id)); }

uint32_t HeroClassHash(void* hero) {
    if (!g_ok || !hero) return 0;
    BYTE* cls = *(BYTE**)((BYTE*)hero + 0x12B0);
    if (!cls || !Readable(cls + 0xD4, 4)) return 0;
    return *(uint32_t*)(cls + 0xD4);
}

bool CanUse(const TrinketInfo& t, uint32_t heroClass) {
    if (t.classes.empty() || heroClass == 0) return true;
    for (uint32_t c : t.classes) if (c == heroClass) return true;
    return false;
}

bool ClassesCompatible(const TrinketInfo& a, const TrinketInfo& b) {
    if (a.classes.empty() || b.classes.empty()) return true;
    for (uint32_t x : a.classes) for (uint32_t y : b.classes) if (x == y) return true;
    return false;
}

void PlaySound(const char* path) {
    if (!g_ok || !path) return;
    alignas(16) unsigned char cb[64] = {};      // 빈 std::function (콜백 없음)
    At<FnPlaySound>(RVA_PlaySound)(path, 0, cb);
}

std::unordered_map<uint32_t, int> OwnedCounts(bool* ok) {
    std::unordered_map<uint32_t, int> m;
    *ok = false;
    if (!g_ok || !HashVerified()) return m;
    BYTE* est = EstateInv();
    if (!est) return m;
    size_t estSlots = InvSize(est), estTrinkets = 0, heroes = 0, heroTrinkets = 0;
    for (size_t i = 0; i < estSlots; ++i) {
        BYTE* it = InvItem(est, i);
        if (IsTrinket(it)) { m[ItemHash(it)] += ItemQty(it); ++estTrinkets; }
    }
    ForEachRosterInv([&](BYTE* inv) {
        ++heroes;
        size_t n = InvSize(inv);
        for (size_t i = 0; i < n; ++i) { BYTE* it = InvItem(inv, i); if (IsTrinket(it)) { m[ItemHash(it)] += ItemQty(it); ++heroTrinkets; } }
    });
    int estCount = *(int*)(est + 0x24);   // 게임이 세는 가방의 빈칸 아닌 아이템 수
    static int logged = 0;
    if (logged++ < 3)
        Log("보유 확인: 가방 칸 %zu, 게임 집계 %d, 가방 장신구 %zu / 영웅 %zu명, 영웅 장신구 %zu", estSlots, estCount, estTrinkets, heroes, heroTrinkets);
    // 안전장치: 가방에서 장신구를 못 읽었거나 게임 집계와 크게 다르면 판단하지 않음
    *ok = estTrinkets > 0 && (estCount <= 0 || estTrinkets * 2 >= (size_t)estCount);
    return m;
}

int TrinketSlots() {
    static int cached = 2; static DWORD at = 0;
    DWORD now = GetTickCount();
    if (at && now - at < 1000) return cached;
    at = now;
    if (!g_ok || !Campaign()) return cached;
    size_t mx = 0;
    ForEachRosterInv([&](BYTE* inv) { mx = std::max(mx, InvSize(inv)); });
    int n = mx ? (int)std::min<size_t>(mx, 8) : cached;
    if (n != cached) Log("영웅 장신구 칸 수: %d", n);
    cached = std::max(1, n);
    return cached;
}

EquipResult EquipPreset(void* hero, const char* const* ids, int n) {
    EquipResult r;
    if (!g_ok || !hero) { r.error = Txt::T("err_off"); return r; }
    if (!InTown())      { r.error = Txt::T("town_only_load"); return r; }
    if (!HashVerified()) { r.error = Txt::T("err_format"); return r; }
    BYTE* est = EstateInv();
    BYTE* hinv = HeroInv(hero);
    size_t nh = InvSize(hinv), ne = est ? InvSize(est) : 0;
    if (!est || nh == 0 || nh > 8 || ne == 0) { r.error = Txt::T("err_data"); return r; }
    if (n > (int)nh) n = (int)nh;           // 영웅 칸보다 많은 것은 무시
    if (n > 8) n = 8;

    const char* want[8] = {};
    for (int j = 0; j < n; ++j) want[j] = ids[j];
    uint32_t hc = HeroClassHash(hero);
    for (int j = 0; j < n; ++j)
        if (want[j] && want[j][0] && !CanUse(TrinketById(want[j]), hc)) { r.error = Txt::T("err_class"); return r; }
    bool used[8] = {};
    bool done[8] = {};
    // 1) 이미 끼고 있는 것은 그대로
    for (int j = 0; j < n; ++j) {
        if (!want[j] || !want[j][0]) { done[j] = true; continue; }
        uint32_t h = Hash(want[j]);
        for (size_t s = 0; s < nh; ++s) {
            BYTE* it = InvItem(hinv, s);
            if (!used[s] && IsTrinket(it) && ItemHash(it) == h) { used[s] = true; done[j] = true; ++r.already; break; }
        }
    }
    // 2) 나머지는 가방에서 꺼내 끼움
    for (int j = 0; j < n; ++j) {
        if (done[j]) continue;
        size_t slot = (size_t)j < nh && !used[j] ? (size_t)j : nh;
        if (slot == nh) for (size_t s = 0; s < nh; ++s) if (!used[s]) { slot = s; break; }
        if (slot == nh) break;
        uint32_t h = Hash(want[j]);
        size_t e = ne;
        for (size_t i = 0; i < ne; ++i) { BYTE* it = InvItem(est, i); if (IsTrinket(it) && ItemHash(it) == h) { e = i; break; } }
        const TrinketInfo& ti = Trinket(h);
        if (e == ne) { r.missing.push_back(ti.name.empty() ? want[j] : ti.name); continue; }

        BYTE* src = InvItem(est, e);
        alignas(16) static BYTE tmp[kItemSize];
        memset(tmp, 0, sizeof tmp);
        At<FnItemCopy>(RVA_ItemCopy)(tmp, src);              // tmp = 가방의 장신구 복사본
        bool stacked = ItemQty(src) > 1;
        if (stacked) {
            // 여러 개 겹친 경우: 하나만 꺼내고 영웅의 원래 장신구는 가방 빈 칸으로
            size_t empty = ne;
            for (size_t i = 0; i < ne; ++i) if (ItemQty(InvItem(est, i)) <= 0) { empty = i; break; }
            *(int*)tmp = 1;
            At<FnSetItem>(RVA_InvSetItem)(hinv, (unsigned)slot, tmp);     // tmp ← 영웅의 원래 장신구
            *(int*)src -= 1;
            if (ItemQty(tmp) > 0) {
                if (empty < ne) At<FnSetItem>(RVA_InvSetItem)(est, (unsigned)empty, tmp);
                else Log("경고: 가방에 빈 칸이 없어 원래 장신구를 돌려놓지 못함");
            }
        } else {
            At<FnSetItem>(RVA_InvSetItem)(hinv, (unsigned)slot, tmp);     // 영웅 칸 ↔ tmp
            At<FnSetItem>(RVA_InvSetItem)(est, (unsigned)e, tmp);         // 가방 칸 ↔ (영웅의 원래 장신구)
        }
        DestroyItem(tmp);
        used[slot] = true;
        ++r.equipped;
    }
    if (r.equipped > 0) At<FnVoidPtr>(RVA_HeroRefresh)(hero);
    Log("프리셋 적용: 장착 %d, 이미 장착 %d, 없음 %zu", r.equipped, r.already, r.missing.size());
    return r;
}

} // namespace Game

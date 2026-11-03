// 게임 함수 후킹 + 게임 데이터 읽기/쓰기 — macOS 판 (베타)
// 대상: Darkest (macOS x86_64), LC_UUID 52A44181-0EB1-3886-9204-53CF2FB87CD1
// 주소는 nm 주소(0x100000000 기준). 실행 시 이미지 슬라이드를 더해서 사용
// 윈도우판(game.cpp)과 같은 기능. 자막/화면 전환/층 정보는 맥판에서 아직 쓰지 않음 (항상 보임 처리)
#include "windows.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include "hook.h"
#include "../gfx.h"
#include "../game.h"
#include "../text.h"
#include "../log.h"

namespace Game {
namespace {
bool Readable(const void* p, size_t n);
void ResetReadCache();

// ---- 주소 (nm) ----------------------------------------------------------------
const uint8_t kUUID[16] = { 0x52,0xA4,0x41,0x81,0x0E,0xB1,0x38,0x86,0x92,0x04,0x53,0xCF,0x2F,0xB8,0x7C,0xD1 };
// 후킹
constexpr uint64_t A_DrawUnequip   = 0x100b2ac70;  // TownUI::Panel::RealmInventory::DrawUnequipButton(vec2)
constexpr uint64_t A_RIDragRender  = 0x100e2cd20;  // RealmInventoryItemDragObject::Render() const
constexpr uint64_t A_Palette       = 0x100eb0650;  // UI::Panel::CharacterDisplay::ShowCharacterPaletteButton(vec2, Hero*)
constexpr uint64_t A_RosterPre     = 0x100b4a600;  // TownUI::Panel::RosterList::PreChildrenShowInternal
constexpr uint64_t A_HeroSlotPre   = 0x100a951d0;  // TownUI::Panel::HeroSlot::PreChildrenShowInternal
constexpr uint64_t A_QuestSelPre   = 0x100af6870;  // TownUI::Panel::QuestSelect::PreChildrenShowInternal
constexpr uint64_t A_ProvPre       = 0x100aeba60;  // TownUI::Panel::ProvisionSelect::PreChildrenShowInternal
constexpr uint64_t A_ModalRender   = 0x100d92270;  // ModalDialog::Render
constexpr uint64_t A_HeroDrag      = 0x100e1e5a0;  // HeroDragObject::Render
constexpr uint64_t A_OpenSave      = 0x100097b70;  // open_SaveData(StorageMount, c_string, unsigned, bool, bool, unsigned)
constexpr uint64_t A_RemoveSave    = 0x100096fe0;  // RemoveSaveFile(StorageMount, c_string)
// 호출
constexpr uint64_t A_TransformTop  = 0x1000598f0;  // Transform_Top() → mat4*
constexpr uint64_t A_GetSlotIface  = 0x100a98800;  // HeroSlot::GetHeroSlotInterface()
constexpr uint64_t A_ModalVisible  = 0x100d92250;  // ModalDialog::IsVisible() const
constexpr uint64_t A_ConfirmInst   = 0x100df25f0;  // ConfirmDialog::System::Instance()
constexpr uint64_t A_ConfirmQueue  = 0x100df6610;  // ConfirmDialog::System::GetIsEntriesInQueue()
constexpr uint64_t A_SetStatus     = 0x100809070;  // Roster::System::SetStatus(guid, type)
constexpr uint64_t A_RefreshParty  = 0x100af5010;  // QuestSelect::RefreshParty
constexpr uint64_t A_ItemCopy      = 0x1002a8dd0;  // Inventory::Item::Item(Item const&)
constexpr uint64_t A_ItemDtor      = 0x10029c9b0;  // Inventory::Item::~Item()
constexpr uint64_t A_SwitchItems   = 0x1008c8e00;  // Inventory::System::SwitchItems(unsigned, Item*) — 칸과 item 을 맞바꿈
constexpr uint64_t A_TrinketBuffs  = 0x10046fbf0;  // Actor::UpdateTrinketBuffs()
constexpr uint64_t A_Localize      = 0x1003cc360;  // Localization::LocalizeStringFromId(char const*)
constexpr uint64_t A_AudioEvent    = 0x100005510;  // Audio_Event(char const*, bool, std::function<void(char const*)>)
constexpr uint64_t A_UsingPad      = 0x10007cdf0;  // Input_IsUsingController()
constexpr uint64_t A_StoreInv      = 0x1007c78d0;  // Provision::System::GetStoreInventorySystem()
constexpr uint64_t A_RaidInv       = 0x1007c78b0;  // Provision::System::GetNextRaidInventorySystem()
constexpr uint64_t A_GoldValue     = 0x1007c85d0;  // Provision::System::GetPurchaseGoldValueOfItem(Item const&)
constexpr uint64_t A_ShardValue    = 0x1007c8740;  // Provision::System::GetPurchaseShardValueOfItem(Item const&)
constexpr uint64_t A_BuyLambda     = 0x100aee3e0;  // ProvisionSelect 상점 클릭 $_1 __func::operator()(unsigned&&) (+0x10 화면, +0x18 상점, +0x20 가방)
constexpr uint64_t A_EstateAmount  = 0x1007a19c0;  // Estate::System::GetAmount(unsigned) const
constexpr uint64_t A_CurrencyHash  = 0x10041eb20;  // Estate::CurrencyType::operator unsigned() const
// 데이터
constexpr uint64_t D_KGold         = 0x102f17928;  // Estate::CurrencyTypes::k_gold (상점 람다가 쓰는 것)
constexpr uint64_t D_Campaign      = 0x103016e70;  // Campaign::g_pCurrent
constexpr uint64_t D_App           = 0x102ffff20;  // _App (+0x54b 다른 저장 방식, +0x748+0x208 적용된 모드 목록)
constexpr uint64_t D_TrinketLib    = 0x102defbc8;  // Trinket::g_TrinketClasses vector (0x428)
constexpr uint64_t D_Rarities      = 0x102defc20;  // Trinket::g_Rarities vector (0x4C)
constexpr uint64_t D_HeroClasses   = 0x102dd08b8;  // g_hero_classes vector<HeroClass*>
constexpr uint64_t D_NetHeroClasses= 0x102dd08d0;  // g_networked_hero_classes (다른 저장 방식)
constexpr uint64_t VT_RIDragData   = 0x101343c70;  // RealmInventoryItemDragData vtable(+0x10)
constexpr uint64_t VT_HeroDragData = 0x101343b98;  // HeroDragData
constexpr uint64_t VT_QSlotIface   = 0x101345348;  // QuestSelectHeroSlotInterface

constexpr size_t kItemSize = 0x5B0;
constexpr size_t kLibEntrySize = 0x428;
constexpr size_t kRaritySize = 0x4C;
// 오프셋
constexpr size_t kHeroInv = 0x1200, kHeroClass = 0x1298, kHeroGuid = 0x12F4;
constexpr size_t kEntryHero = 0x10, kEntryStatus = 0x1520;
constexpr size_t kAltRoster = 0x710;
constexpr size_t kEstate = 0xE20, kEstateInv = 0x40, kEstateInvAlt = 0xA0, kProvision = 0x1550;

intptr_t g_slide = 0;
bool  g_ok = false;
uint32_t HashImpl(const char* s);
int   g_hashOk = -1;
uint32_t g_trinketType = 0;

template <class T> T At(uint64_t nm) { return (T)(uintptr_t)(nm + g_slide); }

struct Vec2 { float x, y; };
struct LocOut { const char* s; uint64_t meta; };
using FnPtrRet   = void* (*)(void*);
using FnVoid     = void  (*)(void*);
using FnBool     = bool  (*)(void*);
using FnStatic   = void* (*)();
using FnBoolS    = bool  (*)();
using FnCopy     = void  (*)(void* dst, const void* src);
using FnSwitch   = bool  (*)(void* inv, unsigned idx, void* item);
using FnSetStat  = void  (*)(void* roster, unsigned guid, int type);
using FnLocalize = LocOut (*)(const char* key);
using FnAudio    = void  (*)(const char* path, bool, void* fn);
using FnPrice    = int   (*)(void* prov, const void* item);
using FnBuy      = void  (*)(void* fn, unsigned* idx);
using FnAmount   = int   (*)(void* estate, unsigned hash);
using FnCurHash  = unsigned (*)(const void* ct);
using FnSetHero  = void  (*)(void* iface, const void* hero);

// ---- 후킹 원본 ----
using FnDrawUnequip = void (*)(void* self, Vec2 pos);
using FnSelfOnly    = void (*)(void* self);
using FnPalette     = void (*)(void* self, Vec2 pos, void* hero);
using FnOpenSave    = void* (*)(int mount, const char* path, unsigned a, int b, int c, unsigned d);
using FnRemoveSave  = uint64_t (*)(int mount, const char* path);
FnDrawUnequip o_DrawUnequip = nullptr;
FnSelfOnly o_RIDragRender = nullptr, o_RosterPre = nullptr, o_HeroSlotPre = nullptr, o_QuestSelPre = nullptr,
           o_ProvPre = nullptr, o_Modal = nullptr, o_HeroDrag = nullptr;
FnPalette  o_Palette = nullptr;
FnOpenSave o_OpenSave = nullptr;
FnRemoveSave o_RemoveSave = nullptr;

BagState g_bagPending, g_bag;            unsigned g_bagSeen = 0, g_bagLast = 0;
HeroPanelState g_heroPending, g_hero;    unsigned g_heroSeen = 0, g_heroLast = 0;
DragState g_dragPending, g_drag;         unsigned g_dragSeen = 0, g_dragLast = 0;
unsigned g_townSeen = 0, g_townLast = 0; bool g_town = false; int g_townMiss = 0;
HeroDragState g_hdragPending, g_hdrag;   unsigned g_hdragSeen = 0, g_hdragLast = 0;
PartyState g_partyPending, g_party;      unsigned g_qsSeen = 0, g_qsLast = 0;
void* g_slotIfacePending[4] = {}; void* g_slotIface[4] = {};
void* g_questSelect = nullptr;
bool g_bulkOk = false;
bool g_qsRecent = false;
bool g_logHDrag = false, g_logBag = false, g_logHero = false, g_logDrag = false;
void (*g_frameCb)() = nullptr; bool g_frameCbDone = false;

float g_mapK = 1.0f, g_mapX = 0.0f, g_mapY = 0.0f; int g_mapVW = 0, g_mapVH = 0;

// 이번 프레임 처음 불린 후킹에서: 게임 화면 크기 확인 (윈도우판의 '첫 그리기 요소'와 같은 역할)
inline void FrameStart() { if (!g_frameCbDone && g_frameCb) { g_frameCbDone = true; g_frameCb(); } }

Xform CurrentXform(float raw[16] = nullptr) {
    const float* mat = At<const float* (*)()>(A_TransformTop)();
    Xform x;
    if (!mat) return x;
    if (raw) memcpy(raw, mat, 16 * sizeof(float));
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

void Hook_DrawUnequip(void* self, Vec2 pos) {
    FrameStart();
    BagState s;
    s.visible = true;
    s.xf = CurrentXform(s.raw);
    s.unequipPos = ImVec2(pos.x, pos.y);
    s.spacing = ImVec2(42.0f, 0.0f);          // realm_inventory_layout.sort_button_spacing
    g_bagPending = s; ++g_bagSeen;
    if (!g_logBag) {
        g_logBag = true;
        ImVec2 o = s.ToScreen(ImVec2(0, 0));
        Log("장신구 가방 감지: unequipPos=(%.1f,%.1f) 원점=(%.1f,%.1f) 배율=%.3f", pos.x, pos.y, o.x, o.y, s.Scale());
    }
    o_DrawUnequip(self, pos);
}

void Hook_RIDragRender(void* self) {
    FrameStart();
    uint8_t* data = self ? *(uint8_t**)((uint8_t*)self + 8) : nullptr;
    if (data && Readable(data, 0x10 + kItemSize) && *(void**)data == At<void*>(VT_RIDragData)) {
        uint8_t* item = data + 8;                 // InventoryItemDragData::GetItem()
        DragState d;
        d.active = *(int*)item > 0;
        d.hash = *(uint32_t*)(item + 0x88);
        d.isTrinket = *(uint32_t*)(item + 0x44) == g_trinketType;
        g_dragPending = d; ++g_dragSeen;
        if (!g_logDrag) { g_logDrag = true; Log("가방 드래그 감지: hash=0x%08X trinket=%d", d.hash, d.isTrinket); }
    }
    o_RIDragRender(self);
}

void Hook_Palette(void* self, Vec2 pos, void* hero) {
    FrameStart();
    HeroPanelState h;
    h.visible = true;
    h.xf = CurrentXform();
    h.anchor = ImVec2(pos.x, pos.y);
    h.hero = hero;
    g_heroPending = h; ++g_heroSeen;
    if (!g_logHero) {
        g_logHero = true;
        ImVec2 s = h.xf.ToScreen(h.anchor);
        Log("영웅 정보창 감지: pos=(%.1f,%.1f) 화면=(%.1f,%.1f) 배율=%.3f hero=%p", pos.x, pos.y, s.x, s.y, h.xf.Scale(), hero);
    }
    o_Palette(self, pos, hero);
}

void Hook_RosterPre(void* self) { FrameStart(); ++g_townSeen; o_RosterPre(self); }

void Hook_HeroSlotPre(void* self) {
    FrameStart();
    uint8_t* iface = self ? (uint8_t*)At<FnPtrRet>(A_GetSlotIface)(self) : nullptr;
    if (iface && Readable(iface, 0x240) && *(void**)iface == At<void*>(VT_QSlotIface)) {
        unsigned idx = *(unsigned*)(iface + 0x230);
        if (idx < 4) {
            g_partyPending.slotSeen[idx] = true;
            g_partyPending.slotXf[idx] = CurrentXform();
            g_slotIfacePending[idx] = iface;
            static int logged = 0;
            if (!(logged & (1 << idx))) {
                logged |= 1 << idx;
                ImVec2 o = g_partyPending.slotXf[idx].ToScreen(ImVec2(0, 0));
                Log("파티 슬롯 %u 감지: 화면 (%.1f,%.1f) 배율 %.3f", idx, o.x, o.y, g_partyPending.slotXf[idx].Scale());
            }
        }
    }
    o_HeroSlotPre(self);
}

float g_modalPending = 0, g_modal = 0; unsigned g_modalSeen = 0, g_modalLast = 0;
void Hook_Modal(void* self) {
    if (self && At<FnBool>(A_ModalVisible)(self)) {
        g_modalPending = 1.0f; ++g_modalSeen;
        static bool logged = false;
        if (!logged) { logged = true; Log("확인 창 감지"); }
    }
    o_Modal(self);
}

void* g_provSelect = nullptr; unsigned g_provSeen = 0, g_provLast = 0; bool g_provVisible = false;
void Hook_ProvPre(void* self) { FrameStart(); g_provSelect = self; ++g_provSeen; o_ProvPre(self); }
void Hook_QuestSelPre(void* self) { FrameStart(); g_questSelect = self; ++g_qsSeen; o_QuestSelPre(self); }

void Hook_HeroDrag(void* self) {
    FrameStart();
    uint8_t* data = self ? *(uint8_t**)((uint8_t*)self + 8) : nullptr;
    if (data && Readable(data, 0x10) && *(void**)data == At<void*>(VT_HeroDragData)) {
        uint8_t* hero = *(uint8_t**)(data + 8);
        if (hero && Readable(hero + kHeroGuid, 4)) {
            g_hdragPending.active = true;
            g_hdragPending.guid = *(int*)(hero + kHeroGuid);
            ++g_hdragSeen;
            if (!g_logHDrag) { g_logHDrag = true; Log("영웅 드래그 감지: guid=%d", g_hdragPending.guid); }
        }
    }
    o_HeroDrag(self);
}

// 세이브 슬롯 감지: 게임이 profile_N/persist.roster.json 을 열면 그 영지를 플레이 중
std::atomic<long> g_profile{-1};
std::atomic<unsigned> g_deletedSlots{0};
int SlotOfPath(const char* name, const char* key) {
    if (!name || !strcasestr(name, key)) return -1;
    const char* p = nullptr;
    for (const char* q = strcasestr(name, "profile_"); q; q = strcasestr(q + 1, "profile_"))
        if (q == name || q[-1] == '\\' || q[-1] == '/') p = q;
    if (!p || p[8] < '0' || p[8] > '9') return -1;
    return atoi(p + 8);
}
void* Hook_OpenSave(int mount, const char* path, unsigned a, int b, int c, unsigned d) {
    int n = SlotOfPath(path, "persist.roster.json");
    if (n >= 0 && g_profile.exchange(n) != n) Log("세이브 슬롯 감지: profile_%d", n);
    return o_OpenSave(mount, path, a, b, c, d);
}
uint64_t Hook_RemoveSave(int mount, const char* path) {
    int n = SlotOfPath(path, "persist.game.json");
    uint64_t r = o_RemoveSave(mount, path);
    if (n >= 0 && n <= 31) { g_deletedSlots |= 1u << n; Log("영지 삭제 감지: profile_%d → 이 슬롯의 조합/편성을 비움", n); }
    return r;
}

// ---- 후킹 설치 (함수 시작 바이트 확인 포함) ----
int g_hookOk = 0, g_hookAll = 0;
bool HookOne(uint64_t nm, void* detour, void** orig, const char* name) {
    ++g_hookAll;
    if (!Hook::Install(At<void*>(nm), detour, orig)) { Log("  %s: 후킹 실패 (함수 모양이 다르거나 메모리 보호)", name); return false; }
    Log("  %s: OK", name);
    ++g_hookOk;
    return true;
}

// ---- 게임 데이터 ----
uint8_t* InvBegin(uint8_t* inv) { return *(uint8_t**)(inv + 8); }
uint8_t* InvEnd(uint8_t* inv)   { return *(uint8_t**)(inv + 0x10); }
size_t InvSize(uint8_t* inv) {
    if (!inv || !Readable(inv, 0x28)) return 0;
    uint8_t* b = InvBegin(inv); uint8_t* e = InvEnd(inv);
    if (!b || e < b || (size_t)(e - b) % kItemSize != 0) return 0;
    size_t n = (size_t)(e - b) / kItemSize;
    if (n > 200000 || (n && !Readable(b, n * kItemSize))) return 0;
    return n;
}
uint8_t* InvItem(uint8_t* inv, size_t i) { return InvBegin(inv) + i * kItemSize; }
int      ItemQty(uint8_t* it)  { return *(int*)it; }
uint32_t ItemType(uint8_t* it) { return *(uint32_t*)(it + 0x44); }
uint32_t ItemHash(uint8_t* it) { return *(uint32_t*)(it + 0x88); }
bool IsTrinket(uint8_t* it) { return ItemQty(it) > 0 && ItemType(it) == g_trinketType; }

uint8_t* Campaign() { return *At<uint8_t**>(D_Campaign); }
bool AltMode() { return *(At<uint8_t*>(D_App) + 0x54B) != 0; }
uint8_t* EstateSys() { uint8_t* c = Campaign(); return c ? c + kEstate : nullptr; }
uint8_t* EstateInv() { uint8_t* e = EstateSys(); return e ? e + (AltMode() ? kEstateInvAlt : kEstateInv) : nullptr; }
uint8_t* HeroInv(void* hero) { return (uint8_t*)hero + kHeroInv; }
uint8_t* RosterSys() { uint8_t* c = Campaign(); return c ? c + (AltMode() ? kAltRoster : 0) : nullptr; }

bool LooksLikeHeroInv(uint8_t* inv) {
    if (!Readable(inv, 0x18)) return false;
    uint8_t* b = *(uint8_t**)(inv + 8); uint8_t* e = *(uint8_t**)(inv + 0x10);
    if (!b || e <= b || (size_t)(e - b) % kItemSize != 0) return false;
    size_t n = (size_t)(e - b) / kItemSize;
    return n >= 1 && n <= 8;
}
template <class F> void ForEachRosterEntry(F f) {
    uint8_t* r = RosterSys(); if (!r || !Readable(r + 0x20, 0x10)) return;
    uint8_t** b = *(uint8_t***)(r + 0x20); uint8_t** e = *(uint8_t***)(r + 0x28);
    if (!b || e < b || e - b > 2000 || (e > b && !Readable(b, (size_t)(e - b) * sizeof(uint8_t*)))) return;
    for (uint8_t** p = b; p != e; ++p) if (*p && Readable(*p, kEntryStatus + 4)) f(*p);
}
template <class F> void ForEachRosterInv(F f) {
    ForEachRosterEntry([&](uint8_t* entry) {
        uint8_t* inv = entry + kEntryHero + kHeroInv;
        if (LooksLikeHeroInv(inv)) f(inv);
    });
}

void DestroyItem(uint8_t* it) { At<FnVoid>(A_ItemDtor)(it); }

std::unordered_map<uint32_t, TrinketInfo> g_info;
int g_rarityOffset = -2;
uint8_t** LibVec() { return At<uint8_t**>(D_TrinketLib); }

void CalibrateRarity() {
    g_rarityOffset = -1;
    uint8_t* lb = LibVec()[0]; uint8_t* le = LibVec()[1];
    uint8_t** rv = At<uint8_t**>(D_Rarities);
    uint8_t* rb = rv[0]; uint8_t* re = rv[1];
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

// 읽어도 되는 메모리인지 (mach_vm_region). 같은 프레임 안에서는 확인한 영역을 기억
struct ReadRegion { const uint8_t* b = nullptr; const uint8_t* e = nullptr; };
ReadRegion g_readCache[16]; int g_readNext = 0;
void ResetReadCache() { for (auto& r : g_readCache) r = ReadRegion{}; }
bool Readable(const void* p, size_t n) {
    const uint8_t* a = (const uint8_t*)p;
    if (!a || a + n < a || (uintptr_t)a < 0x10000) return false;
    for (auto& r : g_readCache) if (r.b && a >= r.b && a + n <= r.e) return true;
    const uint8_t* cur = a;
    while (cur < a + n) {
        mach_vm_address_t addr = (uintptr_t)cur; mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info; mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t obj;
        if (mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &cnt, &obj) != KERN_SUCCESS) return false;
        if (addr > (uintptr_t)cur || !(info.protection & VM_PROT_READ)) return false;
        g_readCache[g_readNext] = { (const uint8_t*)addr, (const uint8_t*)(addr + size) }; g_readNext = (g_readNext + 1) % 16;
        cur = (const uint8_t*)(addr + size);
    }
    return true;
}
std::unordered_map<uint32_t, int> ClassSet() {
    std::unordered_map<uint32_t, int> set;
    uint8_t*** v = At<uint8_t***>(AltMode() ? D_NetHeroClasses : D_HeroClasses);
    uint8_t** b = v[0]; uint8_t** e = v[1];
    if (!b || e <= b || e - b > 500) return set;
    for (uint8_t** p = b; p != e; ++p) if (*p && Readable(*p + 0xD4, 4)) set[*(uint32_t*)(*p + 0xD4)] = 1;
    return set;
}

// ---- 장신구 정의 파일(JSON) — 윈도우판과 같음 ----
struct TrinketDef { std::vector<std::string> classes; std::wstring root; };
std::unordered_map<uint32_t, TrinketDef> g_defs;
bool g_defsLoaded = false;
std::string g_defsKey;

std::string ReadModTitle(const std::wstring& dir) {
    std::string title;
    FILE* f = _wfopen((dir + L"\\project.xml").c_str(), L"rb");
    if (!f) return title;
    std::string xml; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && xml.size() < 65536) xml.append(buf, n);
    fclose(f);
    size_t s = xml.find("<Title>"), t = xml.find("</Title>");
    if (s == std::string::npos || t == std::string::npos || t <= s + 7) return title;
    title = xml.substr(s + 7, t - s - 7);
    while (!title.empty() && isspace((unsigned char)title.back())) title.pop_back();
    while (!title.empty() && isspace((unsigned char)title.front())) title.erase(0, 1);
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
    return out;
}
bool IsDlcRoot(const std::wstring& r) {
    std::wstring d = Gfx::GameRoot() + L"\\dlc";
    return r.size() > d.size() && _wcsnicmp(r.c_str(), d.c_str(), d.size()) == 0;
}
std::vector<std::wstring> g_activeRoots;
std::string g_activeKey;
void UpdateActiveRoots(const std::vector<std::string>& names, const std::string& key) {
    if (key == g_activeKey) return;
    struct R { std::wstring root; std::string folder, title; };
    static std::vector<R> idx; static bool built = false;
    if (!built) {
        built = true;
        for (auto& r : Gfx::ModRoots()) {
            if (IsDlcRoot(r)) continue;
            std::wstring w = r.substr(r.find_last_of(L"\\/") + 1);
            char u[520] = {}; WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u, sizeof u, nullptr, nullptr);
            idx.push_back({ r, u, ReadModTitle(r) });
        }
    }
    g_activeKey = key;
    g_activeRoots.clear();
    int miss = 0;
    for (auto& n : names) {
        const R* hit = nullptr;
        for (auto& r : idx) if (_stricmp(r.folder.c_str(), n.c_str()) == 0) { hit = &r; break; }
        if (!hit) for (auto& r : idx) if (!r.title.empty() && strncmp(r.title.c_str(), n.c_str(), 0x7F) == 0) { hit = &r; break; }
        if (hit) g_activeRoots.push_back(hit->root); else ++miss;
    }
    Gfx::SetPreferredRoots(g_activeRoots);
    Log("적용된 모드 폴더 %zu개 (찾지 못함 %d개)", g_activeRoots.size(), miss);
    ReportSet("mods", "%zu(miss%d)", names.size(), miss);
}
void ParseTrinketJson(const std::string& t, const std::wstring& root, int& count) {
    auto readStr = [&](size_t& i) -> std::string {
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
        if (!id.empty()) { g_defs[HashImpl(id.c_str())] = def; ++count; }
        i = q;
    }
}
void LoadTrinketDefs() {
    g_defsLoaded = true;
    g_defsKey = g_activeKey;
    std::vector<std::wstring> roots;
    roots.push_back(Gfx::GameRoot());
    for (auto& r : Gfx::ModRoots()) if (IsDlcRoot(r)) roots.push_back(r);
    for (auto& r : g_activeRoots) roots.push_back(r);
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

std::string LocalizeImpl(const std::string& key) {
    LocOut out = At<FnLocalize>(A_Localize)(key.c_str());
    if (!out.s || !out.s[0]) return {};
    std::string r(out.s, strnlen(out.s, 512));
    if (r == key) return {};
    if (r.find("[<" + key + ">]") != std::string::npos) return {};
    if (r.find(key) != std::string::npos && r.find('<') != std::string::npos) return {};   // "번역 없음" 표시
    return r;
}
std::string ClassLabel(const std::vector<std::string>& cls) {
    std::string out;
    for (auto& c : cls) {
        std::string n = LocalizeImpl("hero_class_name_" + c);
        if (!out.empty()) out += ", ";
        out += n.empty() ? c : n;
    }
    return out.empty() ? out : Txt::F("class_only", { out });
}

std::string SteamLanguageImpl() {
    static std::string lang; static bool done = false;
    if (done) return lang;
    using FnApps = void* (*)();
    using FnLang = const char* (*)(void*);
    FnApps apps = nullptr;
    for (const char* n : { "SteamAPI_SteamApps_v008", "SteamAPI_SteamApps_v009", "SteamAPI_SteamApps" })
        if ((apps = (FnApps)dlsym(RTLD_DEFAULT, n))) break;
    FnLang gl = (FnLang)dlsym(RTLD_DEFAULT, "SteamAPI_ISteamApps_GetCurrentGameLanguage");
    void* a = apps ? apps() : nullptr;
    if (!a || !gl) return lang;
    const char* l = gl(a);
    done = true;
    if (l) lang = l;
    Log("Steam 게임 언어: %s", lang.c_str());
    return lang;
}

uint32_t HashImpl(const char* s) { uint32_t h = 0; while (*s) h = h * 0x35 + (unsigned char)*s++; return h; }

bool HashVerified() {
    if (g_hashOk >= 0) return g_hashOk == 1;
    uint8_t* lb = LibVec()[0]; uint8_t* le = LibVec()[1];
    if (!lb || le <= lb) return false;
    size_t n = (le - lb) / kLibEntrySize, good = 0, checked = 0;
    for (size_t i = 0; i < n && checked < 50; ++i, ++checked) {
        uint8_t* e = lb + i * kLibEntrySize;
        char id[65] = {}; memcpy(id, e, 64);
        if (HashImpl(id) == *(uint32_t*)(e + 0x40)) ++good;
    }
    g_hashOk = (checked > 0 && good == checked) ? 1 : 0;
    Log("해시 확인: %zu/%zu 일치 → %s", good, checked, g_hashOk ? "OK" : u8"불일치 - 자동 정리/불러오기 끔");
    ReportSet("hash", "%zu/%zu", good, checked);
    return g_hashOk == 1;
}

// 실행 파일 확인: LC_UUID 가 우리가 분석한 빌드와 같은지
bool CheckBinary(char* uuidOut) {
    const mach_header_64* mh = (const mach_header_64*)_dyld_get_image_header(0);
    if (!mh || mh->magic != MH_MAGIC_64) return false;
    g_slide = _dyld_get_image_vmaddr_slide(0);
    const uint8_t* p = (const uint8_t*)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; ++i) {
        const load_command* lc = (const load_command*)p;
        if (lc->cmd == LC_UUID) {
            const uint8_t* u = ((const uuid_command*)lc)->uuid;
            for (int k = 0; k < 16; ++k) sprintf(uuidOut + k * 2, "%02X", u[k]);
            return memcmp(u, kUUID, 16) == 0;
        }
        p += lc->cmdsize;
    }
    return false;
}

void* g_lam[5];
} // namespace

uint32_t Hash(const char* s) { return HashImpl(s); }
bool Supported() { return g_ok; }

bool Install() {
    char uuid[40] = "?";
    if (!CheckBinary(uuid)) {
        Log("게임 버전이 다릅니다 (UUID %s). 게임 연동 기능을 끕니다.", uuid);
        ReportSet("game", "WRONG-VERSION-%.8s", uuid);
        return false;
    }
    Log("게임 실행 파일 확인 OK (슬라이드 0x%lX)", (long)g_slide);
    g_trinketType = Hash("trinket");
    Log("게임 연동 후킹:");
    bool a = HookOne(A_DrawUnequip, (void*)&Hook_DrawUnequip, (void**)&o_DrawUnequip, "장신구 가방");
    HookOne(A_RIDragRender, (void*)&Hook_RIDragRender, (void**)&o_RIDragRender, "가방 드래그");
    HookOne(A_Palette, (void*)&Hook_Palette, (void**)&o_Palette, "영웅 정보창");
    HookOne(A_RosterPre, (void*)&Hook_RosterPre, (void**)&o_RosterPre, "마을 영웅 목록");
    HookOne(A_HeroSlotPre, (void*)&Hook_HeroSlotPre, (void**)&o_HeroSlotPre, "파티 슬롯");
    HookOne(A_QuestSelPre, (void*)&Hook_QuestSelPre, (void**)&o_QuestSelPre, "영지 지도");
    HookOne(A_ProvPre, (void*)&Hook_ProvPre, (void**)&o_ProvPre, "원정 준비");
    HookOne(A_ModalRender, (void*)&Hook_Modal, (void**)&o_Modal, "확인 창");
    HookOne(A_HeroDrag, (void*)&Hook_HeroDrag, (void**)&o_HeroDrag, "영웅 드래그");
    bool s1 = HookOne(A_OpenSave, (void*)&Hook_OpenSave, (void**)&o_OpenSave, "세이브 슬롯 감지");
    HookOne(A_RemoveSave, (void*)&Hook_RemoveSave, (void**)&o_RemoveSave, "영지 삭제 감지");
    (void)s1;
    g_bulkOk = true;
    g_ok = a;
    ReportSet("game", g_ok ? "OK" : "BAG-HOOK-FAIL");
    ReportSet("hooks", "%d/%d", g_hookOk, g_hookAll);
    return g_ok;
}

int PopDeletedSlot() {
    unsigned m = g_deletedSlots.load();
    if (!m) return -1;
    for (int i = 0; i < 32; ++i) if (m & (1u << i)) { g_deletedSlots &= ~(1u << i); return i; }
    return -1;
}

void SetScreenMap(float k, float ox, float oy, int vw, int vh) {
    static int logs = 0;
    bool changed = k != g_mapK || ox != g_mapX || oy != g_mapY || vw != g_mapVW || vh != g_mapVH;
    if (changed && logs < 30 && ++logs) Log("화면 맞춤: 게임 화면 %dx%d → 배율 %.3f, 여백 (%.1f, %.1f)", vw, vh, k, ox, oy);
    if (changed) { ImVec2 ds = ImGui::GetIO().DisplaySize; ReportSet("screen", "%.0fx%.0f/game%dx%d", ds.x, ds.y, vw, vh); }
    g_mapK = k; g_mapX = ox; g_mapY = oy; g_mapVW = vw; g_mapVH = vh;
}
void FullScreenXform1080(float& k, float& ox, float& oy) {
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (g_mapVW <= 0 || g_mapVH <= 0) { k = ds.y / 1080.0f; ox = ds.x * 0.5f - 960 * k; oy = 0; return; }
    float s = g_mapVH / 1080.0f;
    k  = g_mapK * s;
    ox = g_mapX + (g_mapVW * 0.5f - 960 * s) * g_mapK;
    oy = g_mapY;
}

void UpdateProvisionBaseline();
void BeginFrame() {
    ResetReadCache();
    if (g_defsLoaded && g_defsKey != g_activeKey) {
        g_defs.clear(); g_info.clear(); g_defsLoaded = false;
        Log("모드 구성이 바뀜 → 장신구 정보 다시 읽음");
    }
    if (g_bagSeen != g_bagLast)   { g_bag = g_bagPending; g_bagLast = g_bagSeen; }    else g_bag.visible = false;
    if (g_heroSeen != g_heroLast) { g_hero = g_heroPending; g_heroLast = g_heroSeen; } else g_hero.visible = false;
    if (g_dragSeen != g_dragLast) { g_drag = g_dragPending; g_dragLast = g_dragSeen; } else g_drag.active = false;
    if (g_hdragSeen != g_hdragLast) { g_hdrag = g_hdragPending; g_hdragLast = g_hdragSeen; } else g_hdrag.active = false;
    bool anySlot = false;
    for (bool b : g_partyPending.slotSeen) anySlot |= b;
    g_qsRecent = (g_qsSeen != g_qsLast); g_qsLast = g_qsSeen;
    if (anySlot) {
        g_party = g_partyPending; g_party.visible = true;
        for (int i = 0; i < 4; ++i) g_slotIface[i] = g_partyPending.slotSeen[i] ? g_slotIfacePending[i] : nullptr;
    } else { g_party.visible = false; for (auto& p : g_slotIface) p = nullptr; }
    for (bool& b : g_partyPending.slotSeen) b = false;
    g_provVisible = (g_provSeen != g_provLast); g_provLast = g_provSeen;
    UpdateProvisionBaseline();
    if (g_modalSeen != g_modalLast) { g_modal = g_modalPending; g_modalLast = g_modalSeen; } else g_modal = 0;
    g_modalPending = 0;
    if (g_ok) {      // 확인 창(ConfirmDialog) 대기열
        void* cd = At<FnStatic>(A_ConfirmInst)();
        if (cd && At<FnBool>(A_ConfirmQueue)(cd)) g_modal = 1.0f;
    }
    if (g_townSeen != g_townLast) { g_town = true; g_townMiss = 0; g_townLast = g_townSeen; }
    else if (++g_townMiss > 30) g_town = false;
}

const BagState& Bag() { return g_bag; }
const HeroPanelState& HeroPanel() { return g_hero; }
const DragState& Drag() { return g_drag; }
bool InTown() { return g_town; }
int  CurrentProfile() { return (int)g_profile.load(); }
const HeroDragState& HeroDrag() { return g_hdrag; }
const PartyState& Party() { return g_party; }
float SubtitleAlpha() { return 0.0f; }
bool  UnderSubtitle() { return false; }
float ScreenVisibility() { return 1.0f; }
bool TownTransition() { return false; }
bool QuestSelectVisible() { return g_qsRecent; }
bool TopLayerActive() { return false; }
bool TownFadingOut() { return false; }
std::string Localize(const std::string& key) { return g_ok ? LocalizeImpl(key) : std::string(); }
std::string SteamLanguage() { return SteamLanguageImpl(); }

bool ModEnabled() {
    if (!g_ok) return false;
    const std::wstring& tr = Gfx::ToolRoot();
    if (tr.empty()) return false;
    std::wstring parent = tr.substr(0, tr.find_last_of(L"\\/"));
    std::wstring wname = parent.substr(parent.find_last_of(L"\\/") + 1);
    char name[520] = {};
    WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, name, sizeof name, nullptr, nullptr);
    static std::wstring titleFor; static std::string title;
    if (titleFor != parent) { titleFor = parent; title = ReadModTitle(parent); Log("모드 제목: '%s'", title.c_str()); }
    uint8_t* gp = At<uint8_t*>(D_App) + 0x748;
    uint8_t* b = *(uint8_t**)(gp + 0x208); uint8_t* e = *(uint8_t**)(gp + 0x210);
    bool on = false;
    std::string list, key;
    std::vector<std::string> names;
    if (b && e >= b && (e - b) / 0xC0 <= 1000 && (e == b || Readable(b, (size_t)(e - b)))) {
        for (uint8_t* p = b; p < e; p += 0xC0) {
            const char* n = (const char*)p;
            names.emplace_back(n, strnlen(n, 0x80));
            key += names.back(); key += '|';
            if (_stricmp(n, name) == 0 || (!title.empty() && strncmp(n, title.c_str(), 0x7F) == 0)) on = true;
            if (list.size() < 600) { list += names.back(); list += "("; list.append((const char*)(p + 0x80), strnlen((const char*)(p + 0x80), 0x40)); list += ") "; }
        }
    }
    UpdateActiveRoots(names, key);
    static std::string lastList = "?"; static bool lastOn = false;
    if (list != lastList || on != lastOn) {
        lastList = list; lastOn = on;
        Log("모드 '%s' %s / 적용된 모드: %s", name, on ? u8"켜짐" : u8"꺼짐", list.empty() ? u8"(없음)" : list.c_str());
        ReportSet("mod", on ? "ON" : "OFF");
    }
    return on;
}
bool ProvisionVisible() { return g_provVisible; }

// ---- 원정 준비: 골드 아이템 일괄 구매 ----
uint8_t* Provision() { uint8_t* c = Campaign(); return c ? c + kProvision : nullptr; }
unsigned GoldHash() { return At<FnCurHash>(A_CurrencyHash)(At<void*>(D_KGold)); }
int Gold() {
    if (!g_ok) return -1;
    uint8_t* es = EstateSys(); if (!es) return -1;
    return At<FnAmount>(A_EstateAmount)(es, GoldHash());
}

struct BuyTarget { std::string type, id, label; int count; };
struct ItemAlias { const char* name; const char* type; const char* id; };
const ItemAlias kAliases[] = {
    { u8"식량", "provision", "" },        { u8"횃불", "supply", "torch" },     { u8"삽", "supply", "shovel" },
    { u8"열쇠", "supply", "skeleton_key" }, { u8"해독제", "supply", "antivenom" }, { u8"붕대", "supply", "bandage" },
    { u8"약초", "supply", "medicinal_herbs" }, { u8"성수", "supply", "holy_water" }, { u8"아편", "supply", "laudanum" },
    { u8"장작", "supply", "firewood" },    { u8"개 간식", "supply", "dog_treats" },
};
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
std::vector<BuyTarget> LoadBuyPlan() {
    const std::wstring& tr = Gfx::ToolRoot();
    if (tr.empty()) return DefaultPlan();
    std::string txt = Gfx::ReadFile(tr + L"\\autobuy.txt");
    if (txt.empty()) return DefaultPlan();
    if (txt.size() >= 3 && (unsigned char)txt[0] == 0xEF) txt = txt.substr(3);
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
        int count = (cnt == u8"전부" || lc == "all") ? -1 : atoi(cnt.c_str());
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

uint8_t* StoreInv() { uint8_t* p = Provision(); return p ? (uint8_t*)At<FnPtrRet>(A_StoreInv)(p) : nullptr; }
uint8_t* RaidBag()  { uint8_t* p = Provision(); return p ? (uint8_t*)At<FnPtrRet>(A_RaidInv)(p) : nullptr; }

std::unordered_map<unsigned long long, int> BagCounts() {
    std::unordered_map<unsigned long long, int> m;
    uint8_t* bag = RaidBag(); if (!bag) return m;
    size_t n = InvSize(bag); if (n > 200) return m;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* p = InvItem(bag, i);
        int q = *(int*)p; if (q <= 0) continue;
        m[((unsigned long long)ItemType(p) << 32) | ItemHash(p)] += q;
    }
    return m;
}
std::unordered_map<unsigned long long, int> g_bagBaseline;
bool g_bagBaselineSet = false, g_provWasVisible = false;
void UpdateProvisionBaseline() {
    if (g_provVisible && !g_provWasVisible && g_ok) { g_bagBaseline = BagCounts(); g_bagBaselineSet = true; }
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
    uint8_t* prov = Provision(); uint8_t* es = EstateSys();
    uint8_t* store = StoreInv(); uint8_t* bag = RaidBag();
    if (!prov || !es || !store || !bag) { r.error = Txt::T("err_data"); return r; }
    auto goldPrice = At<FnPrice>(A_GoldValue);
    auto shardPrice = At<FnPrice>(A_ShardValue);
    auto buyOne = At<FnBuy>(A_BuyLambda);
    auto amount = At<FnAmount>(A_EstateAmount);
    unsigned gh = GoldHash();
    g_lam[0] = nullptr; g_lam[1] = nullptr; g_lam[2] = g_provSelect; g_lam[3] = store; g_lam[4] = bag;   // 게임 람다(__func)와 같은 배치
    bool ordinal = At<FnBoolS>(A_UsingPad)();      // 패드 모드는 빈칸을 건너뛴 번호
    int slots = (int)InvSize(store);
    if (slots <= 0) { r.error = Txt::T("err_store_empty"); return r; }
    if (slots > 200) { r.error = Txt::T("err_store"); return r; }
    r.ok = true;
    for (const BuyTarget& t : LoadBuyPlan()) {
        if (r.outOfGold || r.bagFull) break;
        uint32_t th = Hash(t.type.c_str());
        int want = t.count < 0 ? 1 << 30 : t.count;
        for (int i = 0; i < slots && want > 0 && !r.outOfGold && !r.bagFull; ++i) {
            uint8_t* item = InvItem(store, i);
            int qty = *(int*)item;
            if (qty <= 0 || ItemType(item) != th) continue;
            if (strncmp((const char*)(item + 0x48), t.id.c_str(), 64) != 0) continue;
            if (shardPrice(prov, item) > 0) continue;          // 파편 아이템 제외
            int gp = goldPrice(prov, item);
            while (qty > 0 && want > 0) {
                int gold = amount(es, gh);
                if (gold < gp) { r.outOfGold = true; break; }
                unsigned idx = (unsigned)i;
                if (ordinal) { idx = 0; for (int k = 0; k < i; ++k) if (*(int*)InvItem(store, k) > 0) ++idx; }
                buyOne(g_lam, &idx);
                if ((int)InvSize(store) != slots) { r.error = Txt::T("err_store"); r.ok = false; return r; }
                item = InvItem(store, i);
                int nq = *(int*)item;
                if (nq >= qty) { r.bagFull = true; break; }
                r.bought += qty - nq; want -= qty - nq;
                r.spent += gold - amount(es, gh);
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
        std::string name = LocalizeImpl("str_inventory_title_" + t.type + t.id);
        if (name.empty()) name = t.label;
        s += name + " " + (t.count < 0 ? std::string(Txt::T("buy_all")) : Txt::F("buy_count", { std::to_string(t.count) }));
    }
    return s;
}

bool ShowHeroInfo(int) { return false; }      // 맥판: 아직 지원 안 함 (버튼을 눌러도 아무 일 없음)
float ModalAmount() { return g_modal; }
void SetModalRenderCallback(void (*)()) {}
void SetLateRenderCallback(void (*)()) {}
void SetFrameStartCallback(void (*cb)()) { g_frameCb = cb; }
void NewGameFrame() { g_frameCbDone = false; }
bool TopMenuOpen() { return g_modal > 0; }

// ---- 영웅 ----
uint8_t* FindEntry(int guid) {
    uint8_t* hit = nullptr;
    ForEachRosterEntry([&](uint8_t* e) { if (!hit && *(int*)e == guid) hit = e; });
    return hit;
}
bool RosterReadable() {
    if (!g_ok) return false;
    uint8_t* r = RosterSys(); if (!r || !Readable(r + 0x20, 0x10)) return false;
    uint8_t** b = *(uint8_t***)(r + 0x20); uint8_t** e = *(uint8_t***)(r + 0x28);
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
    uint8_t* entry = FindEntry(guid);
    if (!entry) return false;
    uint8_t* hero = entry + kEntryHero;
    HeroInfo h;
    h.guid = guid;
    h.status = *(int*)(entry + kEntryStatus);
    h.name.assign((const char*)(hero + 8), strnlen((const char*)(hero + 8), 64));
    uint8_t* cls = *(uint8_t**)(hero + kHeroClass);
    if (cls && Readable(cls, 0x100)) {
        h.classId.assign((const char*)(cls + 8), strnlen((const char*)(cls + 8), 64));
        h.portrait = "heroes/" + h.classId + "/" + h.classId + "_portrait_roster.png";
    }
    *out = h;
    return true;
}

PartyResult LoadParty(const int guids[4]) {
    PartyResult r;
    if (!g_ok)            { r.error = Txt::T("err_off"); return r; }
    if (!g_party.visible) { r.error = Txt::T("err_party_screen"); return r; }
    uint8_t* roster = RosterSys();
    if (!roster) { r.error = Txt::T("err_data"); return r; }
    uint8_t* iface[4] = {};
    for (int i = 0; i < 4; ++i) {
        iface[i] = (uint8_t*)g_slotIface[i];
        if (!iface[i] || !Readable(iface[i], 0x240) || *(void**)iface[i] != At<void*>(VT_QSlotIface) || *(unsigned*)(iface[i] + 0x230) != (unsigned)i) {
            r.error = Txt::T("err_party_slots"); return r;
        }
    }
    uint8_t* entries[4] = {};
    for (int i = 0; i < 4; ++i) {
        if (guids[i] < 0) continue;
        entries[i] = FindEntry(guids[i]);
        if (!entries[i]) { r.error = Txt::T("err_hero_missing"); return r; }
        int st = *(int*)(entries[i] + kEntryStatus);
        if (st != 0 && st != 1) { r.error = Txt::T("err_hero_unavail"); return r; }
    }
    auto setStatus = At<FnSetStat>(A_SetStatus);
    auto setHero = [](uint8_t* f, const void* hero) { (*(FnSetHero**)f)[7](f, hero); };   // vtable +0x38 SetHero
    for (int i = 0; i < 4; ++i) {
        uint8_t* hero = *(uint8_t**)(iface[i] + 0x28);         // GetHero()
        if (hero && Readable(hero + kHeroGuid, 4)) setStatus(roster, *(unsigned*)(hero + kHeroGuid), 0);
        *(iface[i] + 0xC) = 0;
        setHero(iface[i], nullptr);
    }
    for (int i = 0; i < 4; ++i) {
        if (!entries[i]) continue;
        setStatus(roster, (unsigned)guids[i], 1);
        *(iface[i] + 0xC) = 1;
        setHero(iface[i], entries[i] + kEntryHero);
    }
    if (g_questSelect && g_qsRecent) At<FnVoid>(A_RefreshParty)(g_questSelect);
    Log("영웅 프리셋 적용: %d %d %d %d", guids[0], guids[1], guids[2], guids[3]);
    r.ok = true;
    return r;
}

int g_classOffset = -2;
const TrinketInfo& Trinket(uint32_t hash) {
    auto it = g_info.find(hash);
    if (it != g_info.end()) return it->second;
    TrinketInfo info;
    static const TrinketInfo kNone;
    if (g_ok) {
        uint8_t* lb = LibVec()[0]; uint8_t* le = LibVec()[1];
        if (!lb || le <= lb) return kNone;
        if (g_rarityOffset == -2) CalibrateRarity();
        for (uint8_t* e = lb; e + kLibEntrySize <= le; e += kLibEntrySize) {
            if (*(uint32_t*)(e + 0x40) != hash) continue;
            info.valid = true;
            info.id.assign((const char*)e, strnlen((const char*)e, 0x40));
            if (g_rarityOffset > 0) {
                uint32_t rh = *(uint32_t*)(e + g_rarityOffset);
                uint8_t** rv = At<uint8_t**>(D_Rarities);
                for (uint8_t* rr = rv[0]; rr && rr + kRaritySize <= rv[1]; rr += kRaritySize)
                    if (*(uint32_t*)(rr + 0x40) == rh) { info.rarity.assign((const char*)rr, strnlen((const char*)rr, 0x40)); break; }
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
    return g_info[hash] = info;
}
const TrinketInfo& TrinketById(const char* id) { return Trinket(Hash(id)); }

uint32_t HeroClassHash(void* hero) {
    if (!g_ok || !hero) return 0;
    uint8_t* cls = *(uint8_t**)((uint8_t*)hero + kHeroClass);
    if (!cls || !Readable(cls + 0xD4, 4)) return 0;
    uint32_t h = *(uint32_t*)(cls + 0xD4);
    static bool checked = false;
    if (!checked) { checked = true; auto set = ClassSet(); if (!set.empty() && !set.count(h)) Log("경고: 직업 해시 확인 실패 (0x%08X)", h); }
    return h;
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
    alignas(16) unsigned char fn[64] = {};       // 빈 std::function (libc++: 호출한 쪽이 소멸 → 비어 있으니 아무 일 없음)
    At<FnAudio>(A_AudioEvent)(path, false, fn);
}

std::unordered_map<uint32_t, int> OwnedCounts(bool* ok) {
    std::unordered_map<uint32_t, int> m;
    *ok = false;
    if (!g_ok || !HashVerified()) return m;
    uint8_t* est = EstateInv();
    if (!est) return m;
    size_t estSlots = InvSize(est), estTrinkets = 0, heroes = 0, heroTrinkets = 0;
    for (size_t i = 0; i < estSlots; ++i) {
        uint8_t* it = InvItem(est, i);
        if (IsTrinket(it)) { m[ItemHash(it)] += ItemQty(it); ++estTrinkets; }
    }
    ForEachRosterInv([&](uint8_t* inv) {
        ++heroes;
        size_t n = InvSize(inv);
        for (size_t i = 0; i < n; ++i) { uint8_t* it = InvItem(inv, i); if (IsTrinket(it)) { m[ItemHash(it)] += ItemQty(it); ++heroTrinkets; } }
    });
    int estCount = Readable(est + 0x24, 4) ? *(int*)(est + 0x24) : 0;
    static int logged = 0;
    if (logged++ < 3)
        Log("보유 확인: 가방 칸 %zu, 게임 집계 %d, 가방 장신구 %zu / 영웅 %zu명, 영웅 장신구 %zu", estSlots, estCount, estTrinkets, heroes, heroTrinkets);
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
    ForEachRosterInv([&](uint8_t* inv) { mx = std::max(mx, InvSize(inv)); });
    int n = mx ? (int)std::min<size_t>(mx, 8) : cached;
    if (n != cached) { Log("영웅 장신구 칸 수: %d", n); ReportSet("slots", "%d", n); }
    cached = std::max(1, n);
    return cached;
}

EquipResult EquipPreset(void* hero, const char* const* ids, int n) {
    EquipResult r;
    if (!g_ok || !hero) { r.error = Txt::T("err_off"); return r; }
    if (!InTown())      { r.error = Txt::T("town_only_load"); return r; }
    if (!HashVerified()) { r.error = Txt::T("err_format"); return r; }
    uint8_t* est = EstateInv();
    uint8_t* hinv = HeroInv(hero);
    size_t nh = InvSize(hinv), ne = est ? InvSize(est) : 0;
    if (!est || nh == 0 || nh > 8 || ne == 0) { r.error = Txt::T("err_data"); return r; }
    if (n > (int)nh) n = (int)nh;
    if (n > 8) n = 8;
    const char* want[8] = {};
    for (int j = 0; j < n; ++j) want[j] = ids[j];
    uint32_t hc = HeroClassHash(hero);
    for (int j = 0; j < n; ++j)
        if (want[j] && want[j][0] && !CanUse(TrinketById(want[j]), hc)) { r.error = Txt::T("err_class"); return r; }
    bool used[8] = {}, done[8] = {};
    for (int j = 0; j < n; ++j) {
        if (!want[j] || !want[j][0]) { done[j] = true; continue; }
        uint32_t h = Hash(want[j]);
        for (size_t s = 0; s < nh; ++s) {
            uint8_t* it = InvItem(hinv, s);
            if (!used[s] && IsTrinket(it) && ItemHash(it) == h) { used[s] = true; done[j] = true; ++r.already; break; }
        }
    }
    auto sw = At<FnSwitch>(A_SwitchItems);
    for (int j = 0; j < n; ++j) {
        if (done[j]) continue;
        size_t slot = (size_t)j < nh && !used[j] ? (size_t)j : nh;
        if (slot == nh) for (size_t s = 0; s < nh; ++s) if (!used[s]) { slot = s; break; }
        if (slot == nh) break;
        uint32_t h = Hash(want[j]);
        size_t e = ne;
        for (size_t i = 0; i < ne; ++i) { uint8_t* it = InvItem(est, i); if (IsTrinket(it) && ItemHash(it) == h) { e = i; break; } }
        const TrinketInfo& ti = Trinket(h);
        if (e == ne) { r.missing.push_back(ti.name.empty() ? want[j] : ti.name); continue; }
        uint8_t* src = InvItem(est, e);
        alignas(16) static uint8_t tmp[kItemSize];
        memset(tmp, 0, sizeof tmp);
        At<FnCopy>(A_ItemCopy)(tmp, src);
        bool stacked = ItemQty(src) > 1;
        if (stacked) {
            size_t empty = ne;
            for (size_t i = 0; i < ne; ++i) if (ItemQty(InvItem(est, i)) <= 0) { empty = i; break; }
            bool heroHas = ItemQty(InvItem(hinv, slot)) > 0;
            if (empty == ne && heroHas) {
                DestroyItem(tmp);
                Log("가방에 빈 칸이 없어 겹친 장신구를 꺼내지 않음");
                r.missing.push_back(ti.name.empty() ? want[j] : ti.name);
                continue;
            }
            *(int*)tmp = 1;
            sw(hinv, (unsigned)slot, tmp);
            *(int*)src -= 1;
            if (ItemQty(tmp) > 0) {
                if (empty < ne) sw(est, (unsigned)empty, tmp);
                else Log("경고: 가방에 빈 칸이 없어 원래 장신구를 돌려놓지 못함");
            }
        } else {
            sw(hinv, (unsigned)slot, tmp);
            sw(est, (unsigned)e, tmp);
        }
        DestroyItem(tmp);
        used[slot] = true;
        ++r.equipped;
    }
    if (r.equipped > 0) At<FnVoid>(A_TrinketBuffs)(hero);
    Log("프리셋 적용: 장착 %d, 이미 장착 %d, 없음 %zu", r.equipped, r.already, r.missing.size());
    return r;
}

} // namespace Game

// 영웅(파티) 프리셋 UI
//  - 영지 지도: 파티 슬롯 4칸 오른쪽에 집사 초상화 버튼 → 슬롯 위에 장신구 가방과 같은 모양의 프리셋 패널
//  - 영웅 목록에서 영웅을 끌어다 칸에 놓고 [저장], [불러오기] 로 파티 구성
//  - 죽은(로스터에서 사라진) 영웅은 자동으로 빠지고, 출정 불가(행방불명/활동 중) 영웅이 있으면 불러오기 막음
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include "imgui.h"
#include "ui_heropreset.h"
#include "hero_presets.h"
#include "game.h"
#include "text.h"
#include "gfx.h"
#include "log.h"

using Gfx::FontId;
using HeroPresets::kCount;

namespace UiHeroPreset {
namespace {

// ---- 패널 좌표 (장신구 프리셋 패널과 같은 배경) ----
constexpr float kPanelW = 667, kPanelH = 780;
constexpr float kBgY = -4;
constexpr float kIconX = 16, kIconY = 16, kIconSize = 113;
constexpr float kTitleX = 148, kTitleY = 46;
constexpr float kSubX = 150, kSubY = 100;
constexpr float kCloseX = 610, kCloseY = 22, kBtn = 32;
constexpr float kGridX = 28, kGridY = 192;
constexpr float kCardW = 556, kCardH = 104, kCardGap = 6;
constexpr int   kVisible = 5;
constexpr float kUpX = 590, kUpY = 150, kDownX = 589, kDownY = 713;
constexpr float kSlotX = 156, kSlotSize = 66, kSlotPitch = 70;
constexpr float kSlot = 85;                       // 게임 영웅 칸 그림 크기

const ImU32 kColTitle = IM_COL32(200, 180, 110, 255);
const ImU32 kColSub   = IM_COL32(174, 172, 162, 255);
const ImU32 kColDim   = IM_COL32(120, 115, 105, 255);
const ImU32 kColText  = IM_COL32(215, 205, 185, 255);
const ImU32 kColWarn  = IM_COL32(230, 150, 70, 255);
const ImU32 kColOk    = IM_COL32(150, 210, 130, 255);

const char* kBg       = "campaign/town/realm_inventory/realminv_bg.png";
const char* kClose    = "shared/progression/progression_close.png";
const char* kUp       = "campaign/town/realm_inventory/realm_inventory_uparrow.png";
const char* kDown     = "campaign/town/realm_inventory/realm_inventory_downarrow.png";
const char* kButler   = "campaign/town/hero_slot/caretaker_portrait.png";
const char* kSlotBg   = "campaign/town/hero_slot/hero_slot.background.png";
const char* kSlotHi   = "campaign/town/hero_slot/hero_slot.backgroundhightlight.png";
const char* kFrameOk  = "campaign/town/hero_slot/hero_slot.positive_frame.png";
const char* kFrameBad = "campaign/town/hero_slot/hero_slot.negative_frame.png";
const char* kLocked   = "campaign/town/hero_slot/hero_slot.locked_for_hero.png";
const char* kMissing  = "campaign/town/roster/missing.icon_roster.png";

const char* kSndHover    = "/ui/town/button_mouse_over";
const char* kSndClick    = "/ui/town/button_click";
const char* kSndInvalid  = "/ui/town/button_invalid";
const char* kSndEquip    = "/ui/town/character_equip";
const char* kSndRemove   = "/ui/dungeon/trink_unequip";
const char* kSndOpen     = "/ui/town/page_open";
const char* kSndClose    = "/ui/town/page_close";
const char* kSndScrollUp = "/ui/shared/button_scroll_up";
const char* kSndScrollDn = "/ui/shared/button_scroll_down";
void Snd(const char* e) { Game::PlaySound(e); }

// ---- 상태 ----
bool  g_open = false;
int   g_scroll = 0;
HeroPresets::Preset g_draft[kCount];
bool  g_dirty[kCount] = {};
int   g_editing = -1;
char  g_editBuf[64] = {};
bool  g_editFocus = false;
int   g_editStart = 0;
int   g_dropCard = -1, g_dropSlot = -1;
char  g_toast[200] = {};
float g_toastTime = 0;
bool  g_toastWarn = false;
int   g_order[4] = { 3, 2, 1, 0 };     // 화면 왼쪽→오른쪽 순서의 파티 배열 인덱스 (슬롯 위치로 갱신)
int   g_pruneFrame = 0;
struct { bool active = false; int card = -1, slot = -1, guid = -1; } g_swap;   // 프리셋 안에서 자리 바꾸기
int   g_swapTarget = -1, g_swapTargetCard = -1;
bool  g_swapJustEnded = false;
ImGuiID g_hoverNow = 0, g_hoverPrev = 0;

Game::Xform XF;
ImDrawList* DL = nullptr;
ImVec2 P(float x, float y) { return XF.ToScreen(ImVec2(x, y)); }
float  S() { return XF.Scale(); }

void Toast(const char* msg, bool warn = false) { snprintf(g_toast, sizeof g_toast, "%s", msg); g_toastTime = 3.5f; g_toastWarn = warn; }

float g_alpha = 1.0f;                       // 버튼 서서히 나타내기 (화면 전환 중엔 숨김)
float g_shownTime = 0;
ImU32 A(ImU32 c) { return (c & 0x00FFFFFF) | ((ImU32)(((c >> 24) & 255) * g_alpha) << 24); }

void ImageTex(const Gfx::Texture& t, float x, float y, float w, float h, ImU32 tint = IM_COL32_WHITE) {
    if (!t.ok()) return;
    tint = A(tint);
    if (w < 0) { w = (float)t.w; h = (float)t.h; }
    DL->AddImage(t.tex(), P(x, y), P(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
}
void Image(const char* rel, float x, float y, float w = -1, float h = -1, ImU32 tint = IM_COL32_WHITE) {
    ImageTex(Gfx::GameTex(rel), x, y, w, h, tint);
}
void Text(FontId f, float x, float y, ImU32 col, const char* s, bool shadow = true, float maxW = 0) {
    col = A(col);
    // maxW 보다 길면 글자를 줄여서 칸 안에 맞춤 (긴 번역문이 삐져나가지 않게)
    float k = 1.0f;
    if (maxW > 0) { float w = Gfx::MeasureText(f, s, 1.0f).x; if (w > maxW) k = std::max(0.55f, maxW / w); }
    if (k < 1.0f) y += Gfx::FontLineHeight(f) * (1.0f - k) * 0.5f;
    if (shadow) Gfx::DrawTextShadow(DL, f, P(x, y), S() * k, col, s);
    else        Gfx::DrawStr(DL, f, P(x, y), S() * k, col, s);
}
// 줄인 뒤의 너비 (가운데 정렬용)
float TextWFit(FontId f, const char* s, float maxW) { float w = Gfx::MeasureText(f, s, 1.0f).x; return (maxW > 0 && w > maxW) ? std::max(w * 0.55f, maxW) : w; }
float TextW(FontId f, const char* s) { return Gfx::MeasureText(f, s, 1.0f).x; }
bool Hover(float x, float y, float w, float h) { return ImGui::IsMouseHoveringRect(P(x, y), P(x + w, y + h), false); }

bool HitButton(const char* id, float x, float y, float w, float h, bool* hovered = nullptr) {
    ImGui::SetCursorScreenPos(P(x, y));
    ImGui::InvisibleButton(id, ImVec2(w * S(), h * S()), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hv = ImGui::IsItemHovered();
    if (hv) {
        ImGuiID iid = ImGui::GetItemID();
        if (iid != g_hoverPrev && !Game::HeroDrag().active) Snd(kSndHover);
        g_hoverNow = iid;
    }
    if (hovered) *hovered = hv;
    return ImGui::IsItemClicked(0);
}

void Tooltip(const char* title, const char* body, ImU32 bodyCol = 0) {
    ImVec2 m = ImGui::GetIO().MousePos;
    float s = S();
    ImVec2 tw = Gfx::MeasureText(FontId::Body, title, s);
    ImVec2 bw = body ? Gfx::MeasureText(FontId::Body, body, s) : ImVec2(0, 0);
    float w = std::max(tw.x, bw.x) + 20 * s, h = tw.y + (body ? bw.y + 4 * s : 0) + 14 * s;
    ImVec2 p0(m.x + 18 * s, m.y + 10 * s);
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (p0.x + w > ds.x) p0.x = m.x - w - 8 * s;
    if (p0.y + h > ds.y) p0.y = m.y - h - 8 * s;
    ImVec2 p1(p0.x + w, p0.y + h);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(p0, p1, IM_COL32(12, 10, 8, 240));
    fg->AddRect(p0, p1, IM_COL32(120, 100, 60, 255), 0, 0, 1.5f * s);
    Gfx::DrawStr(fg, FontId::Body, ImVec2(p0.x + 10 * s, p0.y + 7 * s), s, kColTitle, title);
    if (body) Gfx::DrawStr(fg, FontId::Body, ImVec2(p0.x + 10 * s, p0.y + 11 * s + tw.y), s, bodyCol ? bodyCol : kColSub, body);
}

bool TextButton(const char* id, const char* label, float x, float y, float w, float h, bool enabled, bool highlight = false, bool* hovOut = nullptr) {
    bool hov = false;
    bool clicked = HitButton(id, x, y, w, h, &hov);
    if (hovOut) *hovOut = hov;
    ImU32 fill   = !enabled ? IM_COL32(20, 18, 15, 230) : hov ? IM_COL32(58, 42, 24, 240) : IM_COL32(28, 22, 16, 235);
    ImU32 border = !enabled ? IM_COL32(60, 55, 45, 255) : (hov || highlight) ? IM_COL32(205, 170, 100, 255) : IM_COL32(100, 82, 52, 255);
    DL->AddRectFilled(P(x, y), P(x + w, y + h), fill);
    DL->AddRect(P(x, y), P(x + w, y + h), border, 0, 0, (highlight ? 2.0f : 1.5f) * S());
    float tw = TextWFit(FontId::Body, label, w - 8);
    Text(FontId::Body, x + (w - tw) * 0.5f, y + (h - Gfx::FontLineHeight(FontId::Body)) * 0.5f + 1,
         enabled ? (hov ? IM_COL32(255, 240, 200, 255) : kColText) : kColDim, label, false, w - 8);
    if (clicked && !enabled) Snd(kSndInvalid);
    return clicked && enabled;
}

// 영지 지도의 버튼: 빈 파티 슬롯과 같은 틀 + 집사 초상화 (눈에 튀지 않게 금색 없이)
void DrawButlerButton(float x, float y, float size, bool hovered, bool active) {
    bool lit = hovered || active;
    Image(lit ? kSlotHi : kSlotBg, x, y, size, size);
    Image(kButler, x, y, size, size, lit ? IM_COL32(235, 230, 220, 255) : IM_COL32(150, 145, 138, 255));
    DL->AddRect(P(x, y), P(x + size, y + size), A(lit ? IM_COL32(120, 100, 70, 255) : IM_COL32(55, 48, 38, 255)), 0, 0, 1.5f * S());
}

// 패널 아이콘: 집사 초상화 + 금색 테두리
void DrawButler(float x, float y, float size, bool hovered, bool active) {
    (void)active;
    Image(hovered ? kSlotHi : kSlotBg, x, y, size, size);
    Image(kButler, x, y, size, size, hovered ? IM_COL32_WHITE : IM_COL32(215, 210, 200, 255));
    // 테두리: 게임 아이콘처럼 어두운 청동색 (밝은 금색 X)
    DL->AddRect(P(x - 2, y - 2), P(x + size + 2, y + size + 2), IM_COL32(92, 76, 50, 255), 0, 0, 1.5f * S());
    DL->AddRect(P(x - 4, y - 4), P(x + size + 4, y + size + 4), IM_COL32(30, 24, 16, 255), 0, 0, 1.0f * S());
}

// ---- 영웅 정보 ----
enum class HeroState { Empty, Ok, Missing, Busy, Gone };
HeroState StateOf(int guid, Game::HeroInfo* info) {
    if (guid < 0) return HeroState::Empty;
    if (!Game::GetHero(guid, info) || info->Dead()) return HeroState::Gone;
    if (info->Available()) return HeroState::Ok;
    return info->Missing() ? HeroState::Missing : HeroState::Busy;
}

const Gfx::Texture& Portrait(const Game::HeroInfo& h) {
    const Gfx::Texture& t = Gfx::ModTex(h.portrait.c_str());
    if (t.ok()) return t;
    std::string alt = "heroes/" + h.classId + "/" + h.classId + "_A/" + h.classId + "_portrait_roster.png";
    const Gfx::Texture& t2 = Gfx::ModTex(alt.c_str());
    if (t2.ok()) return t2;
    static bool logged = false;
    if (!logged) { logged = true; Log("영웅 초상화를 찾지 못함: %s", h.portrait.c_str()); }
    return t2;
}

void DrawPortrait(const Game::HeroInfo& h, float x, float y, float size, bool dim) {
    const Gfx::Texture& t = Portrait(h);
    if (t.ok()) ImageTex(t, x, y, size, size, dim ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE);
    else {
        std::string c = Game::ClassName(h.classId);
        Text(FontId::Body, x + 6, y + size * 0.5f - 10, kColDim, c.substr(0, c[0] & 0x80 ? 3 : 1).c_str(), false);
    }
}

const char* StateText(HeroState st) {
    switch (st) {
        case HeroState::Missing: return Txt::T("st_missing");
        case HeroState::Busy:    return Txt::T("st_busy");
        case HeroState::Gone:    return Txt::T("st_gone");
        default:                 return Txt::T("st_ok");
    }
}

// 프리셋을 불러올 수 있는지. reason 에 막힌 이유
bool Usable(const HeroPresets::Preset& p, std::string* reason) {
    if (p.Empty()) { if (reason) *reason = Txt::T("empty_lineup"); return false; }
    for (int i = 0; i < 4; ++i) {
        Game::HeroInfo h;
        HeroState st = StateOf(p.guid[i], &h);
        if (st == HeroState::Missing || st == HeroState::Busy || st == HeroState::Gone) {
            if (reason) *reason = (h.name.empty() ? std::string(Txt::T("hero")) : h.name) + ": " + StateText(st);
            return false;
        }
    }
    return true;
}

// 죽은(로스터에서 사라진) 영웅을 프리셋에서 뺌
void PruneDead() {
    if (HeroPresets::Profile() < 0 || !Game::RosterReadable()) return;   // 확실할 때만
    int removed = 0;
    for (int i = 0; i < kCount; ++i)
        for (int s = 0; s < 4; ++s) {
            Game::HeroInfo h;
            HeroPresets::Preset& p = HeroPresets::Get(i);
            if (p.guid[s] >= 0 && (!Game::GetHero(p.guid[s], &h) || h.Dead())) {
                Log("영웅 프리셋 %d: 죽거나 사라진 영웅 제거 (guid %d)", i + 1, p.guid[s]);
                p.guid[s] = -1; ++removed; HeroPresets::MarkDirty();
            }
            int& d = g_draft[i].guid[s];
            if (d >= 0 && (!Game::GetHero(d, &h) || h.Dead())) d = -1;
        }
    if (removed) {
        Toast(Txt::F("removed_heroes", { Txt::Num(removed) }).c_str(), true);
    }
}

void LoadDrafts() {
    for (int i = 0; i < kCount; ++i) { g_draft[i] = HeroPresets::Get(i); g_dirty[i] = false; }
}

// ======================= 패널 =======================

void DrawSlot(int card, int col, float x, float y) {
    int pi = g_order[col];                       // 이 칸이 맡는 파티 배열 인덱스
    HeroPresets::Preset& d = g_draft[card];
    int guid = d.guid[pi];
    bool hov = false;
    char bid[32]; snprintf(bid, sizeof bid, "##hslot%d_%d", card, pi);
    // 왼쪽 버튼: 떼었을 때 클릭(빼기), 누른 채 움직이면 자리 바꾸기 끌기
    ImGui::SetCursorScreenPos(P(x, y));
    bool released = ImGui::InvisibleButton(bid, ImVec2(kSlotSize * S(), kSlotSize * S()),
                                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    hov = ImGui::IsItemHovered();
    if (hov) { ImGuiID iid = ImGui::GetItemID(); if (iid != g_hoverPrev && !Game::HeroDrag().active && !g_swap.active) Snd(kSndHover); g_hoverNow = iid; }
    bool rclick = hov && ImGui::IsItemClicked(1);
    bool lclick = released && !ImGui::IsMouseReleased(1) && !g_swap.active && !g_swapJustEnded;
    if (!g_swap.active && guid >= 0 && ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 6.0f)) {
        g_swap.active = true; g_swap.card = card; g_swap.slot = pi; g_swap.guid = guid;
        Snd(kSndClick);
    }
    const Game::HeroDragState& drag = Game::HeroDrag();
    if (rclick && guid >= 0 && !drag.active && !g_swap.active) {   // 우클릭: 영웅 정보창 (영웅 목록 우클릭과 같음)
        if (Game::ShowHeroInfo(guid)) { Snd(kSndClick); g_open = false; g_editing = -1; }
        else Snd(kSndInvalid);
        rclick = false;
    }
    if (lclick && guid >= 0 && !drag.active) {   // 좌클릭: 칸에서 빼기
        d.guid[pi] = -1; g_dirty[card] = true; guid = -1;
        Snd(kSndRemove);
    }
    bool swapSrc = g_swap.active && g_swap.card == card && g_swap.slot == pi;
    bool swapOver = g_swap.active && g_swap.card == card && Hover(x, y, kSlotSize, kSlotSize);
    if (swapOver) { g_swapTarget = pi; g_swapTargetCard = card; }
    bool dropHere = (drag.active && Hover(x, y, kSlotSize, kSlotSize)) || (swapOver && !swapSrc);
    if (drag.active && dropHere) { g_dropCard = card; g_dropSlot = pi; }

    Game::HeroInfo h;
    HeroState st = StateOf(guid, &h);
    Image(hov || dropHere ? kSlotHi : kSlotBg, x, y, kSlotSize, kSlotSize);
    if (st == HeroState::Empty) {
        ImVec2 c = P(x + kSlotSize * 0.5f, y + kSlotSize * 0.5f);
        float a = 8 * S(), t = 2.0f * S();
        ImU32 colr = (hov || dropHere) ? IM_COL32(220, 200, 150, 255) : IM_COL32(100, 92, 78, 255);
        DL->AddLine(ImVec2(c.x - a, c.y), ImVec2(c.x + a, c.y), colr, t);
        DL->AddLine(ImVec2(c.x, c.y - a), ImVec2(c.x, c.y + a), colr, t);
    } else if (st == HeroState::Gone) {
        Text(FontId::Body, x + kSlotSize * 0.5f - 6, y + kSlotSize * 0.5f - 12, kColDim, "?");
    } else {
        DrawPortrait(h, x, y, kSlotSize, st != HeroState::Ok || swapSrc);
        if (st == HeroState::Missing) {           // 게임의 빨간 물음표 (행방불명)
            float m = kSlotSize * 0.62f;
            Image(kMissing, x + (kSlotSize - m) * 0.5f, y + (kSlotSize - m) * 0.5f, m, m);
            Image(kFrameBad, x, y, kSlotSize, kSlotSize);
        } else if (st == HeroState::Busy) {
            Image(kLocked, x, y, kSlotSize, kSlotSize);
            Image(kFrameBad, x, y, kSlotSize, kSlotSize);
        }
    }
    if (dropHere) Image(kFrameOk, x, y, kSlotSize, kSlotSize);
    else          DL->AddRect(P(x, y), P(x + kSlotSize, y + kSlotSize), IM_COL32(70, 60, 45, 255), 0, 0, 1.0f * S());

    if (hov && !drag.active && !g_swap.active) {
        if (st == HeroState::Empty) Tooltip(Txt::T("empty_slot"), Txt::T("hero_empty_slot_body"));
        else {
            std::string title = h.name.empty() ? std::string(Txt::T("unknown_hero")) : h.name;
            if (!h.classId.empty()) title += "  (" + Game::ClassName(h.classId) + ")";
            std::string body = std::string(StateText(st)) + "   " + Txt::T("hero_slot_help");
            Tooltip(title.c_str(), body.c_str(), st == HeroState::Ok ? 0 : kColWarn);
        }
    }
}

void DrawCard(int i, float x, float y) {
    HeroPresets::Preset& d = g_draft[i];
    bool hov = Hover(x, y, kCardW, kCardH);
    DL->AddRectFilled(P(x, y), P(x + kCardW, y + kCardH), IM_COL32(0, 0, 0, 120));
    DL->AddRect(P(x, y), P(x + kCardW, y + kCardH),
                g_dirty[i] ? IM_COL32(200, 130, 60, 220) : hov ? IM_COL32(150, 125, 75, 220) : IM_COL32(80, 66, 44, 180), 0, 0, 1.0f * S());

    char num[8]; snprintf(num, sizeof num, "%d", i + 1);
    Text(FontId::Body, x + 10, y + 8, kColDim, num);
    float nx = x + 34, nw = kSlotX - 40;
    if (g_editing == i) {
        ImGui::SetCursorScreenPos(P(nx - 2, y + 5));
        ImGui::SetNextItemWidth(nw * S());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 16, 12, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColTitle);
        if (g_editFocus) { ImGui::SetKeyboardFocusHere(); g_editFocus = false; }
        bool done = ImGui::InputText("##hrename", g_editBuf, sizeof g_editBuf,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        bool lost = ImGui::GetFrameCount() > g_editStart + 2 && !ImGui::IsItemActive();
        ImGui::PopStyleColor(2);
        if (done || lost) {
            snprintf(d.name, sizeof d.name, "%s", g_editBuf);
            snprintf(HeroPresets::Get(i).name, sizeof d.name, "%s", g_editBuf);
            HeroPresets::MarkDirty();
            g_editing = -1;
        }
    } else {
        bool nameHov = false;
        char nid[16]; snprintf(nid, sizeof nid, "##hname%d", i);
        if (HitButton(nid, nx - 2, y + 4, nw, 28, &nameHov)) {
            g_editing = i; g_editFocus = true; g_editStart = ImGui::GetFrameCount();
            snprintf(g_editBuf, sizeof g_editBuf, "%s", d.name);
            Snd(kSndClick);
        }
        // 이름이 길면 먼저 글자를 줄여 맞추고, 그래도 넘치면 끝을 ".." 으로 자름
        std::string nm = d.name[0] ? d.name : Txt::T("empty_lineup");
        const float lim = nw / 0.7f;
        if (TextW(FontId::Body, nm.c_str()) > lim) {
            while (nm.size() > 1 && TextW(FontId::Body, (nm + "..").c_str()) > lim) {
                size_t k = nm.size() - 1; while (k > 0 && (nm[k] & 0xC0) == 0x80) --k; nm.resize(k);
            }
            nm += "..";
        }
        Text(FontId::Body, nx, y + 8, d.name[0] ? (nameHov ? IM_COL32(240, 220, 150, 255) : kColTitle) : kColDim, nm.c_str(), true, nw);
        if (nameHov && !Game::HeroDrag().active) Tooltip(Txt::T("rename"), Txt::T("rename_lineup_body"));
    }

    // 상태 줄
    int count = 0; for (int s = 0; s < 4; ++s) if (d.guid[s] >= 0) ++count;
    std::string why;
    bool usable = Usable(HeroPresets::Get(i), &why);
    char line[64];
    if (g_dirty[i]) Text(FontId::Body, x + 12, y + 44, kColWarn, Txt::T("unsaved"), true, kSlotX - 18);
    else if (count == 0) Text(FontId::Body, x + 12, y + 44, kColDim, Txt::T("no_heroes"), true, kSlotX - 18);
    else {
        snprintf(line, sizeof line, "%s", Txt::F("hero_count", { Txt::Num(count) }).c_str());
        Text(FontId::Body, x + 12, y + 44, kColSub, line, true, kSlotX - 18);
    }
    if (!g_dirty[i] && count > 0)
        Text(FontId::Body, x + 12, y + 72, usable ? kColOk : kColWarn, usable ? Txt::T("st_ok") : Txt::T("not_ready"), true, kSlotX - 18);

    for (int c = 0; c < 4; ++c) DrawSlot(i, c, x + kSlotX + c * kSlotPitch, y + (kCardH - kSlotSize) * 0.5f);

    float bx = x + kSlotX + 4 * kSlotPitch + 6, bw = kCardW - (bx - x) - 8, bh = 28;
    char bid[32];
    bool lh = false;
    snprintf(bid, sizeof bid, "##hload%d", i);
    bool canLoad = usable && !g_dirty[i];
    if (TextButton(bid, Txt::T("load"), bx, y + 6, bw, bh, canLoad, false, &lh)) {
        Game::PartyResult r = Game::LoadParty(HeroPresets::Get(i).guid);
        if (r.ok) {
            Snd(kSndEquip);
            Toast(Txt::F("lineup_loaded", { Txt::Num(i + 1) }).c_str());
        } else {
            Snd(kSndInvalid);
            Toast(r.error.c_str(), true);
        }
    }
    if (lh && !Game::HeroDrag().active) {
        if (g_dirty[i]) Tooltip(Txt::T("save_first"), Txt::T("save_first_body"), kColWarn);
        else if (!usable) Tooltip(Txt::T("cant_load"), why.c_str(), kColWarn);
        else Tooltip(Txt::T("form_party"), Txt::T("form_party_body"));
    }
    snprintf(bid, sizeof bid, "##hsave%d", i);
    if (TextButton(bid, Txt::T("save"), bx, y + 38, bw, bh, g_dirty[i], g_dirty[i])) {
        HeroPresets::Get(i) = d;
        HeroPresets::MarkDirty();
        g_dirty[i] = false;
        Snd(kSndEquip);
        Toast(Txt::F("lineup_saved", { Txt::Num(i + 1) }).c_str());
    }
    snprintf(bid, sizeof bid, "##hclear%d", i);
    bool empty = !d.name[0] && d.Empty();
    if (TextButton(bid, Txt::T("clear"), bx, y + 70, bw, bh, !empty)) {
        d = HeroPresets::Preset{};
        HeroPresets::Get(i) = d;
        HeroPresets::MarkDirty();
        g_dirty[i] = false;
        if (g_editing == i) g_editing = -1;
        Snd(kSndRemove);
        Toast(Txt::T("lineup_cleared"));
    }
}

void DrawPanel(const Game::Xform& anchorXf, float leftScreenX, float slotScreenY) {
    float s = anchorXf.Scale();
    XF = Game::Xform{};
    XF.m[0] = XF.m[3] = s;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float x = leftScreenX - 60 * s;
    float y = slotScreenY - (kPanelH + 30) * s;
    x = std::clamp(x, 4.0f, std::max(4.0f, ds.x - kPanelW * s - 4));
    y = std::max(4.0f - kBgY * s, y);
    XF.m[4] = x; XF.m[5] = y;

    ImVec2 p0 = P(0, kBgY), p1 = P(kPanelW, kBgY + kPanelH);
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##hero_preset_panel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoFocusOnAppearing);
    DL = ImGui::GetWindowDrawList();

    Image(kBg, 0, kBgY);
    // 뒷배경: 용어사전의 등불 든 집사 (카드 뒤로 은은하게)
    DL->PushClipRect(P(14, 136), P(kPanelW - 14, kBgY + kPanelH - 14), true);
    Image("campaign/town/glossary/glossary_character.png", kPanelW - 660, kBgY + kPanelH - 630, 660, 616, IM_COL32(255, 255, 255, 200));
    DL->PopClipRect();
    // 아이콘: 장신구 가방 아이콘과 같은 틀에 집사 초상화
    Image("campaign/town/realm_inventory/realm_inventory.icon.png", kIconX, kIconY, kIconSize, kIconSize);
    Image(kButler, kIconX + 9, kIconY + 9, kIconSize - 18, kIconSize - 18);
    Text(FontId::Title, kTitleX, kTitleY, kColTitle, Txt::T("party_title"), true, kCloseX - kTitleX - 12);
    Text(FontId::Body, kSubX, kSubY, kColSub, Txt::T("party_sub"), true, kPanelW - kSubX - 24);

    bool hov = false;
    if (HitButton("##hp_close", kCloseX, kCloseY, kBtn, kBtn, &hov)) { g_open = false; g_editing = -1; Snd(kSndClose); }
    Image(kClose, kCloseX, kCloseY, -1, -1, hov ? IM_COL32_WHITE : IM_COL32(225, 225, 225, 255));
    if (hov) Tooltip(Txt::T("close"), Txt::T("close_body"));

    int maxScroll = std::max(0, kCount - kVisible);
    bool dragging = Game::HeroDrag().active;
    if (Hover(0, kBgY, kPanelW, kPanelH) && ImGui::GetIO().MouseWheel != 0 && g_editing < 0) {
        int prev = g_scroll;
        g_scroll = std::clamp(g_scroll - (ImGui::GetIO().MouseWheel > 0 ? 1 : -1), 0, maxScroll);
        if (g_scroll != prev) Snd(g_scroll < prev ? kSndScrollUp : kSndScrollDn);
    }
    if (g_scroll > 0) {
        bool over = dragging && Hover(kUpX, kUpY, 62, 49);
        if (HitButton("##hup", kUpX, kUpY, 62, 49, &hov) || (over && ImGui::GetFrameCount() % 20 == 0)) { --g_scroll; Snd(kSndScrollUp); }
        Image(kUp, kUpX, kUpY, -1, -1, (hov || over) ? IM_COL32_WHITE : IM_COL32(220, 220, 220, 255));
    }
    if (g_scroll < maxScroll) {
        bool over = dragging && Hover(kDownX, kDownY, 62, 49);
        if (HitButton("##hdown", kDownX, kDownY, 62, 49, &hov) || (over && ImGui::GetFrameCount() % 20 == 0)) { ++g_scroll; Snd(kSndScrollDn); }
        Image(kDown, kDownX, kDownY, -1, -1, (hov || over) ? IM_COL32_WHITE : IM_COL32(220, 220, 220, 255));
    }

    g_dropCard = g_dropSlot = -1;
    ImVec2 m0 = ImGui::GetIO().MousePos;
    for (int r = 0; r < kVisible; ++r) {
        int i = g_scroll + r;
        if (i < kCount) DrawCard(i, kGridX, kGridY + r * (kCardH + kCardGap));
    }

    if (g_toastTime > 0) {
        g_toastTime -= ImGui::GetIO().DeltaTime;
        float a = std::min(1.0f, g_toastTime / 0.5f);
        float tw = TextWFit(FontId::Body, g_toast, kPanelW - 40);
        ImU32 c = g_toastWarn ? IM_COL32(230, 150, 70, (int)(255 * a)) : IM_COL32(220, 200, 150, (int)(255 * a));
        Text(FontId::Body, (kPanelW - tw) * 0.5f, 156, c, g_toast, true, kPanelW - 40);
    }

    // 프리셋 안에서 자리 바꾸기: 끄는 그림 + 놓기
    g_swapJustEnded = false;
    if (g_swap.active) {
        Game::HeroInfo h;
        if (Game::GetHero(g_swap.guid, &h)) {
            ImDrawList* saveDL = DL; Game::Xform saveXF = XF;
            DL = ImGui::GetForegroundDrawList();
            XF.m[4] = m0.x - kSlotSize * 0.5f * s; XF.m[5] = m0.y - kSlotSize * 0.5f * s;
            DrawPortrait(h, 0, 0, kSlotSize, false);
            DL->AddRect(P(0, 0), P(kSlotSize, kSlotSize), IM_COL32(205, 170, 100, 255), 0, 0, 1.5f * s);
            DL = saveDL; XF = saveXF;
        }
        if (!ImGui::IsMouseDown(0)) {
            HeroPresets::Preset& d = g_draft[g_swap.card];
            if (g_swapTargetCard == g_swap.card && g_swapTarget >= 0 && g_swapTarget != g_swap.slot) {
                std::swap(d.guid[g_swap.slot], d.guid[g_swapTarget]);
                g_dirty[g_swap.card] = true;
                Snd(kSndEquip);
            }
            g_swap.active = false; g_swapJustEnded = true;
        }
    }
    g_swapTarget = g_swapTargetCard = -1;

    // 끌고 있는 영웅 초상화를 패널 위에 다시 그림 (게임이 그린 그림은 패널에 가려짐)
    const Game::HeroDragState& drag = Game::HeroDrag();
    ImVec2 m = ImGui::GetIO().MousePos;
    if (drag.active && m.x >= p0.x && m.x < p1.x && m.y >= p0.y && m.y < p1.y) {
        Game::HeroInfo h;
        if (Game::GetHero(drag.guid, &h)) {
            ImDrawList* saveDL = DL; Game::Xform saveXF = XF;
            DL = ImGui::GetForegroundDrawList();
            XF.m[4] = m.x - kSlot * 0.5f * s; XF.m[5] = m.y - kSlot * 0.5f * s;
            DrawPortrait(h, 0, 0, kSlot, false);
            DL = saveDL; XF = saveXF;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void DrawButton(const Game::Xform& bxf) {
    XF = bxf;
    ImVec2 p0 = P(-8, -8), p1 = P(kSlot + 8, kSlot + 8);
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##hero_preset_btn2", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    DL = ImGui::GetWindowDrawList();
    bool hov = false;
    if (HitButton("##hp_open", 0, 0, kSlot, kSlot, &hov) && g_alpha >= 1.0f && !Game::HeroDrag().active) {
        g_open = !g_open; g_editing = -1;
        Snd(g_open ? kSndOpen : kSndClose);
        if (g_open) { LoadDrafts(); PruneDead(); }
    }
    DrawButlerButton(0, 0, kSlot, hov && g_alpha >= 1.0f, g_open);
    if (hov && g_alpha >= 1.0f && !Game::HeroDrag().active) Tooltip(Txt::T("party_title"), g_open ? Txt::T("party_btn_close") : Txt::T("party_btn_body"));
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace

void Draw() {
    g_hoverPrev = g_hoverNow; g_hoverNow = 0;
    int prof = Game::CurrentProfile();
    if (prof != HeroPresets::Profile() || ImGui::GetFrameCount() <= 1) {
        HeroPresets::SetProfile(prof);
        g_open = false; g_editing = -1;
        LoadDrafts();
    }
    HeroPresets::SaveIfDirty();
    const Game::PartyState& party = Game::Party();
    float dt = ImGui::GetIO().DeltaTime;
    static float vis = 0;
    // 영지 지도에서만 (원정 준비 화면에서는 숨김)
    bool onMap = party.visible && Game::QuestSelectVisible();
    if (!onMap) { vis = 0; g_shownTime = 0; g_swap.active = false; if (g_open) { g_open = false; g_editing = -1; } return; }
    if (!g_open) g_swap.active = false;
    // [원정 시작]을 누르면 게임이 맨 위층에 화면 전환을 그리기 시작함 → 즉시 숨김.
    // 확인 문구(장신구 미착용 등)가 뜬 경우는 문구 뒤에 그대로 둠
    static float leaveTimer = 0;
    if (Game::TownFadingOut()) leaveTimer = 0.3f;
    if (leaveTimer > 0) {
        leaveTimer -= dt;
        vis = 0;
        g_shownTime = 0;
        if (g_open) { g_open = false; g_editing = -1; }
    } else {
        // 영지 지도가 열릴 때: 잠시 뒤 서서히 나타남
        g_shownTime += dt;
        vis = std::clamp((g_shownTime - 0.6f) / 0.5f, 0.0f, 1.0f);
    }
    g_alpha = vis;
    // 자막이 뜨면 버튼이 자막을 가리지 않도록 잠시 숨김 (창이 열려 있을 때는 그대로)
    static float subHide = 0;
    float target = (Game::SubtitleAlpha() > 0 && !g_open && !Game::UnderSubtitle()) ? 1.0f : 0.0f;   // 자막 아래에 그려지면 게임이 어둡게 해 줌
    float step = dt * 4.0f;
    subHide = subHide < target ? std::min(target, subHide + step) : std::max(target, subHide - step);
    g_alpha = std::min(g_alpha, 1.0f - subHide);
    if (Game::ScreenVisibility() < 0.99f || Game::ModalAmount() > 0) g_alpha = std::min(g_alpha, 0.999f);   // 전환 중엔 누를 수 없게
    if (g_alpha <= 0) return;

    // 보이는 슬롯을 화면 x 순서로 정렬
    int idx[4], n = 0;
    for (int i = 0; i < 4; ++i) if (party.slotSeen[i]) idx[n++] = i;
    if (n == 0) { g_open = false; return; }
    std::sort(idx, idx + n, [&](int a, int b) { return party.slotXf[a].m[4] < party.slotXf[b].m[4]; });
    if (n == 4) for (int i = 0; i < 4; ++i) g_order[i] = idx[i];
    const Game::Xform& right = party.slotXf[idx[n - 1]];
    const Game::Xform& left = party.slotXf[idx[0]];
    float s = right.Scale();
    float pitch = n >= 2 ? (right.m[4] - left.m[4]) / (n - 1) : 93 * s;

    Game::Xform bxf = right;
    bxf.m[4] += pitch + 22 * s;                  // 오른쪽 끝 슬롯 바로 옆 (빨간 상자 자리)
    DrawButton(bxf);

    if (g_open) {
        if (++g_pruneFrame >= 120) { g_pruneFrame = 0; PruneDead(); }
        g_alpha = 1.0f;
        DrawPanel(right, left.m[4], left.m[5]);
    }
}

bool HandleEvent(const SDL_Event* ev) {
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) {
        if (g_editing >= 0) { g_editing = -1; return true; }
        if (g_open) { g_open = false; Snd(kSndClose); return true; }
    }
    // 영웅 목록에서 끌던 영웅을 칸 위에서 놓음 → 칸에 넣고, 게임에는 그대로 전달
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT && g_open && g_dropCard >= 0) {
        const Game::HeroDragState& d = Game::HeroDrag();
        Game::HeroInfo h;
        if (d.active && Game::GetHero(d.guid, &h)) {
            HeroPresets::Preset& p = g_draft[g_dropCard];
            for (int s = 0; s < 4; ++s) if (p.guid[s] == d.guid) p.guid[s] = -1;   // 같은 영웅은 한 칸만
            p.guid[g_dropSlot] = d.guid;
            g_dirty[g_dropCard] = true;
            Snd(kSndEquip);
            Log("영웅 프리셋 %d 칸 %d ← %s (guid %d, %s)", g_dropCard + 1, g_dropSlot, h.name.c_str(), d.guid, h.classId.c_str());
        }
        g_dropCard = g_dropSlot = -1;
    }
    return false;
}

bool DragInProgress() { return Game::HeroDrag().active; }

} // namespace UiHeroPreset

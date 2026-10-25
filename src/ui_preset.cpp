// 장신구 프리셋 UI
//  - 장신구 가방: 정렬 버튼 줄의 프리셋 버튼 → 가방 왼쪽에 같은 모양의 프리셋 패널 (저장/비우기, 가방에서 끌어다 넣기)
//  - 영웅 정보창: 색상 버튼 위의 프리셋 버튼 → 작은 불러오기 창 (프리셋 1~10, 장신구 그림 포함)
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include "imgui.h"
#include "ui_preset.h"
#include "game.h"
#include "text.h"
#include "gfx.h"
#include "presets.h"
#include "log.h"

using Gfx::FontId;

namespace UiPreset {
namespace {

// ---- 패널 좌표 (가방 패널 기준, realm_inventory.layout.darkest 와 실제 화면 측정값) ----
constexpr float kPanelW = 667, kPanelH = 780;
constexpr float kPanelGap = 16;                   // 가방과 프리셋 패널 사이 간격
constexpr float kBgY = -4;
constexpr float kIconX = 16, kIconY = 16;
constexpr float kTitleX = 148, kTitleY = 46;
constexpr float kSubX = 150, kSubY = 100;
constexpr float kCloseX = 610, kCloseY = 22;
constexpr float kBtn = 32;
constexpr float kGridX = 30, kGridY = 195;
constexpr float kCardW = 272, kCardH = 168, kCardGapX = 16, kCardGapY = 4;
constexpr int   kCols = 2, kVisibleRows = 3;
constexpr float kUpX = 590, kUpY = 150, kDownX = 589, kDownY = 713;
constexpr float kSlotScale = 0.82f;

const ImU32 kColTitle = IM_COL32(200, 180, 110, 255);
const ImU32 kColSub   = IM_COL32(174, 172, 162, 255);
const ImU32 kColDim   = IM_COL32(120, 115, 105, 255);
const ImU32 kColText  = IM_COL32(215, 205, 185, 255);
const ImU32 kColWarn  = IM_COL32(230, 150, 70, 255);

const char* kBg      = "campaign/town/realm_inventory/realminv_bg.png";
const char* kIcon    = "campaign/town/realm_inventory/realm_inventory.icon.png";
const char* kBtnBase = "campaign/town/realm_inventory/realm_inventory_sort_class.png";
const char* kBtnSel  = "campaign/town/realm_inventory/realm_inventory_sort_current_overlay.png";
const char* kClose   = "shared/progression/progression_close.png";
const char* kUp      = "campaign/town/realm_inventory/realm_inventory_uparrow.png";
const char* kDown    = "campaign/town/realm_inventory/realm_inventory_downarrow.png";
const char* kHover   = "overlays/eqp_mouseover.png";
const char* kUnavail = "overlays/eqp_unavailable_mouseover.png";

// ---- 상태 ----
bool  g_open = false;                       // 가방 옆 프리셋 패널
int   g_scrollRow = 0;
int   g_editing = -1;
char  g_editBuf[64] = {};
bool  g_editFocus = false;
int   g_editStart = 0;
Presets::Preset g_draft[Presets::kCount];   // 저장 전 편집본
bool  g_dirty[Presets::kCount] = {};
int   g_dropCard = -1, g_dropSlot = -1;     // 드래그 중 마우스 아래 칸
char  g_toast[200] = {};
float g_toastTime = 0;

bool  g_pickOpen = false;                   // 영웅 정보창의 불러오기 창
void* g_pickHero = nullptr;
char  g_pickMsg[200] = {};
float g_pickMsgTime = 0;
bool  g_pickMsgWarn = false;

// 그리기 대상 (패널 좌표 → 화면)
Game::Xform XF;
ImDrawList* DL = nullptr;

ImVec2 P(float x, float y) { return XF.ToScreen(ImVec2(x, y)); }
float  S() { return XF.Scale(); }

void Toast(const char* msg) { snprintf(g_toast, sizeof g_toast, "%s", msg); g_toastTime = 3.5f; }

void ImageTex(const Gfx::Texture& t, float x, float y, float w, float h, ImU32 tint = IM_COL32_WHITE) {
    if (!t.ok()) return;
    if (w < 0) { w = (float)t.w; h = (float)t.h; }
    DL->AddImage(t.tex(), P(x, y), P(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
}
void Image(const char* rel, float x, float y, float w = -1, float h = -1, ImU32 tint = IM_COL32_WHITE) {
    ImageTex(Gfx::GameTex(rel), x, y, w, h, tint);
}
void Text(FontId f, float x, float y, ImU32 col, const char* s, bool shadow = true, float maxW = 0) {
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

// ---- 효과음 (게임과 같은 소리) ----
const char* kSndHover   = "/ui/town/button_mouse_over";
const char* kSndClick   = "/ui/town/button_click";
const char* kSndBack    = "/ui/town/button_click_back";
const char* kSndInvalid = "/ui/town/button_invalid";
const char* kSndEquip   = "/ui/town/character_equip";
const char* kSndUnequip = "/ui/dungeon/trink_unequip";
const char* kSndOpen    = "/ui/town/trinket_open";
const char* kSndClose   = "/ui/town/trinket_close";
const char* kSndPageOpen  = "/ui/town/page_open";
const char* kSndPageClose = "/ui/town/page_close";
const char* kSndScrollUp  = "/ui/shared/button_scroll_up";
const char* kSndScrollDn  = "/ui/shared/button_scroll_down";
void Snd(const char* e) { Game::PlaySound(e); }
ImGuiID g_hoverNow = 0, g_hoverPrev = 0;   // 마우스가 새 버튼에 올라갈 때만 소리

bool Hover(float x, float y, float w, float h) { return ImGui::IsMouseHoveringRect(P(x, y), P(x + w, y + h), false); }

bool HitButton(const char* id, float x, float y, float w, float h, bool* hovered = nullptr) {
    ImGui::SetCursorScreenPos(P(x, y));
    ImGui::InvisibleButton(id, ImVec2(w * S(), h * S()), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hv = ImGui::IsItemHovered();
    if (hv) {
        ImGuiID iid = ImGui::GetItemID();
        if (iid != g_hoverPrev && !Game::Drag().active) Snd(kSndHover);
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
    if (p0.y + h > ds.y) p0.y = ds.y - h;
    ImVec2 p1(p0.x + w, p0.y + h);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(p0, p1, IM_COL32(12, 10, 8, 240));
    fg->AddRect(p0, p1, IM_COL32(120, 100, 60, 255), 0, 0, 1.5f * s);
    Gfx::DrawStr(fg, FontId::Body, ImVec2(p0.x + 10 * s, p0.y + 7 * s), s, kColTitle, title);
    if (body) Gfx::DrawStr(fg, FontId::Body, ImVec2(p0.x + 10 * s, p0.y + 11 * s + tw.y), s, bodyCol ? bodyCol : kColSub, body);
}

// 정렬 버튼 모양의 프리셋 버튼 (게임 버튼 틀 + 장신구 칸 두 개 그림)
void DrawPresetIcon(float x, float y, bool hovered, bool active, float sz = 32) {
    float k = sz / 32.0f;      // 32 기준 그림을 sz 크기로
    auto Q = [&](float a, float b) { return P(x + a * k, y + b * k); };
    Image(kBtnBase, x, y, sz, sz);
    DL->AddRectFilled(Q(5, 5), Q(27, 27), IM_COL32(0, 0, 0, 255));
    ImU32 c = hovered ? IM_COL32(255, 245, 220, 255) : IM_COL32(205, 205, 205, 255);
    float t = 1.6f * S() * k;
    DL->AddRect(Q(8.5f, 8.5f),  Q(15.0f, 23.5f), c, 0, 0, t);
    DL->AddRect(Q(17.0f, 8.5f), Q(23.5f, 23.5f), c, 0, 0, t);
    DL->AddCircleFilled(Q(11.75f, 13.0f), 1.6f * S() * k, c);
    DL->AddCircleFilled(Q(20.25f, 13.0f), 1.6f * S() * k, c);
    if (hovered) DL->AddRectFilled(Q(2, 2), Q(30, 30), IM_COL32(255, 230, 170, 28));
    if (active) { const Gfx::Texture& t = Gfx::GameTex(kBtnSel); if (t.ok()) Image(kBtnSel, x - 8 * k, y - 8 * k, t.w * k, t.h * k); }
}

bool TextButton(const char* id, const char* label, float x, float y, float w, float h, bool enabled = true, bool highlight = false) {
    bool hov = false;
    bool clicked = HitButton(id, x, y, w, h, &hov) && enabled;
    ImU32 fill   = !enabled ? IM_COL32(20, 18, 15, 230) : hov ? IM_COL32(58, 42, 24, 240) : IM_COL32(28, 22, 16, 235);
    ImU32 border = !enabled ? IM_COL32(60, 55, 45, 255) : (hov || highlight) ? IM_COL32(205, 170, 100, 255) : IM_COL32(100, 82, 52, 255);
    DL->AddRectFilled(P(x, y), P(x + w, y + h), fill);
    DL->AddRect(P(x, y), P(x + w, y + h), border, 0, 0, (highlight ? 2.0f : 1.5f) * S());
    float tw = TextWFit(FontId::Body, label, w - 8);
    Text(FontId::Body, x + (w - tw) * 0.5f, y + (h - Gfx::FontLineHeight(FontId::Body)) * 0.5f + 1,
         enabled ? (hov ? IM_COL32(255, 240, 200, 255) : kColText) : kColDim, label, false, w - 8);
    return clicked;
}

// 장신구 칸 (틀 + 그림). 틀은 등급별 그림, 안쪽이 불투명하므로 틀 → 그림 순서
void DrawTrinket(const char* id, float x, float y, float w, float h, bool dim) {
    if (!id[0]) {
        Image("panels/icons_equip/trinket/rarity_common.png", x, y, w, h, IM_COL32(150, 150, 150, 255));
        return;
    }
    const Game::TrinketInfo& ti = Game::TrinketById(id);
    std::string frame = "panels/icons_equip/trinket/rarity_" + (ti.rarity.empty() ? std::string("common") : ti.rarity) + ".png";
    const Gfx::Texture* ftp = nullptr;
    if (!ti.root.empty()) { const Gfx::Texture& t = Gfx::TexIn(ti.root, frame.c_str()); if (t.ok()) ftp = &t; }
    const Gfx::Texture& ft = ftp ? *ftp : Gfx::ModTex(frame.c_str());
    ImageTex(ft.ok() ? ft : Gfx::GameTex("panels/icons_equip/trinket/rarity_common.png"), x, y, w, h, dim ? IM_COL32(140, 140, 140, 255) : IM_COL32_WHITE);
    // 아이콘: 장신구가 정의된 폴더(모드/DLC)에서 먼저 찾고, 없으면 게임 → 모든 모드 폴더 순서 (인벤토리와 같은 그림)
    std::string icon = std::string("panels/icons_equip/trinket/inv_trinket+") + id + ".png";
    const Gfx::Texture* itp = nullptr;
    if (!ti.root.empty()) { const Gfx::Texture& t = Gfx::TexIn(ti.root, icon.c_str()); if (t.ok()) itp = &t; }
    if (!itp) itp = &Gfx::ModTex(icon.c_str());
    const Gfx::Texture& it = *itp;
    if (it.ok()) ImageTex(it, x, y, w, h, dim ? IM_COL32(140, 140, 140, 255) : IM_COL32_WHITE);
    else {   // 그림을 끝내 못 찾으면 이름 첫 글자라도 표시
        std::string nm = ti.name.empty() ? std::string(id) : ti.name;
        Text(FontId::Body, x + 4, y + h * 0.5f - 10, kColDim, nm.substr(0, nm[0] & 0x80 ? 3 : 1).c_str(), false);
    }
}

std::string TrinketLabel(const char* id) {
    const Game::TrinketInfo& ti = Game::TrinketById(id);
    return ti.name.empty() ? std::string(id) : ti.name;
}

// 판매/분실된 장신구를 프리셋에서 자동으로 뺌
void PruneMissing() {
    if (Presets::Profile() < 0) return;      // 어느 영지인지 모르면 자동 정리하지 않음
    bool ok = false;
    auto owned = Game::OwnedCounts(&ok);
    if (!ok) return;
    int removed = 0;
    for (int i = 0; i < Presets::kCount; ++i) {
        for (int s = 0; s < 2; ++s) {
            Presets::Preset& p = Presets::Get(i);
            if (p.trinket[s][0] && owned.find(Game::Hash(p.trinket[s])) == owned.end()) {
                Log("프리셋 %d: 더 이상 없는 장신구 제거 (%s)", i + 1, p.trinket[s]);
                p.trinket[s][0] = 0; ++removed; Presets::MarkDirty();
            }
            Presets::Preset& d = g_draft[i];
            if (d.trinket[s][0] && owned.find(Game::Hash(d.trinket[s])) == owned.end()) d.trinket[s][0] = 0;
        }
    }
    if (removed) {
        Toast(Txt::F("removed_trinkets", { Txt::Num(removed) }).c_str());
    }
}

void LoadDrafts() {
    for (int i = 0; i < Presets::kCount; ++i) { g_draft[i] = Presets::Get(i); g_dirty[i] = false; }
}

// ======================= 가방 옆 프리셋 패널 =======================

// 끌고 있는 장신구를 이 칸에 넣으면 같은 프리셋의 다른 칸과 충돌하는지
// 1 = 다른 직업 전용, 2 = 같은 장신구 중복
int DropConflict(int card, int slot) {
    const Game::DragState& drag = Game::Drag();
    if (!drag.active || !drag.isTrinket) return 0;
    const char* other = g_draft[card].trinket[1 - slot];
    if (!other[0]) return 0;
    if (Game::Hash(other) == drag.hash) return 2;
    return Game::ClassesCompatible(Game::Trinket(drag.hash), Game::TrinketById(other)) ? 0 : 1;
}

void DrawSlot(int card, int slot, float x, float y) {
    Presets::Preset& d = g_draft[card];
    const char* id = d.trinket[slot];
    float w = 72 * kSlotScale, h = 144 * kSlotScale;
    bool hov = false;
    char bid[32]; snprintf(bid, sizeof bid, "##slot%d_%d", card, slot);
    bool lclick = HitButton(bid, x, y, w, h, &hov);
    bool rclick = hov && ImGui::IsItemClicked(1);
    const Game::DragState& drag = Game::Drag();
    if ((lclick || rclick) && id[0] && !drag.active) {     // 클릭: 칸에서 빼기
        d.trinket[slot][0] = 0; g_dirty[card] = true;
        Snd(kSndUnequip);
        id = d.trinket[slot];
    }
    bool dragging = drag.active && drag.isTrinket;
    int conflictKind = dragging ? DropConflict(card, slot) : 0;
    bool conflict = conflictKind != 0;
    bool dropHere = dragging && Hover(x, y, w, h);
    if (dropHere) { g_dropCard = card; g_dropSlot = slot; }

    DL->AddRectFilled(P(x + 3, y + 3), P(x + w - 3, y + h - 3), IM_COL32(8, 8, 8, 255));
    DrawTrinket(id, x, y, w, h, conflict);
    if (!id[0] && !conflict) {
        ImVec2 c = P(x + w * 0.5f, y + h * 0.5f);
        float a = 8 * S(), t = 2.0f * S();
        ImU32 col = (hov || dropHere) ? IM_COL32(220, 200, 150, 255) : IM_COL32(110, 100, 85, 255);
        DL->AddLine(ImVec2(c.x - a, c.y), ImVec2(c.x + a, c.y), col, t);
        DL->AddLine(ImVec2(c.x, c.y - a), ImVec2(c.x, c.y + a), col, t);
    }
    float ox = x - 23 * kSlotScale, oy = y - 21 * kSlotScale, ow = 118 * kSlotScale, oh = 187 * kSlotScale;
    if (conflict)                         // 직업이 맞지 않는 칸: 게임의 빨간 X
        Image(kUnavail, ox, oy, ow, oh, dropHere ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 200));
    else if (dragging)                    // 넣을 수 있는 칸 표시
        Image(kHover, ox, oy, ow, oh, dropHere ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90));
    else if (hov)
        Image(kHover, ox, oy, ow, oh, IM_COL32(255, 255, 255, 170));

    if (dropHere && conflictKind == 2) {
        Tooltip(Txt::T("dup_title"), Txt::T("dup_body"), kColWarn);
    } else if (dropHere && conflict) {
        const Game::TrinketInfo& o = Game::TrinketById(d.trinket[1 - slot]);
        std::string body = Txt::F("class_body", { o.classLabel });
        Tooltip(Txt::T("class_title"), body.c_str(), kColWarn);
    }
    if (hov && !drag.active) {
        if (id[0]) {
            const Game::TrinketInfo& ti = Game::TrinketById(id);
            std::string body = ti.valid ? (ti.classLabel.empty() ? std::string(Txt::T("click_remove"))
                                                                 : ti.classLabel + "   " + Txt::T("click_remove"))
                                        : std::string(Txt::T("trinket_unknown"));
            Tooltip(TrinketLabel(id).c_str(), body.c_str());
        } else {
            Tooltip(Txt::T("empty_slot"), Txt::T("trinket_empty_slot_body"));
        }
    }
}

void DrawCard(int i, float x, float y) {
    Presets::Preset& d = g_draft[i];
    bool hov = Hover(x, y, kCardW, kCardH);
    DL->AddRectFilled(P(x, y), P(x + kCardW, y + kCardH), IM_COL32(0, 0, 0, 120));
    DL->AddRect(P(x, y), P(x + kCardW, y + kCardH),
                g_dirty[i] ? IM_COL32(200, 130, 60, 220) : hov ? IM_COL32(150, 125, 75, 220) : IM_COL32(80, 66, 44, 180), 0, 0, 1.0f * S());

    char num[8]; snprintf(num, sizeof num, "%d", i + 1);
    Text(FontId::Body, x + 10, y + 7, kColDim, num);
    float nx = x + 34;
    if (g_editing == i) {
        ImGui::SetCursorScreenPos(P(nx - 2, y + 4));
        ImGui::SetNextItemWidth((kCardW - 44) * S());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 16, 12, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColTitle);
        if (g_editFocus) { ImGui::SetKeyboardFocusHere(); g_editFocus = false; }
        bool done = ImGui::InputText("##rename", g_editBuf, sizeof g_editBuf,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        bool lost = ImGui::GetFrameCount() > g_editStart + 2 && !ImGui::IsItemActive();
        ImGui::PopStyleColor(2);
        if (done || lost) {   // 이름은 바로 저장
            snprintf(d.name, sizeof d.name, "%s", g_editBuf);
            snprintf(Presets::Get(i).name, sizeof d.name, "%s", g_editBuf);
            Presets::MarkDirty();
            g_editing = -1;
        }
    } else {
        bool nameHov = false;
        char nid[16]; snprintf(nid, sizeof nid, "##name%d", i);
        if (HitButton(nid, nx - 2, y + 4, kCardW - 44, 26, &nameHov)) {
            g_editing = i; g_editFocus = true; g_editStart = ImGui::GetFrameCount();
            snprintf(g_editBuf, sizeof g_editBuf, "%s", d.name);
            Snd(kSndClick);
        }
        Text(FontId::Body, nx, y + 7, d.name[0] ? (nameHov ? IM_COL32(240, 220, 150, 255) : kColTitle) : kColDim,
             d.name[0] ? d.name : Txt::T("empty_set"), true, (x + kCardW - 10 - (g_dirty[i] ? TextWFit(FontId::Body, Txt::T("unsaved"), 90) + 8 : 0)) - nx);
        if (g_dirty[i]) { const char* t = Txt::T("unsaved"); Text(FontId::Body, x + kCardW - 10 - TextWFit(FontId::Body, t, 90), y + 7, kColWarn, t, true, 90); }
        if (nameHov && !Game::Drag().active) Tooltip(Txt::T("rename"), Txt::T("rename_set_body"));
    }

    DrawSlot(i, 0, x + 12, y + 40);
    DrawSlot(i, 1, x + 12 + 72 * kSlotScale + 8, y + 40);

    float bx = x + 150, bw = 110, bh = 34;
    char bid[32];
    snprintf(bid, sizeof bid, "##save%d", i);
    if (TextButton(bid, Txt::T("save"), bx, y + 52, bw, bh, g_dirty[i], g_dirty[i])) {
        Presets::Get(i) = d;
        Presets::MarkDirty();
        g_dirty[i] = false;
        Snd(kSndEquip);
        Toast(Txt::F("set_saved", { Txt::Num(i + 1) }).c_str());
    }
    snprintf(bid, sizeof bid, "##clear%d", i);
    bool empty = !d.name[0] && !d.trinket[0][0] && !d.trinket[1][0];
    if (TextButton(bid, Txt::T("clear"), bx, y + 98, bw, bh, !empty)) {
        d = Presets::Preset{};
        Presets::Get(i) = d;
        Presets::MarkDirty();
        g_dirty[i] = false;
        if (g_editing == i) g_editing = -1;
        Snd(kSndUnequip);
        Toast(Txt::T("set_cleared"));
    }
}

void DrawBagPanel(const Game::BagState& bag) {
    // 가방 왼쪽에 놓되 화면 밖으로 나가지 않게
    XF = bag.xf;
    float s = XF.Scale();
    XF.m[4] = std::max(4.0f, bag.xf.m[4] - (kPanelW + kPanelGap) * s);

    ImVec2 p0 = P(0, kBgY), p1 = P(kPanelW, kBgY + kPanelH);
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##preset_panel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoFocusOnAppearing);
    DL = ImGui::GetWindowDrawList();

    Image(kBg, 0, kBgY);
    // 뒷배경: 떠돌이 상인(장신구 상점) 그림 (카드 뒤로 은은하게)
    DL->PushClipRect(P(14, 136), P(kPanelW - 14, kBgY + kPanelH - 14), true);
    Image("campaign/town/buildings/nomad_wagon/nomad_wagon.character.png", kPanelW - 660, kBgY + kPanelH - 630, 660, 616, IM_COL32(255, 255, 255, 150));
    DL->PopClipRect();
    Image(kIcon, kIconX, kIconY);
    Text(FontId::Title, kTitleX, kTitleY, kColTitle, Txt::T("trinket_title"), true, kCloseX - kTitleX - 12);
    Text(FontId::Body, kSubX, kSubY, kColSub, Txt::T("trinket_sub"), true, kPanelW - kSubX - 24);

    bool hov = false;
    if (HitButton("##preset_close", kCloseX, kCloseY, kBtn, kBtn, &hov)) { g_open = false; Snd(kSndClose); }
    Image(kClose, kCloseX, kCloseY, -1, -1, hov ? IM_COL32(255, 255, 255, 255) : IM_COL32(225, 225, 225, 255));
    if (hov) Tooltip(Txt::T("close"), Txt::T("close_body"));

    int totalRows = (Presets::kCount + kCols - 1) / kCols;
    int maxRow = std::max(0, totalRows - kVisibleRows);
    if (Hover(0, kBgY, kPanelW, kPanelH) && ImGui::GetIO().MouseWheel != 0 && g_editing < 0)
    {
        int prev = g_scrollRow;
        g_scrollRow = std::clamp(g_scrollRow - (ImGui::GetIO().MouseWheel > 0 ? 1 : -1), 0, maxRow);
        if (g_scrollRow != prev) Snd(g_scrollRow < prev ? kSndScrollUp : kSndScrollDn);
    }
    if (g_scrollRow > 0) {
        bool dragOver = Game::Drag().active && Hover(kUpX, kUpY, 62, 49);   // 끌면서 화살표 위 → 스크롤
        if (HitButton("##up", kUpX, kUpY, 62, 49, &hov) || (dragOver && ImGui::GetFrameCount() % 20 == 0)) { --g_scrollRow; Snd(kSndScrollUp); }
        Image(kUp, kUpX, kUpY, -1, -1, (hov || dragOver) ? IM_COL32_WHITE : IM_COL32(220, 220, 220, 255));
    }
    if (g_scrollRow < maxRow) {
        bool dragOver = Game::Drag().active && Hover(kDownX, kDownY, 62, 49);
        if (HitButton("##down", kDownX, kDownY, 62, 49, &hov) || (dragOver && ImGui::GetFrameCount() % 20 == 0)) { ++g_scrollRow; Snd(kSndScrollDn); }
        Image(kDown, kDownX, kDownY, -1, -1, (hov || dragOver) ? IM_COL32_WHITE : IM_COL32(220, 220, 220, 255));
    }

    g_dropCard = g_dropSlot = -1;
    for (int r = 0; r < kVisibleRows; ++r)
        for (int c = 0; c < kCols; ++c) {
            int i = (g_scrollRow + r) * kCols + c;
            if (i < Presets::kCount) DrawCard(i, kGridX + c * (kCardW + kCardGapX), kGridY + r * (kCardH + kCardGapY));
        }

    if (g_toastTime > 0) {
        g_toastTime -= ImGui::GetIO().DeltaTime;
        float a = std::min(1.0f, g_toastTime / 0.5f);
        float tw = TextWFit(FontId::Body, g_toast, kPanelW - 40);
        Text(FontId::Body, (kPanelW - tw) * 0.5f, 158, IM_COL32(220, 200, 150, (int)(255 * a)), g_toast, true, kPanelW - 40);
    }

    // 끌고 있는 장신구 그림: 프리셋 창 위에서는 게임이 그린 그림이 창에 가려지므로 맨 위에 다시 그림
    const Game::DragState& drag = Game::Drag();
    ImVec2 m = ImGui::GetIO().MousePos;
    if (drag.active && drag.isTrinket && m.x >= p0.x && m.x < p1.x && m.y >= p0.y && m.y < p1.y) {
        const Game::TrinketInfo& ti = Game::Trinket(drag.hash);
        if (ti.valid) {
            ImDrawList* saveDL = DL; Game::Xform saveXF = XF;
            DL = ImGui::GetForegroundDrawList();
            XF.m[0] = s; XF.m[1] = 0; XF.m[2] = 0; XF.m[3] = s;
            XF.m[4] = m.x - 36 * s; XF.m[5] = m.y - 72 * s;
            DrawTrinket(ti.id.c_str(), 0, 0, 72, 144, false);
            DL = saveDL; XF = saveXF;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void DrawBagButton(const Game::BagState& bag) {
    XF = bag.xf;
    ImVec2 bp(bag.unequipPos.x - bag.spacing.x, bag.unequipPos.y - bag.spacing.y);
    ImVec2 p0 = P(bp.x, bp.y), p1 = P(bp.x + kBtn, bp.y + kBtn);
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##preset_btn", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    DL = ImGui::GetWindowDrawList();
    int vtx0 = DL->VtxBuffer.Size;
    bool hov = false;
    if (HitButton("##open", bp.x, bp.y, kBtn, kBtn, &hov)) {
        g_open = !g_open; g_editing = -1;
        Snd(g_open ? kSndOpen : kSndClose);
        if (g_open) { LoadDrafts(); PruneMissing(); }
    }
    DrawPresetIcon(bp.x, bp.y, hov, g_open);
    // 자막이 뜨면 게임의 정렬 버튼들처럼 어둡게 (투명이 아니라 색을 어둡게)
    static float dim = 0;
    float target = (Game::SubtitleAlpha() > 0 && !Game::UnderSubtitle()) ? 1.0f : 0.0f, step = ImGui::GetIO().DeltaTime * 4.0f;
    dim = dim < target ? std::min(target, dim + step) : std::max(target, dim - step);
    if (dim > 0) {
        float k = 1.0f - 0.55f * dim;
        for (int i = vtx0; i < DL->VtxBuffer.Size; ++i) {
            ImU32& c = DL->VtxBuffer[i].col;
            ImU32 r = (ImU32)((c & 255) * k), g = (ImU32)(((c >> 8) & 255) * k), b = (ImU32)(((c >> 16) & 255) * k);
            c = (c & 0xFF000000) | (b << 16) | (g << 8) | r;
        }
    }
    if (hov) Tooltip(Txt::T("trinket_title"), g_open ? Txt::T("trinket_btn_close") : Txt::T("trinket_btn_body"));
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ======================= 영웅 정보창: 불러오기 창 =======================

constexpr float kPickW = 320, kPickRowH = 56, kPickHeadH = 40, kPickPad = 10;

void DrawHeroPicker(const Game::HeroPanelState& hp) {
    XF = hp.xf;
    // 버튼: 색상(염색) 버튼 바로 위, 같은 크기·같은 왼쪽 선에 맞춤
    const Gfx::Texture& rc = Gfx::GameTex("shared/character/icon_recolor.png");
    float bs = rc.ok() && rc.w == rc.h && rc.w > 8 && rc.w < 80 ? (float)rc.w : kBtn;
    float bx = hp.anchor.x, by = hp.anchor.y - bs - 8;
    ImVec2 p0 = P(bx, by), p1 = P(bx + bs, by + bs);
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##hero_preset_btn", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    DL = ImGui::GetWindowDrawList();
    bool hov = false;
    if (HitButton("##hero_open", bx, by, bs, bs, &hov)) {
        g_pickOpen = !g_pickOpen;
        g_pickHero = hp.hero;
        Snd(g_pickOpen ? kSndPageOpen : kSndPageClose);
        g_pickMsg[0] = 0;
        if (g_pickOpen && Game::InTown()) PruneMissing();
    }
    DrawPresetIcon(bx, by, hov, g_pickOpen, bs);
    if (hov) Tooltip(Txt::T("load_title"), Game::InTown() ? Txt::T("load_body") : Txt::T("town_only"));
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (!g_pickOpen) return;
    if (g_pickHero != hp.hero) { g_pickOpen = false; return; }   // 다른 영웅으로 바뀌면 닫음

    // 작은 창: 버튼 오른쪽, 아래쪽을 버튼에 맞춤
    float h = kPickHeadH + Presets::kCount * kPickRowH + kPickPad * 2 + 28;
    float wx = bx + kBtn + 8, wy = by + kBtn - h;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (P(0, wy).y < 4) wy += (4 - P(0, wy).y) / S();
    (void)ds;
    ImVec2 w0 = P(wx, wy), w1 = P(wx + kPickW, wy + h);
    ImGui::SetNextWindowPos(w0);
    ImGui::SetNextWindowSize(ImVec2(w1.x - w0.x, w1.y - w0.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##hero_preset_pick", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    DL = ImGui::GetWindowDrawList();
    DL->AddRectFilled(w0, w1, IM_COL32(10, 8, 6, 245));
    DL->AddRect(w0, w1, IM_COL32(140, 115, 70, 255), 0, 0, 2.0f * S());
    DL->AddRect(P(wx + 4, wy + 4), P(wx + kPickW - 4, wy + h - 4), IM_COL32(70, 58, 38, 255), 0, 0, 1.0f * S());
    Text(FontId::Body, wx + 14, wy + 11, kColTitle, Txt::T("picker_title"), true, kPickW - 56);
    if (HitButton("##pick_close", wx + kPickW - 34, wy + 8, 24, 24, &hov)) { g_pickOpen = false; Snd(kSndPageClose); }
    Image(kClose, wx + kPickW - 34, wy + 8, 24, 24, hov ? IM_COL32_WHITE : IM_COL32(220, 220, 220, 255));

    bool town = Game::InTown();
    uint32_t heroClass = Game::HeroClassHash(hp.hero);
    float y = wy + kPickHeadH;
    for (int i = 0; i < Presets::kCount; ++i, y += kPickRowH) {
        const Presets::Preset& p = Presets::Get(i);
        bool has = p.trinket[0][0] || p.trinket[1][0];
        float rx = wx + kPickPad, rw = kPickW - kPickPad * 2;
        char rid[16]; snprintf(rid, sizeof rid, "##pick%d", i);
        bool rh = false;
        bool click = HitButton(rid, rx, y, rw, kPickRowH - 4, &rh);
        bool classOk = true;
        for (int s = 0; s < 2; ++s)
            if (p.trinket[s][0] && !Game::CanUse(Game::TrinketById(p.trinket[s]), heroClass)) classOk = false;
        bool enabled = has && town && classOk;
        DL->AddRectFilled(P(rx, y), P(rx + rw, y + kPickRowH - 4),
                          rh && enabled ? IM_COL32(58, 42, 24, 240) : IM_COL32(24, 19, 14, 230));
        DL->AddRect(P(rx, y), P(rx + rw, y + kPickRowH - 4),
                    rh && enabled ? IM_COL32(205, 170, 100, 255) : IM_COL32(70, 58, 40, 255), 0, 0, 1.0f * S());
        char num[8]; snprintf(num, sizeof num, "%d", i + 1);
        Text(FontId::Body, rx + 8, y + 14, kColDim, num, false);
        const char* nm = p.name[0] ? p.name : (has ? Txt::T("no_name") : Txt::T("empty_set"));
        Text(FontId::Body, rx + 42, y + 14, has ? (rh && enabled ? IM_COL32(255, 235, 170, 255) : kColTitle) : kColDim, nm, false, rw - 42 - 68);
        // 장신구 미니 그림 2개
        float iw = 24, ih = 48, ix = rx + rw - (iw * 2 + 10);
        for (int s = 0; s < 2; ++s)
            DrawTrinket(p.trinket[s], ix + s * (iw + 4), y + 2, iw, ih, !has || !classOk);
        if (has && !classOk) {      // 다른 직업 전용: 비활성화 + 빨간 X
            DL->AddRectFilled(P(rx, y), P(rx + rw, y + kPickRowH - 4), IM_COL32(0, 0, 0, 120));
            Image(kUnavail, ix - 6, y - 4, iw * 2 + 16, ih + 8, IM_COL32(255, 255, 255, 200));
        }
        if (rh && has) {
            std::string a = p.trinket[0][0] ? TrinketLabel(p.trinket[0]) : std::string(Txt::T("none"));
            std::string b = p.trinket[1][0] ? TrinketLabel(p.trinket[1]) : std::string(Txt::T("none"));
            std::string body = a + " / " + b;
            if (!classOk) Tooltip(Txt::T("class_locked"), body.c_str(), kColWarn);
            else if (!town) Tooltip(Txt::T("town_only_load"), body.c_str(), kColWarn);
            else       Tooltip(Txt::T("click_equip"), body.c_str());
        }
        if (click && has && !enabled) Snd(kSndInvalid);
        if (click && enabled) {
            Game::EquipResult r = Game::EquipPreset(hp.hero, p.trinket[0], p.trinket[1]);
            Snd(r.error.empty() && r.missing.empty() ? kSndEquip : kSndInvalid);
            if (!r.error.empty()) { snprintf(g_pickMsg, sizeof g_pickMsg, "%s", r.error.c_str()); g_pickMsgWarn = true; }
            else if (!r.missing.empty()) {
                std::string m = Txt::F("not_in_bag", { r.missing.size() > 1 ? Txt::F("and_more", { r.missing[0] }) : r.missing[0] });
                snprintf(g_pickMsg, sizeof g_pickMsg, "%s", m.c_str()); g_pickMsgWarn = true;
            } else {
                snprintf(g_pickMsg, sizeof g_pickMsg, "%s", Txt::F("set_equipped", { Txt::Num(i + 1) }).c_str()); g_pickMsgWarn = false;
            }
            g_pickMsgTime = 4.0f;
        }
    }
    if (g_pickMsgTime > 0 && g_pickMsg[0]) {
        g_pickMsgTime -= ImGui::GetIO().DeltaTime;
        Text(FontId::Body, wx + 14, y + 4, g_pickMsgWarn ? kColWarn : IM_COL32(150, 210, 130, 255), g_pickMsg, false, kPickW - 28);
    } else if (!town) {
        Text(FontId::Body, wx + 14, y + 4, kColWarn, Txt::T("town_only_load"), false, kPickW - 28);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace

bool IsOpen() { return g_open || g_pickOpen; }

void Draw() {
    g_hoverPrev = g_hoverNow; g_hoverNow = 0;
    // 다른 영지를 불러오면 그 영지의 프리셋으로 바꿈
    int prof = Game::CurrentProfile();
    if (prof != Presets::Profile()) {
        Presets::SetProfile(prof);
        g_open = false; g_pickOpen = false; g_editing = -1;
        LoadDrafts();
    }
    const Game::BagState& bag = Game::Bag();
    if (bag.visible) {
        DrawBagButton(bag);
        if (g_open) DrawBagPanel(bag);
    } else if (g_open) { g_open = false; g_editing = -1; }

    const Game::HeroPanelState& hp = Game::HeroPanel();
    if (hp.visible && hp.hero) DrawHeroPicker(hp);
    else g_pickOpen = false;
}

bool HandleEvent(const SDL_Event* ev) {
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) {
        if (g_editing >= 0) { g_editing = -1; return true; }
        if (g_pickOpen) { g_pickOpen = false; Snd(kSndPageClose); return true; }
        if (g_open) { g_open = false; Snd(kSndClose); return true; }
    }
    // 가방에서 끌던 장신구를 프리셋 칸 위에서 놓음 → 칸에 넣고, 게임에는 그대로 전달(원래 자리로 돌아감)
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT && g_open && g_dropCard >= 0) {
        const Game::DragState& d = Game::Drag();
        if (d.active && d.isTrinket) {
            const Game::TrinketInfo& ti = Game::Trinket(d.hash);
            int ck = ti.valid ? DropConflict(g_dropCard, g_dropSlot) : 0;
            if (ck) {
                Snd(kSndInvalid);
                Toast(ck == 2 ? Txt::T("dup_body") : Txt::T("class_toast"));
            } else if (ti.valid) {
                Snd(kSndEquip);
                Presets::Preset& p = g_draft[g_dropCard];
                snprintf(p.trinket[g_dropSlot], sizeof p.trinket[g_dropSlot], "%s", ti.id.c_str());
                g_dirty[g_dropCard] = true;
                Log("프리셋 %d 칸 %d ← %s", g_dropCard + 1, g_dropSlot + 1, ti.id.c_str());
            }
        }
        g_dropCard = g_dropSlot = -1;
    }
    return false;
}

bool DragInProgress() { return Game::Drag().active; }

} // namespace UiPreset

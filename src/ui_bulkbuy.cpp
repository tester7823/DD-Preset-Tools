// 원정 준비 화면: 골드 아이템 일괄 구매
//  - 파티 슬롯 오른쪽(영웅 프리셋 버튼 자리)에 원정 준비 아이콘 버튼
//  - 누르면 게임의 확인 창 모양으로 "골드로 살 수 있는 아이템을 일괄로 담겠습니까?"
//  - [네] → 상점 왼쪽→오른쪽 순서로 한 번에 구매, 골드가 모자란 시점에서 중단
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include "imgui.h"
#include "ui_bulkbuy.h"
#include "game.h"
#include "text.h"
#include "gfx.h"
#include "log.h"

using Gfx::FontId;

namespace UiBulkBuy {
namespace {

constexpr float kSlot = 85;
const char* kIcon    = "campaign/town/provision/provision.icon.png";
const char* kSlotBg  = "campaign/town/hero_slot/hero_slot.background.png";
const char* kSlotHi  = "campaign/town/hero_slot/hero_slot.backgroundhightlight.png";
const char* kDlgBg   = "shared/confirm_dialog/confirm_dialog.background.png";
const char* kDlgSel  = "shared/confirm_dialog/confirm_dialog.answer_text_selected_overlay.png";

const char* kSndHover  = "/ui/town/button_mouse_over";
const char* kSndClick  = "/ui/town/button_click";
const char* kSndBuy    = "/ui/town/buy";
const char* kSndInvalid = "/ui/town/button_invalid";

const ImU32 kColTitle = IM_COL32(200, 180, 110, 255);
const ImU32 kColText  = IM_COL32(225, 215, 195, 255);
const ImU32 kColSub   = IM_COL32(170, 160, 140, 255);
const ImU32 kColWarn  = IM_COL32(230, 150, 70, 255);

bool  g_dialog = false;
float g_dialogT = 0;            // 창 나타나는 정도
int   g_hoverAns = -1, g_hoverAnsPrev = -1;
float g_shownTime = 0;
ImGuiID g_hoverNow = 0, g_hoverPrev = 0;
char  g_msg[200] = {};
float g_msgTime = 0;
bool  g_msgWarn = false;

Game::Xform XF;
ImDrawList* DL = nullptr;
ImVec2 P(float x, float y) { return XF.ToScreen(ImVec2(x, y)); }
float  S() { return XF.Scale(); }
void Image(const char* rel, float x, float y, float w, float h, ImU32 tint = IM_COL32_WHITE) {
    const Gfx::Texture& t = Gfx::GameTex(rel);
    if (t.ok()) DL->AddImage(t.tex(), P(x, y), P(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
}
void Text(float x, float y, ImU32 col, const char* s, float maxW = 0) {
    float k = 1.0f;   // maxW 보다 길면 줄여서 맞춤 (긴 번역문)
    if (maxW > 0) { float w = Gfx::MeasureText(FontId::Body, s, 1.0f).x; if (w > maxW) k = std::max(0.6f, maxW / w); }
    if (k < 1.0f) y += Gfx::FontLineHeight(FontId::Body) * (1.0f - k) * 0.5f;
    Gfx::DrawTextShadow(DL, FontId::Body, P(x, y), S() * k, col, s);
}
float TextW(const char* s) { return Gfx::MeasureText(FontId::Body, s, 1.0f).x; }

void ScaleAlpha(ImDrawList* dl, int from, float a) {
    if (a >= 0.999f) return;
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
        ImU32& c = dl->VtxBuffer[i].col;
        c = (c & 0x00FFFFFF) | ((ImU32)(((c >> 24) & 255) * a) << 24);
    }
}

void Tooltip(const char* title, const char* body) {
    ImVec2 m = ImGui::GetIO().MousePos;
    float s = S();
    ImVec2 tw = Gfx::MeasureText(FontId::Body, title, s);
    ImVec2 bw = body ? Gfx::MeasureText(FontId::Body, body, s) : ImVec2(0, 0);
    float w = std::max(tw.x, bw.x) + 20 * s, h = tw.y + (body ? bw.y + 4 * s : 0) + 14 * s;
    ImVec2 p0(m.x + 18 * s, m.y - h - 8 * s);
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (p0.x + w > ds.x) p0.x = m.x - w - 8 * s;
    if (p0.y < 0) p0.y = 0;
    ImVec2 p1(p0.x + w, p0.y + h);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(p0, p1, IM_COL32(12, 10, 8, 240));
    fg->AddRect(p0, p1, IM_COL32(120, 100, 60, 255), 0, 0, 1.5f * s);
    Gfx::DrawStr(fg, FontId::Body, ImVec2(p0.x + 10 * s, p0.y + 7 * s), s, kColTitle, title);
    if (body) Gfx::DrawStr(fg, FontId::Body, ImVec2(p0.x + 10 * s, p0.y + 11 * s + tw.y), s, kColSub, body);
}

void DoBuy() {
    int goldBefore = Game::Gold();
    Game::BulkBuyResult r = Game::BulkBuyProvisions();
    if (!r.ok) {
        Game::PlaySound(kSndInvalid);
        snprintf(g_msg, sizeof g_msg, "%s", r.error.c_str()); g_msgWarn = true;
    } else if (r.bought == 0) {
        Game::PlaySound(kSndInvalid);
        snprintf(g_msg, sizeof g_msg, "%s", r.outOfGold ? Txt::T("ab_no_gold") : r.bagFull ? Txt::T("ab_bag_full") : Txt::T("ab_nothing"));
        g_msgWarn = true;
    } else {
        Game::PlaySound(kSndBuy);
        const char* k = r.outOfGold ? "ab_done_gold" : r.bagFull ? "ab_done_bag" : "ab_done";
        snprintf(g_msg, sizeof g_msg, "%s", Txt::F(k, { Txt::Num(r.bought), Txt::Num(r.spent) }).c_str());
        g_msgWarn = r.outOfGold || r.bagFull;
    }
    (void)goldBefore;
    g_msgTime = 4.0f;
}

// 게임 확인 창과 같은 모양 (confirm_dialog.layout.darkest 기준, 1920x1080 화면 좌표)
void DrawDialog() {
    ImGuiIO& io = ImGui::GetIO();
    g_dialogT = std::min(1.0f, g_dialogT + io.DeltaTime / 0.15f);
    ImVec2 ds = io.DisplaySize;
    float s = ds.y / 1080.0f;
    XF = Game::Xform{};
    XF.m[0] = XF.m[3] = s; XF.m[4] = ds.x * 0.5f - 960 * s; XF.m[5] = 0;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##bulk_dialog", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    DL = ImGui::GetWindowDrawList();
    int v0 = DL->VtxBuffer.Size;
    DL->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 150));          // 뒤 화면 어둡게
    float bx = 960 - 420, by = 200;
    Image(kDlgBg, bx, by, 840, 600);

    // 질문 + 구매 목록(설정 파일 내용) 자동 줄바꿈
    std::vector<std::string> lines;
    lines.push_back(Txt::T("ab_q"));
    {
        std::string plan = Game::BuyPlanText(), cur;
        size_t pos = 0;
        while (pos <= plan.size()) {
            size_t e = plan.find(", ", pos); if (e == std::string::npos) e = plan.size();
            std::string tok = plan.substr(pos, e - pos);
            std::string trial = cur.empty() ? tok : cur + ", " + tok;
            if (!cur.empty() && TextW(trial.c_str()) > 560) { lines.push_back(cur + ","); cur = tok; }
            else cur = trial;
            pos = e + 2;
        }
        if (!cur.empty()) lines.push_back(cur);
        while (lines.size() > 4) { lines[3] += "..."; lines.pop_back(); }
    }
    size_t planEnd = lines.size();
    lines.push_back(Txt::T("ab_gold_note"));
    lines.push_back(Txt::T("ab_adjust"));
    float cx = 960 - 18, ly = by + 178;
    for (size_t i = 0; i < lines.size(); ++i) {
        ImU32 col = i == 0 ? kColText : i < planEnd ? kColTitle : i == planEnd ? kColSub : kColWarn;
        if (i == 1 || i == planEnd) ly += 10;
        float lw = std::min(TextW(lines[i].c_str()), 620.0f);
        Text(cx - lw * 0.5f, ly, col, lines[i].c_str(), 620);
        ly += 27;
    }

    // 답
    const char* ans[2] = { Txt::T("yes"), Txt::T("no") };
    g_hoverAns = -1;
    int clicked = -1;
    for (int i = 0; i < 2; ++i) {
        float ax = 960 - 12 - 250, ay = by + 390 - 20 + i * 55 - 19;
        ImGui::SetCursorScreenPos(P(ax, ay));
        char id[16]; snprintf(id, sizeof id, "##ans%d", i);
        if (ImGui::InvisibleButton(id, ImVec2(500 * s, 38 * s))) clicked = i;
        bool hv = ImGui::IsItemHovered();
        if (hv) {
            g_hoverAns = i;
            Image(kDlgSel, 960 - 12 - 233, ay + 19 - 40, 466, 81);
        }
        float tw = TextW(ans[i]);
        Text(960 - 12 - tw * 0.5f, ay + 19 - Gfx::FontLineHeight(FontId::Body) * 0.5f,
             hv ? IM_COL32(255, 240, 200, 255) : kColText, ans[i]);
    }
    if (g_hoverAns >= 0 && g_hoverAns != g_hoverAnsPrev) Game::PlaySound(kSndHover);
    g_hoverAnsPrev = g_hoverAns;
    ScaleAlpha(DL, v0, g_dialogT);
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (clicked >= 0 && g_dialogT >= 1.0f) {
        g_dialog = false;
        Game::PlaySound(kSndClick);
        if (clicked == 0) DoBuy();
    }
}

void DrawButton(const Game::Xform& bxf, float alpha) {
    XF = bxf;
    ImVec2 p0 = P(-8, -8), p1 = P(kSlot + 8, kSlot + 8);
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##bulk_btn", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    DL = ImGui::GetWindowDrawList();
    int v0 = DL->VtxBuffer.Size;
    ImGui::SetCursorScreenPos(P(0, 0));
    bool click = ImGui::InvisibleButton("##bulk_open", ImVec2(kSlot * S(), kSlot * S()));
    bool hov = ImGui::IsItemHovered() && alpha >= 1.0f;
    bool avail = Game::AutoBuyAvailable();
    if (hov) { ImGuiID iid = ImGui::GetItemID(); if (iid != g_hoverPrev) Game::PlaySound(kSndHover); g_hoverNow = iid; }
    if (click && alpha >= 1.0f && !g_dialog) {
        if (avail) { g_dialog = true; g_dialogT = 0; g_hoverAnsPrev = -1; Game::PlaySound(kSndClick); }
        else Game::PlaySound(kSndInvalid);
    }
    bool lit = (hov && avail) || g_dialog;
    Image(lit ? kSlotHi : kSlotBg, 0, 0, kSlot, kSlot);
    Image(kIcon, 4, 4, kSlot - 8, kSlot - 8, lit ? IM_COL32(235, 230, 220, 255) : avail ? IM_COL32(150, 145, 138, 255) : IM_COL32(70, 68, 64, 255));
    DL->AddRect(P(0, 0), P(kSlot, kSlot), lit ? IM_COL32(120, 100, 70, 255) : IM_COL32(55, 48, 38, 255), 0, 0, 1.5f * S());
    ScaleAlpha(DL, v0, alpha);
    if (hov && !g_dialog) {
        if (avail) Tooltip(Txt::T("ab_title"), Txt::T("ab_body"));
        else Tooltip(Txt::T("ab_title_off"), Txt::T("ab_off_body"));
    }
    // 결과 알림 (버튼 위)
    if (g_msgTime > 0 && g_msg[0]) {
        g_msgTime -= ImGui::GetIO().DeltaTime;
        float a = std::min(1.0f, g_msgTime / 0.5f) * alpha;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float s = S();
        ImVec2 tw = Gfx::MeasureText(FontId::Body, g_msg, s);
        ImVec2 c = P(kSlot * 0.5f, -14);
        float x = std::min(c.x - tw.x * 0.5f, ImGui::GetIO().DisplaySize.x - tw.x - 8);
        ImU32 col = g_msgWarn ? IM_COL32(230, 150, 70, (int)(255 * a)) : IM_COL32(220, 200, 150, (int)(255 * a));
        fg->AddRectFilled(ImVec2(x - 8 * s, c.y - tw.y - 4 * s), ImVec2(x + tw.x + 8 * s, c.y + 4 * s), IM_COL32(10, 8, 6, (int)(200 * a)));
        Gfx::DrawTextShadow(fg, FontId::Body, ImVec2(x, c.y - tw.y), s, col, g_msg);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace

void Draw() {
    g_hoverPrev = g_hoverNow; g_hoverNow = 0;
    const Game::PartyState& party = Game::Party();
    float dt = ImGui::GetIO().DeltaTime;
    if (!(party.visible && Game::ProvisionVisible()) || Game::TownFadingOut()) {
        g_shownTime = 0; g_dialog = false; g_msgTime = 0;
        return;
    }
    // 원정 준비 화면이 열릴 때: 잠시 뒤 서서히 나타남
    g_shownTime += dt;
    float alpha = std::clamp((g_shownTime - 0.6f) / 0.5f, 0.0f, 1.0f);
    if (Game::ScreenVisibility() < 0.99f) alpha = std::min(alpha, 0.999f);
    if (alpha <= 0) return;

    int idx[4], n = 0;
    for (int i = 0; i < 4; ++i) if (party.slotSeen[i]) idx[n++] = i;
    if (n == 0) return;
    std::sort(idx, idx + n, [&](int a, int b) { return party.slotXf[a].m[4] < party.slotXf[b].m[4]; });
    const Game::Xform& right = party.slotXf[idx[n - 1]];
    const Game::Xform& left = party.slotXf[idx[0]];
    float s = right.Scale();
    float pitch = n >= 2 ? (right.m[4] - left.m[4]) / (n - 1) : 93 * s;
    Game::Xform bxf = right;
    bxf.m[4] += pitch + 22 * s;
    DrawButton(bxf, alpha);
    if (g_dialog && !Game::AutoBuyAvailable()) g_dialog = false;
    if (g_dialog) DrawDialog();
}

bool HandleEvent(const SDL_Event* ev) {
    if (!g_dialog) return false;
    if (ev->type == SDL_KEYDOWN) {
        if (ev->key.keysym.sym == SDLK_ESCAPE) { g_dialog = false; Game::PlaySound(kSndClick); return true; }
        if (ev->key.keysym.sym == SDLK_RETURN || ev->key.keysym.sym == SDLK_KP_ENTER) {
            g_dialog = false; Game::PlaySound(kSndClick); DoBuy(); return true;
        }
        return true;                                   // 창이 떠 있는 동안 게임 단축키 막기
    }
    return false;
}

bool DialogOpen() { return g_dialog; }

} // namespace UiBulkBuy

// 인게임 오버레이 (Dear ImGui, SDL2 + OpenGL)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <cfloat>
#include <algorithm>

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl2.h"
#include "backends/imgui_impl_opengl3.h"

#include "overlay.h"
#include "presets.h"
#include "game.h"
#include "hero_presets.h"
#include "gfx.h"
#include "ui_preset.h"
#include "ui_heropreset.h"
#include "ui_bulkbuy.h"
#include "text.h"
#include "log.h"

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#define GL_CONTEXT_PROFILE_MASK_ 0x9126
#define GL_CONTEXT_CORE_PROFILE_BIT_ 0x1

namespace {

enum class Backend { None, GL2, GL3 };

bool          g_ready      = false;
bool          g_failed     = false;
Backend       g_backend    = Backend::None;
SDL_Window*   g_window     = nullptr;
SDL_GLContext g_context    = nullptr;
bool          g_showWindow = false;
bool          g_showDemo   = false;
unsigned      g_frames     = 0;

using PFN_BindFB = void (APIENTRY*)(GLenum, GLuint);
PFN_BindFB p_glBindFramebuffer = nullptr;

std::string WToUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\malgun.ttf",
        "C:\\Windows\\Fonts\\gulim.ttc",
    };
    for (const char* f : candidates) {
        if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig cfg; cfg.OversampleH = 1; cfg.OversampleV = 1;
            if (io.Fonts->AddFontFromFileTTF(f, 18.0f, &cfg, io.Fonts->GetGlyphRangesKorean())) {
                Log("폰트 로드: %s", f);
                return;
            }
        }
    }
    io.Fonts->AddFontDefault();
    Log("한글 폰트를 찾지 못해 기본 폰트 사용");
}

void ApplyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 2.0f; s.FrameRounding = 2.0f; s.WindowBorderSize = 1.0f;
    ImVec4* c = s.Colors;
    // 다키스트 던전 느낌의 어두운 갈색/금색 톤
    c[ImGuiCol_WindowBg]       = ImVec4(0.07f, 0.06f, 0.05f, 0.96f);
    c[ImGuiCol_TitleBg]        = ImVec4(0.18f, 0.12f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.30f, 0.18f, 0.10f, 1.00f);
    c[ImGuiCol_Border]         = ImVec4(0.55f, 0.42f, 0.25f, 0.60f);
    c[ImGuiCol_Button]         = ImVec4(0.30f, 0.20f, 0.12f, 1.00f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.48f, 0.32f, 0.16f, 1.00f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.62f, 0.42f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.16f, 0.12f, 0.09f, 1.00f);
    c[ImGuiCol_Header]         = ImVec4(0.30f, 0.20f, 0.12f, 1.00f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.45f, 0.30f, 0.15f, 1.00f);
    c[ImGuiCol_Text]           = ImVec4(0.92f, 0.86f, 0.74f, 1.00f);
}

bool Init(SDL_Window* window) {
    g_window  = window;
    g_context = SDL_GL_GetCurrentContext();
    if (!g_context) { Log("현재 GL 컨텍스트 없음 - 다음 프레임에 재시도"); return false; }

    const char* ver = (const char*)glGetString(GL_VERSION);
    const char* ren = (const char*)glGetString(GL_RENDERER);
    int major = ver ? atoi(ver) : 0;
    GLint profile = 0;
    if (major >= 3) glGetIntegerv(GL_CONTEXT_PROFILE_MASK_, &profile);
    while (glGetError() != GL_NO_ERROR) {}
    bool core = (profile & GL_CONTEXT_CORE_PROFILE_BIT_) != 0;
    Log("GL_VERSION=%s  RENDERER=%s  core=%d", ver ? ver : "?", ren ? ren : "?", core);

    p_glBindFramebuffer = (PFN_BindFB)SDL_GL_GetProcAddress("glBindFramebuffer");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;     // 창 위치는 코드가 정하므로 설정 파일을 만들지 않음
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   // 게임 커서를 건드리지 않음
    ApplyStyle();
    LoadFonts();

    if (!ImGui_ImplSDL2_InitForOpenGL(window, g_context)) { Log("ImGui SDL2 백엔드 초기화 실패"); return false; }

    bool ok;
    if (major >= 3) { ok = ImGui_ImplOpenGL3_Init(core ? "#version 150" : "#version 130"); g_backend = Backend::GL3; }
    else            { ok = ImGui_ImplOpenGL2_Init();                                      g_backend = Backend::GL2; }
    if (!ok) { Log("ImGui OpenGL 백엔드 초기화 실패"); return false; }

    Presets::Load();
    Log("오버레이 초기화 완료 (backend=%s)", g_backend == Backend::GL3 ? "GL3" : "GL2");
    return true;
}

void Shutdown() {
    if (!g_ready) return;
    Gfx::ReleaseAll();
    if (g_backend == Backend::GL3) ImGui_ImplOpenGL3_Shutdown(); else ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    g_ready = false; g_backend = Backend::None; g_context = nullptr;
    Log("오버레이 종료(컨텍스트 파괴)");
}

// --- UI --------------------------------------------------------------------

// F8: 진단 창 (위치 보정/문제 확인용)
void DrawDebugWindow() {
    if (!g_showWindow) return;
    ImGui::SetNextWindowSize(ImVec2(520, 330), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("DD Preset Tools - diagnostics (F8)", &g_showWindow)) { ImGui::End(); return; }
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("v0.9   backend %s   screen %.0f x %.0f   FPS %.0f",
                g_backend == Backend::GL3 ? "GL3" : "GL2", io.DisplaySize.x, io.DisplaySize.y, io.Framerate);
    ImGui::Text("game hooks: %s", Game::Supported() ? "on" : "off (unsupported game version)");
    ImGui::Text("game fonts: title %s / body %s", Gfx::FontAvailable(Gfx::FontId::Title) ? "OK" : "missing",
                Gfx::FontAvailable(Gfx::FontId::Body) ? "OK" : "missing");
    const Game::BagState& b = Game::Bag();
    ImGui::Separator();
    ImGui::Text("trinket bag open: %s", b.visible ? "yes" : "no");
    if (b.visible) {
        ImVec2 o = b.ToScreen(ImVec2(0, 0));
        ImGui::Text("panel origin (%.1f, %.1f)  scale %.3f", o.x, o.y, b.Scale());
        ImGui::Text("unequip button (%.1f, %.1f)  spacing (%.1f, %.1f)", b.unequipPos.x, b.unequipPos.y, b.spacing.x, b.spacing.y);
        ImGui::Text("M: %.2f %.2f %.2f %.2f / %.2f %.2f %.2f %.2f", b.raw[0], b.raw[1], b.raw[2], b.raw[3], b.raw[4], b.raw[5], b.raw[6], b.raw[7]);
        ImGui::Text("   %.2f %.2f %.2f %.2f / %.2f %.2f %.2f %.2f", b.raw[8], b.raw[9], b.raw[10], b.raw[11], b.raw[12], b.raw[13], b.raw[14], b.raw[15]);
    }
    const Game::PartyState& pt = Game::Party();
    ImGui::Separator();
    ImGui::Text("estate map: %s   hero drag: %s (guid %d)   profile_%d", pt.visible ? "yes" : "no",
                Game::HeroDrag().active ? "yes" : "no", Game::HeroDrag().guid, Game::CurrentProfile());
    for (int i = 0; i < 4; ++i) if (pt.slotSeen[i]) {
        ImVec2 o = pt.slotXf[i].ToScreen(ImVec2(0, 0));
        ImGui::Text("  party slot %d: (%.1f, %.1f) scale %.3f", i, o.x, o.y, pt.slotXf[i].Scale());
    }
    ImGui::Checkbox("ImGui demo window", &g_showDemo);
    ImGui::End();
}

bool g_earlyDone = false;     // 이번 프레임은 확인 창 직전에 이미 그렸음
bool g_earlyBroken = false;   // 확인 창 직전 그리기가 맞지 않는 환경 → 확인 창이 뜨면 숨김으로 대체

// 화면 전환/확인 창에 맞춰 오버레이 전체 투명도 조절
void ScaleAlpha(ImDrawData* dd, float a) {
    if (a >= 0.999f) return;
    for (int n = 0; n < dd->CmdListsCount; ++n)
        for (ImDrawVert& v : dd->CmdLists[n]->VtxBuffer) {
            ImU32 al = (ImU32)(((v.col >> 24) & 255) * a);
            v.col = (v.col & 0x00FFFFFF) | (al << 24);
        }
}

// 게임이 그리는 내부 화면 크기(vw x vh)를 창에 가운데 맞춤으로 늘린다고 보고 좌표 변환값을 정함
int g_virtW = 0, g_virtH = 0;
void UpdateScreenMap(int vw, int vh) {
    if (vw < 320 || vh < 180) return;                 // 이상한 값(작은 보조 화면 등)은 무시
    static int candW = 0, candH = 0;
    if (vw != g_virtW || vh != g_virtH) {             // 크기가 바뀌면 다음 프레임에도 같을 때만 반영
        if (vw != candW || vh != candH) { candW = vw; candH = vh; return; }
    }
    g_virtW = vw; g_virtH = vh;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(g_window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    if (ww == vw && wh == vh) { Game::SetScreenMap(1.0f, 0.0f, 0.0f, vw, vh); return; }
    float k = std::min((float)ww / vw, (float)wh / vh);
    Game::SetScreenMap(k, (ww - vw * k) * 0.5f, (wh - vh * k) * 0.5f, vw, vh);
}

ImDrawData g_lateData;
ImVector<ImDrawList*> g_lateLists;
bool g_latePending = false;

void RenderUI(bool atSwap) {
    Game::BeginFrame();
    Txt::NewFrame();
    // 게임에서 영지를 삭제했으면 그 슬롯의 조합/편성도 비움 (같은 슬롯에 새로 시작해도 깨끗하게)
    for (int slot = Game::PopDeletedSlot(); slot >= 0; slot = Game::PopDeletedSlot()) {
        Presets::ResetProfile(slot);
        HeroPresets::ResetProfile(slot);
    }
    GLint prevFB = 0;
    if (atSwap && p_glBindFramebuffer) { glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB); p_glBindFramebuffer(GL_FRAMEBUFFER, 0); }

    if (g_backend == Backend::GL3) ImGui_ImplOpenGL3_NewFrame(); else ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    {   // 화면 크기 진단 (바뀔 때만 기록)
        static int lw = -1, lh = -1, ldw = -1, ldh = -1; static bool lbag = false;
        int ww = 0, wh = 0, dw = 0, dh = 0;
        SDL_GetWindowSize(g_window, &ww, &wh); SDL_GL_GetDrawableSize(g_window, &dw, &dh);
        GLint vp[4] = {}; glGetIntegerv(GL_VIEWPORT, vp);
        if (ww != lw || wh != lh || dw != ldw || dh != ldh) {
            lw = ww; lh = wh; ldw = dw; ldh = dh; lbag = false;
            ImGuiIO& io = ImGui::GetIO();
            Log("[화면] 창 %dx%d / 그리기 %dx%d / ImGui %.0fx%.0f / viewport %d,%d %dx%d (%s)", ww, wh, dw, dh,
                io.DisplaySize.x, io.DisplaySize.y, vp[0], vp[1], vp[2], vp[3], atSwap ? "프레임 끝" : "게임 화면 안");
        }
        const Game::BagState& b = Game::Bag();
        if (b.visible && !lbag) {
            lbag = true; ImVec2 o = b.ToScreen(ImVec2(0, 0));
            Log("[화면] 장신구 가방 위치 (%.1f, %.1f) 배율 %.3f / M %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
                o.x, o.y, b.Scale(), b.raw[0], b.raw[1], b.raw[2], b.raw[3], b.raw[4], b.raw[5], b.raw[6], b.raw[7],
                b.raw[8], b.raw[9], b.raw[10], b.raw[11], b.raw[12], b.raw[13], b.raw[14], b.raw[15]);
        }
    }

    static bool modOn = false;
    modOn = Game::ModEnabled();
    if (modOn) {                       // 모드 목록에서 꺼져 있으면 기능 전체를 숨김
        UiPreset::Draw();
        UiHeroPreset::Draw();
        UiBulkBuy::Draw();
    }
    DrawDebugWindow();
    if (g_showDemo) ImGui::ShowDemoWindow(&g_showDemo);
    // 창 겹침 순서 고정: 장신구 쪽 < 영웅 프리셋 < 자동 구매 확인 창 (클릭해도 순서가 바뀌지 않게)
    for (const char* n : { "##hero_preset_panel", "##hero_preset_btn2", "##bulk_btn", "##bulk_dialog" })
        if (ImGuiWindow* w = ImGui::FindWindowByName(n)) if (w->Active) ImGui::BringWindowToDisplayFront(w);

    ImGui::Render();
    float a = Game::ScreenVisibility();
    if (atSwap && g_earlyBroken && Game::ModalAmount() > 0) a *= 1.0f - std::min(1.0f, Game::ModalAmount());   // 대체: 확인 창 위로 올라오지 않게 숨김
    ScaleAlpha(ImGui::GetDrawData(), a);
    // 자막 아래에 그리는 중이면: 게임 패널 위에 얹히는 버튼·툴팁은 따로 떼어 나중(게임 요소 다 그린 뒤, 자막 글자 직전)에 그림
    g_lateData.Clear(); g_lateLists.clear(); g_latePending = false;
    if (false) {   // (예전 방식: 버튼만 따로 떼어 그리기 — 지금은 자막 직전 한 번에 그림)
        ImDrawData* dd = ImGui::GetDrawData();
        ImVector<ImDrawList*> keep;
        for (ImDrawList* l : dd->CmdLists) {
            const char* n = l->_OwnerName ? l->_OwnerName : "";
            bool late = !strcmp(n, "##preset_btn") || !strcmp(n, "##hero_preset_btn") || !strcmp(n, "##hero_preset_btn2") ||
                        !strcmp(n, "##bulk_btn") || !strcmp(n, "##Foreground") || !strncmp(n, "##Tooltip", 9);
            (late ? g_lateLists : keep).push_back(l);
        }
        if (!g_lateLists.empty()) {
            dd->CmdLists = keep; dd->CmdListsCount = keep.Size;
            dd->TotalVtxCount = dd->TotalIdxCount = 0;
            for (ImDrawList* l : keep) { dd->TotalVtxCount += l->VtxBuffer.Size; dd->TotalIdxCount += l->IdxBuffer.Size; }
            g_lateData.Valid = true; g_lateData.DisplayPos = dd->DisplayPos; g_lateData.DisplaySize = dd->DisplaySize;
            g_lateData.FramebufferScale = dd->FramebufferScale; g_lateData.OwnerViewport = dd->OwnerViewport;
            g_lateData.CmdLists = g_lateLists; g_lateData.CmdListsCount = g_lateLists.Size;
            for (ImDrawList* l : g_lateLists) { g_lateData.TotalVtxCount += l->VtxBuffer.Size; g_lateData.TotalIdxCount += l->IdxBuffer.Size; }
            g_latePending = true;
        }
    }
    if (g_backend == Backend::GL3) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    else                           ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    if (atSwap && p_glBindFramebuffer) p_glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFB);
}

// 떼어 둔 버튼을 그림 (atSwap = 프레임 끝에서 대신 그릴 때)
void RenderLate(bool atSwap) {
    if (!g_latePending) return;
    g_latePending = false;
    GLint prevFB = 0;
    if (atSwap && p_glBindFramebuffer) { glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB); p_glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    if (g_backend == Backend::GL3) ImGui_ImplOpenGL3_RenderDrawData(&g_lateData);
    else                           ImGui_ImplOpenGL2_RenderDrawData(&g_lateData);
    if (atSwap && p_glBindFramebuffer) p_glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFB);
    static bool logged = false;
    if (!logged) { logged = true; Log(atSwap ? "버튼을 프레임 끝에 그림 (자막 글자 위치 못 찾음)" : "버튼을 자막 글자 직전에 그림"); }
}
void RenderLateCb() { RenderLate(false); }

// 게임이 이번 프레임을 그리기 시작할 때: 게임 내부 화면 크기를 읽어 창 좌표 변환값 갱신
void OnGameFrameStart() {
    if (!g_ready) return;
    GLint vp[4] = {};
    glGetIntegerv(GL_VIEWPORT, vp);
    UpdateScreenMap(vp[2], vp[3]);
}

// 게임이 확인 창을 그리기 직전 (게임 화면 위, 확인 창 아래)
void RenderBeforeModal() {
    if (!g_ready) return;
    GLint vp[4] = {};
    glGetIntegerv(GL_VIEWPORT, vp);
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(g_window, &w, &h);
    if (g_earlyDone || g_earlyBroken) return;
    if (vp[0] != 0 || vp[1] != 0 || vp[2] != w || vp[3] != h) {
        g_earlyBroken = true;
        Log("확인 창 직전 그리기 불가 (viewport %d,%d %dx%d / 창 %dx%d) → 확인 창이 뜨면 숨김", vp[0], vp[1], vp[2], vp[3], w, h);
        return;
    }
    static bool logged = false;
    if (!logged) { logged = true; GLint fb = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb); Log("게임 화면 안(확인 창 층 아래)에 오버레이 그림 (FBO %d)", fb); }
    RenderUI(false);
    g_earlyDone = true;
}

} // namespace

namespace Overlay {

void OnFrame(SDL_Window* window) {
    if (g_failed) return;
    if (!g_ready) {
        g_ready = Init(window);
        if (!g_ready) {
            static int tries = 0;
            if (++tries > 5) { g_failed = true; Log("초기화를 포기합니다. 오버레이 비활성화."); }
            return;
        }
    }
    ++g_frames;
    if (g_frames == 1) { Game::Install(); Game::SetModalRenderCallback(&RenderBeforeModal); Game::SetLateRenderCallback(&RenderLateCb); Game::SetFrameStartCallback(&OnGameFrameStart); }
    if (g_earlyDone) g_earlyDone = false;     // 이미 게임 화면 안(확인 창 아래)에 그렸음
    else RenderUI(true);
    RenderLate(true);                          // 자막 글자 직전에 못 그렸으면 여기서
    Game::NewGameFrame();
    Presets::SaveIfDirty();
}

bool OnEvent(SDL_Event* ev) {
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F8 && !ev->key.repeat) {
        g_showWindow = !g_showWindow;
        return true;
    }
    if (!g_ready) return false;
    // 확인 창이 떠 있거나 화면 전환 중이면 마우스는 전부 게임으로 (오버레이 버튼이 눌리지 않게)
    if ((Game::ModalAmount() > 0 || Game::TopMenuOpen() || Game::ScreenVisibility() < 0.5f) &&
        (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEWHEEL)) {
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return false;
    }
    if (!Game::ModEnabled()) {
        ImGui_ImplSDL2_ProcessEvent(ev);
        return ImGui::GetIO().WantCaptureMouse && (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP);
    }
    if (UiPreset::HandleEvent(ev)) return true;
    if (UiHeroPreset::HandleEvent(ev)) return true;
    if (UiBulkBuy::HandleEvent(ev)) return true;

    ImGui_ImplSDL2_ProcessEvent(ev);
    ImGuiIO& io = ImGui::GetIO();
    if ((UiPreset::DragInProgress() || UiHeroPreset::DragInProgress()) &&
        (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEBUTTONDOWN))
        return false;                                       // 게임의 드래그가 끊기지 않도록
    switch (ev->type) {
        case SDL_MOUSEMOTION:
            return io.WantCaptureMouse;                     // 우리 창 위에서는 게임 툴팁이 뜨지 않도록
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL:
            return io.WantCaptureMouse;
        case SDL_KEYDOWN: case SDL_KEYUP: case SDL_TEXTINPUT: case SDL_TEXTEDITING:
            return io.WantCaptureKeyboard;
        default:
            return false;
    }
}

void OnContextDestroyed(SDL_GLContext ctx) {
    Presets::Flush(); HeroPresets::Flush();   // 게임을 닫을 때 방금 고친 내용도 저장
    if (g_ready && ctx == g_context) Shutdown();
}

} // namespace Overlay

// DD Preset Tools - 로더 (게임 폴더 _windows\win64\SDL2.dll)
// 원래 SDL2.dll 은 SDL2_orig.dll 로 이름을 바꿔 두고, SDL 함수는 전부 그쪽으로 넘깁니다.
// 기능 본체(dd_preset_tools.dll)는 mods 폴더나 창작마당 폴더의 dd_preset_tools\ 에 있을 때만 불러옵니다.
// → 모드 폴더를 지우면(또는 구독 해제) 기능이 완전히 사라지고, 이 로더는 아무 일도 하지 않습니다.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include "SDL2/SDL.h"

using PFN_SwapWindow    = void (SDLCALL*)(SDL_Window*);
using PFN_PollEvent     = int  (SDLCALL*)(SDL_Event*);
using PFN_DeleteContext = void (SDLCALL*)(SDL_GLContext);
using PFN_OnFrame   = void (*)(SDL_Window*);
using PFN_OnEvent   = int  (*)(SDL_Event*);
using PFN_OnDestroy = void (*)(SDL_GLContext);

static PFN_SwapWindow o_Swap = nullptr;
static PFN_PollEvent o_Poll = nullptr;
static PFN_DeleteContext o_Del = nullptr;
static PFN_OnFrame f_Frame = nullptr;
static PFN_OnEvent f_Event = nullptr;
static PFN_OnDestroy f_Destroy = nullptr;
static bool g_tried = false;
static std::wstring g_exeDir;

static void LLog(const char* fmt, const char* a = "") {
    FILE* f = _wfopen((g_exeDir + L"\\dd_preset_loader.log").c_str(), L"a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    fprintf(f, fmt, a); fputc('\n', f); fclose(f);
}
static std::string U8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

static bool Originals() {
    if (o_Swap) return true;
    HMODULE h = GetModuleHandleW(L"SDL2_orig.dll");
    if (!h) h = LoadLibraryW(L"SDL2_orig.dll");
    if (!h) return false;
    o_Swap = (PFN_SwapWindow)GetProcAddress(h, "SDL_GL_SwapWindow");
    o_Poll = (PFN_PollEvent)GetProcAddress(h, "SDL_PollEvent");
    o_Del  = (PFN_DeleteContext)GetProcAddress(h, "SDL_GL_DeleteContext");
    return o_Swap && o_Poll && o_Del;
}

// <폴더>\*\dd_preset_tools\dd_preset_tools.dll 찾기
static std::wstring FindIn(const std::wstring& dir) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        std::wstring p = dir + L"\\" + fd.cFileName + L"\\dd_preset_tools\\dd_preset_tools.dll";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) { found = p; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static void TryLoadTools() {
    if (g_tried) return;
    g_tried = true;
    std::wstring root = g_exeDir.substr(0, g_exeDir.find_last_of(L"\\/"));   // ..\_windows
    root = root.substr(0, root.find_last_of(L"\\/"));                          // 게임 폴더
    std::wstring p = FindIn(root + L"\\mods");
    if (p.empty()) {
        std::wstring sa = root.substr(0, root.find_last_of(L"\\/"));          // ...\common
        sa = sa.substr(0, sa.find_last_of(L"\\/"));                            // ...\steamapps
        p = FindIn(sa + L"\\workshop\\content\\262060");
    }
    if (p.empty()) { LLog("dd_preset_tools.dll not found in mods or workshop - tools disabled"); return; }
    HMODULE m = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) { LLog("failed to load %s", U8(p).c_str()); return; }
    f_Frame   = (PFN_OnFrame)GetProcAddress(m, "DDPT_OnFrame");
    f_Event   = (PFN_OnEvent)GetProcAddress(m, "DDPT_OnEvent");
    f_Destroy = (PFN_OnDestroy)GetProcAddress(m, "DDPT_OnContextDestroyed");
    LLog(f_Frame ? "loaded %s" : "bad tools dll %s", U8(p).c_str());
}

extern "C" void SDLCALL Hook_SDL_GL_SwapWindow(SDL_Window* w) {
    if (!Originals()) return;
    TryLoadTools();
    if (f_Frame) f_Frame(w);
    o_Swap(w);
}
extern "C" int SDLCALL Hook_SDL_PollEvent(SDL_Event* ev) {
    if (!Originals()) return 0;
    for (;;) {
        int r = o_Poll(ev);
        if (!r || !ev || !f_Event) return r;
        if (!f_Event(ev)) return r;
    }
}
extern "C" void SDLCALL Hook_SDL_GL_DeleteContext(SDL_GLContext c) {
    if (!Originals()) return;
    if (f_Destroy) f_Destroy(c);
    o_Del(c);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path); g_exeDir = p.substr(0, p.find_last_of(L"\\/"));
    }
    return TRUE;
}

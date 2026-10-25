// DD Trinket Preset - SDL2 proxy entry point
// 원본 SDL2.dll 을 SDL2_orig.dll 로 바꾸고, 이 DLL 을 SDL2.dll 로 넣으면
// 게임이 시작될 때 자동으로 로드됩니다. 나머지 SDL 함수는 전부 원본으로 전달(forward)됩니다.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>

#include "SDL2/SDL.h"
#include "overlay.h"
#include "log.h"

// ---------------------------------------------------------------------------
// 로그
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;
static std::wstring g_modDir;

const std::wstring& ModDir() { return g_modDir; }

void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------------------------------------------------------------------------
// 로더(SDL2.dll)가 부르는 함수
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) void DDPT_OnFrame(SDL_Window* window) { Overlay::OnFrame(window); }
extern "C" __declspec(dllexport) int  DDPT_OnEvent(SDL_Event* ev) {
    if (ev && ev->type == SDL_QUIT) Log("종료 요청 받음 (SDL_QUIT)");
    if (ev && ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_CLOSE) Log("창 닫기 요청 받음");
    return Overlay::OnEvent(ev) ? 1 : 0;
}
extern "C" __declspec(dllexport) void DDPT_OnContextDestroyed(SDL_GLContext ctx) { Overlay::OnContextDestroyed(ctx); }

// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);   // 게임 실행 파일 폴더 (_windows\\win64)
        std::wstring p(path);
        g_modDir = p.substr(0, p.find_last_of(L"\\/"));
        g_log = _wfopen((g_modDir + L"\\trinket_preset.log").c_str(), L"w");
        Log("DD Trinket Preset v0.9 로드됨 (모드 폴더 DLL) (pid %lu)", GetCurrentProcessId());
        HMODULE exe = GetModuleHandleW(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)exe;
        auto nt  = (IMAGE_NT_HEADERS*)((BYTE*)exe + dos->e_lfanew);
        Log("Darkest.exe base=%p timestamp=0x%08lX", exe, nt->FileHeader.TimeDateStamp);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_log) { Log("종료"); fclose(g_log); g_log = nullptr; }
    }
    return TRUE;
}

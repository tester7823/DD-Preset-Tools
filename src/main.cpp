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

static CRITICAL_SECTION g_logLock;
void Log(const char* fmt, ...) {
    if (!g_log) return;
    char line[2048];
    SYSTEMTIME st; GetLocalTime(&st);
    int n = snprintf(line, sizeof line, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vsnprintf(line + n, sizeof line - n, fmt, ap); va_end(ap);
    EnterCriticalSection(&g_logLock);          // 여러 스레드(파일 읽기 후킹 등)에서 동시에 불려도 안전하게
    if (g_log) { fputs(line, g_log); fputc('\n', g_log); fflush(g_log); }
    LeaveCriticalSection(&g_logLock);
}

// 한 줄 요약: 게임을 닫을 때와 오류가 났을 때 로그 마지막에 남김. 유저가 이 줄만 댓글로 붙여 주면 상태를 알 수 있게
static const char* kReportKeys[] = { "game", "hooks", "mod", "mods", "screen", "slots", "lang", "hash", "err" };
static char g_rep[9][160];
void ReportSet(const char* key, const char* fmt, ...) {
    for (int i = 0; i < 9; ++i) if (strcmp(kReportKeys[i], key) == 0) {
        char buf[160]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
        EnterCriticalSection(&g_logLock); memcpy(g_rep[i], buf, sizeof buf); LeaveCriticalSection(&g_logLock);
        return;
    }
}
void ReportWrite(const char* why) {
    char line[1600]; int n = snprintf(line, sizeof line, "DDPT-REPORT v0.9b (2026-11-03) %s", why);
    EnterCriticalSection(&g_logLock);
    for (int i = 0; i < 9; ++i) if (g_rep[i][0] && n < (int)sizeof line - 1)
        n += snprintf(line + n, sizeof line - n, " | %s=%s", kReportKeys[i], g_rep[i]);
    LeaveCriticalSection(&g_logLock);
    Log("%s", line);
}

// 튕김 진단: 접근 위반 등이 생기면 어느 모듈(우리 DLL인지 게임인지)의 어디인지 로그에 남김
static HMODULE g_self = nullptr;
static void* g_vehHandle = nullptr;
static LONG WINAPI CrashLogger(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG count = 0;
    if (InterlockedIncrement(&count) > 5) return EXCEPTION_CONTINUE_SEARCH;
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr; wchar_t name[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod) && mod) {
        GetModuleFileNameW(mod, name, MAX_PATH);
    }
    const wchar_t* base = wcsrchr(name, L'\\'); base = base ? base + 1 : name;
    char nm[MAX_PATH]; WideCharToMultiByte(CP_UTF8, 0, base, -1, nm, sizeof nm, nullptr, nullptr);
    ULONG_PTR info1 = ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
    Log("[오류] 예외 0x%08lX 위치 %s+0x%llX (%s) 접근 주소 %p", code, nm,
        (unsigned long long)((BYTE*)addr - (BYTE*)mod), mod == g_self ? u8"이 모드" : u8"다른 모듈", (void*)info1);
    ReportSet("err", "%08lX@%s+%llX", code, nm, (unsigned long long)((BYTE*)addr - (BYTE*)mod));
    ReportWrite(mod == g_self ? "ERROR-IN-MOD" : "ERROR-OTHER");
    return EXCEPTION_CONTINUE_SEARCH;           // 처리는 원래대로 (게임/윈도우가 처리)
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
        InitializeCriticalSection(&g_logLock);
        g_self = inst;
        g_vehHandle = AddVectoredExceptionHandler(0, CrashLogger);
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
        if (g_vehHandle) { RemoveVectoredExceptionHandler(g_vehHandle); g_vehHandle = nullptr; }
        if (g_log) { Log("종료"); ReportWrite("EXIT"); fclose(g_log); g_log = nullptr; }
    }
    return TRUE;
}

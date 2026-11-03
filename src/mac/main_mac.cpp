// DD Preset Tools - macOS 기능 본체 (dd_preset_tools.dylib)
// 로더(SDL2 프록시)가 dlopen 한 뒤 DDPT_OnFrame / DDPT_OnEvent / DDPT_OnContextDestroyed 를 부릅니다.
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <pthread.h>
#include <signal.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <mach-o/dyld.h>
#include "windows.h"
#include "SDL2/SDL.h"
#include "../overlay.h"
#include "../log.h"

static FILE* g_log = nullptr;
static std::wstring g_modDir;
static pthread_mutex_t g_logLock = PTHREAD_MUTEX_INITIALIZER;
const std::wstring& ModDir() { return g_modDir; }

void Log(const char* fmt, ...) {
    if (!g_log) return;
    char line[2048];
    struct timeval tv; gettimeofday(&tv, nullptr);
    struct tm lt; localtime_r(&tv.tv_sec, &lt);
    int n = snprintf(line, sizeof line, "[%02d:%02d:%02d.%03d] ", lt.tm_hour, lt.tm_min, lt.tm_sec, (int)(tv.tv_usec / 1000));
    va_list ap; va_start(ap, fmt); vsnprintf(line + n, sizeof line - n, fmt, ap); va_end(ap);
    pthread_mutex_lock(&g_logLock);
    if (g_log) { fputs(line, g_log); fputc('\n', g_log); fflush(g_log); }
    pthread_mutex_unlock(&g_logLock);
}

static const char* kReportKeys[] = { "game", "hooks", "mod", "mods", "screen", "slots", "lang", "hash", "err" };
static char g_rep[9][160];
void ReportSet(const char* key, const char* fmt, ...) {
    for (int i = 0; i < 9; ++i) if (strcmp(kReportKeys[i], key) == 0) {
        char buf[160]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
        pthread_mutex_lock(&g_logLock); memcpy(g_rep[i], buf, sizeof buf); pthread_mutex_unlock(&g_logLock);
        return;
    }
}
void ReportWrite(const char* why) {
    char line[1600]; int n = snprintf(line, sizeof line, "DDPT-REPORT v0.9b-mac (2026-11-03) %s", why);
    pthread_mutex_lock(&g_logLock);
    for (int i = 0; i < 9; ++i) if (g_rep[i][0] && n < (int)sizeof line - 1)
        n += snprintf(line + n, sizeof line - n, " | %s=%s", kReportKeys[i], g_rep[i]);
    pthread_mutex_unlock(&g_logLock);
    Log("%s", line);
}

// 튕김 진단: 어느 모듈의 어디에서 났는지 남기고 원래 처리(게임 종료)로 넘김
static struct sigaction g_oldSegv, g_oldBus, g_oldIll, g_oldFpe;
static void CrashHandler(int sig, siginfo_t* si, void* ctx) {
    static volatile int count = 0;
    if (++count <= 3) {
        void* pc = nullptr;
#if defined(__x86_64__)
        pc = (void*)((ucontext_t*)ctx)->uc_mcontext->__ss.__rip;
#endif
        Dl_info di{}; const char* name = "?"; uintptr_t off = 0; bool mine = false;
        if (pc && dladdr(pc, &di) && di.dli_fname) {
            name = strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname;
            off = (uintptr_t)pc - (uintptr_t)di.dli_fbase;
            Dl_info me{}; mine = dladdr((void*)&CrashHandler, &me) && me.dli_fbase == di.dli_fbase;
        }
        Log("[오류] 신호 %d 위치 %s+0x%lX (%s) 접근 주소 %p", sig, name, (unsigned long)off, mine ? u8"이 모드" : u8"다른 모듈", si ? si->si_addr : nullptr);
        ReportSet("err", "SIG%d@%s+%lX", sig, name, (unsigned long)off);
        ReportWrite(mine ? "ERROR-IN-MOD" : "ERROR-OTHER");
    }
    struct sigaction* old = sig == SIGSEGV ? &g_oldSegv : sig == SIGBUS ? &g_oldBus : sig == SIGILL ? &g_oldIll : &g_oldFpe;
    sigaction(sig, old, nullptr);      // 원래 처리기로 되돌리고 다시 발생시킴
    if (!(old->sa_flags & SA_SIGINFO) && old->sa_handler == SIG_DFL) signal(sig, SIG_DFL);
}
static void InstallCrash() {
    struct sigaction sa{}; sa.sa_sigaction = CrashHandler; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_oldSegv); sigaction(SIGBUS, &sa, &g_oldBus);
    sigaction(SIGILL, &sa, &g_oldIll); sigaction(SIGFPE, &sa, &g_oldFpe);
}

extern "C" __attribute__((visibility("default"))) void DDPT_OnFrame(SDL_Window* window) { Overlay::OnFrame(window); }
extern "C" __attribute__((visibility("default"))) int DDPT_OnEvent(SDL_Event* ev) {
    if (ev && ev->type == SDL_QUIT) Log("종료 요청 받음 (SDL_QUIT)");
    if (ev && ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_CLOSE) Log("창 닫기 요청 받음");
    return Overlay::OnEvent(ev) ? 1 : 0;
}
extern "C" __attribute__((visibility("default"))) void DDPT_OnContextDestroyed(SDL_GLContext ctx) { Overlay::OnContextDestroyed(ctx); }

static void AtExit() { if (g_log) { Log("종료"); ReportWrite("EXIT"); fclose(g_log); g_log = nullptr; } }

__attribute__((constructor)) static void Init() {
    // 로그/설정 위치: 게임 폴더의 _osx (윈도우판의 _windows\win64 와 같은 역할. 창작마당 업데이트에 지워지지 않음)
    wchar_t exe[2048]; GetModuleFileNameW(nullptr, exe, 2048);
    char u[4096]; WideCharToMultiByte(CP_UTF8, 0, exe, -1, u, sizeof u, nullptr, nullptr);
    std::string dir = u;
    for (int i = 0; i < 4 && dir.find('/') != std::string::npos; ++i) dir = dir.substr(0, dir.find_last_of('/'));   // Darkest → MacOS → Contents → Darkest.app → _osx
    if (access(dir.c_str(), W_OK) != 0) {     // 쓸 수 없으면 dylib 폴더
        Dl_info di{};
        if (dladdr((void*)&Init, &di) && di.dli_fname) { dir = di.dli_fname; dir = dir.substr(0, dir.find_last_of('/')); }
    }
    wchar_t w[2048]; MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, w, 2048);
    g_modDir = w;
    g_log = fopen((dir + "/trinket_preset.log").c_str(), "w");
    InstallCrash();
    atexit(AtExit);
    Log("DD Preset Tools v0.9b (macOS 베타) 로드됨 (pid %d)", (int)getpid());
}

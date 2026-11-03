// DD Preset Tools - macOS 로더 (Darkest.app/Contents/Frameworks/SDL2.framework/Versions/A/SDL2 자리에 들어감)
// 원래 SDL2 는 같은 폴더의 SDL2_orig 로 이름을 바꿔 두고, SDL 함수는 전부 그쪽으로 넘깁니다(재내보내기).
// 기능 본체(dd_preset_tools.dylib)는 mods 폴더나 창작마당 폴더의 <모드>/mac_only/ 에 있을 때만 불러옵니다.
#include <cstdio>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdlib.h>

struct SDL_Window; union SDL_Event;
typedef void* SDL_GLContext;
using PFN_Swap = void (*)(SDL_Window*);
using PFN_Poll = int  (*)(SDL_Event*);
using PFN_Del  = void (*)(SDL_GLContext);
using PFN_OnFrame = void (*)(SDL_Window*);
using PFN_OnEvent = int  (*)(SDL_Event*);
using PFN_OnDestroy = void (*)(SDL_GLContext);

static PFN_Swap o_Swap; static PFN_Poll o_Poll; static PFN_Del o_Del;
static PFN_OnFrame f_Frame; static PFN_OnEvent f_Event; static PFN_OnDestroy f_Destroy;
static bool g_tried = false;
static std::string g_osxDir;      // 게임 폴더/_osx (로그 위치)

static void LLog(const char* fmt, const char* a = "") {
    if (g_osxDir.empty()) return;
    FILE* f = fopen((g_osxDir + "/dd_preset_loader.log").c_str(), "a");
    if (!f) return;
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] ", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    fprintf(f, fmt, a); fputc('\n', f); fclose(f);
}
static std::string Up(std::string p, int n) { while (n-- > 0 && p.find('/') != std::string::npos) p = p.substr(0, p.find_last_of('/')); return p; }

static bool Originals() {
    if (o_Swap) return true;
    static void* h = nullptr;
    if (!h) {
        Dl_info di{};
        if (dladdr((void*)&Originals, &di) && di.dli_fname) {
            std::string dir = Up(di.dli_fname, 1);
            h = dlopen((dir + "/SDL2_orig").c_str(), RTLD_NOW | RTLD_GLOBAL);
        }
        if (!h) h = RTLD_NEXT;
    }
    o_Swap = (PFN_Swap)dlsym(h, "SDL_GL_SwapWindow");
    o_Poll = (PFN_Poll)dlsym(h, "SDL_PollEvent");
    o_Del  = (PFN_Del)dlsym(h, "SDL_GL_DeleteContext");
    return o_Swap && o_Poll && o_Del;
}

// <폴더>/*/mac_only/dd_preset_tools.dylib 찾기
static std::string FindIn(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string p = dir + "/" + e->d_name + "/mac_only/dd_preset_tools.dylib";
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) { found = p; break; }
    }
    closedir(d);
    return found;
}

static void TryLoadTools() {
    if (g_tried) return;
    g_tried = true;
    std::string root = Up(g_osxDir, 1);                      // 게임 폴더 (…/common/DarkestDungeon)
    std::string p = FindIn(root + "/mods");
    if (p.empty()) p = FindIn(Up(root, 2) + "/workshop/content/262060");   // …/steamapps/workshop/content/262060
    if (p.empty()) { LLog("dd_preset_tools.dylib not found in mods or workshop - tools disabled"); return; }
    void* m = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m) { LLog("failed to load %s", p.c_str()); LLog("  dlerror: %s", dlerror()); return; }
    f_Frame   = (PFN_OnFrame)dlsym(m, "DDPT_OnFrame");
    f_Event   = (PFN_OnEvent)dlsym(m, "DDPT_OnEvent");
    f_Destroy = (PFN_OnDestroy)dlsym(m, "DDPT_OnContextDestroyed");
    LLog(f_Frame ? "loaded %s" : "bad tools dylib %s", p.c_str());
}

extern "C" __attribute__((visibility("default"))) void SDL_GL_SwapWindow(SDL_Window* w) {
    if (!Originals()) return;
    TryLoadTools();
    if (f_Frame) f_Frame(w);
    o_Swap(w);
}
extern "C" __attribute__((visibility("default"))) int SDL_PollEvent(SDL_Event* ev) {
    if (!Originals()) return 0;
    for (;;) {
        int r = o_Poll(ev);
        if (!r || !ev || !f_Event) return r;
        if (!f_Event(ev)) return r;
    }
}
extern "C" __attribute__((visibility("default"))) void SDL_GL_DeleteContext(SDL_GLContext c) {
    if (!Originals()) return;
    if (f_Destroy) f_Destroy(c);
    o_Del(c);
}

__attribute__((constructor)) static void LoaderInit() {
    char buf[PATH_MAX]; uint32_t n = sizeof buf; char real[PATH_MAX];
    if (_NSGetExecutablePath(buf, &n) == 0) g_osxDir = Up(realpath(buf, real) ? real : buf, 4);   // Darkest → MacOS → Contents → Darkest.app → _osx
}

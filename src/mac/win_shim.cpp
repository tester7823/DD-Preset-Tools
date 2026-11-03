// macOS: windows.h 대체 구현
#include "windows.h"
#include <string>
#include <vector>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <mach-o/dyld.h>

static int EncU8(uint32_t c, char* o) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 63)); o[2] = (char)(0x80 | ((c >> 6) & 63)); o[3] = (char)(0x80 | (c & 63)); return 4;
}
int MultiByteToWideChar(UINT, DWORD, const char* s, int n, wchar_t* out, int cap) {
    if (!s) return 0;
    size_t len = n < 0 ? strlen(s) + 1 : (size_t)n;
    int cnt = 0;
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int k;
        if (c < 0x80) { cp = c; k = 1; } else if ((c & 0xE0) == 0xC0) { cp = c & 31; k = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 15; k = 3; } else if ((c & 0xF8) == 0xF0) { cp = c & 7; k = 4; } else { cp = 0xFFFD; k = 1; }
        if (k > 1) for (int j = 1; j < k; ++j) { if (i + j >= len) { cp = 0xFFFD; k = j; break; } cp = (cp << 6) | ((unsigned char)s[i + j] & 63); }
        i += k;
        if (out && cap) { if (cnt >= cap) return 0; out[cnt] = (wchar_t)cp; }
        ++cnt;
    }
    return cnt;
}
int WideCharToMultiByte(UINT, DWORD, const wchar_t* s, int n, char* out, int cap, const char*, BOOL*) {
    if (!s) return 0;
    size_t len = n < 0 ? wcslen(s) + 1 : (size_t)n;
    int cnt = 0; char b[4];
    for (size_t i = 0; i < len; ++i) {
        int k = EncU8((uint32_t)s[i], b);
        if (out && cap) { if (cnt + k > cap) return 0; memcpy(out + cnt, b, k); }
        cnt += k;
    }
    return cnt;
}
static std::string P(const wchar_t* w) {
    std::string s;
    if (!w) return s;
    char b[4];
    for (; *w; ++w) { int k = EncU8((uint32_t)*w, b); s.append(b, k); }
    for (auto& c : s) if (c == '\\') c = '/';
    return s;
}
static void ToW(const char* s, wchar_t* out, size_t cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0 || (size_t)n > cap) { if (cap) out[0] = 0; return; }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap);
}
FILE* _wfopen(const wchar_t* path, const wchar_t* mode) { return fopen(P(path).c_str(), P(mode).c_str()); }
DWORD GetFileAttributesA(const char* path) {
    std::string p = path ? path : ""; for (auto& c : p) if (c == '\\') c = '/';
    struct stat st; if (stat(p.c_str(), &st) != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
DWORD GetFileAttributesW(const wchar_t* path) { return GetFileAttributesA(P(path).c_str()); }
BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD) { return rename(P(from).c_str(), P(to).c_str()) == 0; }
BOOL DeleteFileW(const wchar_t* path) { return unlink(P(path).c_str()) == 0; }

struct FindH { DIR* d; std::string dir, pat; };
static bool NextMatch(FindH* h, WIN32_FIND_DATAW* fd) {
    while (dirent* e = readdir(h->d)) {
        if (fnmatch(h->pat.c_str(), e->d_name, FNM_CASEFOLD) != 0) continue;
        ToW(e->d_name, fd->cFileName, MAX_PATH);
        fd->dwFileAttributes = GetFileAttributesA((h->dir + "/" + e->d_name).c_str());
        if (fd->dwFileAttributes == INVALID_FILE_ATTRIBUTES) fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        return true;
    }
    return false;
}
HANDLE FindFirstFileW(const wchar_t* pattern, WIN32_FIND_DATAW* fd) {
    std::string p = P(pattern);
    size_t s = p.find_last_of('/');
    std::string dir = s == std::string::npos ? "." : p.substr(0, s), pat = s == std::string::npos ? p : p.substr(s + 1);
    DIR* d = opendir(dir.c_str());
    if (!d) return INVALID_HANDLE_VALUE;
    FindH* h = new FindH{ d, dir, pat };
    if (!NextMatch(h, fd)) { closedir(d); delete h; return INVALID_HANDLE_VALUE; }
    return h;
}
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW* fd) { return h && h != INVALID_HANDLE_VALUE && NextMatch((FindH*)h, fd); }
BOOL FindClose(HANDLE h) { if (h && h != INVALID_HANDLE_VALUE) { closedir(((FindH*)h)->d); delete (FindH*)h; } return TRUE; }
ULONGLONG GetTickCount64() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (ULONGLONG)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
DWORD GetTickCount() { return (DWORD)GetTickCount64(); }
void Sleep(DWORD ms) { usleep(ms * 1000); }
DWORD GetModuleFileNameW(HMODULE, wchar_t* out, DWORD cap) {
    char buf[4096]; uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) { if (cap) out[0] = 0; return 0; }
    char real[4096]; const char* p = realpath(buf, real) ? real : buf;
    ToW(p, out, cap);
    return (DWORD)wcslen(out);
}

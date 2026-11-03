#pragma once
// macOS 용 최소 Win32 대체 (공용 코드가 쓰는 함수만). 경로의 '\\' 는 '/' 로 바꿔서 처리
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <strings.h>

typedef uint32_t DWORD; typedef int BOOL; typedef uint8_t BYTE; typedef void* HANDLE; typedef void* HMODULE;
typedef unsigned int UINT; typedef long LONG; typedef unsigned long long ULONGLONG;
typedef const wchar_t* LPCWSTR; typedef wchar_t* LPWSTR; typedef const char* LPCSTR;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define WINAPI
#define APIENTRY
#define __fastcall
#define MAX_PATH 1024
#define CP_UTF8 65001
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8

struct WIN32_FIND_DATAW { DWORD dwFileAttributes; wchar_t cFileName[MAX_PATH]; };

int  MultiByteToWideChar(UINT cp, DWORD fl, const char* s, int n, wchar_t* out, int cap);
int  WideCharToMultiByte(UINT cp, DWORD fl, const wchar_t* s, int n, char* out, int cap, const char*, BOOL*);
FILE* _wfopen(const wchar_t* path, const wchar_t* mode);
DWORD GetFileAttributesW(const wchar_t* path);
DWORD GetFileAttributesA(const char* path);
BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD flags);
BOOL DeleteFileW(const wchar_t* path);
HANDLE FindFirstFileW(const wchar_t* pattern, WIN32_FIND_DATAW* fd);
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW* fd);
BOOL FindClose(HANDLE h);
DWORD GetTickCount();
ULONGLONG GetTickCount64();
DWORD GetModuleFileNameW(HMODULE m, wchar_t* out, DWORD cap);
void Sleep(DWORD ms);
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncasecmp(a, b, n); }
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }

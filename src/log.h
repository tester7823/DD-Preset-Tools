#pragma once
#include <string>
void Log(const char* fmt, ...);
// 댓글 보고용 한 줄 요약 (로그 마지막 줄: "DDPT-REPORT ...")
void ReportSet(const char* key, const char* fmt, ...);
void ReportWrite(const char* why);
const std::wstring& ModDir();

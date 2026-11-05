// 패턴 스캐너 (Windows x64). 설명은 sigscan.h
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <vector>
#include "sigscan.h"

namespace Sig {
namespace {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> fixed;   // 1 = 비교, 0 = 와일드카드
    size_t anchor = 0, anchorLen = 0;   // 가장 긴 고정 구간 (빠른 검색용)
};

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool Parse(const char* s, Pattern& p) {
    while (*s) {
        if (*s == ' ') { ++s; continue; }
        if (s[0] == '?') {
            p.bytes.push_back(0); p.fixed.push_back(0);
            s += (s[1] == '?') ? 2 : 1;
            continue;
        }
        int h = Hex(s[0]), l = Hex(s[1]);
        if (h < 0 || l < 0) return false;
        p.bytes.push_back((uint8_t)(h * 16 + l)); p.fixed.push_back(1);
        s += 2;
    }
    size_t best = 0, cur = 0, start = 0;
    for (size_t i = 0; i <= p.bytes.size(); ++i) {
        if (i < p.bytes.size() && p.fixed[i]) { if (cur == 0) start = i; if (++cur > best) { best = cur; p.anchor = start; } }
        else cur = 0;
    }
    p.anchorLen = best;
    return !p.bytes.empty() && best > 0;
}

// [lo, hi) 에서 패턴을 찾음. 최대 2개까지 세고 첫 위치를 돌려줌
int Find(const uint8_t* lo, const uint8_t* hi, const Pattern& p, const uint8_t** first) {
    int n = 0;
    const size_t len = p.bytes.size();
    if ((size_t)(hi - lo) < len) return 0;
    const uint8_t* a0 = p.bytes.data() + p.anchor;
    const uint8_t* scan = lo + p.anchor;
    const uint8_t* scanEnd = hi - (len - p.anchor) + 1;
    while (scan < scanEnd) {
        const uint8_t* hit = (const uint8_t*)memchr(scan, a0[0], (size_t)(scanEnd - scan));
        if (!hit) break;
        if (memcmp(hit, a0, p.anchorLen) == 0) {
            const uint8_t* s = hit - p.anchor;
            bool ok = true;
            for (size_t k = 0; k < len; ++k) if (p.fixed[k] && s[k] != p.bytes[k]) { ok = false; break; }
            if (ok) { if (n == 0) *first = s; if (++n >= 2) return n; }
        }
        scan = hit + 1;
    }
    return n;
}

bool IsFunctionStart(const uint8_t* base, const uint8_t* addr) {
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry((DWORD64)addr, &imageBase, nullptr);
    return rf && imageBase == (DWORD64)base && (DWORD64)base + rf->BeginAddress == (DWORD64)addr;
}

} // namespace

int ResolveAll(const uint8_t* base, const SigDef* defs, size_t n, SigResult* out) {
    auto dos = (const IMAGE_DOS_HEADER*)base;
    auto nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    const uint8_t* lo = nullptr; const uint8_t* hi = nullptr;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (memcmp(sec[i].Name, ".text", 6) == 0) {
            lo = base + sec[i].VirtualAddress;
            hi = lo + (sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData);
            break;
        }
    }
    const uint32_t imageSize = nt->OptionalHeader.SizeOfImage;
    int good = 0;
    for (size_t i = 0; i < n; ++i) {
        const SigDef& d = defs[i];
        SigResult& r = out[i];
        r = SigResult{};
        Pattern p;
        if (!lo || !Parse(d.pattern, p)) continue;
        const uint8_t* s = nullptr;
        r.matches = Find(lo, hi, p, &s);
        if (r.matches != 1) continue;
        int64_t v = 0;
        switch (d.kind) {
        case SigKind::Func:
            if (!(d.flags & 1) && !IsFunctionStart(base, s)) { r.matches = -1; continue; }
            v = s - base;
            break;
        case SigKind::Rel: {
            int32_t disp; memcpy(&disp, s + d.field, 4);
            v = (s + d.next + disp) - base;
            break;
        }
        case SigKind::Imm:
            if (d.size == 1) v = s[d.field];
            else { int32_t x; memcpy(&x, s + d.field, 4); v = x; }
            break;
        }
        v += d.adjust;
        if (d.kind != SigKind::Imm && (v <= 0 || v >= (int64_t)imageSize)) continue;
        r.value = (uint32_t)v;
        r.ok = true;
        ++good;
    }
    return good;
}

} // namespace Sig

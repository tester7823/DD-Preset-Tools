#include "hook.h"
#include <cstdint>
#include <cstring>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>

namespace Hook {
namespace {
// 대상 근처(±2GB)에 실행 가능한 메모리 확보: 중계 jmp + 원본 앞부분(트램펄린)
uint8_t* g_pool = nullptr; size_t g_used = 0; const size_t kPool = 0x4000;

uint8_t* NearAlloc(uintptr_t target) {
    mach_port_t task = mach_task_self();
    for (uintptr_t d = 0x10000000; d < 0x7F000000; d += 0x1000000) {
        for (int sign = 0; sign < 2; ++sign) {
            uintptr_t a = sign ? target - d : target + d;
            a &= ~(uintptr_t)0xFFFF;
            mach_vm_address_t addr = a;
            if (mach_vm_allocate(task, &addr, kPool, VM_FLAGS_FIXED) == KERN_SUCCESS) {
                if (mach_vm_protect(task, addr, kPool, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE) != KERN_SUCCESS) {
                    mach_vm_deallocate(task, addr, kPool); continue;
                }
                return (uint8_t*)addr;
            }
        }
    }
    return nullptr;
}
bool Near(uintptr_t a, uintptr_t b) { intptr_t d = (intptr_t)(a - b); return d > -0x7FFF0000LL && d < 0x7FFF0000LL; }

// 함수 앞부분에서 5바이트 이상이 되는 명령어 길이 (알려진 모양만)
int PrologueLen(const uint8_t* p) {
    int n = 0;
    while (n < 5) {
        if (p[n] == 0x55) n += 1;                                              // push rbp
        else if (p[n] == 0x48 && p[n + 1] == 0x89 && p[n + 2] == 0xE5) n += 3; // mov rbp, rsp
        else if (p[n] >= 0x50 && p[n] <= 0x57) n += 1;                         // push r
        else if (p[n] == 0x41 && p[n + 1] >= 0x50 && p[n + 1] <= 0x57) n += 2; // push r8-15
        else return -1;
    }
    return n;
}
bool WriteCode(uint8_t* at, const uint8_t* bytes, size_t n) {
    mach_port_t task = mach_task_self();
    mach_vm_address_t page = (uintptr_t)at & ~(uintptr_t)0xFFF;
    mach_vm_size_t len = (((uintptr_t)at + n + 0xFFF) & ~(uintptr_t)0xFFF) - page;
    if (mach_vm_protect(task, page, len, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return false;
    memcpy(at, bytes, n);
    mach_vm_protect(task, page, len, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    sys_icache_invalidate(at, n);
    return true;
}
} // namespace

bool Install(void* target, void* detour, void** original) {
    uint8_t* t = (uint8_t*)target;
    int len = PrologueLen(t);
    if (len < 5) return false;
    if (!g_pool || !Near((uintptr_t)g_pool, (uintptr_t)t) || g_used + 64 > kPool) {
        g_pool = NearAlloc((uintptr_t)t); g_used = 0;
        if (!g_pool) return false;
    }
    uint8_t* tramp = g_pool + g_used;      // [원본 앞 len 바이트][jmp [rip]][주소] ... [중계: jmp [rip]][detour]
    memcpy(tramp, t, len);
    uint8_t* q = tramp + len;
    q[0] = 0xFF; q[1] = 0x25; memset(q + 2, 0, 4); uint64_t back = (uint64_t)(t + len); memcpy(q + 6, &back, 8);
    uint8_t* relay = tramp + 32;
    relay[0] = 0xFF; relay[1] = 0x25; memset(relay + 2, 0, 4); uint64_t d = (uint64_t)detour; memcpy(relay + 6, &d, 8);
    g_used += 64;
    *original = tramp;
    uint8_t patch[16]; memset(patch, 0x90, sizeof patch);    // 남는 바이트는 nop
    patch[0] = 0xE9;
    int32_t rel = (int32_t)((intptr_t)relay - (intptr_t)(t + 5));
    memcpy(patch + 1, &rel, 4);
    return WriteCode(t, patch, (size_t)len);
}
} // namespace Hook

#pragma once
// macOS x86_64 인라인 후킹 (함수 앞 5바이트를 jmp 로 바꿈). 함수 시작 모양이
// push rbp; mov rbp,rsp; push rXX 인 경우만 (게임 함수는 모두 이 모양) → 그 외에는 거부
#include <cstddef>
namespace Hook {
bool Install(void* target, void* detour, void** original);   // 실패하면 false (아무것도 바꾸지 않음)
}

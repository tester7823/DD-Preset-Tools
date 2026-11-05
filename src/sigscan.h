// 실행 시 게임 실행 파일(Darkest.exe)에서 함수/데이터 주소와 구조체 오프셋을 찾아내는 스캐너.
// 게임이 업데이트되어 주소가 바뀌어도 패턴이 맞으면 자동으로 따라감.
// 패턴 표(sigs.inc)는 tools/sigs/gen_sigs.py 로 만들고, 그 스크립트가 각 패턴이 정확히 한 곳에만 맞는지 검증함.
#pragma once
#include <cstdint>
#include <cstddef>

namespace Sig {

enum class SigKind : uint8_t {
    Func,   // 패턴 시작 위치 = 함수 시작 (.pdata 로 함수 시작인지 확인)
    Rel,    // 패턴 안의 rel32/disp32 가 가리키는 곳 (RIP 상대)
    Imm,    // 패턴 안의 즉시값/변위 (구조체 오프셋, 크기, 이미지 기준 RVA)
};

struct SigDef {
    const char* name;
    SigKind kind;
    const char* pattern;   // "48 8B ?? ..." 형식
    int field;             // Rel/Imm: 값이 있는 바이트 위치 (패턴 시작 기준)
    int next;              // Rel: 그 명령어가 끝나는 위치 (RIP 기준)
    int size;              // Imm: 1 또는 4
    int adjust;            // 결과에 더할 값
    int flags;             // 1 = .pdata 확인 생략 (작은 썽크 함수)
    uint32_t built;        // 패턴을 만든 빌드에서의 값 (로그용)
};

struct SigResult {
    uint32_t value = 0;    // Func/Rel: RVA, Imm: 값
    int matches = 0;       // 패턴이 맞은 곳의 수 (1 이어야 정상)
    bool ok = false;
};

// base: 게임 모듈 시작 주소. defs[n] 를 모두 찾아 out[n] 에 채움. 성공한 개수를 돌려줌.
int ResolveAll(const uint8_t* base, const SigDef* defs, size_t n, SigResult* out);

} // namespace Sig

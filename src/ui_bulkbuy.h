#pragma once
#include "SDL2/SDL.h"

// 원정 준비 화면: 골드 아이템 일괄 구매 버튼 + 확인 창
namespace UiBulkBuy {
    void Draw();
    bool HandleEvent(const SDL_Event* ev);
    bool DialogOpen();
}

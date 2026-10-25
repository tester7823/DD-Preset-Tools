#pragma once
#include "SDL2/SDL.h"

// 영웅(파티) 프리셋 UI — 영지 지도 화면의 파티 슬롯 오른쪽 집사 버튼
namespace UiHeroPreset {
    void Draw();
    bool HandleEvent(const SDL_Event* ev);
    bool DragInProgress();   // 영웅을 끌고 있는 중 (마우스 이벤트를 게임에 그대로 넘김)
}

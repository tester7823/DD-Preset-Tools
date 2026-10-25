#pragma once
#include "SDL2/SDL.h"

namespace UiPreset {
    void Draw();                       // 매 프레임 (ImGui::NewFrame 이후)
    bool HandleEvent(const SDL_Event* ev);   // ESC 등 처리. true = 게임에 전달하지 않음
    bool IsOpen();
    bool DragInProgress();   // 게임에서 장신구를 끌고 있는 중 (마우스 이벤트를 게임에 그대로 넘겨야 함)
}

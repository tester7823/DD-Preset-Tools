#pragma once
#include "SDL2/SDL.h"

namespace Overlay {
    void OnFrame(SDL_Window* window);          // SDL_GL_SwapWindow 직전에 호출
    bool OnEvent(SDL_Event* ev);               // true 를 반환하면 게임에 전달하지 않음
    void OnContextDestroyed(SDL_GLContext ctx);
}

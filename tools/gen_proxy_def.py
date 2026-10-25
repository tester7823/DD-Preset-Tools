#!/usr/bin/env python3
# src/sdl2_orig.def (SDL2 export list) -> proxy.def for the loader SDL2.dll:
# every export is forwarded to SDL2_orig.dll, except the three the loader hooks.
import sys
HOOKED = {"SDL_GL_DeleteContext", "SDL_GL_SwapWindow", "SDL_PollEvent"}
src, out = sys.argv[1], sys.argv[2]
names = [l.strip() for l in open(src).read().splitlines()[2:] if l.strip()]
with open(out, "w", newline="\n") as f:
    f.write("LIBRARY SDL2.dll\nEXPORTS\n")
    for n in names:
        f.write(f"    {n} = {'Hook_' + n if n in HOOKED else 'SDL2_orig.' + n}\n")

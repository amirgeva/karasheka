#pragma once

// Reading the Shift keys reliably next to the numpad. With NumLock on,
// Windows turns Shift + a numpad key into a cursor key by pretending Shift
// was let go while the numpad key is down, so SDL sees Shift released (and
// pressed again on letting go of the numpad key). Raw keyboard input reports
// the keys actually held, so on Windows the Shift keys are read from it.

#include <SDL.h>

namespace kara {

void rawKeysInit(SDL_Window* window);
void rawKeysFocus(bool gained);  // the held-key picture is stale after a focus change

// Whether a key is held: SDL's keyboard state, except for the Shift keys on
// Windows.
bool keyHeld(const Uint8* sdlState, SDL_Scancode sc);

}  // namespace kara

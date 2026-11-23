#include "app/rawkeys.h"

#ifdef _WIN32
#include <SDL_syswm.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace kara {

#ifdef _WIN32

namespace {

bool g_live = false;          // raw Shift events are arriving
bool g_shift[2] = {false, false};  // left, right

void SDLCALL messageHook(void*, void*, unsigned int message, Uint64, Sint64 lParam) {
    if (message != WM_INPUT) return;
    RAWINPUT ri;
    UINT size = sizeof(ri);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) ==
        UINT(-1))
        return;
    if (ri.header.dwType != RIM_TYPEKEYBOARD) return;
    const RAWKEYBOARD& k = ri.data.keyboard;
    // E0-prefixed shift codes are the keyboard's own fake shifts around the
    // grey arrow keys, not a Shift key.
    if (k.Flags & RI_KEY_E0) return;
    const bool down = (k.Flags & RI_KEY_BREAK) == 0;
    if (k.MakeCode == 0x2A) g_shift[0] = down;
    else if (k.MakeCode == 0x36) g_shift[1] = down;
    else return;
    g_live = true;
}

}  // namespace

void rawKeysInit(SDL_Window* window) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return;
    RAWINPUTDEVICE dev{};
    dev.usUsagePage = 0x01;  // generic desktop
    dev.usUsage = 0x06;      // keyboard
    dev.dwFlags = 0;         // the usual key messages keep coming too
    dev.hwndTarget = info.info.win.window;
    if (!RegisterRawInputDevices(&dev, 1, sizeof(dev))) return;
    SDL_SetWindowsMessageHook(messageHook, nullptr);
    rawKeysFocus(true);
}

void rawKeysFocus(bool gained) {
    // Raw input only arrives while the window has the focus.
    g_shift[0] = gained && (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    g_shift[1] = gained && (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
}

bool keyHeld(const Uint8* sdlState, SDL_Scancode sc) {
    if (g_live && sc == SDL_SCANCODE_LSHIFT) return g_shift[0];
    if (g_live && sc == SDL_SCANCODE_RSHIFT) return g_shift[1];
    return sdlState[sc] != 0;
}

#else

void rawKeysInit(SDL_Window*) {}
void rawKeysFocus(bool) {}
bool keyHeld(const Uint8* sdlState, SDL_Scancode sc) { return sdlState[sc] != 0; }

#endif

}  // namespace kara

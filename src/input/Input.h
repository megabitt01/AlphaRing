#pragma once

#include <Windows.h>
#include <Xinput.h>

namespace AlphaRing {
    namespace Input {
        bool Init();
        bool Shutdown();
        bool Update();
        // Safe for per-frame polling: skips disconnected slots (cached
        // connected mask) to avoid XInputGetState's stall on empty slots,
        // and always zeroes *pState when returning false.
        bool GetXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState);
        // Full rescan of the controller slots on the next poll; called on
        // WM_DEVICECHANGE so hot-plug is detected at once.
        void RequestPadRescan();
        void SetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
        // The Xbox menu (F4) has controller `pad`: while it's open, and after it closes until what was held then
        // (the A or B that closed it) is let go, so the game and MCC's menus see the pad released.
        bool PadTakenByMenu(DWORD pad, const XINPUT_GAMEPAD& gamepad);
        // The Xbox menu is open: the game and MCC's menus get no input.
        bool XboxMenuOpen();
    };
}
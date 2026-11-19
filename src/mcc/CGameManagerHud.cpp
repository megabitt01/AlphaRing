#include "CGameManager.h"

#include "mcc/hud/Hud.h"

#include <intrin.h>

// The games call these host methods from their HUD drawing (MCC's own versions back a feature-flagged HUD editor
// and the colour-blind filter). MCC's answer is kept and the drawing player's HUD settings are added on top - see
// mcc/hud/Hud.h. With a player's settings at their defaults the game gets MCC's answer as it was.

bool CGameManager::get_hud_element_transform(CGameManager* self, int element, float* dx, float* dy, float* scale) {
    if (ppOriginal.get_hud_element_transform(self, element, dx, dy, scale)) {
        MCC::Hud::Transform(element, dx, dy, scale, _ReturnAddress());
        return true;
    }
    // MCC has none: ours alone, written only when there is one
    float x = 0.0f, y = 0.0f, s = 1.0f;
    if (!MCC::Hud::Transform(element, &x, &y, &s, _ReturnAddress())) return false;
    *dx = x;
    *dy = y;
    *scale = s;
    return true;
}

// Only asked after get_hud_element_transform answered true.
bool CGameManager::get_hud_element_anchor(CGameManager* self, int element, int* anchor) {
    if (ppOriginal.get_hud_element_anchor(self, element, anchor)) return true;
    return MCC::Hud::Anchor(element, anchor);
}

unsigned CGameManager::transform_hud_color(CGameManager* self, int user, unsigned argb) {
    return MCC::Hud::Color(user, ppOriginal.transform_hud_color(self, user, argb));
}

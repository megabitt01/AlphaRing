#include "Hud.h"

#include "common.h"

#include "mcc/CGameGlobal.h"
#include "mcc/module/Module.h"
#include "mcc/splitscreen/LeftRight.h"

#include <offset_halo1.h>
#include <offset_halo2.h>
#include <offset_halo3.h>
#include <offset_halo3odst.h>
#include <offset_haloreach.h>

#include <algorithm>
#include <cmath>
#include <iterator>

// Halo CE's placement hook (module/entry/halo1/hud.cpp)
namespace Halo1::Entry::Hud { const AlphaRing::Feature& Hooks(); }

namespace MCC::Hud {
    static PlayerHud s_players[kPlayers];

    PlayerHud& Player(int player) { return s_players[std::clamp(player, 0, kPlayers - 1)]; }

    // --- the games -----------------------------------------------------------------------------

    // How a game's host offsets are measured.
    enum class Units {
        Canvas,  // the HUD's virtual canvas (Halo 3, ODST, Reach): size = float width, height
        Pixels,  // screen pixels / unit scale (Halo 2): size = int16 view rect {t, l, b, r}
        None,    // offsets come from elsewhere (Halo CE: OffsetCE)
    };

    // What each game's answers read. In an MCC build without one of them, that game's HUD is left as it is. Halo CE's
    // also need its placement hook, which moves CE's elements (OffsetCE), and Reach's the return address that tells
    // its widgets' motion-sensor ask apart (without it the radar would move twice).
    static AlphaRing::Feature s_halo1{"Halo CE per-player HUD", {OFFSET_HALO1_PV_HUD_DRAWING_PLAYER}};
    const bool s_halo1_hook = (s_halo1.Requires(Halo1::Entry::Hud::Hooks()), true);
    static AlphaRing::Feature s_halo2{"Halo 2 per-player HUD", {OFFSET_HALO2_PV_HUD_DRAWING_PLAYER,
                                      OFFSET_HALO2_PV_HUD_VIEW_BOUNDS, OFFSET_HALO2_PV_HUD_UNIT_SCALE}};
    static AlphaRing::Feature s_halo3{"Halo 3 per-player HUD", {OFFSET_HALO3_PV_HUD_DRAWING_PLAYER,
                                      OFFSET_HALO3_PV_HUD_CANVAS}};
    static AlphaRing::Feature s_halo3odst{"ODST per-player HUD", {OFFSET_HALO3ODST_PV_HUD_DRAWING_PLAYER,
                                          OFFSET_HALO3ODST_PV_HUD_CANVAS}};
    static AlphaRing::Feature s_haloreach{"Reach per-player HUD", {OFFSET_HALOREACH_DAT_CURRENT_HUD_PLAYER,
                                          OFFSET_HALOREACH_PV_HUD_CANVAS,
                                          OFFSET_HALOREACH_V_HUD_WIDGET_TRANSFORM_RETURN}};

    // Per game: where it keeps the player whose HUD is being drawn, how MCC's element ids map to
    // ours (1 motion sensor, 2-5 grenade kinds, 6 shield/health, 7 weapon, 9 crosshair,
    // 11 equipment, 12 messages; Halo 2 has no separate shield element - its shield sits with
    // the motion tracker), the units of its host offsets, and for the HUD area presets the shape
    // of the box the game lays its HUD out in (0: the whole view; Halo CE: a centered 4:3 box)
    // and which side of it each of our elements sits on (-1 left, 1 right, 0 centered).
    struct Game {
        int id;
        const AlphaRing::Feature* offsets; // every offset below
        const AlphaRing::Offset* drawing_user;
        bool user_is_short;
        const signed char* elements; // [16], by MCC element id
        Units units;
        const AlphaRing::Offset *size, *unit_scale; // or none
        float area;
        signed char sides[kElementCount]; // motion sensor, shield, weapon, grenades, crosshair, equipment, messages
        bool (*box_is_area)(__int64 module) = nullptr; // whether the HUD box really is `area` right now
        const AlphaRing::Offset* widget_motion_sensor = nullptr; // a widget asking for the motion sensor the view moved
    };

    // Halo 2 lays its HUD out in the whole view unless the profiles' "HUD anchor: Centered" is on
    // and our fix for it (the "HUD at screen edges" patch) has been switched off. In a build where
    // that can't be told, the area presets leave Halo 2's HUD alone.
    static bool Halo2HudInView(__int64 module) {
        if (!AlphaRing::Found({OFFSET_HALO2_PV_HUD_ASPECT_LOCK, OFFSET_HALO2_PF_HUD_ASPECT_LOCK})) return false;
        return !*(const bool*)(module + OFFSET_HALO2_PV_HUD_ASPECT_LOCK) ||
               *(const unsigned char*)(module + OFFSET_HALO2_PF_HUD_ASPECT_LOCK) == 0xEB;
    }

    static constexpr signed char kHalo1Elements[16] = {
        -1, MotionSensor, Grenades, -1, -1, -1, Shield, Weapon, -1, Crosshair, -1, -1, Messages, -1, -1, -1};
    static constexpr signed char kHalo2Elements[16] = {
        -1, MotionSensor, Grenades, -1, -1, -1, -1, Weapon, -1, Crosshair, -1, -1, Messages, -1, -1, -1};
    static constexpr signed char kGen3Elements[16] = {
        -1, MotionSensor, Grenades, Grenades, Grenades, Grenades, Shield, Weapon, -1, Crosshair, -1, Equipment, -1, -1, -1, -1};

    static constexpr Game kGames[] = {
        {CGameGlobal::Halo1, &s_halo1, &OFFSET_HALO1_PV_HUD_DRAWING_PLAYER, true, kHalo1Elements, Units::None, nullptr,
         nullptr, 4.0f / 3.0f, {-1, 1, -1, -1, 0, 0, -1}},
        {CGameGlobal::Halo2, &s_halo2, &OFFSET_HALO2_PV_HUD_DRAWING_PLAYER, false, kHalo2Elements, Units::Pixels,
         &OFFSET_HALO2_PV_HUD_VIEW_BOUNDS, &OFFSET_HALO2_PV_HUD_UNIT_SCALE, 0.0f, {-1, 0, 1, -1, 0, 0, -1}, Halo2HudInView},
        {CGameGlobal::Halo3, &s_halo3, &OFFSET_HALO3_PV_HUD_DRAWING_PLAYER, false, kGen3Elements, Units::Canvas,
         &OFFSET_HALO3_PV_HUD_CANVAS, nullptr, 0.0f, {-1, 0, 1, -1, 0, -1, 0}},
        {CGameGlobal::Halo3ODST, &s_halo3odst, &OFFSET_HALO3ODST_PV_HUD_DRAWING_PLAYER, false, kGen3Elements,
         Units::Canvas, &OFFSET_HALO3ODST_PV_HUD_CANVAS, nullptr, 0.0f, {-1, -1, 1, 1, 0, 1, 0}},
        {CGameGlobal::HaloReach, &s_haloreach, &OFFSET_HALOREACH_DAT_CURRENT_HUD_PLAYER, false, kGen3Elements,
         Units::Canvas, &OFFSET_HALOREACH_PV_HUD_CANVAS, nullptr, 0.0f, {-1, 0, 1, -1, 0, -1, 0}, nullptr,
         &OFFSET_HALOREACH_V_HUD_WIDGET_TRANSFORM_RETURN},
    };
    static_assert(kGames[0].id == CGameGlobal::Halo1); // OffsetCE's

    static const Game* CurrentGame(__int64& module) {
        auto p_global = GameGlobal();
        if (p_global == nullptr) return nullptr;
        for (auto& game : kGames) {
            if (game.id != p_global->current_game) continue;
            module = MCC::Module::GetSubModule(game.id)->info().hModule;
            if (module == 0) return nullptr;
            if (auto missing = game.offsets->Missing()) { // an MCC build without it (src/offsets)
                static bool logged[std::size(kGames)];
                if (!logged[&game - kGames]) {
                    logged[&game - kGames] = true;
                    LOG_WARNING("[Offsets] {} stays off: {} isn't in this build", game.offsets->Name(), missing->name);
                }
                return nullptr;
            }
            return &game;
        }
        return nullptr;
    }

    static int ElementOf(const Game& game, int game_element) {
        return game_element >= 0 && game_element < 16 ? game.elements[game_element] : -1;
    }

    // What `player`'s settings do to one element of `game` in a view `aspect` wide per unit of
    // height (0: no area presets); false when nothing.
    static bool Effective(const Game& game, int player, int game_element, float aspect, float* x, float* y, float* scale) {
        int element = ElementOf(game, game_element);
        if (player < 0 || player >= kPlayers || element < 0) return false;
        auto& settings = s_players[player];
        auto& layout = settings.elements[element];
        *x = layout.x;
        *y = layout.y;
        *scale = layout.hidden ? 0.0f : layout.scale * settings.scale;
        // the area preset moves the sides of the game's HUD box to those of a centered box of that
        // shape (no wider than the view)
        if (settings.area > 0 && aspect > 0.0f) {
            float from = game.area > 0.0f ? std::min(game.area, aspect) : aspect;
            *x += game.sides[element] * 0.5f * (std::min(kAreas[settings.area], aspect) - from) / aspect;
        }
        return *x != 0.0f || *y != 0.0f || *scale != 1.0f;
    }

    bool Transform(int game_element, float* dx, float* dy, float* scale, const void* caller) {
        __int64 module;
        auto game = CurrentGame(module);
        if (game == nullptr) return false;
        if (game->widget_motion_sensor && caller == (const void*)(module + *game->widget_motion_sensor) &&
            ElementOf(*game, game_element) == MotionSensor)
            return false;

        // the size of the drawing player's view: the HUD's virtual canvas, or pixels (Halo 2);
        // Halo CE takes only sizes from here (its offsets: OffsetCE)
        float width = 0.0f, height = 1.0f;
        if (game->units == Units::Canvas) {
            auto canvas = (const float*)(module + *game->size); // width, height
            width = canvas[0];
            height = canvas[1];
        } else if (game->units == Units::Pixels) {
            auto& rect = *(const Splitscreen::LeftRight::Rect*)(module + *game->size);
            width = rect.right - rect.left;
            height = rect.bottom - rect.top;
        }

        auto user_at = module + *game->drawing_user;
        int user = game->user_is_short ? *(short*)user_at : *(int*)user_at;
        float aspect = height > 0.0f && (!game->box_is_area || game->box_is_area(module)) ? width / height : 0.0f;
        float x, y, s;
        if (!Effective(*game, user, game_element, aspect, &x, &y, &s)) return false;
        *scale *= s;
        if (s == 0.0f || game->units == Units::None || height <= 0.0f) return true;
        if (game->units == Units::Pixels) {
            // Halo 2 moves an element by offset x unit scale x its final scale pixels
            float unit = *(const float*)(module + *game->unit_scale) * *scale;
            if (unit <= 0.0f) return true;
            width /= unit;
            height /= unit;
        }
        *dx += x * width;
        *dy -= y * height; // our y points up, the screen's down
        return true;
    }

    bool Anchor(int game_element, int* anchor) {
        float dx = 0.0f, dy = 0.0f, scale = 1.0f;
        if (!Transform(game_element, &dx, &dy, &scale)) return false;
        *anchor = 1; // "unchanged": our offsets are relative to where the element already is
        return true;
    }

    bool OffsetCE(int player, int game_element, float view_width, float view_height, float* dx, float* dy) {
        float x, y, s;
        if (!s_halo1.Available() || view_height <= 0.0f || // with CE's other settings only
            !Effective(kGames[0], player, game_element, view_width / view_height, &x, &y, &s) ||
            (x == 0.0f && y == 0.0f))
            return false;
        *dx = x * view_width;
        *dy = -y * view_height;
        return true;
    }

    // --- colours ---------------------------------------------------------------------------------

    static float Wrap(float degrees) { return degrees - 360.0f * std::floor(degrees / 360.0f); }

    // Moves a colour's hue toward `hue`, keeping its brightness, saturation and alpha. Greys and
    // whites stay as they are, and so do strong reds: the HUD uses them for enemies and danger.
    static unsigned Recolor(unsigned argb, float hue, float strength) {
        float r = ((argb >> 16) & 0xFF) / 255.0f, g = ((argb >> 8) & 0xFF) / 255.0f, b = (argb & 0xFF) / 255.0f;
        float max = std::max({r, g, b}), min = std::min({r, g, b}), delta = max - min;
        if (max <= 0.0f || delta / max < 0.15f) return argb;

        float h = max == r ? 60.0f * std::fmod((g - b) / delta, 6.0f) : max == g ? 60.0f * ((b - r) / delta + 2.0f)
                                                                                 : 60.0f * ((r - g) / delta + 4.0f);
        h = Wrap(h);
        if ((h < 18.0f || h > 342.0f) && delta / max > 0.45f) return argb;

        float shift = Wrap(hue - h + 180.0f) - 180.0f; // shortest way around
        h = Wrap(h + shift * strength);

        float s = delta / max, v = max, c = v * s, x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
        float rgb[3];
        switch ((int)(h / 60.0f) % 6) {
            case 0: rgb[0] = c; rgb[1] = x; rgb[2] = 0; break;
            case 1: rgb[0] = x; rgb[1] = c; rgb[2] = 0; break;
            case 2: rgb[0] = 0; rgb[1] = c; rgb[2] = x; break;
            case 3: rgb[0] = 0; rgb[1] = x; rgb[2] = c; break;
            case 4: rgb[0] = x; rgb[1] = 0; rgb[2] = c; break;
            default: rgb[0] = c; rgb[1] = 0; rgb[2] = x; break;
        }
        auto byte = [](float f) { return (unsigned)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
        return (argb & 0xFF000000u) | byte(rgb[0] + m) << 16 | byte(rgb[1] + m) << 8 | byte(rgb[2] + m);
    }

    unsigned Color(int user, unsigned argb) {
        if (user < 0 || user >= kPlayers) return argb;
        auto& player = s_players[user];
        return player.recolor ? Recolor(argb, player.hue, player.strength) : argb;
    }
}

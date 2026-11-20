#include "LeftRight.h"

#include "common.h"

#include "mcc/CGameGlobal.h"
#include "mcc/module/CModule.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>

namespace MCC::Splitscreen::LeftRight {
    namespace Store = AlphaRing::SplitscreenConfigStore;

    namespace {
        // The table entries the layout owns: 2-player slots 0-1 and 3-player slots 0-2.
        struct Slot { int players, slot; };
        constexpr Slot kSlots[] = {{2, 0}, {2, 1}, {3, 0}, {3, 1}, {3, 2}};
        static_assert(std::size(kSlots) == sizeof(State::saved) / sizeof(State::saved[0]));

        // The render-target variant (the table's "resolution") the full-height halves draw into. The stock
        // two-player layout uses it too, so no other view shares its surface.
        constexpr int kHalfSurface = 3;

        std::atomic<int> s_classic_side_by_side{-1}; // the Halo CE / Halo 2 game whose mission started side by side

        bool ClassicGame(int game) { return game == CGameGlobal::Halo1 || game == CGameGlobal::Halo2; }

        Store::LayoutEntry* Entry(const Gen3& game, __int64 module, const Slot& s) {
            return (Store::LayoutEntry*)(module + game.table) + s.players * Store::SLOT_COUNT + s.slot;
        }

        const int* Screen(const Gen3& game, __int64 module) { return (const int*)(module + game.screen); }

        // A descriptor size in pixels, as the pool computes it: flag bit 0 makes the value a divisor of the
        // screen size, rounded half up; otherwise it is the size itself.
        int Pixels(const Gen3& game, float value, int screen, unsigned flags) {
            if (!(flags & 1)) return (int)value;
            float divided = (float)screen / (std::max)(value, 1.0f);
            return game.double_rounding ? (int)std::floor((double)divided + 0.5) : (int)std::floor(divided + 0.5f);
        }
    }

    int Players(const Gen3& game, __int64 module) { return ((int (*)())(module + game.player_count))(); }

    bool Chosen() {
        return Store::GetTwoPlayerLayout() == Store::TwoPlayerLayout::LeftRight;
    }

    bool Active(int player_count) {
        return Chosen() && (player_count == 2 || player_count == 3);
    }

    bool Supports(int game, int player_count) {
        switch (game) {
            case CGameGlobal::Halo1: case CGameGlobal::Halo2: case CGameGlobal::Halo3: case CGameGlobal::Halo4:
            case CGameGlobal::Halo3ODST: case CGameGlobal::HaloReach:
                return player_count == 2 || player_count == 3;
            default:
                return false;
        }
    }

    bool OnScreen(int game, int player_count) {
        return Supports(game, player_count) &&
               (ClassicGame(game) ? s_classic_side_by_side == game : Chosen()) &&
               (game != CGameGlobal::HaloReach || Store::LeftRightAvailable());
    }

    void StartClassicMission(int game, bool side_by_side) { s_classic_side_by_side = side_by_side ? game : -1; }

    void Choose(bool left_right) {
        auto reach = MCC::Module::GetSubModule(MCC::Module::MODULE_HALOREACH);
        Store::SetTwoPlayerLayout(left_right ? Store::TwoPlayerLayout::LeftRight : Store::TwoPlayerLayout::TopBottom,
                                  reach->info().hModule);
        // Reach's store writes the stock entries back over any black-bar patch the player turned on.
        if (!left_right) reach->patches()->apply();
    }

    Rect ViewSafeBox(const Rect& v) {
        short dy = (short)((v.bottom - v.top) * 0.05f), dx = (short)((v.right - v.left) * 0.05f);
        return {(short)(v.top + dy), (short)(v.left + dx), (short)(v.bottom - dy), (short)(v.right - dx)};
    }

    namespace {
        thread_local int t_view_players = 0, t_view_slot = 0; // the view being set up (ViewSetup)
    }

    ViewSetup::ViewSetup(int slot, int players) { t_view_players = players, t_view_slot = slot; }
    ViewSetup::~ViewSetup() { t_view_players = 0; }

    bool TitleSafe(const Gen3& game, __int64 module, __int64 return_address, short box[4]) {
        int players = t_view_players, slot = t_view_slot;
        if (game.title_safe_return == nullptr || return_address - module != *game.title_safe_return || players < 2 ||
            players > 4 || slot >= players)
            return false;
        auto& entry = *Entry(game, module, {players, slot});
        auto screen = Screen(game, module);
        // the view, as the engine rounds it
        Rect view = {(short)(screen[1] * entry.y0), (short)(screen[0] * entry.x0),
                     (short)(screen[1] * entry.y1), (short)(screen[0] * entry.x1)};
        if (view.right <= view.left || view.bottom <= view.top) return false;
        *(Rect*)box = ViewSafeBox(view);
        return true;
    }

    __int64 ViewportRect(void* original, void* slot, void* players, void* view, void* box) {
        auto result = ((__int64 (*)(void*, void*, void*, void*))original)(slot, players, view, box);
        if ((int)(__int64)players > 1) *(Rect*)box = ViewSafeBox(*(const Rect*)view);
        return result;
    }

    void Frame(const Gen3& game, __int64 module, State& state) {
        if (state.module != module) {
            state.module = module;
            state.table_left_right = false;
        }

        if (Chosen()) {
            for (size_t i = 0; i < std::size(kSlots); ++i) {
                auto entry = Entry(game, module, kSlots[i]);
                if (!state.table_left_right) state.saved[i] = *entry;
                auto& want = Store::LeftRightRegion(kSlots[i].players, kSlots[i].slot);
                if (memcmp(entry, &want, sizeof(want)) != 0) CPatch::apply(entry, &want, sizeof(want));
            }
            state.table_left_right = true;
        } else if (state.table_left_right) {
            for (size_t i = 0; i < std::size(kSlots); ++i)
                CPatch::apply(Entry(game, module, kSlots[i]), &state.saved[i], sizeof(state.saved[i]));
            state.table_left_right = false;
        }

        // The pool sizes its surfaces once, so a switch rebuilds it: the same release/init pair the
        // engine's own resize path runs right before this render call, minus the swap-chain resize.
        unsigned generation = Store::GetLayoutGeneration();
        if (state.built != generation) {
            ((void (*)())(module + game.pool_release))();
            ((void (*)())(module + game.pool_init))();
            state.built = generation;
        }
    }

    bool PaintDividers(const Gen3& game, __int64 module) {
        if (!Chosen()) return false;
        int players = Players(game, module);
        if (!Active(players)) return false;

        // the stock dividers are 2px bands at the screen's middle
        if (game.before_painting) game.before_painting(module);
        auto screen = Screen(game, module);
        PaintBands({0, 0, (short)screen[1], (short)screen[0]}, 1, players,
                   (void (*)(Rect*, unsigned))(module + game.fill_rect));
        return true;
    }

    void PaintBands(const Rect& screen, short half_width, int players, void (*fill)(Rect*, unsigned argb)) {
        short middle_x = (short)((screen.left + screen.right) / 2), middle_y = (short)((screen.top + screen.bottom) / 2);
        Rect down{screen.top, (short)(middle_x - half_width), screen.bottom, (short)(middle_x + half_width)};
        fill(&down, 0xFF000000);
        if (players == 3) {
            Rect across{(short)(middle_y - half_width), middle_x, (short)(middle_y + half_width), screen.right};
            fill(&across, 0xFF000000);
        }
    }

    int HudResolution(const Gen3& game, __int64 module, State& state, int user, int resolution) {
        bool half = resolution == 1 || resolution == 5;
        bool tall = half && Chosen() && Active(Players(game, module));
        if (user >= 0 && user < 4) state.tall_hud[user] = tall;
        if (tall) resolution = resolution == 1 ? 4 : 6;
        // a quarter has the screen's own shape, so the full-screen layout (0, or 2 on 4:3) fits it whole
        if (game.whole_quarter_hud) resolution = resolution == 4 ? 0 : resolution == 6 ? 2 : resolution;
        return resolution;
    }

    const void* HudLayout(const Gen3& game, __int64 module, State& state, int user, const void* layout) {
        if (layout == nullptr || user < 0 || user >= 4) return layout;
        bool tall = state.tall_hud[user], split = game.hud_safe_frame && Players(game, module) > 1;
        if (!tall && !split) return layout;
        auto copy = state.hud_layouts[user];
        memcpy(copy, layout, (std::min)(game.hud_layout_size, kMaxHudLayoutSize));
        if (tall) {
            auto screen = Screen(game, module);
            auto canvas = (int*)(copy + game.hud_canvas);
            if (screen[0] > 1) canvas[1] = (int)((float)canvas[0] * screen[1] / (screen[0] / 2) + 0.5f);
        }
        if (split) { // zero: the game skips the clamp
            auto frame = (float*)(copy + game.hud_safe_frame);
            frame[0] = frame[1] = 0.0f;
        }
        return copy;
    }

    bool ResizesHalfSurface(State& state, int variant) {
        state.built = Store::GetLayoutGeneration();
        return Chosen() && (variant & 3) == kHalfSurface;
    }

    void SizeSurface(const Gen3& game, __int64 module, State& state, int* sizes, const unsigned char* desc,
                     int variant) {
        if (!ResizesHalfSurface(state, variant) || !sizes || !desc) return;

        unsigned flags = *(const unsigned*)desc;
        if (!(flags & 0x20)) return; // not sized per view variant

        auto screen = Screen(game, module);
        auto field = [&](int offset) { return *(const float*)(desc + offset); };
        int w1 = Pixels(game, field(0x08), screen[0], flags), h1 = Pixels(game, field(0x0C), screen[1], flags);
        int w2 = Pixels(game, field(0x20), screen[0], flags), h2 = Pixels(game, field(0x24), screen[1], flags);
        bool same = (flags & 6) == 2; // the second pair copies the first

        int* pair1 = sizes + 6;
        int* pair2 = sizes + 1;
        if (pair1[0] != w1 * 3 / 4 || pair1[1] != h1 / 2 ||
            (!same && (pair2[0] != w2 * 3 / 4 || pair2[1] != h2 / 2))) {
            static bool logged;
            if (!logged) {
                logged = true;
                LOG_WARNING("Left/Right split: render target {}x{} does not match its descriptor, left stock",
                            pair1[0], pair1[1]);
            }
            return;
        }

        pair1[0] = w1 / 2;
        pair1[1] = h1;
        pair2[0] = same ? pair1[0] : w2 / 2;
        pair2[1] = same ? pair1[1] : h2;
    }
}

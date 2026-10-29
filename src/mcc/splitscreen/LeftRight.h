#pragma once

#include "Offset.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"

// The Left/Right split screen in every game. One saved choice (the store's TwoPlayerLayout) drives them all.
// XiaoDanny's Reach port (mcc/module/patch/SplitscreenConfigStore, module/entry/haloreach) showed what the
// layout takes: the split-screen table's regions, the black-bar painter, the render target the half-screen
// views draw into and the HUD's shape. Halo 3, ODST and Halo 4 share Reach's table and render-target pool
// design, so each says where those live in a Gen3 below; Halo 2 and CE lay their views out on a grid
// (module/entry/halo2, halo1). Reach keeps its own implementation (module/entry/haloreach).
namespace MCC::Splitscreen::LeftRight {
    // A screen rectangle in the engines' order.
    struct Rect { short top, left, bottom, right; };

    // Left/Right is the saved choice.
    bool Chosen();

    // Left/Right is what `player_count` local players see: it is chosen and 2 or 3 players are on screen.
    // (Reach's own paths also need every Reach hook that draws it, SplitscreenConfigStore::LeftRightAvailable.)
    bool Active(int player_count);

    // `game` (CGameGlobal::eGame) has the layout for `player_count` players.
    bool Supports(int game, int player_count);

    // Left/Right is what `player_count` players of `game` see. Halo CE and Halo 2 are side by side only in
    // Classic graphics, which a mission starts in (Splitscreen::ClassicGraphicsScope): for them this is whether
    // the mission running now started side by side, and their hooks also ask the game whether Classic graphics
    // are still on screen (Back switches to Anniversary's stacked views in the middle of a mission).
    bool OnScreen(int game, int player_count);

    // A Halo CE or Halo 2 mission starts, side by side or not; the choice takes effect there at the next one.
    void StartClassicMission(int game, bool side_by_side);

    // Saves the choice and puts it on screen in whichever game is running.
    void Choose(bool left_right);

    // Where a Gen3 engine keeps what the layout touches (module-relative addresses).
    struct Gen3 {
        const AlphaRing::Offset& table;        // c_splitscreen_config::m_config_table, {x0, y0, x1, y1, variant}[players * 4 + slot]
        const AlphaRing::Offset& screen;       // int width, height the views are laid out on
        const AlphaRing::Offset& player_count; // int () - local players on screen
        const AlphaRing::Offset& fill_rect;    // void (short rect[4] {top, left, bottom, right}, unsigned argb)
        const AlphaRing::Offset& pool_release; // the render-target pool teardown and rebuild the engine's resize path runs
        const AlphaRing::Offset& pool_init;
        void (*before_painting)(__int64 module) = nullptr; // render state the painter sets up itself (Halo 4)
        bool double_rounding = false; // render-target sizes are rounded in double precision (Halo 3), not float
        int hud_layout_size = 0;      // bytes in a HUD layout record (chud globals) ...
        int hud_canvas = 0;           // ... where it keeps its int canvas width, height ...
        int hud_safe_frame = 0;       // ... and the share of the screen a HUD is clamped to, float H, V (Halo 3, ODST)
        const AlphaRing::Offset* title_safe_return = nullptr; // where the view setup's call for the title-safe box returns (Halo 3, ODST)
        bool whole_quarter_hud = false; // quarters use the full-screen HUD layout (ODST: its quarter ones lack most elements)
    };

    constexpr int kMaxHudLayoutSize = 0x110;

    // Local players on screen (the game's player_count).
    int Players(const Gen3& game, __int64 module);

    // Per-game runtime state, one per game module.
    struct State {
        __int64 module = 0;
        bool table_left_right = false;                     // the table holds the Left/Right regions
        AlphaRing::SplitscreenConfigStore::LayoutEntry saved[5]{}; // the entries they replaced (kSlots)
        unsigned built = 0;                                // layout generation the render targets were made for
        bool tall_hud[4]{};                                // the user's HUD was switched to a full-height half's
        unsigned char hud_layouts[4][kMaxHudLayoutSize]{}; // ... layout, and its layout record, reshaped
    };

    // Before each frame is drawn: keeps the table on the chosen layout (restoring the game's own regions,
    // black-bar patches included, when Left/Right is turned off) and rebuilds the render targets when the
    // choice changed since they were made.
    void Frame(const Gen3& game, __int64 module, State& state);

    // In place of the black-bar painter: draws the Left/Right dividers and returns true while the layout
    // is on screen; returns false to let the game paint its own bars.
    bool PaintDividers(const Gen3& game, __int64 module);

    // The Left/Right dividers on `screen`, bands `half_width` either side of its middle: one down it, and for
    // three players one across the right half, where players 2 and 3 share it.
    void PaintBands(const Rect& screen, short half_width, int players, void (*fill)(Rect*, unsigned argb));

    // HudLayout also leaves off, for every split view, the share of the whole screen Halo 3 and ODST clamp
    // each view's HUD frame to (hud_safe_frame): it trimmed a view on its outer sides only, which sat a
    // quarter's HUD ~34 px toward the middle of the screen, and each view's frame now comes from its own
    // safe box (ViewSafeBox).
    //
    // The HUD of a full-height half. The engine picks a player's HUD layout by the shape of their view
    // (resolution 1 for a two-player half, 4 for a quarter; 5 and 6 on 4:3 screens) and each layout has a
    // virtual canvas, stretched over the view. No layout is taller than 4:3, so a Left/Right half takes a
    // quarter's layout - made for a view half the screen wide - and its canvas is made as tall as the
    // view, which keeps the HUD at a quarter's size without stretching it and puts the elements the
    // layout anchors at the bottom (the motion tracker) at the bottom of the half.
    int HudResolution(const Gen3& game, __int64 module, State& state, int user, int resolution);
    const void* HudLayout(const Gen3& game, __int64 module, State& state, int user, const void* layout);

    // A split-screen view's title-safe box: the box its HUD is laid out in, whose centre its crosshair and
    // aim sit on. The engines clip each view by the whole screen's box (5% in from each edge), which pulls
    // every view's box toward the middle of the screen - a 1080p quarter's crosshair sits 48 px off its
    // centre, and on a screen spanning two monitors the HUD crowds the seam. This is the view's own box
    // instead: the same size, 5% in from the view's edges; a lone view keeps the game's box.
    Rect ViewSafeBox(const Rect& view);

    // Halo 3 and ODST: the view setup asks for the screen's box, then clips the view by it. Their hooks
    // put a ViewSetup around the view setup, and in the title-safe function answer the view setup's call
    // with TitleSafe (true: `box` is the view's own).
    struct ViewSetup {
        ViewSetup(int slot, int players);
        ~ViewSetup();
    };
    bool TitleSafe(const Gen3& game, __int64 module, __int64 return_address, short box[4]);

    // Halo 4 and Reach work a view and its box out together in one function, (int slot, int players,
    // short view[4], short box[4]). From its hook: calls the game's and makes the box the view's own.
    __int64 ViewportRect(void* original, void* slot, void* players, void* view, void* box);

    // From a render-target sizing hook: records that the pool is being built for the current choice, and
    // returns true when a target of this variant is the full-height halves' surface while Left/Right is
    // chosen - the caller then makes it half the screen's width and its full height.
    bool ResizesHalfSurface(State& state, int variant);

    // ResizesHalfSurface for the pool's creation hook (Halo 3, ODST): `sizes` holds the target's two {w, h}
    // pairs (at + 0x18 and + 0x04), already shrunk to the stock variant's three quarters by half. The
    // unshrunk sizes are recomputed from the descriptor and only used when they reproduce the pool's own
    // result, so a misread descriptor keeps the stock size.
    void SizeSurface(const Gen3& game, __int64 module, State& state, int* sizes, const unsigned char* desc,
                     int variant);
}

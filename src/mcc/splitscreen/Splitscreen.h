#pragma once

namespace MCC::Splitscreen {
    bool Initialize();
    void ImGuiContext();
}

namespace MCC::Splitscreen {
    // Halo CE and Halo 2's Anniversary renderer only ever drew two views, stacked, so missions with
    // three or four players, or side by side (LeftRight.h, which takes the layout choice here), start in
    // Classic: bit 0 of the first game-options byte selects Anniversary visuals in both engines, and it
    // is cleared only while the engine copies the options, leaving MCC's own setting untouched. With 3-4
    // players (not side by side) both can keep Anniversary instead, an experimental choice
    // (module/entry/halo1/anniversary.cpp, module/entry/halo2/anniversary.cpp).
    class ClassicGraphicsScope {
    public:
        // `game`: CGameGlobal::Halo1 or Halo2
        ClassicGraphicsScope(unsigned char* game_options, int game);
        ~ClassicGraphicsScope();

    private:
        unsigned char* m_options = nullptr;
        unsigned char m_saved = 0;
    };
}

namespace MCC::Splitscreen {
    // Halo CE and Halo 2 with 3-4 players in Anniversary graphics: everyone's saved choice (off unless
    // chosen), which takes effect as a mission starts, and whether the mission running in `game`
    // (CGameGlobal::Halo1 or Halo2) started with it. Such a mission draws its 3-4 views whenever
    // Anniversary graphics are on screen; it starts in them unless it's side by side.
    bool AnniversaryQuadChosen();
    void ChooseAnniversaryQuad(bool on);
    bool AnniversaryQuadActive(int game);
    // The mode is drawing Halo CE (module/entry/halo1/anniversary.cpp) and puts three players in quarters, as four;
    // Halo 2's keeps its own grid, player 1 on the top half.
    bool AnniversaryQuartersShown(int game);
}

#pragma once

namespace MCC::Splitscreen {
    bool Initialize();
    void ImGuiContext();
}

namespace MCC::Splitscreen {
    // Halo CE and Halo 2's Anniversary renderer only ever drew two views, stacked, so missions with
    // three or four players, or side by side (LeftRight.h, which takes the layout choice here), start in
    // Classic: bit 0 of the first game-options byte selects Anniversary visuals in both engines, and it
    // is cleared only while the engine copies the options, leaving MCC's own setting untouched.
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

#pragma once

struct CUserProfile;
struct _XINPUT_GAMEPAD;
class CPatch;

namespace MCC::Splitscreen {
    bool Initialize();
    void ImGuiContext();
}

namespace MCC::Splitscreen {
    // The Players window (Join.cpp): each controller presses A to join and B to leave. It changes the menus' player
    // state (mcc/settings/MenuSettings) - the players and their controllers - as the Splitscreen window's player count
    // does, and a menu saving writes it with the rest.
    void JoinContext();
    // From the game's input poll (CGameManagerSplitscreen.cpp): true while controller `pad` still holds what it joined
    // with, when the game must see it released - until its buttons and triggers are all let go.
    bool JoinPressHeld(int pad, const _XINPUT_GAMEPAD& gamepad);
    // From the Splitscreen window's Bind: true while controller `pad` still holds a press the Players window or hot join
    // acted on (a join, a leave, a species), which Bind doesn't also take - until it's let go.
    bool PressTaken(int pad, const _XINPUT_GAMEPAD& gamepad);
    // The Splitscreen window's Bind is waiting for a controller on its player's page, drawn with the overlay shown
    // (Splitscreen.cpp): until it has one it has the controllers, as the dashboard has while it's open, so the Players
    // window and hot join don't act on them. Once the page isn't drawn they're theirs again.
    bool BindPending();
    // The player count (Global::MCC::Splitscreen), read with an acquire load: the Players window and hot join put a new
    // player's controller in place before the count, which they publish with a release store, so a reader on the game
    // thread that sees the count sees the controller too.
    int PlayerCount();
    // A player's species (Spartan or Elite) chosen in the Players window, back on their AlphaRing profile after it was
    // copied over from player 1's (CGameManager::apply_profiles, Reach's armour sync); nothing if none was chosen.
    void ApplySpecies(int index, CUserProfile& profile);
}

namespace MCC::Splitscreen {
    // Halo CE and Halo 2's Anniversary renderer only ever drew two views, stacked, so missions with
    // three or four players, or side by side (LeftRight.h, which takes the layout choice here), start in
    // Classic: bit 0 of the first game-options byte selects Anniversary visuals in both engines, and it
    // is cleared only while the engine copies the options, leaving MCC's own setting untouched. With 3-4
    // players (not side by side) both can keep Anniversary instead, an experimental choice
    // (module/entry/halo1/anniversary.cpp, module/entry/halo2/anniversary.cpp). A hot-join mission
    // starts as one for four players would, side by side for when two or three are in: a player can
    // join it at any time.
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
    // Halo CE and Halo 2 with 3-4 players in Anniversary graphics: everyone's saved choice (on unless
    // turned off), which takes effect as a mission starts, and whether the mission running in `game`
    // (CGameGlobal::Halo1 or Halo2) started with it. Such a mission draws its 3-4 views whenever
    // Anniversary graphics are on screen; it starts in them unless it's side by side.
    bool AnniversaryQuadChosen();
    void ChooseAnniversaryQuad(bool on);
    bool AnniversaryQuadActive(int game);
    // The mode is drawing Halo CE (module/entry/halo1/anniversary.cpp) and puts three players in quarters, as four;
    // Halo 2's keeps its own grid, player 1 on the top half.
    bool AnniversaryQuartersShown(int game);
}

namespace MCC::Splitscreen {
    // Remove black bars: one choice for every game with the "Remove Black Bar1-3" patches (Halo 3, ODST, Halo 4, Halo 2
    // Anniversary multiplayer, Reach; module/CModule.cpp), which widen the two-player views and the first three-player
    // one to fill their part of the screen. It's the menus' MenuState::useBlackBars (mcc/settings/MenuSettings), which
    // the Splitscreen window and the Xbox menu both edit and the menu file saves; the patches follow it, not the
    // per-game patch config. While Left / Right is chosen it owns the same table entries, so the choice shows with
    // Top / Bottom.
    bool IsBlackBarPatch(const CPatch* patch);
    // Every game's black-bar patches on or off as chosen (a module not loaded takes it as it loads).
    void ApplyBlackBars();
    // After the table entries of the module loaded at `module` were put back (Left / Right turned off): its black-bar
    // patches written again as chosen, over entries saved before the choice changed.
    void ReapplyBlackBars(__int64 module);
}

namespace MCC::Splitscreen {
    // Hot join (experimental): players join in the middle of a mission, and in Halo CE leave (Join.cpp). The choice
    // is kept for the session (off as MCC starts) and needs AlphaRing's split screen, which it turns on: whether it's
    // chosen, and on; whether the running mission takes joins, and leaves; each frame, a controller no player uses
    // joining with A.
    bool HotJoinChosen();
    void ChooseHotJoin(bool on);
    bool HotJoinOn();
    bool HotJoinActive();
    bool HotJoinLeaves();
    void HotJoinPoll();
    // Local players MCC is told of: all four slots in a hot-join Halo CE or Halo 2 mission, else the player count.
    int HotJoinSlots(int player_count);
    // A Halo CE or Halo 2 mission of `game` starting now is a hot-join one: hot join is on and the game's module, which
    // holds back the players who haven't joined, has its addresses (module/entry/halo1/hotjoin.cpp, halo2/hotjoin.cpp).
    bool HotJoinReserves(int game);
    // In a Halo CE or Halo 2 game, where the choice takes effect as a mission starts, it's changed at MCC's menus.
    bool HotJoinLocked();
    // In a Halo CE or Halo 2 game with hot join (its module there), MCC has all four local players from the mission's
    // start until the session exits, and ends it if they go away; the held-back ones come in only by joining. So until
    // then the player count changes only by joining and leaving, not with the dashboard's or the Splitscreen window's.
    bool HotJoinHeld();
}

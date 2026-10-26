#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "input/MenuConfig.h"
#include "render/imgui/game/xbox/CXboxMenuState.h"

struct CGamepadMapping;
struct CUserProfile;

// Settings shared by the Xbox menu (F4) and the advanced menu (F1), kept in
// one binary file: the per-player menu state, the custom controller profiles
// and the saved characters. Either menu edits the same data, so a change made
// in one shows up in the other. Hold Mutex() around any access: the menus edit
// it on the render thread while game_setup/game_restart apply it on the game's.
namespace AlphaRing::MenuSettings {
    struct ControllerProfile {
        std::string name;
        ControllerProfileMapping buttons;
        int builtin = -1; // the MenuConfig profile it started as (0 = Default ... 5 = Recon), -1 for none
    };

    // The games whose armor the advanced menu keeps, each separately: an
    // item number means a different item in every game, so one game's armor
    // must never reach another (Reach armor in Halo 4 or H2A leaves player 2
    // unable to spawn, and Halo 4 ends the match).
    enum ArmorGame { ArmorHalo2A, ArmorHalo3, ArmorReach, ArmorHalo4, ArmorGameCount };
    const char* ArmorGameName(int game);       // "Halo 2A", ...
    int ArmorItemGame(int game);               // its CUserProfile::eItemGame
    int CurrentArmorGame();                    // the running game's, or -1 (CE, Halo 2, ODST, none)

    // One game's armor: one value per armor field (ArmorValueIndex), then the
    // four ServiceTag characters. A field below 0 is unset and keeps the
    // game's own choice; a tag starting with 0 is unset.
    struct Character {
        std::string name;
        std::vector<int> values;
        std::string playerName; // UTF-8 username of the player it was saved from
        int game = ArmorReach;  // the ArmorGame it belongs to
    };

    std::recursive_mutex& Mutex();

    // Loads the menu file now and puts the saved player names, player count and
    // controls in place before the game asks for them. Needs g_menuConfig loaded
    // (Input::Init) first; doesn't need the game engine.
    void Initialize();

    MenuState& State();
    std::vector<ControllerProfile>& Profiles();
    std::vector<Character>& Characters();

    bool Save();

    // Pushing the menu state into the player profiles. Changes still need a
    // GameEngine()->load_setting() to reach a running game.
    void ApplyControllerProfile(int profileIndex, CGamepadMapping& mapping);
    bool IsProfileAction(int action); // set by ApplyControllerProfile
    void ApplyPlayerControls(int player); // controller slot, look inversion, look sensitivity
    void ApplyPlayerMapping(int player);  // the player's controller profile
    void ApplyPlayerTeam(int player); // only for a player in the match (below the player count)
    void ApplyPlayerColors(int player);
    // atGameStart sets the splitscreen override from the player count; a menu
    // change only ever turns it on, as a lone player may still want it.
    void ApplySplitscreen(bool atGameStart);
    // Team changes: Halo 4 and H2A apply a team change even when the mode
    // has no teams, which drops the player from a free-for-all match and ends
    // it ("-1th" place). In those two games a player only gets one when their
    // team differs from previousTeams[player] - a team picked in a menu - and
    // never from a null previousTeams (game start). Other games get them all.
    void ApplyAll(bool atGameStart, const int* previousTeams = nullptr);

    // Controller profiles. The last profile can't be deleted, and players on a
    // deleted profile fall back to the first one.
    static constexpr int k_profileActionCount = 14;
    const char* ProfileActionName(int action);
    CGamepadMapping::eButton& ProfileButton(ControllerProfileMapping& buttons, int action);
    int AddProfile(const std::string& name, const ControllerProfileMapping& buttons);
    void DeleteProfile(int index);
    void ReapplyProfile(int index); // to every player using it
    // Back to the buttons it started with: its built-in profile's (including
    // alpha_ring_menu.cfg overrides), or Default's for a profile made in the menu.
    void ResetProfile(int index);

    // Per-player, per-game armor picked in the advanced menu. The running
    // game's is laid over whatever profile the game hands that player
    // (player 1's copy at game start, Reach's live sync of it, or the game's
    // own profile), so it survives those copies and game restarts.
    std::vector<int>& Armor(int player, int game);   // the values, unset ones below 0
    int ArmorValueIndex(int CUserProfile::* field);  // a field's place in it, -1 if not armor
    int ArmorServiceTagIndex();                      // the first ServiceTag character's place
    bool HasArmor(int player);                       // any set for the running game
    bool HasArmor(int player, int game);
    void ClearArmor(int player, int game);
    void ApplyArmor(int player, CUserProfile& profile); // the running game's set fields

    // Takes the four players' names from their profiles, then writes
    // everything to the file. Called when either menu closes; loading the
    // file puts the names back.
    void SaveAll();

    // Characters. Saving under an existing name replaces that record.
    std::string CharacterName(const wchar_t* playerName); // "{PlayerName}'s Character"
    // A player's armor for `game`, saved as a character of that game.
    int SaveCharacter(const std::string& name, int game, const std::vector<int>& armor, const wchar_t* playerName);
    // Makes the character the player's armor for its game, and writes the
    // saved username into `playerName` (a buffer of `playerNameSize`
    // characters) when the record has one.
    void LoadCharacter(int index, int player, wchar_t* playerName, int playerNameSize);
    void DeleteCharacter(int index);
}

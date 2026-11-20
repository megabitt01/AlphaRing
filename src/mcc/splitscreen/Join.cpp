// The Players window: couch-style joining. Every controller answers here directly: A joins as the next player, B
// leaves, Y picks Spartan or Elite. At MCC's menus the players and their controllers can still change before a
// mission. In a game it shows the lineup: the game reads the controllers live, so players only join there with hot
// join (a controller no player uses presses A), and only in Halo CE does the last player leave (B).
//
// The players and their controllers are the menus' (mcc/settings/MenuSettings, which the dashboard and the overlay
// share): a change goes into that state and in place at once, as the Splitscreen window's player count does, and is
// written with the rest when a menu saves (the overlay closing, for one). A game start applies the state, joins
// included. On a running map the controllers are the ones the players have then (State).
#include "Splitscreen.h"

#include "common.h"

#include "global/Global.h"
#include "input/Input.h"
#include "mcc/CGameManager.h"
#include "mcc/mcc.h"
#include "mcc/settings/MenuSettings.h"
#include "render/imgui/game/xbox/CXboxContext.h"

#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace MCC::Splitscreen {
    static constexpr int kSlots = 4;
    static constexpr ImU32 kColors[kSlots] = {IM_COL32(85, 164, 242, 255), IM_COL32(242, 85, 85, 255),
                                              IM_COL32(85, 242, 85, 255), IM_COL32(242, 216, 85, 255)}; // the cards

    // ---- the lineup: how many play, with which controllers, and player 1 on keyboard and mouse or not

    namespace Settings = AlphaRing::MenuSettings;

    // The menus' state, under Settings::Mutex(). On a running map the game reads each player's controller and keyboard
    // and mouse for player 1 live (CGameManager::get_input_device), as that game's start put them in place and the
    // Splitscreen window's Input, Bind and "Enable K/M for player1" may have changed them since: those are the
    // players', so a controller rebound to a player isn't a free one, and a join or a leave keeps them.
    static MenuState& State() {
        auto& state = Settings::State();
        if (MCC::IsInGame() && CGameManager::running()) {
            for (int i = 0; i < kSlots; ++i)
                state.controllerIndex[i] = CGameManager::get_profile(i)->controller_index;
            state.useKM = AlphaRing::Global::MCC::Splitscreen()->b_player0_use_km;
        }
        return state;
    }

    // The player count, published last with a release store and read on the game thread with an acquire load, so that
    // whoever reads a new count sees the controllers put in place before it. It's the dashboard's plain int (Global.h),
    // so through Windows' interlocked-ordered accessors for it (winnt.h).
    static_assert(sizeof(LONG) == sizeof(int));
    static void PublishPlayerCount(int count) {
        WriteRelease(reinterpret_cast<volatile LONG*>(&AlphaRing::Global::MCC::Splitscreen()->player_count), count);
    }

    int PlayerCount() {
        return ReadAcquire(reinterpret_cast<const volatile LONG*>(&AlphaRing::Global::MCC::Splitscreen()->player_count));
    }

    // A change goes in place as the Splitscreen window's player count puts its own (Settings::ApplySplitscreen): the
    // controllers, keyboard and mouse for player 1, split screen on above one player, and the count.
    static void PutInPlace(const MenuState& state) {
        auto setting = AlphaRing::Global::MCC::Splitscreen();
        for (int i = 0; i < kSlots; ++i) CGameManager::get_profile(i)->controller_index = state.controllerIndex[i];
        setting->b_player0_use_km = state.useKM;
        if (state.playerCount > 1) setting->b_override = true;
        PublishPlayerCount(state.playerCount); // last: a new player's controller before the count makes them one
    }

    // The player `pad` plays as, or -1 (player 1 on keyboard and mouse has no pad).
    static int SlotOf(int pad) {
        auto& state = State();
        for (int i = state.useKM ? 1 : 0; i < state.playerCount; ++i)
            if (state.controllerIndex[i] == pad) return i;
        return -1;
    }

    // A pad that just joined gives the game nothing until what it joined with is let go: the A would jump, and anything
    // else held would act at once (JoinPressHeld, from the game's input poll).
    static std::atomic<bool> s_join_held[kSlots] = {};

    static bool Held(const XINPUT_GAMEPAD& gamepad) {
        return gamepad.wButtons || gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
               gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    }

    bool JoinPressHeld(int pad, const XINPUT_GAMEPAD& gamepad) {
        if (pad < 0 || pad >= kSlots || !s_join_held[pad]) return false;
        if (Held(gamepad)) return true;
        s_join_held[pad] = false;
        return false;
    }

    // A press hot join or the window acted on (a join, a leave, a species), which the Splitscreen window's Bind doesn't
    // also take until it's let go (PressTaken): Bind can take a press in the same frame, after them (BindPending).
    static bool s_taken[kSlots] = {};

    bool PressTaken(int pad, const XINPUT_GAMEPAD& gamepad) {
        if (pad < 0 || pad >= kSlots || !s_taken[pad]) return false;
        if (Held(gamepad)) return true;
        s_taken[pad] = false;
        return false;
    }

    // Each frame (HotJoinPoll), whether Bind is drawn or not, and whether the game reads the controller or not (not
    // while the overlay is shown, by default): a press acted on, a join's included, is let go once its controller holds
    // nothing.
    static void LetGoTaken() {
        for (int pad = 0; pad < kSlots; ++pad) {
            XINPUT_STATE state{};
            if ((s_taken[pad] || s_join_held[pad]) &&
                (!AlphaRing::Input::GetXInputGetState(pad, &state) || !Held(state.Gamepad))) {
                s_taken[pad] = false;
                s_join_held[pad] = false;
            }
        }
    }

    // A new player with `pad`: their slot, or -1. The game reads these live in a mission (hot join), so the pad is held
    // back before the count makes it a player's.
    static int Join(int pad) {
        auto& state = State();
        int slot = state.playerCount;
        if (slot >= kSlots || SlotOf(pad) >= 0) return -1;
        s_join_held[pad] = true;
        s_taken[pad] = true;
        state.controllerIndex[slot] = pad;
        state.playerCount = slot + 1;
        PutInPlace(state);
        return slot;
    }

    // Everyone after the leaving player moves up a slot, keeping their controller; the leaver's goes to the slot left
    // free, so each of the dashboard's player pages keeps a controller of its own.
    static bool Leave(int slot) {
        auto& state = State();
        if (slot < 0 || slot >= state.playerCount || state.playerCount <= 1) return false;
        std::rotate(state.controllerIndex + slot, state.controllerIndex + slot + 1,
                    state.controllerIndex + state.playerCount);
        --state.playerCount;
        PutInPlace(state);
        return true;
    }

    static bool DashboardOpen() { return g_pXboxContext && g_pXboxContext->isOpen(); }

    // ---- the pads, read at most once a frame, by hot join or the window, whichever needs them first: which are
    // connected, and the buttons newly down on each. Buttons already held when reading starts aren't presses.

    static unsigned s_frame = 0, s_read_frame = ~0u; // frames counted by HotJoinPoll; not read yet
    static WORD s_buttons[kSlots] = {}, s_pressed[kSlots] = {};
    static bool s_connected[kSlots] = {};

    static void ReadPads() {
        if (s_read_frame == s_frame) return;
        bool continuing = s_read_frame + 1 == s_frame;
        s_read_frame = s_frame;
        for (int pad = 0; pad < kSlots; ++pad) {
            XINPUT_STATE state{};
            s_connected[pad] = AlphaRing::Input::GetXInputGetState(pad, &state);
            s_pressed[pad] = continuing ? state.Gamepad.wButtons & ~s_buttons[pad] : 0;
            s_buttons[pad] = state.Gamepad.wButtons;
        }
    }

    // The Splitscreen window's Bind has the controllers while it waits for one: nothing here acts on them, and reading
    // starts again from what's held then, so the press Bind took is no press here until it's let go.
    static bool BindHasPads() {
        if (!BindPending()) return false;
        s_read_frame = ~0u;
        return true;
    }

    // ---- species: each player's Spartan or Elite (CUserProfile::UseEliteModel), which Halo 2, Halo 3 and Reach read
    // as a game loads. -1: the profile's own (player 1's, which AlphaRing copies to every player's profile).

    static std::atomic<int> s_species[kSlots] = {-1, -1, -1, -1};

    void ApplySpecies(int index, CUserProfile& profile) {
        int species = index >= 0 && index < kSlots ? s_species[index].load() : -1;
        if (species >= 0) profile.UseEliteModel = species != 0;
    }

    // The game takes an AlphaRing profile of the player's own (CGameManager::get_player_profile): only with
    // AlphaRing's split screen on, player 1's while "Override profile" is, the others' unless they share player 1's.
    static bool OwnsProfile(int slot) {
        auto setting = AlphaRing::Global::MCC::Splitscreen();
        if (!setting->b_override) return false;
        return slot == 0 ? setting->b_override_profile : !setting->b_use_player0_profile;
    }

    static bool Elite(int slot) {
        int species = s_species[slot];
        return species >= 0 ? species != 0 : CGameManager::get_profile(slot)->profile.UseEliteModel;
    }

    static void ToggleSpecies(int slot) {
        if (!OwnsProfile(slot)) return;
        bool elite = !Elite(slot);
        s_species[slot] = elite;
        CGameManager::get_profile(slot)->profile.UseEliteModel = elite;
    }

    // ---- hot join (Splitscreen::HotJoinActive): the lineup changes during a mission

    void HotJoinPoll() {
        std::lock_guard lock(Settings::Mutex());
        ++s_frame;
        LetGoTaken();
        // the dashboard has the controllers while it's open
        if (BindHasPads() || !HotJoinActive() || DashboardOpen() || State().playerCount >= kSlots) return;
        ReadPads();
        for (int pad = 0; pad < kSlots; ++pad) {
            int slot = s_pressed[pad] & XINPUT_GAMEPAD_A && SlotOf(pad) < 0 ? Join(pad) : -1;
            if (slot >= 0) LOG_INFO("Hot join: controller {} joins as player {}", pad + 1, slot + 1);
        }
    }

    // A Halo CE hot-join mission: the last player leaves.
    static void HotJoinLeave(int slot) {
        if (!HotJoinLeaves() || slot <= 0 || slot != State().playerCount - 1 || !Leave(slot)) return;
        LOG_INFO("Hot join: player {} leaves", slot + 1);
    }

    // ---- the window (the overlay's menu bar)

    static bool s_show = false;

    static void PollPads(bool changes) {
        ReadPads();
        for (int pad = 0; pad < kSlots; ++pad) {
            WORD pressed = s_pressed[pad];
            int slot = SlotOf(pad);
            if (changes && (pressed & XINPUT_GAMEPAD_A)) {
                Join(pad);
            } else if (pressed & XINPUT_GAMEPAD_B) {
                s_taken[pad] = true;
                if (changes) Leave(slot);
                else HotJoinLeave(slot);
            } else if ((pressed & XINPUT_GAMEPAD_Y) && slot >= 0) {
                s_taken[pad] = true;
                ToggleSpecies(slot);
            }
        }
    }

    static void Card(int slot, bool changes) {
        auto& state = State();
        bool joined = slot < state.playerCount;
        float unit = ImGui::GetFontSize();
        ImVec2 size(unit * 13.0f, unit * 8.0f);
        ImVec2 at = ImGui::GetCursorScreenPos();
        auto draw = ImGui::GetWindowDrawList();
        ImU32 color = kColors[slot];
        draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y),
                            joined ? (color & 0x60FFFFFF) : IM_COL32(40, 40, 40, 160), unit * 0.5f);
        draw->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), joined ? color : IM_COL32(90, 90, 90, 255),
                      unit * 0.5f, 0, 3.0f);

        float x = at.x + unit * 0.8f;
        char line[64];
        std::snprintf(line, sizeof(line), "PLAYER %d", slot + 1);
        draw->AddText(ImVec2(x, at.y + unit * 0.7f), joined ? color : IM_COL32(160, 160, 160, 255), line);
        const char* status;
        if (!joined) {
            status = changes ? "Press A to join" : "Free";
        } else if (slot == 0 && state.useKM) {
            status = "Keyboard & mouse";
        } else if (int pad = state.controllerIndex[slot]; pad < 0 || pad >= kSlots) {
            status = "No controller"; // the Splitscreen window's NONE
        } else {
            bool connected = s_connected[pad];
            std::snprintf(line, sizeof(line), "Controller %d%s", pad + 1, connected ? "" : " (disconnected)");
            status = line;
        }
        draw->AddText(ImVec2(x, at.y + unit * 2.4f), IM_COL32(230, 230, 230, 255), status);

        if (joined && OwnsProfile(slot)) { // Spartan or Elite, also with Y on the player's controller
            ImGui::SetCursorScreenPos(ImVec2(x, at.y + unit * 4.1f));
            std::snprintf(line, sizeof(line), "%s##species%d", Elite(slot) ? "Elite" : "Spartan", slot);
            if (ImGui::SmallButton(line)) ToggleSpecies(slot);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Spartan or Elite (Y on the player's controller): Halo 2, Halo 3 and Reach, from "
                                  "the next game");
        }
        if (changes && joined && !(slot == 0 && state.useKM))
            draw->AddText(ImVec2(x, at.y + unit * 6.1f), IM_COL32(170, 170, 170, 255), "B to leave");
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy(size);
    }

    void JoinContext() {
        std::lock_guard lock(Settings::Mutex());
        bool bind = BindHasPads(); // every overlay frame, before the Splitscreen window can take a press (ImGuiContext)
        if (ImGui::BeginMainMenuBar()) {
            ImGui::MenuItem("Players", nullptr, &s_show);
            ImGui::EndMainMenuBar();
        }
        if (!s_show || DashboardOpen()) return; // one open at a time: the dashboard has the controllers

        bool changes = !MCC::IsInGame();
        if (!bind) PollPads(changes);
        if (ImGui::Begin("Players", &s_show,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings)) {
            for (int slot = 0; slot < kSlots; ++slot) {
                if (slot) ImGui::SameLine();
                Card(slot, changes);
            }
            if (bind) ImGui::TextDisabled("Bind (Splitscreen window) is waiting for a controller");
            else if (changes) ImGui::TextDisabled("A: join   B: leave   Y: Spartan / Elite");
            else if (HotJoinLeaves())
                ImGui::TextDisabled("Hot join: A on a free controller joins, B leaves (the last player)   Y: Spartan / Elite");
            else if (HotJoinActive())
                ImGui::TextDisabled("Hot join: A on a free controller joins; players leave at MCC's menus   Y: Spartan / Elite");
            else ImGui::TextDisabled("In a game: players join and leave at MCC's menus   Y: Spartan / Elite");

            bool hot_join = HotJoinChosen();
            ImGui::BeginDisabled(HotJoinLocked());
            if (ImGui::Checkbox("Hot join (experimental)", &hot_join)) ChooseHotJoin(hot_join);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip(); // wrapped: one line would run off a 1080p screen
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("Players join in the middle of a mission: a controller nobody is playing with "
                                       "presses A to join, and comes in beside a teammate. In Halo CE the last player can "
                                       "also leave, with B here; in the other games players leave at MCC's menus. Halo CE "
                                       "and Halo 2 take it as a mission starts, so in their games it's changed at MCC's "
                                       "menus. On until MCC closes.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::End();
    }
}

#include "Splitscreen.h"

#include "common.h"

#include "global/Global.h"

#include <offset_mcc.h>

#include "../CGameManager.h"
#include "log/MatchLog.h"
#include "mcc/CGameGlobal.h"
#include "mcc/mcc.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"
#include "LeftRight.h"

#include <atomic>

namespace Halo1::Entry::Anniversary { bool Available(); bool QuadShown(); }
namespace Halo2::Entry::Anniversary { bool Available(); }
namespace Halo1::Entry::HotJoin { bool Available(); bool Active(); }
namespace Halo2::Entry::HotJoin { bool Available(); bool Active(); }

namespace MCC::Splitscreen {
    DefDetourFunction(__int64, __fastcall, get_index_by_xuid, void* a1, __int64 xuid) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        __int64 result = p_setting->b_override ? CGameManager::get_index(xuid) : ppOriginal_get_index_by_xuid(a1, xuid);

        // once per match per answer
        static std::atomic<long> s_seen[5];
        int slot = (result >= 0 && result < 4) ? (int)result : 4;
        if (AlphaRing::MatchLog::FirstThisMatch(s_seen[slot]))
            MATCH_LOG("get_index_by_xuid xuid={:#x} -> {} override={} (caller {})", (unsigned long long)xuid, result,
                      p_setting->b_override, AlphaRing::MatchLog::Describe(_ReturnAddress()));
        return result;
    }

    // todo:: let other players have the ability to pause the game

    bool Initialize() {
        bool result;

        // fix: changing team freeze the game
        result = AlphaRing::Hook::Detour({
            {OFFSET_MCC_PF_GET_INDEX_BY_XUID, OFFSET_MCC_WS_PF_GET_INDEX_BY_XUID, get_index_by_xuid, (void**)&ppOriginal_get_index_by_xuid},
        });

        assertm(result, "MCC:Splitscreen: failed to hook");

        return true;
    }

    static std::atomic<int> s_anniversary_quad_game{-1}; // the Halo CE / Halo 2 game whose mission started with it

    bool AnniversaryQuadChosen() { return AlphaRing::SplitscreenConfigStore::GetAnniversaryQuad(); }
    void ChooseAnniversaryQuad(bool on) { AlphaRing::SplitscreenConfigStore::SetAnniversaryQuad(on); }
    bool AnniversaryQuadActive(int game) { return s_anniversary_quad_game == game; }
    bool AnniversaryQuartersShown(int game) {
        return game == CGameGlobal::Halo1 && Halo1::Entry::Anniversary::QuadShown();
    }

    // The game's 3-4 player mode has all its offsets in this MCC build (always in the one they were written for), and
    // for Halo CE the render targets it draws into.
    static bool AnniversaryQuadAvailable(int game) {
        return game == CGameGlobal::Halo1 ? Halo1::Entry::Anniversary::Available()
             : game == CGameGlobal::Halo2 ? Halo2::Entry::Anniversary::Available()
             : false;
    }

    ClassicGraphicsScope::ClassicGraphicsScope(unsigned char* game_options, int game) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        bool split = p_setting->b_override && game_options != nullptr; // AlphaRing's split screen sets this mission up
        // A hot-join mission can have any number of players: it's set up as one for four, and side by side for when
        // two or three are in.
        bool hot_join = split && HotJoinReserves(game);
        bool left_right = split && LeftRight::Chosen() &&
                          LeftRight::Supports(game, hot_join ? 2 : p_setting->player_count);
        LeftRight::StartClassicMission(game, left_right);
        // Chosen, the 3-4 player mode is set up for the whole mission, whichever graphics it starts in: Back
        // switches Halo CE and Halo 2 to Anniversary in the middle of one, side by side included.
        bool quad = split && AnniversaryQuadChosen() && AnniversaryQuadAvailable(game);
        s_anniversary_quad_game = quad ? game : -1;
        if (!split) return;
        if (p_setting->player_count <= 2 && !hot_join && !left_right) return;
        bool anniversary = game_options[0] & 1;
        if (!anniversary) return;
        if (quad && !left_right) {
            LOG_INFO("Splitscreen: {} players{} in Anniversary graphics (experimental)", p_setting->player_count,
                     hot_join ? " and hot join" : "");
            return;
        }

        LOG_INFO("Splitscreen: {} players{}{}, starting in Classic graphics", p_setting->player_count,
                 left_right ? " side by side" : "", hot_join ? " and hot join" : "");
        m_options = game_options;
        m_saved = game_options[0];
        game_options[0] &= ~1;
    }

    ClassicGraphicsScope::~ClassicGraphicsScope() {
        if (m_options) m_options[0] = m_saved;
    }

    static std::atomic<bool> s_hot_join{false};

    bool HotJoinChosen() { return s_hot_join; }

    // The slots are AlphaRing's, a lone player 1 the first of several; every game start keeps split screen on for it
    // too (AlphaRing::MenuSettings::ApplySplitscreen).
    void ChooseHotJoin(bool on) {
        s_hot_join = on;
        if (on) AlphaRing::Global::MCC::Splitscreen()->b_override = true;
    }

    bool HotJoinOn() { return AlphaRing::Global::MCC::Splitscreen()->b_override && s_hot_join; }

    // How a game takes a player joining in the middle of a mission. Halo 3, ODST, Reach and Halo 4 make the player MCC
    // signs in. Halo CE and Halo 2 make their players as a mission loads, so their modules keep all four and hold back
    // the ones who haven't joined (module/entry/halo1/hotjoin.cpp, halo2/hotjoin.cpp), where this build has all the
    // module's addresses; the module notes whether the running mission started so (Active).
    bool HotJoinReserves(int game) {
        return HotJoinOn() && (game == CGameGlobal::Halo1 ? Halo1::Entry::HotJoin::Available()
                             : game == CGameGlobal::Halo2 && Halo2::Entry::HotJoin::Available());
    }

    static bool SignsIn(int game) {
        return game == CGameGlobal::Halo3 || game == CGameGlobal::Halo3ODST || game == CGameGlobal::HaloReach ||
               game == CGameGlobal::Halo4;
    }

    // A running map takes joins (not one that is still loading).
    bool HotJoinActive() {
        if (!HotJoinOn() || !MCC::IsInGame() || !CGameManager::running()) return false;
        auto p_global = GameGlobal();
        if (p_global == nullptr) return false;
        int game = p_global->current_game;
        return game == CGameGlobal::Halo1 ? Halo1::Entry::HotJoin::Active()
             : game == CGameGlobal::Halo2 ? Halo2::Entry::HotJoin::Active()
             : SignsIn(game);
    }

    // Players leave in the middle of a Halo CE mission only: its module takes the leaver's Spartan away, and the games
    // that sign players in end the mission when MCC signs one out.
    bool HotJoinLeaves() {
        auto p_global = GameGlobal();
        return HotJoinActive() && p_global && p_global->current_game == CGameGlobal::Halo1;
    }

    int HotJoinSlots(int player_count) {
        auto p_global = GameGlobal();
        return p_global && HotJoinReserves(p_global->current_game) ? 4 : player_count;
    }

    // Halo CE and Halo 2 take the choice as a mission starts, and MCC ends one whose held-back players go away.
    bool HotJoinLocked() {
        auto p_global = GameGlobal();
        return MCC::IsInGame() && p_global &&
               (p_global->current_game == CGameGlobal::Halo1 || p_global->current_game == CGameGlobal::Halo2);
    }

    // Its missions start as hot-join ones (with split screen on, as every game start puts it with hot join chosen).
    bool HotJoinHeld() {
        if (!HotJoinChosen() || !HotJoinLocked()) return false;
        return GameGlobal()->current_game == CGameGlobal::Halo1 ? Halo1::Entry::HotJoin::Available()
                                                               : Halo2::Entry::HotJoin::Available();
    }
}

#include "imgui.h"
#include "mcc/mcc.h"
#include "input/Input.h"
#include "mcc/settings/MenuSettings.h"
#include "render/imgui/game/xbox/assets/colors.h"

#include <algorithm>
#include <string>

// Settings the Xbox menu (F4) also shows - player count, K/M, controller slot
// and profile, sensitivity, inversion, team and colors - live in
// AlphaRing::MenuSettings, so an edit here is written to the shared menu file
// and shows in the Xbox menu, and the other way round. Every frame of this
// window holds MenuSettings::Mutex().
namespace MCC::Splitscreen {
    namespace Settings = AlphaRing::MenuSettings;

    void RealContext();

    static void ReloadSettings() {
        if (auto p_engine = GameEngine())
            p_engine->load_setting();
    }

    // Detect which controller (0-3) has any button/trigger pressed, returns -1 if none
    static int DetectActiveController() {
        for (int i = 0; i < 4; i++) {
            XINPUT_STATE state;
            if (AlphaRing::Input::GetXInputGetState(i, &state)) {
                if (PressTaken(i, state.Gamepad)) continue; // the Players window or hot join acted on it (Join.cpp)
                if (state.Gamepad.wButtons != 0 ||
                    state.Gamepad.bLeftTrigger > 30 ||
                    state.Gamepad.bRightTrigger > 30) {
                    return i;
                }
            }
        }
        return -1;
    }

    // Binding state: which player slot is waiting for controller input (-1 = none)
    static int s_binding_player = -1;

    // Bind takes a controller only on its player's page as it's drawn, with the overlay shown: so only while that page
    // was drawn waiting the frame before (or this one: before a frame starts the count is still the last one's). A Bind
    // whose player has gone is dropped.
    static int s_binding_frame = -2; // the frame the page last drew the Bind waiting
    bool BindPending() {
        if (s_binding_player >= PlayerCount()) s_binding_player = -1;
        return s_binding_player >= 0 && AlphaRing::Global::Global()->show_imgui &&
               s_binding_frame >= ImGui::GetFrameCount() - 1;
    }

    void ImGuiContext() {
        static bool show_splitscreen;

        JoinContext();

        if (ImGui::BeginMainMenuBar()) {
            ImGui::MenuItem("Splitscreen", nullptr, &show_splitscreen);
            ImGui::EndMainMenuBar();
        }

        if (show_splitscreen) {
            if (ImGui::Begin("Splitscreen", &show_splitscreen, ImGuiWindowFlags_MenuBar)) {
                std::lock_guard lock(Settings::Mutex());
                RealContext();
            }
            ImGui::End();
        }
    }

    static bool ProfileCombo(const char* label, int* index) {
        auto& profiles = Settings::Profiles();
        const char* preview = (*index >= 0 && *index < (int)profiles.size()) ? profiles[*index].name.c_str() : "";
        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            for (int i = 0; i < (int)profiles.size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(profiles[i].name.c_str(), *index == i)) {
                    *index = i;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    static bool ColorCombo(const char* label, int* color) {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::ColorButton("##swatch", ImGui::ColorConvertU32ToFloat4(defaultColors[*color]), ImGuiColorEditFlags_NoTooltip);
        ImGui::PopID();
        ImGui::SameLine();
        if (ImGui::BeginCombo(label, k_colorNames[*color])) {
            for (int c = 0; c < IM_ARRAYSIZE(k_colorNames); ++c) {
                ImGui::PushID(c);
                ImGui::ColorButton("##swatch", ImGui::ColorConvertU32ToFloat4(defaultColors[c]), ImGuiColorEditFlags_NoTooltip);
                ImGui::SameLine();
                if (ImGui::Selectable(k_colorNames[c], *color == c)) {
                    *color = c;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // The per-player settings the Xbox menu has too.
    static void SharedPlayerContext(int index) {
        auto& ms = Settings::State();

        ImGui::PushItemWidth(200);

        if (ProfileCombo("Controller Profile", &ms.controllerProfile[index])) {
            Settings::ApplyPlayerMapping(index);
            ReloadSettings();
            Settings::Save();
        }

        if (ImGui::SliderInt("Look Sensitivity", &ms.sensitivity[index], 1, 20)) {
            Settings::ApplyPlayerControls(index);
            ReloadSettings();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            Settings::Save();

        if (ImGui::Checkbox("Inverted Look", &ms.invert[index])) {
            Settings::ApplyPlayerControls(index);
            ReloadSettings();
            Settings::Save();
        }

        const char* teams[] = {"Red Team", "Blue Team"};
        if (ImGui::Combo("Team", &ms.teamIndex[index], teams, IM_ARRAYSIZE(teams))) {
            Settings::ApplyPlayerTeam(index);
            Settings::Save();
        }

        const char* colorLabels[] = {"Primary Color", "Secondary Color", "Tertiary Color"};
        for (int c = 0; c < 3; ++c) {
            if (ColorCombo(colorLabels[c], &ms.playerColors[index].colors[c])) {
                Settings::ApplyPlayerColors(index);
                ReloadSettings();
                Settings::Save();
            }
        }

        ImGui::PopItemWidth();
    }

    // One game's armor for a player: its characters, Use Game Armor, the
    // ServiceTag and the armor pickers, which only offer that game's items.
    // Saved per game, and applied only while that game runs.
    static void ArmorGameContext(int index, CGameManager::Profile_t* p_profile, int game) {
        static int selected[4][Settings::ArmorGameCount] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};
        int& chosen = selected[index][game];
        auto& characters = Settings::Characters();
        auto& armor = Settings::Armor(index, game);
        bool current = Settings::CurrentArmorGame() == game;
        bool changed = false;

        if (chosen >= (int)characters.size() || (chosen >= 0 && characters[chosen].game != game))
            chosen = -1;

        std::string name = Settings::CharacterName(p_profile->name);
        if (ImGui::Button("Save Character")) {
            chosen = Settings::SaveCharacter(name, game, armor, p_profile->name);
            Settings::Save();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Saves this player's %s armor, service tag and name as \"%s\"",
                              Settings::ArmorGameName(game), name.c_str());

        ImGui::PushItemWidth(250);
        const char* preview = chosen >= 0 ? characters[chosen].name.c_str() : "-- Select Character --";
        if (ImGui::BeginCombo("Load Character", preview)) {
            for (int i = 0; i < (int)characters.size(); ++i) {
                if (characters[i].game != game) continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(characters[i].name.c_str(), chosen == i)) {
                    chosen = i;
                    Settings::LoadCharacter(i, index, p_profile->name, (int)std::size(p_profile->name));
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::BeginDisabled(chosen < 0);
        if (ImGui::Button("Delete##character")) {
            Settings::DeleteCharacter(chosen);
            Settings::Save();
            chosen = -1;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!Settings::HasArmor(index, game));
        if (ImGui::Button("Use Game Armor")) {
            Settings::ClearArmor(index, game);
            Settings::Save();
            if (current) {
                CGameManager::apply_profiles();
                Settings::ApplyAll(false, Settings::State().teamIndex); // no team was picked here
                ReloadSettings();
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Drops the %s armor picked here, going back to the armor from the game",
                              Settings::ArmorGameName(game));
        ImGui::EndDisabled();
        ImGui::Separator();

        // ServiceTag: up to four characters, none = the game's own
        int tag_index = Settings::ArmorServiceTagIndex();
        char tag[5] = {};
        for (int i = 0; i < 4; ++i)
            tag[i] = (char)armor[tag_index + i];
        if (ImGui::InputText("ServiceTag", tag, sizeof(tag))) {
            for (int i = 0; i < 4; ++i)
                armor[tag_index + i] = (unsigned char)tag[i];
            changed = true;
        }

        changed |= CUserProfile::ArmorImGuiContext((CUserProfile::eItemGame)Settings::ArmorItemGame(game),
            [&](int CUserProfile::* field) -> int* {
                int v = Settings::ArmorValueIndex(field);
                return v >= 0 ? &armor[v] : nullptr;
            });

        if (changed) {
            Settings::Save();
            if (current) {
                Settings::ApplyArmor(index, p_profile->profile);
                ReloadSettings();
            }
        }
    }

    // Edits the buttons of controller profile `profile` - a Reset to Defaults
    // button and a row per action - for every player using it, and saves.
    // `owner` tells apart the places it's shown, so a bind started in one
    // doesn't show up in another; `controller` is the pad a bind listens to.
    static void ProfileButtonsContext(const char* owner, int profile, int controller) {
        static std::string bind_owner;
        static int bind_profile = -1;
        static int bind_action = -1;
        static bool bind_armed = false; // the bind waits for every button to be let go first

        auto& profiles = Settings::Profiles();
        if (profile < 0 || profile >= (int)profiles.size())
            return;
        auto& buttons = profiles[profile].buttons;
        auto button_names = CGamepadMapping::ButtonNames();
        bool binding_here = bind_owner == owner && bind_profile == profile;
        bool changed = false;

        ImGui::PushID(owner);

        if (ImGui::Button("Reset to Defaults")) {
            Settings::ResetProfile(profile);
            if (binding_here) bind_action = -1;
            ReloadSettings();
            Settings::Save();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Puts \"%s\" back to its default buttons", profiles[profile].name.c_str());
        ImGui::Separator();

        if (binding_here && bind_action >= 0) {
            int pressed = CGamepadMapping::DetectPressedButton(controller);
            if (pressed < 0) {
                bind_armed = true;
            } else if (bind_armed) {
                Settings::ProfileButton(buttons, bind_action) = static_cast<CGamepadMapping::eButton>(pressed);
                bind_action = -1;
                changed = true;
            }
        }

        for (int a = 0; a < Settings::k_profileActionCount; ++a) {
            ImGui::PushID(a);
            auto& button = Settings::ProfileButton(buttons, a);
            if (binding_here && bind_action == a) {
                ImGui::Text("%s:", Settings::ProfileActionName(a));
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Press button...");
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    bind_action = -1;
            } else {
                // None (-1) shows as the last entry
                int value = (button == CGamepadMapping::None) ? 16 : static_cast<int>(button);
                ImGui::PushItemWidth(150);
                if (ImGui::Combo(Settings::ProfileActionName(a), &value, button_names->data(), (int)button_names->size())) {
                    button = (value == 16) ? CGamepadMapping::None : static_cast<CGamepadMapping::eButton>(value);
                    changed = true;
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Bind")) {
                    bind_owner = owner;
                    bind_profile = profile;
                    bind_action = a;
                    bind_armed = false;
                }
            }
            ImGui::PopID();
        }

        ImGui::PopID();

        if (changed) {
            Settings::ReapplyProfile(profile);
            ReloadSettings();
            Settings::Save();
        }
    }

    void ProfileContext(int index) {
        char buffer[1024];
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        auto p_profile = CGameManager::get_profile(index);
        auto& ms = Settings::State();
        const char* items[] = {"Controller 1", "Controller 2", "Controller 3", "Controller 4", "NONE"};

        if (p_profile == nullptr)
            return;

        ImGui::PushItemWidth(200);
        String::convert(buffer, p_profile->name, 1024);
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
            String::convert(p_profile->name, buffer, 1024);
        ImGui::PopItemWidth();

        ImGui::BeginDisabled(!index && p_setting->b_player0_use_km);

        int controller_index = p_profile->controller_index;

        // Check for controller binding completion
        if (s_binding_player == index) {
            int detected = DetectActiveController();
            if (detected >= 0) {
                p_profile->controller_index = detected;
                s_binding_player = -1;
            }
        }

        if (s_binding_player == index) {
            // Show binding prompt
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Press any button on controller...");
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                s_binding_player = -1;
            }
        } else {
            // Show dropdown and bind button
            ImGui::PushItemWidth(200);
            ImGui::Combo("Input", &p_profile->controller_index, items, IM_ARRAYSIZE(items));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            sprintf(buffer, "Bind##ctrl%d", index);
            if (ImGui::Button(buffer) && s_binding_player < 0) {
                s_binding_player = index;
            }
        }
        if (s_binding_player == index) s_binding_frame = ImGui::GetFrameCount(); // BindPending

        if (p_profile->controller_index != controller_index) {
            ms.controllerIndex[index] = p_profile->controller_index;
            Settings::Save();
        }

        ImGui::EndDisabled();

        SharedPlayerContext(index);

        bool is_disabled = (!index && !p_setting->b_override_profile) || (index && p_setting->b_use_player0_profile);

        if (ImGui::CollapsingHeader("Gamepad Mapping")) {
            ImGui::Indent();
            ImGui::BeginDisabled(is_disabled);

            ImGui::PushItemWidth(200);
            if (ProfileCombo("Controller Profile##mapping", &ms.controllerProfile[index])) {
                Settings::ApplyPlayerMapping(index);
                ReloadSettings();
                Settings::Save();
            }
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Changing a button below changes this profile for every player using it");

            // binds listen to this player's own controller
            int controller = (p_profile->controller_index >= 0 && p_profile->controller_index < 4) ? p_profile->controller_index : 0;
            char owner[16];
            snprintf(owner, sizeof(owner), "player%d", index);
            ProfileButtonsContext(owner, ms.controllerProfile[index], controller);

            if (ImGui::TreeNode("Other Actions")) {
                ImGui::TextDisabled("Not part of a controller profile: this player only, and not saved");
                p_profile->mapping.ImGuiContext(true);
                ImGui::TreePop();
            }
            ImGui::EndDisabled();
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Armor")) {
            ImGui::Indent();
            ImGui::BeginDisabled(is_disabled);
            for (int game = 0; game < Settings::ArmorGameCount; ++game) {
                ImGui::PushID(game);
                if (ImGui::CollapsingHeader(Settings::ArmorGameName(game))) {
                    ImGui::Indent();
                    ArmorGameContext(index, p_profile, game);
                    ImGui::Unindent();
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Profile")) {
            ImGui::Indent();
            ImGui::BeginDisabled(is_disabled);

            // The raw editor also shows look inversion and sensitivity; carry
            // an edit there over to the shared settings.
            bool invert = p_profile->profile.LookControlsInverted;
            unsigned char sensitivity = p_profile->profile.VerticalLookSensitivity;
            p_profile->profile.ImGuiContext();

            if (p_profile->profile.LookControlsInverted != invert ||
                p_profile->profile.VerticalLookSensitivity != sensitivity) {
                ms.invert[index] = p_profile->profile.LookControlsInverted;
                ms.sensitivity[index] = p_profile->profile.VerticalLookSensitivity;
                Settings::Save();
            }

            ImGui::EndDisabled();
            ImGui::Unindent();
        }
    }

    void RealContext() {
        char buffer[10];
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        auto& ms = Settings::State();

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Options")) {
                if (ImGui::MenuItem("Use player1's profile", nullptr, &p_setting->b_use_player0_profile)) {
                    ms.usePlayer0Profile = p_setting->b_use_player0_profile;
                    Settings::Save();
                }
                if (ImGui::MenuItem("Enable K/M for player1", nullptr, &p_setting->b_player0_use_km)) {
                    ms.useKM = p_setting->b_player0_use_km;
                    Settings::Save();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Players: [-] n [+]
        int player_count = ms.playerCount;
        float button_size = ImGui::GetFrameHeight();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Players");
        ImGui::SameLine();
        // in a hot-join Halo CE or Halo 2 game the count changes only by joining and leaving
        bool held = HotJoinHeld();
        ImGui::BeginDisabled(held || player_count <= 1);
        if (ImGui::Button("-", ImVec2(button_size, button_size)))
            --player_count;
        ImGui::EndDisabled();
        ImGui::SameLine();
        char count_text[4];
        snprintf(count_text, sizeof(count_text), "%d", ms.playerCount);
        // centered in a box as wide as a button
        float text_width = ImGui::CalcTextSize(count_text).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (button_size - text_width) * 0.5f);
        ImGui::TextUnformatted(count_text);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + (button_size - text_width) * 0.5f);
        ImGui::BeginDisabled(held || player_count >= 4);
        if (ImGui::Button("+", ImVec2(button_size, button_size)))
            ++player_count;
        ImGui::EndDisabled();
        if (held && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Players join and leave in the Players window until this hot-join game ends");
        if (player_count != ms.playerCount) {
            ms.playerCount = player_count;
            Settings::ApplySplitscreen(false);
            Settings::Save();
        }

        if (ImGui::BeginTabBar("Players")) {
            for (int i = 0; i < p_setting->player_count; ++i) {
                sprintf(buffer, "Player %d", i + 1);
                if (ImGui::BeginTabItem(buffer)) {
                    ProfileContext(i);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
}

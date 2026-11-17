#include "CGameManager.h"

#include "common.h"

#include "./mcc.h"
#include "global/Global.h"
#include "input/Input.h"
#include "mcc/settings/MenuSettings.h"
#include "log/MatchLog.h"

using AlphaRing::MatchLog::Describe;
using AlphaRing::MatchLog::FirstThisMatch;

#include <atomic>

void CGameManager::set_vibration(CGameManager *self, DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) {
    CInputDevice* p_device;
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();

    if (!p_setting->b_override) {
        ppOriginal.set_vibration(self, dwUserIndex, pVibration);
        return;
    }

    if (dwUserIndex >= p_setting->player_count)
        return;

    if (pVibration == nullptr || (p_device = get_controller(dwUserIndex)) == nullptr)
        return;

    AlphaRing::Input::SetState(p_device->input_user, pVibration);
}

// Halo 4 campaigns fail when a mission *starts* with players 3-4 (every view stays black),
// yet the engine spawns late joiners next to a teammate, like a controller signing in
// mid-game on the original console. So Halo 4 starts with two players and the rest are
// revealed once the game has been running for a moment. (Halo CE and Halo 2 never pick up
// late joiners, so they - like the other games - start with everyone.)
static constexpr int kDeferredJoinStartPlayers = 2;
static constexpr ULONGLONG kDeferredJoinDelayMs = 3000;
static std::atomic<ULONGLONG> s_running_since; // 0 until the current session first runs

void CGameManager::track_state(eState state) {
    if (state == Running && !s_running_since) // also re-sent after level transitions
        s_running_since = GetTickCount64();
}

// MCC ends every session with a restart after the exit states. The clock is cleared there,
// not on the exit states: dropping players while the engine is still shutting down can hang
// it, and the next session queries its players before its own loading state.
void CGameManager::end_session() {
    s_running_since = 0;
}

static int deferred_player_count(int count) {
    if (count <= kDeferredJoinStartPlayers) return count;

    auto p_global = GameGlobal();
    if (p_global == nullptr) return count;

    if (p_global->current_game != CGameGlobal::Halo4)
        return count;

    ULONGLONG since = s_running_since;
    if (since && GetTickCount64() - since >= kDeferredJoinDelayMs)
        return count;

    return kDeferredJoinStartPlayers;
}

int CGameManager::active_player_count() {
    static std::atomic<int> s_last_logged = -1;
    int active = deferred_player_count(AlphaRing::Global::MCC::Splitscreen()->player_count);

    if (active != s_last_logged) {
        s_last_logged = active;
        auto p_global = GameGlobal();
        LOG_INFO("Splitscreen: exposing {} local players (game {}, in game {})",
                 active, p_global ? (int)p_global->current_game : -1, MCC::IsInGame());
    }

    return active;
}

static bool get_xbox_user_id_inner(CGameManager *self, __int64 *pId, wchar_t *pName, int size, int index,
                                   const char** path) {
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();
    auto p_profile = CGameManager::get_profile(index);

    if (!p_setting->b_override || !index) {
        *path = "original";
        return CGameManager::ppOriginal.get_xbox_user_id(self, pId, pName, size, index);
    }

    if (index >= CGameManager::active_player_count()) {
        *path = "beyond player_count";
        return false;
    }

    *path = "alpha ring";
    if (pId)
        *pId = p_profile->id;

    if (pName)
        String::wstrcpy(pName, p_profile->name, size >> 1);

    return true;
}

bool CGameManager::get_xbox_user_id(CGameManager *self, __int64 *pId, wchar_t *pName, int size, int index) {
    const char* path = "";
    bool result = get_xbox_user_id_inner(self, pId, pName, size, index, &path);

    // once per match per slot, and whenever a slot's answer changes
    static std::atomic<long> s_seen[8];
    static std::atomic<int> s_last[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    if (index >= 0 && index < 8) {
        bool first = FirstThisMatch(s_seen[index]);
        bool changed = s_last[index].exchange(result ? 1 : 0) != (result ? 1 : 0);
        if (first || changed)
            MATCH_LOG("get_xbox_user_id index={} -> {} id={:#x} via {} (caller {})", index, result,
                      (result && pId) ? (unsigned long long)*pId : 0ull, path, Describe(_ReturnAddress()));
    } else {
        MATCH_LOG("get_xbox_user_id index={} (out of range) -> {} via {}", index, result, path);
    }
    return result;
}

bool CGameManager::get_key_state(CGameManager *self, DWORD index, input_data_t *p_input) {
    bool result;
    LARGE_INTEGER qpc;
    float delta_time = 0;
    CInputDevice* p_device;

    auto p_profile = AlphaRing::Global::MCC::Splitscreen();
    auto device_manager = DeviceManager();
    auto p_global = AlphaRing::Global::Global();

    memset(p_input, 0, sizeof(input_data_t));

    static std::atomic<long> s_seen[8];
    if (index < 8 && FirstThisMatch(s_seen[index]))
        MATCH_LOG("get_key_state index={} override={} player_count={} menu={} (caller {})", index,
                  p_profile->b_override, p_profile->player_count, p_global->show_imgui, Describe(_ReturnAddress()));

    if (p_global->show_imgui) {
        if (p_global->pause_game_on_menu_shown)
            return false;
        else if (p_global->disable_input_on_menu_shown)
            return true;
    }

    if (!p_profile->b_override)
        return ppOriginal.get_key_state(self, index, p_input);

    if (index >= p_profile->player_count)
        return false;

    static std::atomic<long> s_device_seen[8];
    bool log_device = index < 8 && FirstThisMatch(s_device_seen[index]);

    if (p_profile->b_player0_use_km && !index) {
        if (log_device)
            MATCH_LOG("input player={} reads keyboard/mouse", index);
        p_device = device_manager->p_input_device[4];
        device_manager->table->update_state(device_manager, 0, 0, false);
        QueryPerformanceCounter(&qpc);
        auto v1 = qpc.QuadPart - device_manager->qpc.QuadPart;
        auto v2 = device_manager->qpc.QuadPart - qpc.QuadPart;
        delta_time = fminf(fmaxf(MCC::DeltaTime(v1 >= v2 ? v2 : v1) * 1000.0,0.1), 1000.0);
        device_manager->qpc = qpc;
    } else {
        if ((p_device = get_controller(index)) == nullptr) {
            if (log_device)
                MATCH_LOG("input player={} has no controller (controller_index={})", index,
                          get_profile(index) ? get_profile(index)->controller_index : -1);
            return true;
        }

        bool connected = AlphaRing::Input::GetXInputGetState(p_device->input_user, &p_device->state);
        if (log_device)
            MATCH_LOG("input player={} reads controller_index={} input_user={} connected={}", index,
                      get_profile(index)->controller_index, p_device->input_user, connected);

        // first button press per player per match: shows a pad's input reaches that player
        static std::atomic<long> s_press_seen[8];
        if (index < 8 && p_device->state.Gamepad.wButtons != 0 && FirstThisMatch(s_press_seen[index]))
            MATCH_LOG("input player={} first press buttons={:#06x} (input_user={})", index,
                      p_device->state.Gamepad.wButtons, p_device->input_user);
    }

    result = p_device
            ->p_method_table
            ->set_state(p_device, delta_time, p_input);

    p_device
            ->p_method_table
            ->check(p_device);

    return result;
}

// Player 1's own profile from the game, with the armor picked for player 1 in
// the advanced menu laid over a copy of it. The game's profile itself is left
// alone, so the pick is never written back into the player's MCC profile.
static CUserProfile* player0_game_profile(CGameManager* self, CUserProfile* base) {
    if (!base || !AlphaRing::MenuSettings::HasArmor(0))
        return base;
    static CUserProfile s_player0;
    memcpy(&s_player0, base, sizeof(CUserProfile));
    AlphaRing::MenuSettings::ApplyArmor(0, s_player0);
    return &s_player0;
}

CUserProfile* CGameManager::get_player_profile(CGameManager *self, __int64 xid)  {
    auto index = get_index(xid);
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();

    static std::atomic<long> s_seen[4];
    if (FirstThisMatch(s_seen[index]))
        MATCH_LOG("get_player_profile xid={:#x} -> index {} override={} (caller {})", (unsigned long long)xid, index,
                  p_setting->b_override, Describe(_ReturnAddress()));

    if (!p_setting->b_override)
        return index ? ppOriginal.get_player_profile(self, xid)
                     : player0_game_profile(self, ppOriginal.get_player_profile(self, xid));

    if (!p_setting->b_override_profile && ((!index) || (index && p_setting->b_use_player0_profile)))
        return player0_game_profile(self, ppOriginal.get_player_profile(self, get_xuid(0)));

    if (p_setting->b_use_player0_profile)
        return &get_profile(0)->profile;

    auto* dest = get_profile(get_index(xid));

    // Reach initializes armor into the player-0 profile after game_setup fires, so the
    // snapshot taken by apply_profiles() is stale. Sync armor from the live player-0
    // profile on every call while preserving the per-player custom colors and look
    // settings we applied.
    auto* gg = GameGlobal();
    if (gg && gg->current_game == CGameGlobal::HaloReach) {
        auto* base = ppOriginal.get_player_profile(self, get_xuid(0));
        if (base) {
            int pc  = dest->profile.PlayerModelPrimaryColorIndex;
            int sc  = dest->profile.PlayerModelSecondaryColorIndex;
            int tc  = dest->profile.PlayerModelTertiaryColorIndex;
            int pci = dest->profile.PlayerModelPrimaryColor;
            int sci = dest->profile.PlayerModelSecondaryColor;
            int tci = dest->profile.PlayerModelTertiaryColor;
            bool lookInverted      = dest->profile.LookControlsInverted;
            bool mouseLookInverted = dest->profile.MouseLookControlsInverted;
            unsigned char vertSens  = dest->profile.VerticalLookSensitivity;
            unsigned char horizSens = dest->profile.HorizontalLookSensitivity;
            memcpy(&dest->profile, base, sizeof(CUserProfile));
            dest->profile.PlayerModelPrimaryColorIndex   = pc;
            dest->profile.PlayerModelSecondaryColorIndex = sc;
            dest->profile.PlayerModelTertiaryColorIndex  = tc;
            dest->profile.PlayerModelPrimaryColor        = pci;
            dest->profile.PlayerModelSecondaryColor      = sci;
            dest->profile.PlayerModelTertiaryColor       = tci;
            dest->profile.LookControlsInverted           = lookInverted;
            dest->profile.MouseLookControlsInverted      = mouseLookInverted;
            dest->profile.VerticalLookSensitivity        = vertSens;
            dest->profile.HorizontalLookSensitivity      = horizSens;
            // and the armor picked for this player in the advanced menu
            AlphaRing::MenuSettings::ApplyArmor(get_index(xid), dest->profile);
        }
    }

    return &dest->profile;
}

CGamepadMapping* CGameManager::retrive_gamepad_mapping(CGameManager *self, __int64 xid) {
    auto index = get_index(xid);
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();

    static std::atomic<long> s_seen[4];
    if (FirstThisMatch(s_seen[index]))
        MATCH_LOG("retrive_gamepad_mapping xid={:#x} -> index {} (caller {})", (unsigned long long)xid, index,
                  Describe(_ReturnAddress()));

    // Player 0's controller profile (Xbox menu button mapping) is not a splitscreen-only
    // feature and must apply in every game/mode, so it's kept independent of b_override.
    if (!index)
        return &get_profile(0)->mapping;

    if (!p_setting->b_override)
        return ppOriginal.retrive_gamepad_mapping(self, xid);

    if (p_setting->b_use_player0_profile)
        return &get_profile(0)->mapping;

    return &get_profile(index)->mapping;
}

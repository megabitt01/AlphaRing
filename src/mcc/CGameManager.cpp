#include "CGameManager.h"

#include "common.h"
#include "mcc/mcc.h"
#include "mcc/settings/MenuSettings.h"
#include "log/MatchLog.h"

#include <cstdio>
#include <guiddef.h>
#include <combaseapi.h>
#include <thread>
#include <chrono>

static struct ProfileContainer_t {CGameManager::Profile_t profiles[4]; ProfileContainer_t();} container;

CGameManager::Profile_t* CGameManager::get_profile(int index) {
    // Bounds check to prevent out-of-bounds access
    if (index < 0 || index >= 4)
        return nullptr;
    return container.profiles + index;
}

// Initialize default Xbox controller mapping for standard Halo controls
static void InitializeDefaultMapping(CGamepadMapping& mapping) {
    // Set all to None (unbound) first
    for (int i = 0; i < 66; i++) {
        mapping.actions[i] = CGamepadMapping::None;
    }

    // Standard Xbox Halo controls - only bind the essential actions
    mapping.actions[0]  = CGamepadMapping::A;             // Jump
    mapping.actions[1]  = CGamepadMapping::LeftShoulder;  // Switch Grenades
    mapping.actions[2]  = CGamepadMapping::X;             // Action/Interact
    mapping.actions[3]  = CGamepadMapping::RightShoulder; // Reload Right Weapon
    mapping.actions[4]  = CGamepadMapping::Y;             // Change Weapon
    mapping.actions[13] = mapping.actions[3];             // Swap/Reload Left Weapon (defaults to Reload Right Weapon)
    mapping.actions[5]  = CGamepadMapping::B;             // Melee
    mapping.actions[6]  = CGamepadMapping::DpadUp;        // Toggle Flashlight
    mapping.actions[7]  = CGamepadMapping::LeftTrigger;   // Throw Grenade
    mapping.actions[49] = mapping.actions[7];             // Use Left Weapon
    mapping.actions[28] = mapping.actions[7];             // Thrust
    mapping.actions[24] = mapping.actions[7];             // Vehicle Function 1
    mapping.actions[8]  = CGamepadMapping::RightTrigger;  // Use Right Weapon (Shoot)
    mapping.actions[9]  = CGamepadMapping::LeftThumb;     // Crouch
    mapping.actions[21] = mapping.actions[9];             // Vehicle Function 2
    mapping.actions[10] = CGamepadMapping::RightThumb;    // Player Zoom
    mapping.actions[20] = CGamepadMapping::Back;          // Multiplayer Scoreboard
    mapping.actions[22] = mapping.actions[0];             // Vehicle Function 3 (mirrors Jump)

    // Player Move Forward/Backward/Left/Right (H1A) stay unbound — movement is
    // handled by the analog stick, not the D-Pad.
    mapping.actions[16] = CGamepadMapping::None;
    mapping.actions[17] = CGamepadMapping::None;
    mapping.actions[18] = CGamepadMapping::None;
    mapping.actions[19] = CGamepadMapping::None;
}

ProfileContainer_t::ProfileContainer_t() {
    __int64 guid[2];
    const int controller_map[4] {0, 1, 2, 3};
    memset(this, 0, sizeof(ProfileContainer_t));

    CoCreateGuid((GUID*)guid);
    auto id = guid[0] ^ guid[1];

    for (int i = 0; i < 4; i++) {
        profiles[i].controller_index = controller_map[i];
        profiles[i].id = id + i;
        swprintf(profiles[i].name, L"Player %d", i + 1);

        // Initialize with standard Xbox Halo controls
        InitializeDefaultMapping(profiles[i].mapping);
    }
}

CGameManager* pGameManager;
CGameManager::FunctionTable CGameManager::ppOriginal;

bool CGameManager::Initialize(CGameManager* mng) {
    pGameManager = mng;
    return AlphaRing::Hook::Detour({
        {pGameManager->table->get_player_profile, get_player_profile, (void**)&ppOriginal.get_player_profile},
        {pGameManager->table->get_key_state, get_key_state, (void**)&ppOriginal.get_key_state},
        {pGameManager->table->get_xbox_user_id, get_xbox_user_id, (void**)&ppOriginal.get_xbox_user_id},
        {pGameManager->table->set_vibration, set_vibration, (void**)&ppOriginal.set_vibration},
        {pGameManager->table->retrive_gamepad_mapping, retrive_gamepad_mapping, (void**)&ppOriginal.retrive_gamepad_mapping},
        {pGameManager->table->set_state, set_state, (void**)&ppOriginal.set_state},
        {pGameManager->table->game_restart, game_restart, (void**)&ppOriginal.game_restart},
        {pGameManager->table->game_setup, game_setup, (void**)&ppOriginal.game_setup},
    });
}

__int64 CGameManager::get_xuid(int index) {
    __int64 result;

    // Bounds check
    if (index < 0 || index >= 4)
        return 0;

    if (index)
        return container.profiles[index].id;
    else
        return pGameManager->ppOriginal.get_xbox_user_id(pGameManager, &result, nullptr, 0, index) ? result : 0;
}

CInputDevice *CGameManager::get_controller(int index) {
    auto mng = DeviceManager();
    auto setting = AlphaRing::Global::MCC::Splitscreen();
    auto profile = get_profile(index);

    // Null checks to prevent crashes
    if (mng == nullptr || setting == nullptr || profile == nullptr)
        return nullptr;

    auto controller_index = profile->controller_index;

    if ((!index && setting->b_player0_use_km) || controller_index >= 4 || controller_index < 0)
        return nullptr;

    return mng->p_input_device[controller_index];
}

int CGameManager::get_index(__int64 xuid) {
    for (int i = 1; i < 4; ++i)
        if (container.profiles[i].id == xuid)
            return i;
    return 0;
}

void CGameManager::set_state(CGameManager *self, eState state) {
    static std::atomic<int> s_last_state{-1};
    if (s_last_state.exchange((int)state) != (int)state)
        MATCH_LOG("set_state {} (caller {})", (int)state, AlphaRing::MatchLog::Describe(_ReturnAddress()));
    return ppOriginal.set_state(self, state);
}

static void log_match_settings(const char* when) {
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();
    auto* gg = GameGlobal();
    MATCH_LOG("{}: game={} override={} player_count={} p1_km={} use_p1_profile={} override_profile={} in_game={}",
              when, gg ? (int)gg->current_game : -1, p_setting->b_override, p_setting->player_count,
              p_setting->b_player0_use_km, p_setting->b_use_player0_profile, p_setting->b_override_profile,
              MCC::IsInGame());
}

void CGameManager::apply_profiles() {
    auto p_mng = GameManager();
    if (!MCC::IsInGame() || !p_mng)
        return;
    // Always use player 0's real XUID — fake XUIDs for players 1-3 are unknown to the original game functions
    auto xuid = get_xuid(0);
    if (!xuid)
        return;
    auto* base_profile = p_mng->ppOriginal.get_player_profile(p_mng, xuid);
    auto* base_mapping  = p_mng->ppOriginal.retrive_gamepad_mapping(p_mng, xuid);
    if (!base_profile || !base_mapping)
        return;
    for (int i = 0; i < 4; ++i) {
        auto profile = get_profile(i);
        if (profile) {
            memcpy(&profile->profile, base_profile, sizeof(CUserProfile));
            memcpy(&profile->mapping,  base_mapping,  sizeof(CGamepadMapping));
        }
    }
}

// Events some engines can't take while a match is still being set up, so they
// are sent once it is running instead:
// - The settings reload makes the engine run its match setup again; Halo 2 and
//   Halo 4 break a starting match on it (Halo 4 ends it ~9 s in, one player
//   placing "-1th").
// - Team changes: H2A ends a starting match on them at once, and Halo 4 is
//   treated the same until shown otherwise. ApplyAll(true) leaves these two
//   games' teams out, and they are sent here.
// Only the latest pending batch fires, and only while a match is still on.
static std::atomic<unsigned> s_deferred_generation{0};

static void send_after_match_start(DWORD delay_ms, bool teams, bool reload) {
    unsigned generation = ++s_deferred_generation;
    MATCH_LOG("deferred {} ms (#{}): teams={} load_setting={}", delay_ms, generation, teams, reload);
    std::thread([generation, delay_ms, teams, reload]() {
        Sleep(delay_ms);
        if (generation != s_deferred_generation.load()) {
            MATCH_LOG("deferred #{} dropped: a newer one is pending", generation);
            return;
        }
        if (!MCC::IsInGame()) {
            MATCH_LOG("deferred #{} dropped: no match running", generation);
            return;
        }
        MATCH_LOG("deferred #{} firing", generation);
        if (teams) {
            std::lock_guard lock(AlphaRing::MenuSettings::Mutex());
            for (int i = 0; i < 4; ++i)
                AlphaRing::MenuSettings::ApplyPlayerTeam(i); // skips players not in the match
        }
        auto* e = GameEngine();
        if (reload && e) {
            MATCH_LOG("load_setting (deferred #{})", generation);
            e->load_setting();
        }
    }).detach();
}

static void apply_menu_settings() {
    AlphaRing::MenuSettings::ApplyAll(true);

    auto* gg = GameGlobal();
    auto game = gg ? gg->current_game : CGameGlobal::Halo3;
    bool late_teams  = game == CGameGlobal::Halo4 || game == CGameGlobal::GroundHog;
    bool late_reload = game == CGameGlobal::Halo2 || game == CGameGlobal::Halo4;

    if (!late_reload) {
        if (auto* engine = GameEngine()) {
            MATCH_LOG("load_setting");
            engine->load_setting();
        } else {
            MATCH_LOG("load_setting skipped: no engine");
        }
    }
    if (late_teams || late_reload)
        send_after_match_start(game == CGameGlobal::GroundHog ? 5000 : 10000, late_teams, late_reload);
}

static bool ShouldThrottleApply() {
    using clock = std::chrono::steady_clock;
    static clock::time_point s_last_apply{};
    constexpr auto k_min_interval = std::chrono::milliseconds(250);

    auto now = clock::now();
    bool first_call = s_last_apply.time_since_epoch().count() == 0;
    if (!first_call && (now - s_last_apply) < k_min_interval)
        return true;

    s_last_apply = now;
    return false;
}

void *CGameManager::game_restart(CGameManager *self, int type, const char *reason) {
    AlphaRing::MatchLog::NewMatch();
    MATCH_LOG("game_restart enter: type={} reason=\"{}\" (caller {})", type, reason ? reason : "<null>",
              AlphaRing::MatchLog::Describe(_ReturnAddress()));
    log_match_settings("game_restart before");
    auto result = ppOriginal.game_restart(self, type, reason);
    if (!ShouldThrottleApply()) {
        apply_profiles();
        apply_menu_settings();
        log_match_settings("game_restart applied");
    } else {
        MATCH_LOG("game_restart: apply throttled");
    }
    return result;
}

char __fastcall CGameManager::game_setup(CGameManager* self, void* a2) {
    AlphaRing::MatchLog::NewMatch();
    MATCH_LOG("game_setup enter (caller {})", AlphaRing::MatchLog::Describe(_ReturnAddress()));
    log_match_settings("game_setup before");
    char result = ppOriginal.game_setup(self, a2);
    MATCH_LOG("game_setup original returned {}", (int)result);
    if (!ShouldThrottleApply()) {
        apply_profiles();
        apply_menu_settings();
        log_match_settings("game_setup applied");
    } else {
        MATCH_LOG("game_setup: apply throttled");
    }
    return result;
}

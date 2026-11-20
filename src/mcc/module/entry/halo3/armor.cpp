#include "halo3.h"

#include "mcc/CGameManager.h"

#include <functional>

// Armor picked in the middle of a match. On load_setting (Xbox menu, Splitscreen window) Halo 3 copies each local
// user's MCC profile into its own settings, appearance included (built by the code around halo3.dll+0x2F7F0D and
// stored by SETTINGS_SET_APPEARANCE). But a player in a match keeps the configuration it was given as the match was
// set up, so a new pick showed from the next map on; Reach and Halo 4 read the profile again. So the stored
// appearance is given to that user's player in the match, through the game's own PLAYER_SET_CONFIGURATION (which
// also re-colors the player's body), and the network session's copy is brought up to date first
// (SESSION_UPDATE_LOCAL_USER, what Halo 3 does as a user's settings change in the lobby) so that its player updates
// (halo3.dll+0x514C9) carry the new armor too.
//
// A body's armor pieces are its region permutations, set only as it's made (from the player's appearance through
// APPEARANCE_TO_PLACEMENT), so the body the player is in is then re-dressed the same way: the appearance's
// permutations, applied with OBJECT_SET_PERMUTATIONS as an object updated from placement data is.
namespace Halo3::Entry::World { void AddTask(const std::function<void()>& func); }

namespace Halo3::Entry::Armor {
    // These run on the simulation thread (World's tasks), with the player table and globals the engine hook sets up.
    EntryFeature("Halo 3 armor in the match", OFFSET_HALO3_PF_PLAYER_SET_CONFIGURATION,
                 OFFSET_HALO3_PF_SESSION_UPDATE_LOCAL_USER, OFFSET_HALO3_PF_APPEARANCE_TO_PLACEMENT,
                 OFFSET_HALO3_PF_OBJECT_SET_PERMUTATIONS, OFFSET_HALO3_PF_WORLD, OFFSET_HALO3_PF_ENGINE);

    constexpr int kSettingsUser = 0x10; // the settings' local user (0-3)

    // object_placement_data: its size, and its region permutations (APPEARANCE_TO_PLACEMENT, OBJECT_SET_PERMUTATIONS)
    constexpr size_t kPlacementSize = 0x180;
    constexpr int kPlacementPermutationCount = 0xA8, kPlacementPermutations = 0xAC, kPlacementVariant = 0x12C;
    constexpr int kPlacementMaxPermutations = 16;

    constexpr char kGameTypeCampaign = 1; // game_globals map_info.game_type: spawns there leave the appearance out

    using set_configuration_t = void (*)(INDEX player, const c_player_configuration* configuration);
    using session_update_local_user_t = void (*)(int user);
    using appearance_to_placement_t = void (*)(const c_player_appearance* appearance, void* placement);
    using object_set_permutations_t = void (*)(INDEX object, int variant, const void* permutations, int count);

    // The body `unit` takes the armor pieces of `appearance`, as one made with it would have.
    void Redress(__int64 module, INDEX unit, const c_player_appearance& appearance) {
        auto globals = Halo3::Native::game_globals();
        if (globals == nullptr || globals->map_info.game_type == kGameTypeCampaign) return;

        alignas(16) unsigned char placement[kPlacementSize] = {};
        ((appearance_to_placement_t)(module + OFFSET_HALO3_PF_APPEARANCE_TO_PLACEMENT))(&appearance, placement);
        int count = *(int*)(placement + kPlacementPermutationCount);
        if (count <= 0 || count > kPlacementMaxPermutations) return;
        ((object_set_permutations_t)(module + OFFSET_HALO3_PF_OBJECT_SET_PERMUTATIONS))(
                unit, *(int*)(placement + kPlacementVariant), placement + kPlacementPermutations, count);
    }

    // On the simulation thread: the player of local user `user` (by the XUID AlphaRing gives that player) wears
    // `appearance` from now on, the body they're in too.
    void GiveToPlayer(__int64 module, int user, const c_player_appearance& appearance) {
        // the session's copy first, built from the settings the appearance was just stored in
        ((session_update_local_user_t)(module + OFFSET_HALO3_PF_SESSION_UPDATE_LOCAL_USER))(user);

        auto xuid = CGameManager::get_xuid(user);
        auto players = Halo3::Native::players();
        if (xuid == 0 || players == nullptr || players->m_data == 0) return;
        for (short i = 0; players->isValid(i); ++i) {
            auto player = players->get(i);
            unsigned short salt = *(unsigned short*)player;
            if (salt == 0) continue; // a free slot
            if (player->player_identifier != xuid && player->configuration.client.player_xuid != xuid) continue;

            // The session's player update can set it first (as the session copy changes), so the configuration
            // and the body are each brought up to date on their own.
            if (memcmp(&player->configuration.client.player_appearance, &appearance, sizeof(appearance)) != 0) {
                c_player_configuration configuration = player->configuration; // the rest unchanged, the team too
                configuration.client.player_appearance = appearance;
                ((set_configuration_t)(module + OFFSET_HALO3_PF_PLAYER_SET_CONFIGURATION))(
                        ((INDEX)salt << 16) | (unsigned short)i, &configuration);
            }

            // the body last dressed for this user, and in what (a new body is made in the player's appearance)
            static Datum s_unit[4] = {(Datum)NONE, (Datum)NONE, (Datum)NONE, (Datum)NONE};
            static c_player_appearance s_worn[4];
            Datum unit = player->unit_datum;
            if (unit != (Datum)NONE && (unit != s_unit[user] || memcmp(&s_worn[user], &appearance, sizeof(appearance)) != 0)) {
                Redress(module, unit, appearance);
                s_unit[user] = unit;
                s_worn[user] = appearance;
                LOG_INFO("Halo 3: player {} (user {}) wears the armor picked", i, user);
            }
            return;
        }
    }

    Halo3Entry(entry, OFFSET_HALO3_PF_SETTINGS_SET_APPEARANCE, __int64, detour, char* settings,
               const c_player_appearance* appearance) {
        auto result = ((detour_t)entry.m_pOriginal)(settings, appearance);
        int user = *(int*)(settings + kSettingsUser);
        if (appearance != nullptr && user >= 0 && user < 4) {
            __int64 module = entry.m_target - entry.m_offset;
            c_player_appearance copy = *appearance;
            Halo3::Entry::World::AddTask([module, user, copy] { GiveToPlayer(module, user, copy); });
        }
        return result;
    }
}

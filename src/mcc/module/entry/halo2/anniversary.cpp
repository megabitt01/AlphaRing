// Halo 2 Anniversary graphics with 3-4 players (experimental, MCC::Splitscreen::AnniversaryQuadChosen).
//
// H2A's Saber renderer draws split screen for exactly two local players: the game hands two views over, the renderer
// draws each at full width and half height, and a quad puts each image on its half of the screen. As in Halo CE
// (halo1/anniversary.cpp) the frame keeps its two views and the pairs alternate: players 1 and 2 on one frame, 3 and 4
// on the next. The views are drawn a quarter of the screen in size (QuarterViews), each camera shaped like its
// player's cell in Halo 2's own grid, and each image is drawn into that cell. The other pair's cells are kept in a
// texture of ours and put back before the HUD, which Halo 2's window loop already draws for every player. What the
// game works out per view (visible objects, the own body, first-person models) follows the pair. Two players are left
// to the game and the D3D11 composite clip (render/d3d11/Hook.cpp).
#include "halo2.h"

#include "mcc/CGameGlobal.h"
#include "mcc/module/entry/PreservingThunk.h"
#include "mcc/splitscreen/Splitscreen.h"
#include "CellShaders.h"

#include <d3d11.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <malloc.h>
#include <mutex>
#include <vector>

namespace Halo2::Entry::Anniversary {
    EntryFeature("Halo 2 Anniversary graphics with 3-4 players",
                 OFFSET_HALO2_PV_RESPAWN, OFFSET_HALO2_PV_ANNIVERSARY, OFFSET_HALO2_PV_CLASSIC_SHOWN, OFFSET_HALO2_PF_CINEMATIC,
                 OFFSET_HALO2_PV_PLAYERS, OFFSET_HALO2_PV_SABER_DEVICE, OFFSET_HALO2_PF_SPLIT_GRID, OFFSET_HALO2_PF_SPLIT_CELL,
                 OFFSET_HALO2_TWO_PLAYERS_TEST_HAND_OVER, OFFSET_HALO2_TWO_PLAYERS_TEST_OBJECT_SYNC,
                 OFFSET_HALO2_TWO_PLAYERS_TEST_SCREEN_EFFECT, OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_1,
                 OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_2, OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_3,
                 OFFSET_HALO2_PV_SABER_CAMERAS, OFFSET_HALO2_PF_SABER_CAMERA_SET_FOV, OFFSET_HALO2_PV_MODEL_LEVELS,
                 OFFSET_HALO2_PF_FIRST_PERSON_BUILD, OFFSET_HALO2_PV_FIRST_PERSON_MODELS, OFFSET_HALO2_PF_SABER_OBJECT_SHOW,
                 OFFSET_HALO2_PF_SABER_OBJECT_HIDE, OFFSET_HALO2_PV_SABER_CONTEXT, OFFSET_HALO2_PV_SABER_SCENE,
                 OFFSET_HALO2_PV_SABER_HDR_VIEWS, OFFSET_HALO2_PV_SABER_HDR_READ_BACKS);

    // Everything the mode hooks and reaches is in this build (MCC::Splitscreen::ClassicGraphicsScope asks).
    bool Available() { return entry_feature->Available(); }

    int Players(__int64 module) {
        return std::clamp((int)*(short*)(*(char**)(module + OFFSET_HALO2_PV_RESPAWN) + 8), 0, 4);
    }

    // A mission that started with the mode chosen, with 3-4 players and Anniversary graphics on screen (Back switches
    // them in the middle of a mission), outside cinematics.
    bool Quad(__int64 module) {
        return MCC::Splitscreen::AnniversaryQuadActive(CGameGlobal::Halo2) && Players(module) >= 3 &&
               !Splitscreen::ClassicShown(module) && !((bool (*)())(module + OFFSET_HALO2_PF_CINEMATIC))();
    }

    // Whether local player `player` has had a unit since the module loaded. Players 3 and 4 wait for a co-op respawn
    // (the missions have starting places for two), and until then their camera looks at nothing: Classic shows their
    // window black, and so does this.
    std::atomic<bool> s_spawned[4] = {};
    bool Spawned(__int64 module, int player) {
        if (s_spawned[player]) return true;
        int handle = Entry::Players::Handle(module, player);
        char* data = handle != -1 ? Entry::Players::Player(module, handle) : nullptr;
        return data != nullptr && (s_spawned[player] = *(int*)(data + 0x2C) != -1); // the player's unit
    }

    char* Settings(__int64 module) { return *(char**)(*(char**)(module + OFFSET_HALO2_PV_SABER_DEVICE) + 0x118); }

    // A player's cell in Halo 2's grid (the one its HUD is drawn in), in pixels of the screen or of the views.
    struct Cell {
        int left, top, right, bottom;
    };
    Cell PlayerCell(__int64 module, int player, int players, bool screen) {
        char* settings = Settings(module);
        int width = *(int*)(settings + (screen ? 0x10 : 0x20)), height = *(int*)(settings + (screen ? 0x14 : 0x24));
        short grid[2], cell[2], span[2];
        ((void (*)(int, int, short*))(module + OFFSET_HALO2_PF_SPLIT_GRID))(players, 1, grid);
        ((void (*)(int, int, int, short*, short*, short*))(module + OFFSET_HALO2_PF_SPLIT_CELL))(player, players, 1, grid,
                                                                                                cell, span);
        int columns = (std::max)((int)grid[0], 1), rows = (std::max)((int)grid[1], 1);
        return {cell[0] * width / columns, cell[1] * height / rows, (cell[0] + span[0]) * width / columns,
                (cell[1] + span[1]) * height / rows};
    }

    // ---- split screen: the game's glue turns it on for exactly two local players; at its sites quad mode answers two

    const AlphaRing::Offset* const kTwoPlayerTests[] = {
        &OFFSET_HALO2_TWO_PLAYERS_TEST_HAND_OVER, &OFFSET_HALO2_TWO_PLAYERS_TEST_OBJECT_SYNC,
        &OFFSET_HALO2_TWO_PLAYERS_TEST_SCREEN_EFFECT, &OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_1,
        &OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_2, &OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_3};

    bool TwoPlayerTest(__int64 at) {
        return std::any_of(std::begin(kTwoPlayerTests), std::end(kTwoPlayerTests), [&](auto test) { return *test == at; });
    }

    PreservedEntry(entry_local_players, Halo2EntrySet(), OFFSET_HALO2_PF_LOCAL_PLAYER_COUNT, local_players, void*, void*,
                   void*, void*, __int64 caller) {
        __int64 module = entry_local_players.m_target - entry_local_players.m_offset;
        int players = *(short*)(*(char**)(module + OFFSET_HALO2_PV_RESPAWN) + 8); // as the game's leaf reads it
        if (players > 2 && MCC::Splitscreen::AnniversaryQuadActive(CGameGlobal::Halo2) && TwoPlayerTest(caller - module) &&
            Quad(module))
            return 2;
        return players;
    }

    // ---- players 3 and 4: two cameras of ours (the renderer's slots past 1 can be the scene's picture-in-picture
    // cameras), handed over after the game's two

    char* s_cameras[2] = {};
    std::atomic<int> s_pair = 0; // the pair of the next camera list

    char*** CameraArray(__int64 module) { return (char***)(*(char**)(module + OFFSET_HALO2_PV_SABER_CAMERAS) + 0x100); }
    int& CameraCount(__int64 module) { return *(int*)(*(char**)(module + OFFSET_HALO2_PV_SABER_CAMERAS) + 0x108); }

    Halo2Entry(entry_hand_over, OFFSET_HALO2_PF_SABER_HAND_OVER_VIEW, void, hand_over, int view, bool split) {
        ((hand_over_t)entry_hand_over.m_pOriginal)(view, split);
        __int64 module = entry_hand_over.m_target - entry_hand_over.m_offset;
        if (view != 1 || !Quad(module) || CameraCount(module) < 2) return;
        char**& array = *CameraArray(module);
        int& count = CameraCount(module);
        for (char*& camera : s_cameras) {
            if (camera == nullptr) {
                camera = (char*)_aligned_malloc(0x398, 16);
                if (camera == nullptr) return;
                memcpy(camera, array[1], 0x398);
            }
            memcpy(camera + 0x13C, array[1] + 0x13C, 0x10); // the viewport, as the game lays out view 1's
        }
        // The hand-over writes the renderer's camera `view`: ours stand in a copy of the array meanwhile (the list
        // is built after this whole hand-over, which it waits for).
        constexpr int kMaxCameras = 16;
        if (count > kMaxCameras) return;
        char* cameras[kMaxCameras] = {};
        std::copy(array, array + count, cameras);
        cameras[2] = s_cameras[0];
        cameras[3] = s_cameras[1];
        char** stock = array;
        int stock_count = count;
        array = cameras;
        count = (std::max)(count, 4);
        for (int player = 2; player < Players(module); ++player)
            ((hand_over_t)entry_hand_over.m_pOriginal)(player, split);
        array = stock;
        count = stock_count;
    }

    // ---- the camera list: the pair's cameras, each shaped like its player's cell, and the pair in the list's flags
    // (which the renderer only reads bits 0 and 1 of) so that it reaches the frame that draws the list

    constexpr unsigned kQuadList = 1u << 28, kSecondPairList = 1u << 30;

    thread_local int t_pair = -1; // the pair of the list being built

    Halo2Entry(entry_build_list, OFFSET_HALO2_PF_SABER_BUILD_CAMERA_LIST, __int64, build_list, void* renderer,
               unsigned* list, __int64 split, void* r9) {
        __int64 module = entry_build_list.m_target - entry_build_list.m_offset;
        if (!(unsigned char)split || !Quad(module)) {
            s_pair = 0;
            return ((build_list_t)entry_build_list.m_pOriginal)(renderer, list, split, r9);
        }
        int pair = s_pair;
        t_pair = pair;
        auto result = ((build_list_t)entry_build_list.m_pOriginal)(renderer, list, split, r9);
        t_pair = -1;
        *list |= kQuadList | (pair ? kSecondPairList : 0);
        s_pair = pair ^ 1;
        return result;
    }

    Halo2Entry(entry_list_camera, OFFSET_HALO2_PF_SABER_LIST_CAMERA, __int64, list_camera, void* list, char* camera,
               __int64 flags, __int64 index, __int64 a5, __int64 a6, __int64 a7, __int64 a8, __int64 a9, __int64 a10) {
        auto original = [&](char* listed) {
            return ((list_camera_t)entry_list_camera.m_pOriginal)(list, listed, flags, index, a5, a6, a7, a8, a9, a10);
        };
        if (t_pair < 0 || !(flags & 0x30)) return original(camera);
        __int64 module = entry_list_camera.m_target - entry_list_camera.m_offset;
        int slot = (flags & 0x10) ? 0 : 1, players = Players(module);
        int player = (std::min)(slot + 2 * t_pair, players - 1); // 3 players: the third twice, the second not drawn
        char* source = player < 2 ? camera : s_cameras[player - 2];
        if (source == nullptr) return original(camera);

        alignas(16) char view[0x398];
        memcpy(view, source, sizeof view);
        if (source != camera) { // prepared as the builder prepares the game's: no scene links, its near and far
            memset(view + 0x200, 0, 0x20);
            memcpy(view + 0x80, camera + 0x80, 8);
        }
        Cell cell = PlayerCell(module, player, players, false);
        *(float*)(view + 0x158) = (float)(cell.bottom - cell.top) / (float)(cell.right - cell.left) / *(float*)(Settings(module) + 0x1C);
        *(float*)(view + 0x144) = (float)(cell.right - cell.left);
        *(float*)(view + 0x148) = (float)(cell.bottom - cell.top);
        ((void (*)(char*, float))(module + OFFSET_HALO2_PF_SABER_CAMERA_SET_FOV))(view, *(float*)(view + 0x154));
        *(int*)(view + 0x220) = slot; // the renderer's per-view tables have two slots
        if (player != slot + 2 * t_pair) flags |= 4; // 3 players: listed as a second view, which is skipped
        return original(view);
    }

    // ---- what each view shows (game side): the game works out two views' visible objects, own body and first-person
    // models, for local players 0 and 1; in quad mode those are the players of the pair whose list the frame builds

    std::atomic<int> s_sync_pair = 0;      // the pair the game frame's per-view work is for
    std::atomic<bool> s_sync_quad = false; // and whether it's in quad mode (as of its window loop)

    int Viewer(int slot) { return slot + 2 * s_sync_pair; }

    // Each window's visible objects, stored for view 0 (local player 0) or view 1 (the others): here for the view of
    // its player in the pair, and not at all for the other pair.
    Halo2Entry(entry_visible_objects, OFFSET_HALO2_PF_PLAYER_VISIBLE_OBJECTS, void, visible_objects, int local) {
        __int64 module = entry_visible_objects.m_target - entry_visible_objects.m_offset;
        bool quad = Quad(module);
        if (local == 0) { // the window loop starts every game frame with player 1
            s_sync_quad = quad;
            s_sync_pair = s_pair.load();
        }
        if (!quad) return ((visible_objects_t)entry_visible_objects.m_pOriginal)(local);
        int slot = local - 2 * s_sync_pair;
        if (slot == 0 || slot == 1) ((visible_objects_t)entry_visible_objects.m_pOriginal)(slot);
    }

    // Whether `object` is the unit of view `local`'s player, seen in first person (hidden in that view).
    Halo2Entry(entry_own_unit, OFFSET_HALO2_PF_OWN_UNIT_IN_FIRST_PERSON, bool, own_unit, int object, int local) {
        if (local < 0 || local > 1 || !s_sync_quad) return ((own_unit_t)entry_own_unit.m_pOriginal)(object, local);
        int viewer = Viewer(local);
        return viewer < Players(entry_own_unit.m_target - entry_own_unit.m_offset) &&
               ((own_unit_t)entry_own_unit.m_pOriginal)(object, viewer);
    }

    // A first-person model's parts (as the game's first-person sync finds them).
    char* ModelComponent(__int64 module, char* object) {
        char* model = *(char**)(object + 0x18);
        if (model == nullptr) return nullptr;
        char* levels = *(char**)(module + OFFSET_HALO2_PV_MODEL_LEVELS);
        int level = (unsigned char)levels[*(int*)(module + OFFSET_HALO2_PV_MODEL_LEVELS + 8) - 1];
        unsigned char index = (*(unsigned char**)(*(char**)(model + 0xC0) + 0x10))[level];
        return index == 0xFF ? nullptr : (*(char***)(model + 0xB0))[index];
    }

    // The first-person sync, for the pair: every player's models are built (the game builds players 1 and 2's), and
    // each shows only in its owner's view. The game's own only hides, as its owners never change views.
    Halo2Entry(entry_first_person, OFFSET_HALO2_PF_FIRST_PERSON_SYNC, void, first_person) {
        __int64 module = entry_first_person.m_target - entry_first_person.m_offset;
        if (!Quad(module)) return ((first_person_t)entry_first_person.m_pOriginal)();
        for (int player = 0; player < Players(module); ++player)
            ((void (*)(int))(module + OFFSET_HALO2_PF_FIRST_PERSON_BUILD))(player);

        char* entries = *(char**)(module + OFFSET_HALO2_PV_FIRST_PERSON_MODELS);
        int count = *(int*)(module + OFFSET_HALO2_PV_FIRST_PERSON_MODELS + 8);
        for (int i = 0; i < count; ++i) {
            char* entry = entries + i * 0x28;
            char** handle = *(char***)(entry + 8);
            char* object = handle != nullptr ? *handle : nullptr;
            if (object == nullptr) continue;
            bool active = entry[0x20] != 0, shown = (object[0xC] & 1) != 0;
            if (active != shown)
                ((void (*)(char*))(module + (active ? OFFSET_HALO2_PF_SABER_OBJECT_SHOW : OFFSET_HALO2_PF_SABER_OBJECT_HIDE)))(object);

            int owner = *(int*)(entry + 0x1C);
            char* component = ModelComponent(module, object);
            for (int slot = 0; slot < 2; ++slot) {
                bool mine = owner == Viewer(slot);
                unsigned& views = *(unsigned*)(object + 0x10);
                views = mine ? views & ~(0x80u << slot) : views | (0x80u << slot);
                if (component == nullptr) continue;
                char** parts = *(char***)(component + 0x68);
                for (int j = 0; j < *(int*)(component + 0x70); ++j) {
                    char* part = parts[j];
                    unsigned char hidden = (unsigned char)(8 << slot), & flags = *(unsigned char*)(part + 0x50);
                    void** vtable = *(void***)part;
                    if (!mine && !(flags & hidden)) {
                        ((void (*)(char*, int))vtable[0x88 / 8])(part, slot);
                        flags |= hidden;
                    } else if (mine && (flags & hidden)) {
                        if (!(flags & 4)) ((void (*)(char*, int))vtable[0x90 / 8])(part, slot);
                        flags &= ~hidden;
                    }
                }
            }
        }
    }

    // ---- the frame (render thread): each view is drawn into its player's cell, the cell is kept in a texture of
    // ours, and the other pair's cells are put back before the HUD

    bool s_frame_quad = false;
    int s_frame_pair = 0, s_frame_players = 0;
    bool s_drawn[4] = {};
    bool s_collapse = false;                 // the game's composite quad covers nothing (CompositeQuad)
    ID3D11Texture2D* s_kept = nullptr;       // the cells, as last drawn
    ID3D11Resource* s_target = nullptr;      // the frame's target the views are drawn to
    bool s_target_ready = false;             // and ours made to match it (PrepareTarget)
    // players 3 and 4's exposure (hdr_pass): views 0 and 1's adaptation on their frames, and whether it's kept yet
    constexpr int kHdrView = 0x4C, kHdrAdaptation = 0x3C, kHdrAdaptationSize = 0x10;
    char s_second_pair_adaptation[2][kHdrAdaptationSize];
    bool s_second_pair_adapting[2] = {};

    // 3 players: nobody's second view on the second pair's frames
    bool NoSecondView() { return s_frame_pair == 1 && s_frame_players < 4; }

    ID3D11DeviceContext* Context(__int64 module) {
        return *(ID3D11DeviceContext**)(*(char**)(module + OFFSET_HALO2_PV_SABER_CONTEXT) + 0xD58);
    }

    // ---- quarter-size views. The renderer draws a view the size of the target it's drawn into - the "_split"
    // textures of its render targets, full width and half height, both views in turn. For as long as quad mode lasts
    // they're made half as wide, their cell's size: half the pixels to draw.

    std::mutex s_split_mutex; // both lists: targets are added and deleted off the render thread too
    std::vector<char*> s_split_textures; // every render target's "_split" textures

    Halo2Entry(entry_add_target, OFFSET_HALO2_PF_SABER_ADD_RENDER_TARGET, char*, add_target, char* set, void* name,
               __int64 a3, __int64 a4, __int64 a5, __int64 a6, __int64 a7, __int64 a8, __int64 a9) {
        int before = *(int*)(set + 0xC);
        char* target = ((add_target_t)entry_add_target.m_pOriginal)(set, name, a3, a4, a5, a6, a7, a8, a9);
        int after = *(int*)(set + 0xC);
        std::lock_guard<std::mutex> lock(s_split_mutex);
        for (int i = before; i < after; ++i) {
            char* entry = set + 0x10 + i * 0x30;
            char* texture = *(char**)entry;
            if (texture != nullptr && (*(unsigned*)(entry + 8) & 0x60000000) &&
                std::find(s_split_textures.begin(), s_split_textures.end(), texture) == s_split_textures.end())
                s_split_textures.push_back(texture);
        }
        return target;
    }

    struct Narrowed {
        char* texture;
        short width; // as the renderer made it
    };
    std::vector<Narrowed> s_narrowed;
    bool s_views_narrowed = false; // render thread: the last frame was quad mode's, so there may be widths to put back

    // The renderer deletes a texture whatever its reference count (a target set's teardown, a new resolution): it
    // leaves both lists first. Nothing the lists' users call on a texture takes the texture manager's lock, which the
    // deleting thread may hold.
    Halo2Entry(entry_delete_texture, OFFSET_HALO2_PF_SABER_DELETE_TEXTURE, void, delete_texture, char* manager,
               int index) {
        if (char* texture = (*(char***)(manager + 0x220))[index]) {
            std::lock_guard<std::mutex> lock(s_split_mutex);
            s_split_textures.erase(std::remove(s_split_textures.begin(), s_split_textures.end(), texture),
                                   s_split_textures.end());
            s_narrowed.erase(std::remove_if(s_narrowed.begin(), s_narrowed.end(),
                                            [&](auto& n) { return n.texture == texture; }),
                             s_narrowed.end());
        }
        ((delete_texture_t)entry_delete_texture.m_pOriginal)(manager, index);
    }

    // Made again at `width`: its GPU resources released (vtable +0xC8), and its description, which the renderer keeps
    // fixed once made (bit 27 of +0x98), set again (+0xA0 makes the resources).
    void Remake(char* texture, int width) {
        using release_t = void (*)(char*);
        using make_t = bool (*)(char*, int width, int height, int mips, int format, int depth, int samples, void*);
        void** vtable = *(void***)texture;
        ((release_t)vtable[0xC8 / 8])(texture);
        unsigned& flags = *(unsigned*)(texture + 0x98);
        unsigned fixed = flags & 0x08000000u;
        flags &= ~0x08000000u;
        ((make_t)vtable[0xA0 / 8])(texture, width, *(short*)(texture + 0x22), *(unsigned char*)(texture + 0x2A),
                                   *(short*)(texture + 0x26), *(short*)(texture + 0x24), *(short*)(texture + 0x28), nullptr);
        flags |= fixed;
    }

    void QuarterViews(bool on) {
        std::lock_guard<std::mutex> lock(s_split_mutex);
        if (!on) {
            for (auto& narrowed : s_narrowed) Remake(narrowed.texture, narrowed.width);
            s_narrowed.clear();
            return;
        }
        int narrowed = 0;
        for (char* texture : s_split_textures) {
            short width = *(short*)(texture + 0x20);
            auto it = std::find_if(s_narrowed.begin(), s_narrowed.end(), [&](auto& n) { return n.texture == texture; });
            if (it != s_narrowed.end() && width == it->width / 2) continue;
            if (width < 2) continue;
            if (it == s_narrowed.end()) it = s_narrowed.insert(s_narrowed.end(), {texture, width});
            else it->width = width; // the renderer made it again (a new resolution)
            Remake(texture, width / 2);
            ++narrowed;
        }
        if (narrowed > 0)
            LOG_INFO("Halo 2 Anniversary 3-4 players: {} render targets made cell-sized", narrowed);
    }

    Halo2Entry(entry_frame, OFFSET_HALO2_PF_SABER_RENDER_FRAME, void, frame) {
        __int64 module = entry_frame.m_target - entry_frame.m_offset;
        unsigned flags = *(unsigned*)(*(char**)(module + OFFSET_HALO2_PV_SABER_SCENE) + 0x140);
        s_frame_quad = (flags & 1) && (flags & kQuadList);
        s_frame_pair = (flags & kSecondPairList) ? 1 : 0;
        s_frame_players = Players(module);
        memset(s_drawn, 0, sizeof s_drawn);
        if (!s_frame_quad) s_second_pair_adapting[0] = s_second_pair_adapting[1] = false;
        if (s_frame_quad || s_views_narrowed) QuarterViews(s_frame_quad);
        s_views_narrowed = s_frame_quad;
        ((frame_t)entry_frame.m_pOriginal)();
        if (s_target != nullptr) {
            s_target->Release();
            s_target = nullptr;
        }
        s_target_ready = false;
    }

    // ---- each view's exposure. H2A adapts a view's exposure to its luminance of the frame before (read back a
    // frame late, into one of two textures in turn): with the pairs alternating, that was the other pair's picture,
    // and player 3's view followed where player 1 looked - washed out while player 1 faced something dark, pulsing as
    // both moved. The renderer keeps this for four views; players 3 and 4's frames get their own adaptation and the
    // read-back textures of views 2 and 3, which two views never use, for their HDR pass.

    Halo2Entry(entry_hdr_pass, OFFSET_HALO2_PF_SABER_HDR_PASS, void, hdr_pass, __int64 a1, __int64 a2, __int64 a3,
               __int64 a4, __int64 a5, __int64 a6, __int64 hdr_view, __int64 a8) {
        auto original = (hdr_pass_t)entry_hdr_pass.m_pOriginal;
        int view = (int)hdr_view;
        if (!s_frame_quad || s_frame_pair != 1 || view < 0 || view > 1)
            return original(a1, a2, a3, a4, a5, a6, hdr_view, a8);
        __int64 module = entry_hdr_pass.m_target - entry_hdr_pass.m_offset;
        char* adaptation = (char*)(module + OFFSET_HALO2_PV_SABER_HDR_VIEWS) + view * kHdrView + kHdrAdaptation;
        void** read_backs = (void**)(module + OFFSET_HALO2_PV_SABER_HDR_READ_BACKS) + view * 2;
        void** spare = read_backs + 4; // view + 2's
        char* mine = s_second_pair_adaptation[view];
        if (!s_second_pair_adapting[view]) { // starting from the first pair's, its frame count started again
            memcpy(mine, adaptation, kHdrAdaptationSize);
            *(int*)(mine + 0xC) = 0;
            s_second_pair_adapting[view] = true;
        }
        auto exchange = [&] {
            std::swap_ranges(adaptation, adaptation + kHdrAdaptationSize, mine);
            std::swap_ranges(read_backs, read_backs + 2, spare);
        };
        exchange();
        original(a1, a2, a3, a4, a5, a6, hdr_view, a8);
        exchange();
    }

    // Copies a cell from the frame's target to ours (keep) or back.
    void CopyCell(ID3D11DeviceContext* context, const Cell& cell, bool keep) {
        D3D11_BOX box{(UINT)cell.left, (UINT)cell.top, 0, (UINT)cell.right, (UINT)cell.bottom, 1};
        if (keep) context->CopySubresourceRegion(s_kept, 0, cell.left, cell.top, 0, s_target, 0, &box);
        else context->CopySubresourceRegion(s_target, 0, cell.left, cell.top, 0, s_kept, 0, &box);
    }

    // The view's image drawn into its cell by shaders of ours (CellShaders.h). The game's own composite quad leaves no
    // pixels with more than two players, retargeted or not (its stock 3-4 player Anniversary is black too), though its
    // target and image are bound; it is kept to bind them and made to cover nothing.
    struct CellDrawer {
        ID3D11Device* device = nullptr; // not held: the objects below hold it
        ID3D11VertexShader* vs = nullptr;
        ID3D11PixelShader* ps = nullptr;
        ID3D11SamplerState* sampler = nullptr;
        ID3D11RasterizerState* raster = nullptr;

        void Release() {
            for (IUnknown* held : std::initializer_list<IUnknown*>{vs, ps, sampler, raster})
                if (held != nullptr) held->Release();
            *this = {};
        }

        bool Ready(ID3D11Device* current) {
            if (device == current && raster != nullptr) return true;
            Release();
            D3D11_SAMPLER_DESC sampling{};
            sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampling.MaxLOD = D3D11_FLOAT32_MAX;
            D3D11_RASTERIZER_DESC rasterizing{};
            rasterizing.FillMode = D3D11_FILL_SOLID;
            rasterizing.CullMode = D3D11_CULL_NONE;
            rasterizing.DepthClipEnable = TRUE;
            if (FAILED(current->CreateVertexShader(kCellVS, sizeof kCellVS, nullptr, &vs)) ||
                FAILED(current->CreatePixelShader(kCellPS, sizeof kCellPS, nullptr, &ps)) ||
                FAILED(current->CreateSamplerState(&sampling, &sampler)) ||
                FAILED(current->CreateRasterizerState(&rasterizing, &raster))) {
                Release();
                return false;
            }
            device = current;
            return true;
        }
    } s_drawer;

    // The frame's target, at its first view: our kept copy made to its description, and our shaders to its device.
    bool PrepareTarget(ID3D11DeviceContext* context, ID3D11RenderTargetView* screen) {
        screen->GetResource(&s_target);
        ID3D11Texture2D* target = nullptr;
        if (s_target == nullptr || FAILED(s_target->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&target)))
            return false;
        D3D11_TEXTURE2D_DESC desc;
        target->GetDesc(&desc);
        target->Release();
        if (s_kept != nullptr) {
            D3D11_TEXTURE2D_DESC kept;
            s_kept->GetDesc(&kept);
            if (kept.Width != desc.Width || kept.Height != desc.Height || kept.Format != desc.Format) {
                s_kept->Release();
                s_kept = nullptr;
            }
        }
        ID3D11Device* device = nullptr;
        context->GetDevice(&device);
        if (device == nullptr) return false;
        if (s_kept == nullptr) {
            desc.MipLevels = desc.ArraySize = 1;
            desc.SampleDesc = {1, 0};
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = desc.CPUAccessFlags = desc.MiscFlags = 0;
            device->CreateTexture2D(&desc, nullptr, &s_kept);
        }
        bool ready = s_kept != nullptr && s_drawer.Ready(device);
        device->Release();
        return ready;
    }

    // Draws `image` into `cell` of the bound target, leaving the pipeline as it was.
    void DrawCell(ID3D11DeviceContext* context, ID3D11ShaderResourceView* image, const Cell& cell) {
        D3D11_PRIMITIVE_TOPOLOGY topology;
        context->IAGetPrimitiveTopology(&topology);
        ID3D11InputLayout* layout = nullptr;
        context->IAGetInputLayout(&layout);
        ID3D11VertexShader* vs = nullptr;
        context->VSGetShader(&vs, nullptr, nullptr);
        ID3D11HullShader* hs = nullptr;
        context->HSGetShader(&hs, nullptr, nullptr);
        ID3D11DomainShader* ds = nullptr;
        context->DSGetShader(&ds, nullptr, nullptr);
        ID3D11GeometryShader* gs = nullptr;
        context->GSGetShader(&gs, nullptr, nullptr);
        ID3D11PixelShader* ps = nullptr;
        context->PSGetShader(&ps, nullptr, nullptr);
        ID3D11SamplerState* sampler = nullptr;
        context->PSGetSamplers(0, 1, &sampler);
        ID3D11RasterizerState* raster = nullptr;
        context->RSGetState(&raster);
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        context->RSGetViewports(&viewport_count, viewports);
        ID3D11BlendState* blend = nullptr;
        float blend_factor[4];
        UINT sample_mask = 0;
        context->OMGetBlendState(&blend, blend_factor, &sample_mask);
        ID3D11DepthStencilState* depth = nullptr;
        UINT stencil_ref = 0;
        context->OMGetDepthStencilState(&depth, &stencil_ref);

        D3D11_VIEWPORT viewport{(float)cell.left, (float)cell.top, (float)(cell.right - cell.left),
                                (float)(cell.bottom - cell.top), 0.0f, 1.0f};
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetInputLayout(nullptr);
        context->VSSetShader(s_drawer.vs, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(s_drawer.ps, nullptr, 0);
        context->PSSetShaderResources(0, 1, &image);
        context->PSSetSamplers(0, 1, &s_drawer.sampler);
        context->RSSetState(s_drawer.raster);
        context->RSSetViewports(1, &viewport);
        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        context->OMSetDepthStencilState(nullptr, 0);
        context->Draw(3, 0);

        context->IASetPrimitiveTopology(topology);
        context->IASetInputLayout(layout);
        context->VSSetShader(vs, nullptr, 0);
        context->HSSetShader(hs, nullptr, 0);
        context->DSSetShader(ds, nullptr, 0);
        context->GSSetShader(gs, nullptr, 0);
        context->PSSetShader(ps, nullptr, 0);
        context->PSSetSamplers(0, 1, &sampler);
        context->RSSetState(raster);
        context->RSSetViewports(viewport_count, viewports);
        context->OMSetBlendState(blend, blend_factor, sample_mask);
        context->OMSetDepthStencilState(depth, stencil_ref);
        for (IUnknown* held : std::initializer_list<IUnknown*>{layout, vs, hs, ds, gs, ps, sampler, raster, blend, depth})
            if (held != nullptr) held->Release();
    }

    extern ::Entry entry_composite;
    using composite_t = void (*)(void* texture, bool split, int index);

    // The game's composite with its quad collapsed: the view's image, which it leaves bound, for our shaders to draw.
    ID3D11ShaderResourceView* CollapsedImage(ID3D11DeviceContext* context, void* texture, bool split, int index) {
        s_collapse = true;
        ((composite_t)entry_composite.m_pOriginal)(texture, split, index);
        s_collapse = false;
        ID3D11ShaderResourceView* image = nullptr;
        context->PSGetShaderResources(0, 1, &image);
        return image;
    }

    // Quad mode's frames only. With two players the game's composite runs as it is, under the D3D11 composite clip
    // (megabitt01/AlphaRing#23, render/d3d11/Hook.cpp), which fixed its black screen; that clip matches a quad by its
    // image being one player's share of the screen tall, which quad mode's half-height images aren't with 3-4.
    Halo2Entry(entry_composite, OFFSET_HALO2_PF_SABER_COMPOSITE, void, composite, void* texture, bool split, int index) {
        auto original = (composite_t)entry_composite.m_pOriginal;
        if (!s_frame_quad || !split || index < 0 || index > 1) return original(texture, split, index);
        if (index == 1 && NoSecondView()) return;
        __int64 module = entry_composite.m_target - entry_composite.m_offset;
        int player = index + 2 * s_frame_pair;
        Cell cell = PlayerCell(module, player, s_frame_players, true);
        auto context = Context(module);
        ID3D11ShaderResourceView* image = CollapsedImage(context, texture, split, index);
        ID3D11RenderTargetView* screen = nullptr;
        context->OMGetRenderTargets(1, &screen, nullptr);
        if (s_target == nullptr && screen != nullptr) s_target_ready = PrepareTarget(context, screen);
        if (s_target_ready && image != nullptr) {
            if (Spawned(module, player)) DrawCell(context, image, cell);
            CopyCell(context, cell, true);
            s_drawn[player] = true;
        }
        if (image != nullptr) image->Release();
        if (screen != nullptr) screen->Release();
    }

    // The game's quad with all four corners on one point.
    void CompositeQuad(char* locals) {
        if (!s_collapse) return;
        for (int i = 0; i < 4; ++i) {
            float* vertex = (float*)(locals + 0xC0 + i * 0x18);
            vertex[0] = vertex[1] = 0.0f;
        }
    }
    extern ::Entry entry_composite_quad;
    ::Entry entry_composite_quad(Halo2EntrySet(), OFFSET_HALO2_SABER_COMPOSITE_DRAW,
                                 MidFunctionThunk(&CompositeQuad, &entry_composite_quad.m_pOriginal, nullptr, 0),
                                 entry_feature);

    Halo2Entry(entry_after_views, OFFSET_HALO2_PF_SABER_AFTER_CAMERAS, void, after_views) {
        if (s_frame_quad && s_target_ready) {
            __int64 module = entry_after_views.m_target - entry_after_views.m_offset;
            for (int player = 0; player < s_frame_players; ++player)
                if (!s_drawn[player]) CopyCell(Context(module), PlayerCell(module, player, s_frame_players, true), false);
        }
        ((after_views_t)entry_after_views.m_pOriginal)();
    }

    // ---- the module unloading (MCC reloads it for the next game): nothing of ours may point into it

    void Reset() {
        for (char*& camera : s_cameras) {
            _aligned_free(camera);
            camera = nullptr;
        }
        if (s_kept != nullptr) {
            s_kept->Release();
            s_kept = nullptr;
        }
        s_drawer.Release();
        {
            std::lock_guard<std::mutex> lock(s_split_mutex);
            s_split_textures.clear();
            s_narrowed.clear();
        }
        s_views_narrowed = false;
        for (auto& spawned : s_spawned) spawned = false;
        s_pair = s_sync_pair = 0;
        s_frame_quad = false;
        s_second_pair_adapting[0] = s_second_pair_adapting[1] = false;
    }
    const bool s_reset = (Halo2EntrySet()->on_remove(&Reset), true);
}

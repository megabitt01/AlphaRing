#include <unordered_map>
#include "CModule.h"
#include "mcc/module/patch/PatchConfig.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"
#include "global/Global.h"
#include "common.h"

CModule::CModule(EntrySet *entrySet, std::initializer_list<CPatch> patches)
: m_entries(entrySet), m_patches(patches) {};

void CModule::load_module(const module_info_t *p_info) {
    if (p_info == nullptr) return;

    m_info = *p_info;

    if (m_info.hModule == 0 || m_info.errorCode != 0) return;

    m_patches.update(m_info.hModule);

    // Restore any Dev Tools patch states saved from a previous session before
    // applying - this must happen before apply() so restored "on" states
    // actually get written to the freshly-loaded module.
    if (m_info.title >= MCC::Module::MODULE_HALO1 && m_info.title <= MCC::Module::MODULE_MCC) {
        const char* module_name = MCC::Module::cModuleName[m_info.title];
        bool saved;

        for (auto patch : m_patches.embed_patches())
            if (AlphaRing::PatchConfig::Get(module_name, patch->name(), saved))
                patch->setState(saved);

        for (auto patch : m_patches.patches())
            if (AlphaRing::PatchConfig::Get(module_name, patch->name(), saved))
                patch->setState(saved);
    }

    m_patches.apply();

    // Splitscreen Config Editor values are restored here rather than from the
    // Dev Tools ImGui draw path, where they used to live. Restoring from the
    // draw path meant the saved config only ever got applied if the user opened
    // the F1 menu - so the editor's own "restored next time haloreach.dll loads"
    // claim was false, and any test of a custom slot shape silently became a
    // test of "config applied part-way through a match" instead.
    //
    // This call alone is not sufficient: the game re-initialises the table on
    // every level load, so the values written here are erased before a match is
    // drawn. HaloReach::Entry::Render re-asserts them per frame; this is the
    // "already correct before the first frame" half.
    if (m_info.title == MCC::Module::MODULE_HALOREACH) {
        AlphaRing::SplitscreenConfigStore::Load();

        // User debug setting only. The Left/Right layout's own painter bypass is
        // derived per frame in blackbars.cpp, so it never overwrites this.
        float debug_bars_value;
        if (AlphaRing::SplitscreenConfigStore::Get(-1, "bars_painter_off_debug", debug_bars_value))
            AlphaRing::Global::Global()->disable_splitscreen_bars_debug = debug_bars_value != 0.0f;

        float hud_target_value;
        if (AlphaRing::SplitscreenConfigStore::Get(-1, "hud_target", hud_target_value))
            AlphaRing::Global::Global()->splitscreen_hud_target = (int)hud_target_value;

        // Restore saved per-slot FOV overrides so a launch comes up already
        // configured rather than needing the F1 menu each time.
        {
            auto* g = AlphaRing::Global::Global();
            float v;
            auto get = [&](const char* k, float& out) {
                if (AlphaRing::SplitscreenConfigStore::Get(-1, k, v)) { out = v; return true; }
                return false;
            };
            float tmp;

            // Alpha Ring per-slot split-screen FOV override (see Global.h).
            // Absence of a saved "_on" key leaves splitscreen_fov_set false,
            // i.e. no override - matches an untouched install's behavior.
            for (int slot = 0; slot < 4; ++slot) {
                std::string prefix = "fov_slot" + std::to_string(slot);
                if (AlphaRing::SplitscreenConfigStore::Get(-1, (prefix + "_on").c_str(), tmp))
                    g->splitscreen_fov_set[slot] = tmp != 0.0f;
                get((prefix + "_deg").c_str(), g->splitscreen_fov_deg[slot]);
            }
        }

        AlphaRing::SplitscreenConfigStore::Apply(m_info.hModule);
    }

    if (m_entries)
        m_entries->update(m_info.hModule);
}

void CModule::unload_module() {
    if (m_entries)
        m_entries->remove();
    m_patches.update(0);
    memset(&m_info, 0, sizeof(module_info_t));
}

#include "mcc/module/entry/halo1/halo1.h"
#include "offset_halo2.h"
#include "mcc/module/entry/halo3/halo3.h"
#include "mcc/module/entry/halo3odst/halo3odst.h"
#include "mcc/module/entry/haloreach/haloreach.h"
#include "mcc/module/entry/halo4/halo4.h"
#include "mcc/module/entry/groundhog/groundhog.h"

static struct {
    CModule halo1;
    CModule halo2;
    CModule halo3;
    CModule halo4;
    CModule groundhog;
    CModule halo3odst;
    CModule haloreach;
} modules {
    {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALO1_PF_4PLAYERS, "\xEB\x18", true},
        {"splitscreen_patch2", "", OFFSET_HALO1_PF_PAUSE, "\xEB", true},
        {"splitscreen_patch3", "", OFFSET_HALO1_PF_IDK, "\x90\x90\x90\x90\x90\x90", true}, // fix [issue](https://github.com/WinterSquire/AlphaRing/issues/19)
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALO2_PF_PLAYER_VALID, "\x31\xC0\xB0\x01\xC3\x90", true},
        {"splitscreen_patch2", "", OFFSET_HALO2_PF_PLAYER_COUNT1, "\x04", true},
        {"splitscreen_patch3", "", OFFSET_HALO2_PF_PLAYER_COUNT2, "\x04", true},
        {"splitscreen_patch4", "force making splitscreen works with more than 2 players", 0x5153E, "\x83\xF8\x01\x74\x04", true},
}}, {Halo3EntrySet(), {
        {"splitscreen_patch1", "", OFFSET_HALO3_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"Remove Black Bar1", "remove black bar", 0x8AE150/*0x8AD160*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar2", "remove black bar", 0x8AE164/*0x8AD174*/, "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar3", "remove black bar", 0x8AE1A0/*0x8AD1B0*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALO4_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"splitscreen_patch2", "", OFFSET_HALO4_PF_COOP_REJOIN, "\xEB", true},
        {"splitscreen_patch3", "", OFFSET_HALO4_PF_COOP_PLAYER_LIMIT, "\x90\x90\x90\x90\x90\x90", true},
        {"Remove Black Bar1", "remove black bar", 0xE84E50/*0xE84E40*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar2", "remove black bar", 0xE84E64/*0xE84E54*/, "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar3", "remove black bar", 0xE84EA0/*0xE84E90*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_GROUNDHOG_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"splitscreen_patch2", "", OFFSET_GROUNDHOG_PF_REJOIN, "\xEB", true},
        {"Remove Black Bar1", "remove black bar", OFFSET_GROUNDHOG_BLACKBAR_1 , "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar2", "remove black bar", OFFSET_GROUNDHOG_BLACKBAR_2 , "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar3", "remove black bar", OFFSET_GROUNDHOG_BLACKBAR_3 , "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALO3ODST_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"Remove Black Bar1", "remove black bar", 0x8F1FB0/*0x8F1FC0*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar2", "remove black bar", 0x8F1FC4/*0x8F1FD4*/, "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar3", "remove black bar", 0x8F2000/*0x8F2010*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
}}, {HaloReachEntrySet(), {
        {"splitscreen_patch1", "", OFFSET_HALOREACH_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"splitscreen_patch2", "", OFFSET_HALOREACH_PF_COOP_REJOIN, "\xEB", true},
        {"Remove Black Bar1", "remove black bar", 0xB43CE0/*0xB43D10*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar2", "remove black bar", 0xB43CF4/*0xB43D24*/, "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01\x00\x00\x00", false},
        {"Remove Black Bar3", "remove black bar", 0xB43D30/*0xB43D60*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01\x00\x00\x00", false},
        // The black-bar/divider painter at 0x2C6D84 is now handled by a proper
        // per-slot-aware detour (HaloReach::Entry::BlackBars, blackbars.cpp)
        // instead of being NOP'd out wholesale - a raw byte-patch here would
        // stomp the detour's installed hook on the same function.

        // Reach's split-screen render-quality throttle (FUN_180270258, one call
        // per frame from UpdateAllPlayerViews) selects a 0x50-byte detail record
        // by GetSplitscreenPlayerCount() alone - index count-1 - and MCC's
        // quality tier is then applied on top as a per-category multiply. At 2+
        // local players that record holds ZEROES for water, decorators, decals,
        // dynamic lights and shadows, and no multiplier can rescue a zero: that
        // is why maxing MCC's settings does not restore split-screen quality
        // (docs/REVERSE_ENGINEERING.md, "split-screen render-quality throttle").
        //
        // Replacing the selector's own CALL with `mov eax, 1` makes it pick
        // Reach's native 1-player record at every player count - reusing a
        // native tier rather than forcing individual settings to maximum. Exact
        // 5-byte fit; downstream the value is used only through R11D (the
        // block-length guard) and ECX/R8 (the count-1 index), and EAX is
        // reloaded from XMM2 before its next use. The ~50 other callers of
        // GetSplitscreenPlayerCount are untouched, and at 1 player it is a
        // byte-for-byte no-op because count-1 is already 0.
        //
        // The MCC tier pass still runs on top and stays authoritative: measured
        // 2026-09-18 on Enhanced, the forced record's 1.0 detail fields arrive as
        // 3.0, cpu/gpu light counts as 2->10 and 6->10, shadow_count 4->12. This
        // toggle decides WHICH detail record Reach starts from; the MCC preset
        // still decides how that record is scaled.
        //
        // Runtime validated 2026-09-18: 2P/3P/4P all produce the same live block
        // as forced 1P. Default off - it restores passes Bungie deliberately
        // removed, so it costs frame time at 3-4 players.
        //
        // The name is the persisted config key (alpha_ring_patches.cfg). Do not
        // rename it without accepting that every user's saved toggle resets.
        {"Splitscreen Render Quality (force 1P tier)",
         "disable Reach's split-screen quality reduction - every local player count uses the "
         "single-player detail record instead of a reduced one. Not a maximum-graphics override: "
         "your MCC Performance/Original/Enhanced preset still applies on top. Costs frame time at 3-4 players.",
         OFFSET_HALOREACH_PF_RENDER_THROTTLE_COUNT_CALL, "\xB8\x01\x00\x00\x00", false},
}}};

static std::unordered_map<std::string, CModule*> map_modules {
    {"halo1.dll", &modules.halo1 + 0},
    {"halo2.dll", &modules.halo1 + 1},
    {"halo3.dll", &modules.halo1 + 2},
    {"halo4.dll", &modules.halo1 + 3},
    {"groundhog.dll", &modules.halo1 + 4},
    {"halo3odst.dll", &modules.halo1 + 5},
    {"haloreach.dll", &modules.halo1 + 6},
};

namespace MCC::Module {
    CModule *GetSubModule(int module) {
        return &modules.halo1 + module;
    }

    CModule *GetSubModule(const char *module_name) {
        auto it = map_modules.find(module_name);
        if (it == map_modules.end())
            return nullptr;
        return it->second;
    }
}

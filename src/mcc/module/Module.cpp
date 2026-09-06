#include "Module.h"

#include <functional>
#include <cstring>

#include "tinyxml2.h"

#include "common.h"
#include "offset_mcc.h"
#include "mcc/CGameManager.h"

namespace MCC::Module {
    DefDetourFunction(void, __fastcall, module_load, module_info_t* info, int a2, __int64 a3) {
        ppOriginal_module_load(info, a2, a3);
        GetSubModule(info->title)->load_module(info);
    }

    DefDetourFunction(__int64, __fastcall, module_unload, module_info_t* info) {
        GetSubModule(info->title)->unload_module();
        return ppOriginal_module_unload(info);
    }

    bool Initialize() {
        bool result;

        result = AlphaRing::Hook::Detour({
            {OFFSET_MCC_PF_MODULELOAD, OFFSET_MCC_WS_PF_MODULELOAD,  module_load, (void **)&ppOriginal_module_load},
            {OFFSET_MCC_PF_MODULEUNLOAD, OFFSET_MCC_WS_PF_MODULEUNLOAD, module_unload, (void **)&ppOriginal_module_unload},
        });

        assertm(result, "MCC:Module: failed to create hook");

        result = AlphaRing::Hook::Patch("KERNEL32.DLL", {
            {"IsDebuggerPresent", "\x31\xC0\xC3\x90\x90\x90\x90", 7}
        });

        assertm(result, "MCC:Module: failed to patch module \"kernel32.dll\"");

        // reload patch at startup
        ReloadPatch("../../../alpha_ring/patch.xml");

        return true;
    }
}

bool MCC::Module::ReloadPatch(const char *xml_path) {
    FILE* file;
    wchar_t wbuffer[MAX_PATH];

    auto p_converter = [](const std::string& hex) {
        std::vector<__int8> bytes;

        for (unsigned int i = 0; i < hex.length(); i += 3) {
            std::string byteString = hex.substr(i, 2);
            auto byte = static_cast<__int8>(std::stoi(byteString, nullptr, 16));
            bytes.push_back(byte);
        }

        return bytes;
    };

    AlphaRing::Filesystem::GetDir(xml_path, wbuffer);

    if (_wfopen_s(&file, wbuffer, L"rb") != 0) return false;

    tinyxml2::XMLDocument doc;

    doc.LoadFile(file);

    auto root = doc.FirstChildElement();

    if (root == nullptr) return false;

    for (int i = MODULE_HALO1; i < MODULE_MCC; ++i) {
        GetSubModule(i)->patches()->clear();
    }

    for (auto element = root->FirstChildElement(); element != nullptr; element = element->NextSiblingElement()) {
        auto p_module_name = element->Attribute("name");
        auto p_module_version = element->Attribute("version");

        if (p_module_name == nullptr || p_module_version == nullptr) return false;

        auto p_module = GetSubModule(p_module_name);

        if (p_module == nullptr) continue;

        if (strcmp(p_module_version, GAME_VERSION) != 0) continue;

        for (auto patch = element->FirstChildElement(); patch != nullptr; patch = patch->NextSiblingElement()) {
            auto p_patch_name = patch->Attribute("name");
            auto p_patch_desc = patch->Attribute("desc");
            auto p_patch_offset = patch->Attribute("offset");
            auto p_patch_data = patch->Attribute("aob");
            auto p_patch_enable = patch->Attribute("default");

            if (p_patch_name == nullptr || p_patch_offset == nullptr || p_patch_data == nullptr) continue;

            if (p_patch_desc == nullptr) p_patch_desc = "";

            auto patch_enable = false;

            if (p_patch_enable != nullptr && strcmp(p_patch_enable, "true") == 0)
                patch_enable = true;

            try {
                p_module->patches()->add(p_patch_name, p_patch_desc,
                          std::strtoull(p_patch_offset, nullptr, 16),
                          p_converter(p_patch_data),patch_enable);
            } catch (std::exception& e) {
                LOG_ERROR("Patch %s: %s", p_patch_name, e.what());
                continue;
            }
        }
    }

    return true;
}

#include "imgui.h"
#include "global/Global.h"

namespace MCC::Module {
    void ContextDevTools();
    void ContextEngine();

    void ImGuiContext() {
        static bool show_devtools;
        static bool show_engine;

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Game")) {
                ImGui::MenuItem("Engine", nullptr, &show_engine);
                ImGui::EndMenu();
            }
            ImGui::MenuItem("Dev Tools", nullptr, &show_devtools);
            ImGui::EndMainMenuBar();
        }

        if (show_devtools) {
            if (ImGui::Begin("Dev Tools", &show_devtools, ImGuiWindowFlags_MenuBar))
                ContextDevTools();
            ImGui::End();
        }

        if (show_engine) {
            if (ImGui::Begin("Engine", &show_engine, 0))
                ContextEngine();
            ImGui::End();
        }
    }

    void ContextEngine() {
        auto p_engine = GameEngine();

        if (p_engine == nullptr) {
            ImGui::Text("Engine not created");
            return;
        }

        if (ImGui::Button("Load Checkpoint")) p_engine->load_checkpoint();
        if (ImGui::Button("New Round")) p_engine->new_round();
        if (ImGui::Button("Pause Game")) p_engine->pause(true);
        if (ImGui::Button("Resume Game")) p_engine->pause(false);
        if (ImGui::Button("Restart Game")) p_engine->restart();
        if (ImGui::Button("Exit Game")) p_engine->exit();
        if (ImGui::Button("Reload Setting")) p_engine->reload_setting();
        if (ImGui::Button("Load Setting")) p_engine->load_setting();

#pragma region HaloScript
        static char halo_script[1024] = {0};

        if (ImGui::InputTextMultiline("HaloScript", halo_script + 4, sizeof(halo_script) - 4))
            halo_script[1023] = '\0';

        if (ImGui::Button("Execute HaloScript")) {
            halo_script[0] = 'H';
            halo_script[1] = 'S';
            halo_script[2] = ':';
            halo_script[3] = ' ';
            halo_script[1023] = '\0';
            p_engine->execute_command(halo_script);
        }
#pragma endregion

        ImGui::Separator();

#pragma region ChangeTeam
        static int player = 0;
        static int team = 0;
        ImGui::Combo("Player", &player, "Player 0\0Player 1\0Player 2\0Player 3");
        ImGui::Combo("Team", &team, "Red\0Blue\0Green\0Orange\0Purple\0Gold\0Brown\0Pink");
        if (ImGui::Button("Change Team")) {
            auto xuid = CGameManager::get_xuid(player);
            if (xuid != 0) p_engine->change_team(xuid, team);
        }
#pragma endregion
    }

    void ContextDevTools() {
        static int counter;
        auto p_print = [](CPatch* patch) {
            bool enabled = patch->enabled();

            ImGui::PushID(counter++);
            if (ImGui::Checkbox(patch->name(), &enabled)) patch->setState(enabled);
            ImGui::PopID();

            if (ImGui::IsItemHovered() && patch->have_desc())
                ImGui::SetTooltip("%s", patch->desc());
        };

        auto find_patch = [](CPatchSet* p_patches, const char* name) -> CPatch* {
            for (auto patch : p_patches->embed_patches())
                if (strcmp(patch->name(), name) == 0) return patch;
            return nullptr;
        };

        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem("Reload Patch")) ReloadPatch();

            ImGui::EndMenuBar();
        }

        if (!ImGui::BeginTabBar("patch")) return;

        counter = 0;

        for (int i = MODULE_HALO1; i < MODULE_MCC; ++i) {
            auto p_patches = GetSubModule((eModule)i)->patches();

            if (ImGui::BeginTabItem(cModuleName[i])) {
                ImGui::Text("Embed Patches");
                for (auto patch : p_patches->embed_patches()) {
                    // HaloReach's black-bar patches get replaced below with two
                    // controls scoped by which player/slot they actually affect,
                    // instead of three raw, easy-to-misread checkboxes.
                    if (i == MODULE_HALOREACH &&
                        (strcmp(patch->name(), "Remove Black Bar1") == 0 ||
                         strcmp(patch->name(), "Remove Black Bar2") == 0 ||
                         strcmp(patch->name(), "Remove Black Bar3") == 0))
                        continue;

                    p_print(patch);
                }

                if (i == MODULE_HALOREACH) {
                    auto p_bar1 = find_patch(p_patches, "Remove Black Bar1");
                    auto p_bar2 = find_patch(p_patches, "Remove Black Bar2");
                    auto p_bar3 = find_patch(p_patches, "Remove Black Bar3");

                    ImGui::Separator();
                    ImGui::Text("Splitscreen Display");

                    if (p_bar1 != nullptr && p_bar3 != nullptr) {
                        bool top = p_bar1->enabled() && p_bar3->enabled();
                        ImGui::PushID(counter++);
                        if (ImGui::Checkbox("Remove Black Bars - Player 1 (Top, 2 or 3 Player)", &top)) {
                            p_bar1->setState(top);
                            p_bar3->setState(top);

                            // The game's black-bar overlay is a single shared
                            // painter that only ever reads Player 1's bounds,
                            // so Player 2's bar physically cannot disappear
                            // while Player 1 still has one - turning Player 1
                            // back on makes that combination broken again.
                            if (!top && p_bar2 != nullptr && p_bar2->enabled())
                                p_bar2->setState(false);
                        }
                        ImGui::PopID();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Removes the black bar for whichever player occupies the top half of the screen. Applies to both 2-player and 3-player splitscreen - that slot is the same shape in either mode.");
                    }

                    if (p_bar2 != nullptr) {
                        bool player1_on = p_bar1 != nullptr && p_bar1->enabled();
                        bool bottom = p_bar2->enabled();

                        ImGui::BeginDisabled(!player1_on);
                        ImGui::PushID(counter++);
                        if (ImGui::Checkbox("Remove Black Bars - Player 2 (Bottom, 2 Player Only)", &bottom))
                            p_bar2->setState(bottom);
                        ImGui::PopID();
                        ImGui::EndDisabled();

                        // ImGui suppresses IsItemHovered() by default for items inside
                        // BeginDisabled()/EndDisabled() - AllowWhenDisabled is required
                        // so the explanation tooltip still shows while greyed out.
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            if (player1_on)
                                ImGui::SetTooltip("Removes the black bar for player 2's bottom half of the screen. 2-player only - in 3-player mode, players 2 and 3 already fill their quarter of the screen with no black bars.");
                            else
                                ImGui::SetTooltip("Requires Player 1's black bar removed too - the game's bar-painting logic is shared and only reads Player 1's bounds, so Player 2's bar can't disappear on its own.");
                        }
                    }

                    ImGui::Separator();
                }

                ImGui::Text("Patches");
                for (auto patch : p_patches->patches())
                    p_print(patch);

                if (i == MODULE_HALOREACH) {
                    auto hModule = GetSubModule((eModule)i)->info().hModule;

                    constexpr __int64 base_offset = 0xB43C40;
                    constexpr int entry_size = 20;
                    constexpr int block_count = 5;
                    constexpr int slot_count = 4;

                    if (ImGui::CollapsingHeader("Splitscreen Config Editor")) {
                        if (hModule == 0) {
                            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "haloreach.dll not loaded");
                        } else {
                            static const char* block_labels[block_count] = {
                                "0: alias of 4p", "1: 1 player", "2: 2 players",
                                "3: 3 players", "4: 4 players"
                            };

                            auto write_bytes = [](void* dst, const void* src, size_t size) {
                                DWORD oldProtect;
                                if (VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                                    memcpy(dst, src, size);
                                    VirtualProtect(dst, size, oldProtect, &oldProtect);
                                }
                            };

                            ImGui::TextDisabled("Edits write directly to live game memory. No rebuild needed;");
                            ImGui::TextDisabled("changes are lost on game restart unless also set as a patch.");

                            if (ImGui::BeginTable("splitscreen_config", 6,
                                                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                   ImGuiTableFlags_SizingFixedFit)) {
                                ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                                ImGui::TableSetupColumn("x0", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                                ImGui::TableSetupColumn("y0", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                                ImGui::TableSetupColumn("x1", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                                ImGui::TableSetupColumn("y1", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                                ImGui::TableSetupColumn("resolution", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                                ImGui::TableHeadersRow();

                                for (int block = 0; block < block_count; ++block) {
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", block_labels[block]);

                                    for (int slot = 0; slot < slot_count; ++slot) {
                                        int index = block * slot_count + slot;
                                        auto p_entry = (unsigned char*)(hModule + base_offset + (__int64)index * entry_size);

                                        float vals[4];
                                        __int32 res;
                                        memcpy(vals, p_entry, 16);
                                        memcpy(&res, p_entry + 16, 4);

                                        ImGui::TableNextRow();
                                        ImGui::PushID(index);

                                        ImGui::TableSetColumnIndex(0);
                                        ImGui::Text("  slot %d", slot);

                                        const char* field_labels[4] = {"##x0", "##y0", "##x1", "##y1"};
                                        for (int f = 0; f < 4; ++f) {
                                            ImGui::TableSetColumnIndex(1 + f);
                                            ImGui::SetNextItemWidth(-1);
                                            if (ImGui::InputFloat(field_labels[f], &vals[f], 0.0f, 0.0f, "%.4f"))
                                                write_bytes(p_entry + f * 4, &vals[f], 4);
                                        }

                                        ImGui::TableSetColumnIndex(5);
                                        ImGui::SetNextItemWidth(-1);
                                        if (ImGui::InputInt("##res", &res, 0, 0))
                                            write_bytes(p_entry + 16, &res, 4);

                                        ImGui::PopID();
                                    }
                                }
                                ImGui::EndTable();
                            }
                        }
                    }

                    if (ImGui::Button("Dump Splitscreen Config Table to Log")) {
                        if (hModule == 0) {
                            LOG_ERROR("Dump Splitscreen Config Table: haloreach.dll not loaded");
                        } else {
                            LOG_INFO("=== c_splitscreen_config::m_config_table @ base+0x{:X} ===", base_offset);

                            for (int block = 0; block < block_count; ++block) {
                                for (int slot = 0; slot < slot_count; ++slot) {
                                    int index = block * slot_count + slot;
                                    auto p_entry = (unsigned char*)(hModule + base_offset + (__int64)index * entry_size);

                                    float f0, f1, f2, f3;
                                    __int32 res;
                                    memcpy(&f0, p_entry + 0, 4);
                                    memcpy(&f1, p_entry + 4, 4);
                                    memcpy(&f2, p_entry + 8, 4);
                                    memcpy(&f3, p_entry + 12, 4);
                                    memcpy(&res, p_entry + 16, 4);

                                    LOG_INFO("block {} slot {} (entry {}, offset 0x{:X}): raw=({:.4f}, {:.4f}, {:.4f}, {:.4f}) res={}",
                                              block, slot, index, base_offset + (__int64)index * entry_size, f0, f1, f2, f3, res);
                                }
                            }

                            LOG_INFO("=== end dump ===");
                        }
                    }

                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Reads the live, unpatched-if-not-toggled bytes directly from haloreach.dll and logs them. Run this BEFORE toggling any Remove Black Bar checkbox to capture true defaults.");
                }

                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }
}

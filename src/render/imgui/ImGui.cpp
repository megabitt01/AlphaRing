#include "ImGui.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "input/Input.h"
#include "global/Global.h"
#include "filesystem/Filesystem.h"

#include "../D3d11/D3d11.h"
#include "./game/mcc/CMCCContext.h"
#include "./game/halo3/CHalo3Context.h"
#include "./game/xbox/CXboxContext.h"
#include "./game/xbox/assets/segoeui_ttf.h"

#include "mcc/mcc.h"
#include "mcc/CGameGlobal.h"
#include "mcc/settings/MenuSettings.h"
#include "mcc/spawn/Spawn.h"
#include "mcc/splitscreen/Splitscreen.h"

static ICContext* pages[7] {
        nullptr,
        nullptr,
        g_pHalo3Context,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
};

namespace AlphaRing::Render::ImGui {
    static ImFont* s_menu_font;

    ImFont* MenuFont() { return s_menu_font; }

    // The advanced menu was asked for while the Xbox menu was still closing.
    static bool s_advancedPending = false;
    static bool s_advancedPendingMouse = false;

    void ToggleXboxMenu() {
        if (!g_pXboxContext) return;
        if (g_pXboxContext->isOpen()) {
            g_pXboxContext->close();
            return;
        }
        s_advancedPending = false;
        AlphaRing::Global::Global()->show_imgui = false;
        g_pXboxContext->open();
    }

    void ToggleAdvancedMenu(bool showMouse) {
        if (g_pXboxContext && g_pXboxContext->isOpen()) {
            g_pXboxContext->close();
            s_advancedPending = !s_advancedPending;
            s_advancedPendingMouse = showMouse;
            return;
        }
        auto global = AlphaRing::Global::Global();
        global->show_imgui = !global->show_imgui;
        if (showMouse)
            global->show_imgui_mouse = global->show_imgui;
    }

    bool Initialize() {
        ::ImGui::CreateContext();
        ImGui_ImplWin32_Init(Graphics()->hwnd);
        ImGui_ImplDX11_Init(Graphics()->pDevice, Graphics()->pContext);
        ::ImGui::StyleColorsDark();

        ImGuiIO &io = ::ImGui::GetIO();

        // config
        io.MouseDrawCursor = true;
        io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;

        // ini
        io.IniFilename = "./alpha_ring/imgui.ini";
        ::ImGui::LoadIniSettingsFromDisk("../../../alpha_ring/imgui.ini");

        const float scale = GetDpiForWindow(Graphics()->hwnd) * 1.0f / 96.0f;

        auto font_path = R"(C:\Windows\Fonts\msyh.ttc)";

        io.Fonts->Clear();
        if (AlphaRing::Filesystem::Exist(font_path)) {
            io.Fonts->AddFontFromFileTTF(font_path, 16.0f * scale, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        } else {
            ImFontConfig config;
            config.SizePixels = 16.0f * scale;
            io.Fonts->AddFontDefault(&config);
        }

        // Xbox overlay font (Segoe UI at 25px, matching the original SDL2 prototype)
        // Bundled with the mod so it doesn't depend on the OS having Segoe UI installed.
        ImFont* xboxFont = nullptr;
        static const char* segoe_path = "../../../alpha_ring/segoeui.ttf";
        if (!AlphaRing::Filesystem::Exist(segoe_path)) {
            AlphaRing::Filesystem::Save(segoe_path, reinterpret_cast<const char*>(segoeui_ttf), segoeui_ttf_len);
        }
        if (AlphaRing::Filesystem::Exist(segoe_path)) {
            xboxFont = io.Fonts->AddFontFromFileTTF(segoe_path, 25.0f * scale);
        }
        if (!xboxFont) {
            ImFontConfig cfg;
            cfg.SizePixels = 25.0f * scale;
            xboxFont = io.Fonts->AddFontDefault(&cfg);
        }

        // The players' spawn menus are read from the couch, and drawn up to this size in a view.
        if (AlphaRing::Filesystem::Exist(segoe_path))
            s_menu_font = io.Fonts->AddFontFromFileTTF(segoe_path, 34.0f * scale);
        if (!s_menu_font)
            s_menu_font = xboxFont;

        ::ImGui::GetStyle().ScaleAllSizes(scale);

        // SDL audio subsystem for Xbox menu sounds
        SDL_Init(SDL_INIT_AUDIO);
        Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);

        g_pXboxContext = new CXboxContext(xboxFont);

        return true;
    }

    void Render() {
        // and once the game draws (a console opened late in startup)
        static bool s_console_checked = false;
        if (!s_console_checked) {
            s_console_checked = true;
            AlphaRing::Log::DropConsole("first frame");
        }

        MCC::Splitscreen::HotJoinPoll();
        AlphaRing::Input::Update();

        bool xboxOpen = g_pXboxContext && g_pXboxContext->isOpen();
        if (s_advancedPending && !xboxOpen) {
            s_advancedPending = false;
            AlphaRing::Global::Global()->show_imgui = true;
            if (s_advancedPendingMouse)
                AlphaRing::Global::Global()->show_imgui_mouse = true;
        }

        // The advanced menu just closed, by any of its toggles or its Close item.
        static bool s_advancedWasShown = false;
        bool advancedShown = AlphaRing::Global::Global()->show_imgui;
        if (s_advancedWasShown && !advancedShown)
            AlphaRing::MenuSettings::SaveAll();
        s_advancedWasShown = advancedShown;
        bool overlay = AlphaRing::Global::Global()->show_imgui;
        // the players' spawn menus (mcc/spawn) are also drawn with neither shown
        if (!overlay && !xboxOpen && !MCC::Spawn::AnyPlayerMenuOpen())
            return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ::ImGui::NewFrame();

        bool inGame = MCC::IsInGame();
        auto pGameGlobal = GameGlobal();

        if (!AlphaRing::Global::Global()->show_imgui_mouse || (!overlay && !xboxOpen))
            ::ImGui::SetMouseCursor(ImGuiMouseCursor_None);

        if (xboxOpen)
            g_pXboxContext->render();

        if (AlphaRing::Global::Global()->show_imgui) {
            g_pMCCContext->render();

            if (inGame && pGameGlobal != nullptr) {
                // Bounds check: pages array has 7 elements (indices 0-6)
                if (pGameGlobal->current_game > 0 && pGameGlobal->current_game < 7)
                {
                    auto context = pages[pGameGlobal->current_game];
                    if (context != nullptr)
                        context->render();
                }
            }

            if (::ImGui::BeginMainMenuBar()) {
                if (inGame)
                    ::ImGui::Separator();
                ::ImGui::Text("%.1f fps", ::ImGui::GetIO().Framerate);
                ::ImGui::EndMainMenuBar();
            }
        }

        MCC::Spawn::RenderPlayerMenus();

        ::ImGui::Render();
        Graphics()->SetRenderTargetView();
        ImGui_ImplDX11_RenderDrawData(::ImGui::GetDrawData());
    }
}
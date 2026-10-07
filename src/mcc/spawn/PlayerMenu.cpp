// Per-player menus: a player presses the menu button (D-pad Down by default) and drives a
// small menu in their own split-screen view with their controller while their Spartan stands
// still - a page per spawn category, and a page for their own HUD (mcc/hud). Input arrives on
// the game's input thread (HandlePlayerInput), the menus act and draw on the render thread
// (RenderPlayerMenus).

#define NOMINMAX // std::min/std::max
#include "Spawn.h"

#include "global/Global.h"
#include "input/MenuConfig.h"
#include "mcc/CGameGlobal.h"
#include "mcc/hud/Hud.h"
#include "mcc/splitscreen/LeftRight.h"
#include "render/imgui/ImGui.h"
#include "render/imgui/game/xbox/CXboxContext.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>

namespace MCC::Spawn {
    namespace {
        // the spawn categories, then the player's HUD
        constexpr int kHudPage = kCategoryCount, kPages = kCategoryCount + 1;
        constexpr const char* kPageTitles[kPages] = {"VEHICLES", "WEAPONS", "EQUIPMENT", "CHARACTERS", "MY HUD"};
        constexpr const char* kTeamNames[kTeamCount] = {"Their own side", "Ally", "Enemy"};

        // The HUD page, where the running game's HUD can be customized (mcc/hud). It edits the player's
        // settings as the overlay's HUD window does: in memory, until MCC closes.
        enum HudRow { HudArea, HudSize, HudColor, HudReset, kHudRows };
        constexpr const char* kHudRowNames[kHudRows] = {"AREA", "SIZE", "COLOUR", "RESET"};

        int HudRows() { return MCC::Hud::Supported() ? kHudRows : 0; }
        // LB / RB go through the spawn categories, and the HUD page where it has rows.
        int Pages(int hud_rows) { return hud_rows ? kPages : kCategoryCount; }

        constexpr int kHues = 12; // colour choices: off, then every 30 degrees

        // `value` moved `step` places around 0..count-1.
        int Cycle(int value, int step, int count) { return ((value + step) % count + count) % count; }

        // The colour a recolour toward `hue` shows as (the HUD window's preview).
        ImU32 HueColor(float hue) { return ImColor::HSV(hue / 360.0f, 0.75f, 1.0f); }

        // D-pad left/right (`turn`) on `row` of `player`'s HUD page: the settings are edited as a copy and published
        // whole, as the HUD window does.
        void TurnHud(int player, int row, int turn) {
            auto hud = MCC::Hud::Settings(player);
            switch (row) {
                case HudArea:
                    hud.area = Cycle(hud.area, turn, MCC::Hud::kAreaCount);
                    break;
                case HudSize:
                    hud.scale = std::clamp(std::round(hud.scale * 10.0f + turn) / 10.0f, 0.5f, 2.0f);
                    break;
                case HudColor: { // off, then a hue
                    int hue = hud.recolor ? (int)std::lround(hud.hue / 30.0f) % kHues : -1;
                    hue = Cycle(hue + 1, turn, kHues + 1) - 1;
                    hud.recolor = hue >= 0;
                    if (hue >= 0) hud.hue = hue * 30.0f;
                    break;
                }
                default:
                    return;
            }
            MCC::Hud::SetSettings(player, hud);
        }

        // The triggers, as two of the button bits XInput leaves unused.
        constexpr WORD kLeftTrigger = 0x0400, kRightTrigger = 0x0800;
        WORD Buttons(const XINPUT_GAMEPAD& pad) {
            return pad.wButtons | (pad.bLeftTrigger > 128 ? kLeftTrigger : 0) | (pad.bRightTrigger > 128 ? kRightTrigger : 0);
        }

        // Up/down on the D-pad or the left stick: -1 up, 1 down.
        int Vertical(const XINPUT_GAMEPAD& pad) {
            return (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) || pad.sThumbLY > 20000 ? -1
                 : (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || pad.sThumbLY < -20000 ? 1 : 0;
        }

        // A direction held on the pad: one step at once, then repeated steps while held.
        struct Repeat {
            int dir = 0;
            ULONGLONG at = 0;

            int Step(int held, ULONGLONG now) {
                int step = 0;
                if (held != dir) { step = held; at = now + 350; }
                else if (held && now >= at) { step = held; at = now + 70; }
                dir = held;
                return step;
            }
        };

        struct Menu {
            bool open = false;
            bool refresh = false;       // ask for a fresh catalog on the next frame
            int category = Vehicles;    // a spawn category or kHudPage
            int selected[kPages] = {};
            int team = TeamEnemy;
            int weapon = -1;            // characters' weapon: index in the Weapons list, -1 their usual one
            XINPUT_GAMEPAD pad = {};    // latest pad (input thread)
            WORD seen = 0;              // buttons at the previous poll (input thread)
            WORD handled = 0;           // Buttons() already acted on (render thread)
            Repeat rows, weapon_turns;  // up/down through the list, LT/RT through the weapons
            int first_row = 0;          // scroll position
            bool held = false;          // closed, but the pad is kept until its buttons are released
        };

        std::mutex s_mutex;
        Menu s_menus[kMaxPlayers];

        // Screen area of `player`'s view (x, y, width, height), as the games' split-screen tables
        // lay them out: stacked halves for two players; for three, player 1 on the top half and
        // players 2 and 3 on the bottom quarters; quarters for four. The Left/Right split puts
        // player 1 on the left half and the others on the right half (stacked for three).
        ImVec4 ViewRect(int player, int count, ImVec2 display, bool left_right) {
            float w = display.x * 0.5f, h = display.y * 0.5f;
            if (count <= 1) return {0, 0, display.x, display.y};
            if (left_right && count <= 3) {
                if (player == 0 || count == 2) return {w * player, 0, w, display.y};
                return {w, h * (player - 1), w, h};
            }
            if (count == 2 || (count == 3 && player == 0)) return {0, h * player, display.x, h};
            if (count == 3) return {w * (player - 1), h, w, h};
            return {w * (player % 2), h * (player / 2), w, h};
        }

        constexpr ImU32 kPlayerColors[kMaxPlayers] = {
            IM_COL32(64, 170, 255, 255), IM_COL32(255, 92, 84, 255), IM_COL32(96, 214, 112, 255), IM_COL32(255, 196, 64, 255)};
        constexpr ImU32 kTeamColors[kTeamCount] = {IM_COL32(190, 200, 210, 255), IM_COL32(96, 214, 112, 255), IM_COL32(255, 92, 84, 255)};

        ImU32 WithAlpha(ImU32 color, int alpha) { return (color & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT); }

        // Draws text and returns its width.
        float Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text) {
            dl->AddText(font, size, pos, color, text);
            return font->CalcTextSizeA(size, FLT_MAX, 0, text).x;
        }

        float ButtonWidth(ImFont* font, float size, const char* button) {
            return std::max(size * 1.1f, font->CalcTextSizeA(size * 0.8f, FLT_MAX, 0, button).x + size * 0.6f);
        }

        // A controller button glyph ("A" in a green disc, "LB" in a pill) followed by a label;
        // returns the total width.
        float ButtonHint(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* button, const char* label) {
            static const struct { const char* name; ImU32 color; } kButtons[] = {
                {"A", IM_COL32(60, 176, 67, 255)}, {"B", IM_COL32(224, 60, 49, 255)},
                {"X", IM_COL32(31, 117, 254, 255)}, {"Y", IM_COL32(240, 200, 8, 255)}};
            ImU32 color = IM_COL32(90, 98, 110, 255);
            for (auto& b : kButtons) if (strcmp(b.name, button) == 0) color = b.color;

            ImVec2 text = font->CalcTextSizeA(size * 0.8f, FLT_MAX, 0, button);
            float h = size * 1.1f, w = ButtonWidth(font, size, button);
            dl->AddRectFilled(pos, {pos.x + w, pos.y + h}, color, h * 0.5f);
            dl->AddText(font, size * 0.8f, {pos.x + (w - text.x) * 0.5f, pos.y + (h - text.y) * 0.5f}, IM_COL32_WHITE, button);
            float x = pos.x + w + size * 0.3f;
            if (label) x += Text(dl, font, size, {x, pos.y + (h - size) * 0.5f}, IM_COL32(215, 222, 230, 255), label);
            return x - pos.x;
        }

        // `text` cut to `width` with an ellipsis.
        std::string Fit(ImFont* font, float size, const std::string& text, float width) {
            if (font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x <= width) return text;
            const char* end = nullptr;
            float room = width - font->CalcTextSizeA(size, FLT_MAX, 0, "...").x;
            font->CalcTextSizeA(size, room, 0, text.c_str(), nullptr, &end);
            return std::string(text.c_str(), end) + "...";
        }

        struct Snapshot { int category, selected, team, weapon, first_row; };

        void Draw(int player, const Snapshot& m, int game, int hud_rows, ImVec4 view) {
            ImFont* font = AlphaRing::Render::ImGui::MenuFont();
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImU32 accent = kPlayerColors[player];

            // sized for a quarter of a 1080p screen, scaled with the view
            float scale = std::clamp(view.z / 960.0f * 0.5f + view.w / 540.0f * 0.5f, 0.6f, 1.6f); // view: x, y, width, height
            float text = 22.0f * scale, minor = 17.0f * scale, pad = 14.0f * scale;
            float w = std::min(view.z * 0.46f, 470.0f * scale), h = view.w * 0.86f;
            ImVec2 p0 = {view.x + view.z * 0.03f, view.y + view.w * 0.07f}, p1 = {p0.x + w, p0.y + h};

            dl->AddRectFilled(p0, p1, IM_COL32(8, 12, 18, 222), 10.0f * scale);
            dl->AddRect(p0, p1, WithAlpha(accent, 170), 10.0f * scale, 0, 2.0f * scale);
            dl->AddRectFilled(p0, {p1.x, p0.y + 5.0f * scale}, accent, 10.0f * scale, ImDrawFlags_RoundCornersTop);

            float x = p0.x + pad, y = p0.y + pad;
            bool hud_page = m.category == kHudPage;
            char title[48];
            snprintf(title, sizeof(title), "PLAYER %d  %s", player + 1, hud_page ? "MENU" : "SPAWN");
            Text(dl, font, minor, {x, y}, accent, title);
            y += minor + pad * 0.6f;

            // category: LB  < NAME >  RB, with a dot per page
            float lb = ButtonHint(dl, font, minor, {x, y + (text - minor) * 0.3f}, "LB", nullptr);
            float rb = ButtonWidth(font, minor, "RB");
            ButtonHint(dl, font, minor, {p1.x - pad - rb, y + (text - minor) * 0.3f}, "RB", nullptr);
            const char* name = kPageTitles[m.category];
            float name_w = font->CalcTextSizeA(text, FLT_MAX, 0, name).x;
            Text(dl, font, text, {x + lb + (w - 2 * pad - lb - rb - name_w) * 0.5f, y}, IM_COL32_WHITE, name);
            y += text + pad * 0.3f;
            int pages = Pages(hud_rows);
            float dots_x = p0.x + w * 0.5f - (pages - 1) * 6.0f * scale;
            for (int c = 0; c < pages; ++c)
                dl->AddCircleFilled({dots_x + c * 12.0f * scale, y}, 3.0f * scale,
                                    c == m.category ? accent : IM_COL32(90, 98, 110, 255));
            y += pad;

            if (m.category == Characters) {
                float sx = x + Text(dl, font, minor, {x, y}, IM_COL32(150, 160, 172, 255), "SIDE  ");
                sx += Text(dl, font, minor, {sx, y}, IM_COL32(150, 160, 172, 255), "< ");
                sx += Text(dl, font, minor, {sx, y}, kTeamColors[m.team], kTeamNames[m.team]);
                Text(dl, font, minor, {sx, y}, IM_COL32(150, 160, 172, 255), " >");
                y += minor + pad * 0.6f;

                float wx = x + Text(dl, font, minor, {x, y}, IM_COL32(150, 160, 172, 255), "WEAPON  ");
                wx += ButtonHint(dl, font, minor * 0.85f, {wx, y}, "LT", nullptr) + pad * 0.4f;
                float rt = ButtonWidth(font, minor * 0.85f, "RT");
                auto weapon = Fit(font, minor, Catalog::WeaponName(game, m.weapon), p1.x - pad - rt - pad * 0.4f - wx);
                wx += Text(dl, font, minor, {wx, y}, m.weapon < 0 ? IM_COL32(190, 200, 210, 255) : IM_COL32_WHITE, weapon.c_str());
                ButtonHint(dl, font, minor * 0.85f, {wx + pad * 0.4f, y}, "RT", nullptr);
                y += minor + pad * 0.6f;
            }

            // footer: status line and button hints
            float hints_y = p1.y - pad - minor * 1.1f;
            float status_y = hints_y - minor - pad * 0.5f;
            float hx = x;
            hx += ButtonHint(dl, font, minor, {hx, hints_y}, "A", hud_page ? "Reset" : "Spawn") + pad;
            hx += ButtonHint(dl, font, minor, {hx, hints_y}, "B", "Close") + pad;
            if (!hud_page) ButtonHint(dl, font, minor, {hx, hints_y}, "Y", "Refresh");
            auto status = hud_page ? std::string("D-pad left / right changes a setting, until MCC closes")
                                   : Catalog::Status(player);
            Text(dl, font, minor, {x, status_y}, IM_COL32(170, 180, 190, 255), Fit(font, minor, status, w - 2 * pad).c_str());

            float row = text * 1.45f, top = y, bottom = status_y - pad * 0.5f;
            if (hud_page) {
                auto hud = MCC::Hud::Settings(player);
                if (hud_rows > 0) row = std::min(row, (bottom - top) / hud_rows); // all of them fit a half-height view
                for (int r = 0; r < hud_rows; ++r) {
                    float ry = top + r * row, ty = ry + (row - text) * 0.4f;
                    bool chosen = r == m.selected;
                    if (chosen)
                        dl->AddRectFilled({p0.x + pad * 0.5f, ry}, {p1.x - pad * 0.5f, ry + row - 2.0f * scale},
                                          WithAlpha(accent, 70), 6.0f * scale);
                    Text(dl, font, minor, {x, ty + (text - minor) * 0.5f}, IM_COL32(150, 160, 172, 255), kHudRowNames[r]);
                    char value[48] = "";
                    ImU32 color = chosen ? IM_COL32_WHITE : IM_COL32(185, 194, 204, 255);
                    if (r == HudArea) snprintf(value, sizeof(value), "<  %s  >", MCC::Hud::kAreaNames[hud.area]);
                    else if (r == HudSize) snprintf(value, sizeof(value), "<  %d%%  >", (int)std::lround(hud.scale * 100.0f));
                    else if (r == HudColor && !hud.recolor) snprintf(value, sizeof(value), "<  Game colours  >");
                    else if (r == HudColor) {
                        snprintf(value, sizeof(value), "<  Hue %d  >", (int)std::lround(hud.hue));
                        color = HueColor(hud.hue);
                    } else if (chosen) snprintf(value, sizeof(value), "Press A");
                    Text(dl, font, text, {x + w * 0.3f, ty}, color, value);
                }
                return;
            }

            // the list
            int visible = std::max(1, (int)((bottom - top) / row));
            dl->PushClipRect({p0.x, top}, {p1.x, bottom}, true);
            Catalog::Read(game, (Category)m.category, [&](const std::vector<Item>* items) {
                if (items == nullptr || items->empty()) {
                    Text(dl, font, minor, {x, top + pad * 0.5f}, IM_COL32(150, 160, 172, 255),
                         items == nullptr ? "Loading..." : "Nothing of this kind is loaded here.");
                    return;
                }
                for (int i = m.first_row; i < (int)items->size() && i < m.first_row + visible; ++i) {
                    float ry = top + (i - m.first_row) * row;
                    bool chosen = i == m.selected;
                    if (chosen) {
                        dl->AddRectFilled({p0.x + pad * 0.5f, ry}, {p1.x - pad * 0.5f, ry + row - 2.0f * scale},
                                          WithAlpha(accent, 70), 6.0f * scale);
                        dl->AddRectFilled({p0.x + pad * 0.5f, ry}, {p0.x + pad * 0.5f + 4.0f * scale, ry + row - 2.0f * scale},
                                          accent, 2.0f * scale);
                    }
                    Text(dl, font, text, {x + (chosen ? 6.0f * scale : 0), ry + (row - text) * 0.4f},
                         chosen ? IM_COL32_WHITE : IM_COL32(185, 194, 204, 255),
                         Fit(font, text, (*items)[i].name, w - 3 * pad).c_str());
                }
                if ((int)items->size() > visible) { // scroll bar
                    float track = bottom - top, thumb = std::max(track * visible / items->size(), 12.0f * scale);
                    float at = top + (track - thumb) * m.first_row / std::max(1, (int)items->size() - visible);
                    dl->AddRectFilled({p1.x - pad * 0.45f, at}, {p1.x - pad * 0.2f, at + thumb}, WithAlpha(accent, 150), 3.0f * scale);
                }
            });
            dl->PopClipRect();

            // remember how many rows fit so scrolling keeps the selection visible
            std::lock_guard<std::mutex> lock(s_mutex);
            auto& menu = s_menus[player];
            if (menu.selected[m.category] < menu.first_row) menu.first_row = menu.selected[m.category];
            if (menu.selected[m.category] >= menu.first_row + visible) menu.first_row = menu.selected[m.category] - visible + 1;
        }
    }

    bool HandlePlayerInput(int player, const XINPUT_GAMEPAD& pad) {
        if (player < 0 || player >= kMaxPlayers) return false;

        std::lock_guard<std::mutex> lock(s_mutex);
        auto& m = s_menus[player];
        WORD pressed = pad.wButtons & ~m.seen;
        m.seen = pad.wButtons;

        if (!m.open) {
            // the buttons that closed the menu stay with it until released (B would melee)
            if (m.held && pad.wButtons) return true;
            m.held = false;

            WORD button = g_menuConfig.spawnMenuMask;
            if (button == 0 || (pad.wButtons & button) != button || !(pressed & button)) return false;
            // not while the debug overlay or the dashboard has the controllers
            if (AlphaRing::Global::Global()->show_imgui || (g_pXboxContext && g_pXboxContext->isOpen())) return false;
            // games without spawning (Reach) still get the HUD page
            bool spawning = Catalog::CurrentGame() >= 0;
            if (!spawning && HudRows() == 0) return false;
            m.open = true;
            if (!spawning) m.category = kHudPage;
            m.refresh = true;
            // the press that opened the menu isn't a menu action, nor the start of a scroll
            m.handled = Buttons(pad);
            m.rows = {Vertical(pad), ~0ull};
        }
        m.pad = pad;
        return true;
    }

    bool AnyPlayerMenuOpen() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return std::any_of(std::begin(s_menus), std::end(s_menus), [](const Menu& m) { return m.open; });
    }

    void RenderPlayerMenus() {
        int game = Catalog::CurrentGame();
        int count = LocalPlayerCount();
        int hud_rows = HudRows();
        ImVec2 display = ImGui::GetIO().DisplaySize;
        auto p_global = GameGlobal();
        int shown = p_global ? p_global->current_game : -1;
        bool left_right = MCC::Splitscreen::LeftRight::OnScreen(shown, count);

        for (int player = 0; player < kMaxPlayers; ++player) {
            Snapshot snapshot;
            bool refresh = false, spawn = false, hud_reset = false;
            int hud_turn = 0, hud_row = -1;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                auto& m = s_menus[player];
                if (!m.open) continue;
                if ((game < 0 && hud_rows == 0) || player >= count) { m.open = false; m.held = true; continue; }
                int pages = Pages(hud_rows);

                WORD buttons = Buttons(m.pad);
                WORD pressed = buttons & ~m.handled;
                m.handled = buttons;

                if (pressed & XINPUT_GAMEPAD_B) { m.open = false; m.held = true; continue; }
                if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER) m.category = Cycle(m.category, -1, pages);
                if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) m.category = Cycle(m.category, 1, pages);
                if (game < 0) m.category = kHudPage; // nothing to spawn in this game
                else if (m.category >= pages) m.category = Vehicles; // no HUD page here
                int turn = pressed & XINPUT_GAMEPAD_DPAD_LEFT ? -1 : pressed & XINPUT_GAMEPAD_DPAD_RIGHT ? 1 : 0;
                bool a = pressed & XINPUT_GAMEPAD_A;
                refresh = m.refresh || (pressed & XINPUT_GAMEPAD_Y);
                m.refresh = false;
                if (m.category == kHudPage) {
                    hud_row = m.selected[kHudPage];
                    hud_reset = a && hud_row == HudReset;
                    hud_turn = turn;
                } else {
                    spawn = a;
                }

                // up/down (D-pad or left stick) move once, then repeat while held
                auto now = GetTickCount64();
                int step = m.rows.Step(Vertical(m.pad), now);
                if (int size = !step ? 0 : m.category == kHudPage ? hud_rows : Catalog::Size(game, (Category)m.category))
                    m.selected[m.category] = Cycle(m.selected[m.category], step, size);

                // characters: left/right the side, LT/RT the weapon - "their usual weapon" (-1), then
                // the Weapons list
                if (m.category == Characters) {
                    m.team = Cycle(m.team, turn, kTeamCount);
                    int weapons = Catalog::Size(game, Weapons);
                    if (m.weapon >= weapons) m.weapon = -1;
                    int held = buttons & kLeftTrigger ? -1 : buttons & kRightTrigger ? 1 : 0;
                    if (int w = m.weapon_turns.Step(held, now)) m.weapon = Cycle(m.weapon + 1, w, weapons + 1) - 1;
                }
                snapshot = {m.category, m.selected[m.category], m.team, m.weapon, m.first_row};
            }

            // outside the lock: the HUD settings have a lock of their own
            if (hud_reset) MCC::Hud::SetSettings(player, MCC::Hud::PlayerHud());
            if (hud_turn) TurnHud(player, hud_row, hud_turn);
            Catalog::Refresh(game, refresh); // also picks up a newly loaded map
            Item item;
            if (spawn && Catalog::Get(game, (Category)snapshot.category, snapshot.selected, item))
                Catalog::Spawn((Category)snapshot.category, item, player, (Team)snapshot.team, snapshot.weapon);

            Draw(player, snapshot, game, hud_rows, ViewRect(player, count, display, left_right));
        }
    }
}

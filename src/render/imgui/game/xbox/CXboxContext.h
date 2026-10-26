#pragma once

#include "render/imgui/ICContext.h"
#include <SDL_mixer.h>
#include "CStateMachine.h"

#include <array>
#include <optional>

class CXboxContext : public ICContext {
public:
    CXboxContext(ImFont* font);
    ~CXboxContext();

    void open();
    void close();
    bool isOpen() const;

    void handleInput(InputCommand cmd);
    void render() override;

private:
    Menu m_menu;
    std::array<Mix_Chunk*, 6> m_sounds;
    std::optional<StateMachine> m_stateMachine;
    ImFont* m_font;
    Uint32 m_lastTick = 0;

    // Hands the menu's state back to the shared settings and saves them.
    void commitState();
};

extern CXboxContext* g_pXboxContext;

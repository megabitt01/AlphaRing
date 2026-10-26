#include "CXboxContext.h"

#include "assets/open_wav.h"
#include "assets/close_wav.h"
#include "assets/nav_wav.h"
#include "assets/nav_page_wav.h"
#include "assets/select_wav.h"
#include "assets/denied_wav.h"

#include "mcc/CGameManager.h"
#include "mcc/settings/MenuSettings.h"

#include <SDL.h>
#include <imgui.h>

CXboxContext* g_pXboxContext = nullptr;

CXboxContext::CXboxContext(ImFont* font)
    : m_sounds{
        Mix_LoadWAV_RW(SDL_RWFromConstMem(open_wav,     open_wav_len),     1),
        Mix_LoadWAV_RW(SDL_RWFromConstMem(close_wav,    close_wav_len),    1),
        Mix_LoadWAV_RW(SDL_RWFromConstMem(nav_wav,      nav_wav_len),      1),
        Mix_LoadWAV_RW(SDL_RWFromConstMem(nav_page_wav, nav_page_wav_len), 1),
        Mix_LoadWAV_RW(SDL_RWFromConstMem(select_wav,   select_wav_len),   1),
        Mix_LoadWAV_RW(SDL_RWFromConstMem(denied_wav,   denied_wav_len),   1),
    },
    m_font(font)
{
}

CXboxContext::~CXboxContext() {
    if (m_stateMachine.has_value())
        commitState();
    for (auto* chunk : m_sounds)
        Mix_FreeChunk(chunk);
}

void CXboxContext::open() {
    namespace Settings = AlphaRing::MenuSettings;
    {
        std::lock_guard lock(Settings::Mutex());
        std::vector<std::string> profileNames;
        for (const auto& profile : Settings::Profiles())
            profileNames.push_back(profile.name);
        m_menu.setControllerProfiles(profileNames);

        m_stateMachine.emplace(m_menu, m_sounds);
        m_stateMachine->getState().menuState = Settings::State();
    }
    Mix_PlayChannel(-1, m_sounds[0], 0);
    m_lastTick = SDL_GetTicks();
}

void CXboxContext::close() {
    if (m_stateMachine.has_value())
        m_stateMachine->handleInput(InputCommand::Back);
}

bool CXboxContext::isOpen() const {
    return m_stateMachine.has_value();
}

void CXboxContext::handleInput(InputCommand cmd) {
    if (m_stateMachine.has_value())
        m_stateMachine->handleInput(cmd);
}

void CXboxContext::render() {
    if (!m_stateMachine.has_value()) return;

    Uint32 now = SDL_GetTicks();
    float dt = (now - m_lastTick) / 1000.0f;
    m_lastTick = now;

    m_stateMachine->update(dt);

    auto* vp = ImGui::GetMainViewport();
    m_stateMachine->render((int)vp->Size.x, (int)vp->Size.y, m_font);

    if (!m_stateMachine->isRunning()) {
        // the teams before this menu, so only the ones picked here are sent
        int previousTeams[4];
        {
            std::lock_guard lock(AlphaRing::MenuSettings::Mutex());
            memcpy(previousTeams, AlphaRing::MenuSettings::State().teamIndex, sizeof(previousTeams));
        }
        commitState();

        CGameManager::apply_profiles();
        AlphaRing::MenuSettings::ApplyAll(false, previousTeams);
        if (auto* engine = GameEngine())
            engine->load_setting();

        m_stateMachine.reset();
    }
}

void CXboxContext::commitState() {
    namespace Settings = AlphaRing::MenuSettings;
    std::lock_guard lock(Settings::Mutex());
    Settings::State() = m_stateMachine->getState().menuState;
    Settings::SaveAll();
}

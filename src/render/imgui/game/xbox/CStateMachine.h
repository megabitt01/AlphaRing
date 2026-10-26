#pragma once

#include <SDL_mixer.h>
#include "CXboxMenu.hpp"
#include "CXboxMenuState.h"

#include <deque>
#include <array>
#include <string>

// -------------------------
// Input abstraction (replaces SDL_Event)
// -------------------------
enum class InputCommand {
    Left,
    Right,
    Up,
    Down,
    Select,
    Back
};

// -------------------------
// State definitions
// -------------------------
enum class Phase {
    Opening,
    PostOpening,
    FadeIn,
    InFadeIn,
    Idle,
    InIdle,
    ShiftIn,
    ShiftOut,
    ShiftRight,
    ShiftLeft,
    ShiftUp,
    ShiftDown,
    InShiftUp,
    InShiftDown,
    PreClosing,
    Closing
};

// -------------------------
// Core runtime state
// -------------------------
struct State {
    Menu menu;
    std::array<Mix_Chunk*, 6> sounds;
    int pageIndex;
    int optionIndex;
    std::deque<int> optionWindow;
    int subOptionIndex;
    std::deque<int> subOptionWindow;
    float time;
    float duration;
    Phase phase;
    MenuState menuState;
};

// -------------------------
// State machine (ImGui-driven)
// -------------------------
class StateMachine {
public:
    StateMachine(const Menu& menu, const std::array<Mix_Chunk*, 6>& sounds);

    void handleLeft();
    void handleRight();
    void handleUp();
    void handleDown();
    void handleOption();
    void handleSubOption();
    void handleClose();

    void handleInput(InputCommand cmd);
    void update(float dt);
    void render(int WINDOW_WIDTH, int WINDOW_HEIGHT, ImFont* font);

    bool isRunning() const;
    State& getState();

private:
    void finishShiftUp();
    void finishShiftDown();
    void finishOptionShiftUp();
    void finishOptionShiftDown();

    State currentState;
    bool running;
};
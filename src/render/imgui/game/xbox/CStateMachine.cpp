#pragma once

#include "CXboxRender.hpp"
#include "CStateMachine.h"

#include <SDL.h>
#include <SDL_mixer.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
namespace {
    // The main option list groups Increment/Decrement/PointerDisplay options
    // (e.g. Reduce/Display/Increase Sensitivity) into a single visual row, just
    // like the render loop's row counter does. This computes the row that a
    // given raw option index falls on so window-scroll math matches what's
    // actually drawn. Landing on the 2nd/3rd item of such a group must report
    // the same row as the 1st item, not a later one.
    bool isRowGroupType(OptionType t) {
        return t == OptionType::Increment || t == OptionType::Decrement || t == OptionType::PointerDisplay;
    }

    int rowIndexForOption(const Page& page, int optionIndex) {
        int row = 0;
        bool prevWasGroup = false;
        int limit = (std::min)(optionIndex, (int)page.options.size() - 1);
        for (int i = 0; i <= limit; ++i) {
            bool isGroup = isRowGroupType(page.options[i].type);
            bool continuesRow = isGroup && prevWasGroup;
            if (i > 0 && !continuesRow) row++;
            prevWasGroup = isGroup;
        }
        return row;
    }
}

// ------------------------------------------------------------
// Constructor
// ------------------------------------------------------------
StateMachine::StateMachine(const Menu& menuInstance, const std::array<Mix_Chunk*, 6>& sounds)
    : currentState{
        menuInstance,
        sounds,
        0,
        0,
        {0,1,2,3,4,5},
        0,
        {0,1,2,3,4,5},
        0,
        0.25f,
        Phase::Opening,
        {1, false, {0,0,0,0}, {0,0,0,0}, {3,3,3,3}, {false,false,false,false}, {0,1,0,1}, {{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, false}
    },
    running(true)
{
}

// ------------------------------------------------------------
// Input layer (REPLACES SDL_Event handler)
// ------------------------------------------------------------
void StateMachine::handleInput(InputCommand cmd)
{
    if (cmd == InputCommand::Up && currentState.phase == Phase::InShiftUp) {
        finishShiftUp();
        currentState.phase = Phase::InIdle;
        currentState.time = 0;
        handleUp();
        return;
    }

    if (cmd == InputCommand::Down && currentState.phase == Phase::InShiftDown) {
        finishShiftDown();
        currentState.phase = Phase::InIdle;
        currentState.time = 0;
        handleDown();
        return;
    }

    if (cmd == InputCommand::Up && currentState.phase == Phase::ShiftUp) {
        finishOptionShiftUp();
        currentState.phase = Phase::Idle;
        currentState.time = 0;
        handleUp();
        return;
    }

    if (cmd == InputCommand::Down && currentState.phase == Phase::ShiftDown) {
        finishOptionShiftDown();
        currentState.phase = Phase::Idle;
        currentState.time = 0;
        handleDown();
        return;
    }

    if (!(currentState.phase == Phase::Idle ||
          currentState.phase == Phase::InIdle ||
          currentState.phase == Phase::PostOpening ||
          currentState.phase == Phase::PreClosing)) {
        return;
    }

    switch (cmd) {
        case InputCommand::Left:  handleLeft(); break;
        case InputCommand::Right: handleRight(); break;
        case InputCommand::Up:    handleUp(); break;
        case InputCommand::Down:  handleDown(); break;
        case InputCommand::Select: handleOption(); break;
        case InputCommand::Back:  handleClose(); break;
    }
}

// ------------------------------------------------------------
// Navigation
// ------------------------------------------------------------
void StateMachine::handleLeft() {
    if (currentState.phase != Phase::Idle) return;

    auto& opt = currentState.menu.pages[currentState.pageIndex]
                    .options[currentState.optionIndex];

    if (opt.type == OptionType::Increment) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);
        currentState.optionIndex = (std::max)(0, currentState.optionIndex - 2);
    } else {
        Mix_PlayChannel(-1, currentState.sounds[3], 0);
        if (currentState.pageIndex > 0) {
            currentState.phase = Phase::ShiftLeft;
            currentState.time = 0;
        }
    }
}

void StateMachine::handleRight() {
    if (currentState.phase != Phase::Idle) return;

    auto& opt = currentState.menu.pages[currentState.pageIndex]
                    .options[currentState.optionIndex];

    if (opt.type == OptionType::Decrement) {
        Mix_PlayChannel(-1, currentState.sounds[3], 0);
        currentState.optionIndex = (std::min)(
            (int)currentState.menu.pages[currentState.pageIndex].options.size() - 1,
            currentState.optionIndex + 2
        );
    } else {
        if (currentState.pageIndex >= State().menu.MAX_PAGES - 1)
            return;

        Mix_PlayChannel(-1, currentState.sounds[3], 0);
        currentState.phase = Phase::ShiftRight;
        currentState.time = 0;
    }
}

void StateMachine::handleUp() {
    if (currentState.phase == Phase::Idle) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);
        auto& opt = currentState.menu.pages[currentState.pageIndex]
                .options[currentState.optionIndex];

        if (opt.type == OptionType::Increment) {
            currentState.optionIndex = (std::min)(
                (int)currentState.menu.pages[currentState.pageIndex].options.size() - 1,
                currentState.optionIndex - 3
            );
        } else {
            currentState.optionIndex = (std::max)(0, currentState.optionIndex - 1);
        }
        
        int row = rowIndexForOption(currentState.menu.pages[currentState.pageIndex], currentState.optionIndex);
        if (row < currentState.optionWindow[0]) {
            currentState.phase = Phase::ShiftUp;
            currentState.time = 0;
        }
    }

    if (currentState.phase == Phase::InIdle) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);
        currentState.subOptionIndex = (std::max)(0, currentState.subOptionIndex - 1);

        if (currentState.subOptionIndex < currentState.subOptionWindow[0]) {
            Mix_PlayChannel(-1, currentState.sounds[2], 0);
            currentState.phase = Phase::InShiftUp;
            currentState.time = 0;
        }
    }
}

void StateMachine::handleDown() {
    if (currentState.phase == Phase::Idle) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);

        int maxOption = (int)currentState.menu.pages[currentState.pageIndex].options.size();

        auto& opt = currentState.menu.pages[currentState.pageIndex]
                        .options[currentState.optionIndex];

        if (opt.type == OptionType::Decrement) {
            currentState.optionIndex = (std::min)(maxOption - 1, currentState.optionIndex + 3);
        } else {
            currentState.optionIndex = (std::min)(maxOption - 1, currentState.optionIndex + 1);
        }

        int row = rowIndexForOption(currentState.menu.pages[currentState.pageIndex], currentState.optionIndex);
        int maxWin = (int)currentState.optionWindow.size();
        if (row > currentState.optionWindow[maxWin - 1]) {
            currentState.phase = Phase::ShiftDown;
            currentState.time = 0;
        }
    }

    if (currentState.phase == Phase::InIdle) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);

        int maxSub = (int)currentState.menu.pages[currentState.pageIndex]
                        .options[currentState.optionIndex]
                        .subOptions.size();

        currentState.subOptionIndex = (std::min)(maxSub - 1, currentState.subOptionIndex + 1);

        int maxWin = (int)currentState.subOptionWindow.size();
        if (currentState.subOptionIndex > currentState.subOptionWindow[maxWin - 1]) {
            currentState.phase = Phase::InShiftDown;
            currentState.time = 0;
        }
    }
}

// ------------------------------------------------------------
// Selection logic (audio replaced)
// ------------------------------------------------------------
void StateMachine::handleSubOption() {
    auto& sub = currentState.menu.pages[currentState.pageIndex]
                    .options[currentState.optionIndex]
                    .subOptions[currentState.subOptionIndex];

    Mix_PlayChannel(-1, currentState.sounds[4], 0);

    switch (sub.subOptionType) {
        case 0:
            currentState.menuState.controllerIndex[currentState.pageIndex - 1] = currentState.subOptionIndex;
            break;
        case 1:
            currentState.menuState.playerColors[currentState.pageIndex - 1].colors[0] = currentState.subOptionIndex;
            break;
        case 2:
            currentState.menuState.playerColors[currentState.pageIndex - 1].colors[1] = currentState.subOptionIndex;
            break;
        case 3:
            currentState.menuState.playerColors[currentState.pageIndex - 1].colors[2] = currentState.subOptionIndex;
            break;
        case 4:
            currentState.menuState.controllerProfile[currentState.pageIndex - 1] = currentState.subOptionIndex;
            break;
        default:
            break;
    }
}

void StateMachine::handleOption() {
    auto& opt = currentState.menu.pages[currentState.pageIndex]
                    .options[currentState.optionIndex];

    Mix_PlayChannel(-1, currentState.sounds[4], 0);

    if (currentState.pageIndex == 0) {
        if (opt.type == OptionType::Increment && currentState.menuState.playerCount < 4)
            currentState.menuState.playerCount++;

        if (opt.type == OptionType::Decrement && currentState.menuState.playerCount > 1)
            currentState.menuState.playerCount--;

        if (opt.type == OptionType::Boolean) {
            bool& value = (opt.subOptionType == Menu::k_usePlayer0ProfileOption)
                ? currentState.menuState.usePlayer0Profile
                : currentState.menuState.useKM;
            value = !value;
        }
    } else {
        int player = currentState.pageIndex - 1;

        if (opt.type == OptionType::Increment && currentState.menuState.sensitivity[player] < 20)
            currentState.menuState.sensitivity[player]++;

        if (opt.type == OptionType::Decrement && currentState.menuState.sensitivity[player] > 1)
            currentState.menuState.sensitivity[player]--;

        if (opt.type == OptionType::Boolean)
            currentState.menuState.invert[player] = !currentState.menuState.invert[player];
    }

    if (opt.type == OptionType::TeamToggle) {
        auto& team = currentState.menuState.teamIndex[currentState.pageIndex - 1];
        team = !team;
    }

    if (currentState.phase == Phase::InIdle) {
        handleSubOption();
        currentState.phase = Phase::ShiftOut;
        currentState.time = 0;
    }

    if (opt.type == OptionType::Subpage && currentState.phase == Phase::Idle) {
        currentState.subOptionWindow = {0,1,2,3,4,5};
        currentState.phase = Phase::ShiftIn;
        currentState.time = 0;
    }
}

void StateMachine::handleClose() {
    Mix_PlayChannel(-1, currentState.sounds[1], 0);
    currentState.phase = Phase::Closing;
    currentState.time = 0;
}

void StateMachine::finishShiftUp() {
    currentState.subOptionWindow.pop_back();
    currentState.subOptionWindow.push_front(currentState.subOptionWindow.front() - 1);
}

void StateMachine::finishShiftDown() {
    currentState.subOptionWindow.pop_front();
    currentState.subOptionWindow.push_back(currentState.subOptionWindow.back() + 1);
}

void StateMachine::finishOptionShiftUp() {
    currentState.optionWindow.pop_back();
    currentState.optionWindow.push_front(currentState.optionWindow.front() - 1);
}

void StateMachine::finishOptionShiftDown() {
    currentState.optionWindow.pop_front();
    currentState.optionWindow.push_back(currentState.optionWindow.back() + 1);
}

// ------------------------------------------------------------
// UPDATE (unchanged logic)
// ------------------------------------------------------------
void StateMachine::update(float dt)
{
    if (currentState.time < currentState.duration)
        currentState.time += dt;

    if (currentState.time >= currentState.duration) {

        switch (currentState.phase) {

            case Phase::Opening:
                currentState.phase = Phase::Idle;
                currentState.time = 0;
                break;

            case Phase::Closing:
                running = false;
                break;

            case Phase::ShiftRight: {
                currentState.optionIndex = 0;
                currentState.phase = Phase::FadeIn;
                int maxPage = State().menu.MAX_PAGES;
                currentState.pageIndex = (std::min)(maxPage - 1, currentState.pageIndex + 1);
                break;
            }

            case Phase::ShiftLeft:
                currentState.optionIndex = 0;
                currentState.phase = Phase::FadeIn;
                currentState.pageIndex = (std::max)(0, currentState.pageIndex - 1);
                break;

            case Phase::InShiftUp:
                finishShiftUp();
                currentState.phase = Phase::InFadeIn;
                break;

            case Phase::InShiftDown:
                finishShiftDown();
                currentState.phase = Phase::InFadeIn;
                break;

            case Phase::ShiftUp:
                finishOptionShiftUp();
                currentState.phase = Phase::Idle;
                currentState.time = 0;
                break;

            case Phase::ShiftDown:
                finishOptionShiftDown();
                currentState.phase = Phase::Idle;
                currentState.time = 0;
                break;

            case Phase::FadeIn:
                currentState.optionIndex = 0;
                currentState.optionWindow = {0,1,2,3,4,5};
                currentState.phase = Phase::Idle;
                currentState.time = 0;
                break;

            case Phase::InFadeIn:
                currentState.phase = Phase::InIdle;
                currentState.time = 0;
                break;

            case Phase::ShiftIn:
                currentState.subOptionIndex = 0;
                currentState.phase = Phase::InFadeIn;
                break;

            case Phase::ShiftOut:
                currentState.phase = Phase::FadeIn;
                break;

            default:
                break;
        }
    }
}

// ------------------------------------------------------------
// RENDER (SDL REMOVED → ImGui backend expected elsewhere)
// ------------------------------------------------------------
void StateMachine::render(int WINDOW_WIDTH, int WINDOW_HEIGHT, ImFont* font)
{
    // No SDL. Rendering should be moved into:
    // - ImGui widgets OR
    // - custom ImDrawList rendering layer

    // Example placeholder hook:
    // renderMenuImGui(drawList, currentState);
    renderMenu(WINDOW_WIDTH, WINDOW_HEIGHT, currentState, font);
}

// ------------------------------------------------------------
// Accessors
// ------------------------------------------------------------
bool StateMachine::isRunning() const {
    return running;
}

State& StateMachine::getState() {
    return currentState;
}
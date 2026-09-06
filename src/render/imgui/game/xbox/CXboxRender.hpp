#pragma once

#include "CStateMachine.h"

#include "draw/CXboxRect.hpp"
#include "draw/CXboxButton.hpp"
#include "draw/CXboxPage.hpp"
#include "draw/CXboxIcon.hpp"

#include "assets/colors.h"
#include "assets/mccIcon.h"

#include <algorithm>
#include <optional>
#include <cmath>

void renderMenu(
    int WINDOW_WIDTH,
    int WINDOW_HEIGHT,
    State state,
    ImFont* font
) {
    float alpha = (std::min)(state.time / state.duration, 1.0f);

    float menuScale = 0.0f;
    int menuWidth = 0;
    int menuHeight = 0;
    int menuPosX = 0;
    int menuPosY = 0;
    // int unitSize = WINDOW_WIDTH * WINDOW_HEIGHT / 22217;
    // int unitSize = 75 + ((WINDOW_HEIGHT / 1080) * 35);
    int unitSize = 75 + ((WINDOW_HEIGHT / 720) * 45);
    float buttonOffset = 0.0f;
    float fontSize = 25.0f + ((WINDOW_HEIGHT / 1080.0f) * 3.5f);

    int iconSize = 45;
    int pageCount = State().menu.MAX_PAGES;

    ImU8 globalAlpha = 255;

    // -------------------------
    // MENU SCALE / ANIMATION
    // -------------------------
    
    if (state.phase == Phase::Opening) {
        menuScale = 0.45f * alpha;
        menuWidth = WINDOW_WIDTH * menuScale;
        menuHeight = (alpha > 0.5f)
            ? WINDOW_HEIGHT * menuScale
            : WINDOW_HEIGHT * menuScale * 0.5f;
    }
    else if (state.phase == Phase::Closing) {
        menuScale = 0.45f * (1.0f - alpha);
        menuWidth = WINDOW_WIDTH * menuScale;
        menuHeight = (alpha < 0.5f)
            ? WINDOW_HEIGHT * menuScale
            : WINDOW_HEIGHT * menuScale * 0.5f;
    }
    else {
        menuScale = 0.45f;
        menuWidth = WINDOW_WIDTH * menuScale;
        menuHeight = WINDOW_HEIGHT * menuScale;
    }

    menuPosX = (WINDOW_WIDTH - menuWidth) / 2;
    menuPosY = (WINDOW_HEIGHT - menuHeight) / 2;

    // -------------------------
    // GLOBAL ALPHA
    // -------------------------
    if (state.phase == Phase::FadeIn) {
        globalAlpha = static_cast<ImU8>(255 * alpha);
    }
    else if (
        state.phase == Phase::ShiftRight ||
        state.phase == Phase::ShiftLeft ||
        state.phase == Phase::ShiftIn ||
        state.phase == Phase::ShiftOut
    ) {
        globalAlpha = static_cast<ImU8>(255 * (1.0f - alpha));
    }

    // -------------------------
    // BUTTON OFFSET (SUB MENU)
    // -------------------------
    if (state.phase == Phase::InShiftUp) {
        buttonOffset = -((menuHeight / 7) * state.subOptionWindow[0]) + ((menuHeight / 7) * alpha);
    }
    else if (state.phase == Phase::InShiftDown) {
        buttonOffset = -((menuHeight / 7) * state.subOptionWindow[0]) - ((menuHeight / 7) * alpha);
    }
    else if (
        state.phase == Phase::InIdle ||
        state.phase == Phase::InFadeIn
    ) {
        buttonOffset = -((menuHeight / 7) * state.subOptionWindow[0]);
    }

    // -------------------------
    // OPTION OFFSET (MAIN MENU)
    // -------------------------
    float mainButtonOffset = 0.0f;
    if (state.phase == Phase::ShiftUp) {
        mainButtonOffset = -((menuHeight / 7) * state.optionWindow[0]) + ((menuHeight / 7) * alpha);
    }
    else if (state.phase == Phase::ShiftDown) {
        mainButtonOffset = -((menuHeight / 7) * state.optionWindow[0]) - ((menuHeight / 7) * alpha);
    }
    else if (
        state.phase == Phase::Idle ||
        state.phase == Phase::FadeIn ||
        state.phase == Phase::ShiftRight ||
        state.phase == Phase::ShiftLeft ||
        state.phase == Phase::ShiftIn
    ) {
        mainButtonOffset = -((menuHeight / 7) * state.optionWindow[0]);
    }

    // -------------------------
    // PAGE TRANSITION OFFSETS
    // -------------------------
    auto pageOffset = [&](bool rightSide) -> int {
        int updateSize = (menuWidth / 9);

        // if(state.phase == Phase::ShiftRight) {
        //     offset = -updateSize * (alpha) + 50;
        //     extra = 1;
        // }
        // testing unit size in place of +/- 50
        if (state.phase == Phase::ShiftRight)
            return rightSide ? (-updateSize * alpha - (menuWidth / 9)) : (-updateSize * alpha + (menuWidth / 9));

        if (state.phase == Phase::ShiftLeft)
            return rightSide ? (updateSize * alpha - (menuWidth / 9)) : (updateSize * alpha + (menuWidth / 9));

        if (state.phase == Phase::ShiftIn)
            return rightSide ? (-updateSize * alpha * (alpha * 4)) : (updateSize * alpha * (alpha * 4));

        if (state.phase == Phase::ShiftOut)
            return rightSide ? (updateSize * alpha - (menuWidth / 9)) : (-updateSize * alpha + (menuWidth / 9));
                
        if(state.phase == Phase::InShiftUp) {
            return rightSide ? 0 : ((((menuHeight / 7)*state.subOptionWindow[0]) * -1)) + ((menuHeight / 7) * alpha);
        }

        if(state.phase == Phase::InShiftDown) {
            return rightSide ? 0 : ((((menuHeight / 7)*state.subOptionWindow[0]) * -1)) - ((menuHeight / 7) * alpha);
        }

        if(state.phase == Phase::InIdle || state.phase == Phase::InFadeIn) 
            return rightSide ? 0 : ((menuHeight / 7)*state.subOptionWindow[0]) * -1;

        return 0;
    };

    // -------------------------
    // LEFT PAGES
    // -------------------------
    if (
        state.phase != Phase::Opening &&
        state.phase != Phase::Closing &&
        state.phase != Phase::InIdle &&
        state.phase != Phase::InFadeIn &&
        state.phase != Phase::InShiftUp &&
        state.phase != Phase::InShiftDown
    ) {
        int offset = pageOffset(false);
        int prefixSize = -(menuWidth / 9);

        int extra = (state.phase == Phase::ShiftRight || state.phase == Phase::ShiftLeft) ? 1 : 0;

        for (int i = state.pageIndex - 1 + extra; i >= 0; --i) {
            drawPage(
                offset + menuPosX + prefixSize,
                menuPosY,
                menuWidth,
                menuHeight,
                state.menu.pages[i].label.data(),
                false,
                255,
                font,
                fontSize
            );
            prefixSize -= (menuWidth / 9);
        }
    }

    // -------------------------
    // ICON + TITLE
    // -------------------------
    if(state.phase != Phase::Closing && state.phase != Phase::Opening) {   
        drawText(
            menuPosX - (menuWidth * 0.35f),
            menuPosY - ((menuHeight/5) * 3),
            menuWidth,
            menuHeight,
            font,
            25.0f + ((WINDOW_HEIGHT / 1080.0f) * 15.0f),
            IM_COL32(255, 255, 255, 255),
            "MCC Guide"
        );
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        drawIcon(
            draw,
            menuPosX + (menuWidth * 0.85f),
            menuPosY - ((menuHeight/15) * 3.5f),
            (unitSize*0.75f), 
            (unitSize*0.75f),
            mccIcon_png, 
            mccIcon_png_len,
            255
        );
    }
    

    // -------------------------
    // CURRENT PAGE (MAIN)
    // -------------------------
    const auto& page = state.menu.pages[state.pageIndex];
    if(
        state.phase != Phase::InIdle &&
        state.phase != Phase::InFadeIn &&
        state.phase != Phase::InShiftUp &&
        state.phase != Phase::InShiftDown &&
        state.phase != Phase::ShiftOut
    ) {
        drawPage(
            menuPosX,
            menuPosY,
            menuWidth,
            menuHeight,
            page.label.data(),
            true,
            globalAlpha,
            font,
            fontSize
        );
    } else {
        drawPage(
            menuPosX,
            menuPosY,
            menuWidth,
            menuHeight,
            page.options[state.optionIndex].label.data(),
            true,
            globalAlpha,
            font,
            fontSize
        ); 
    }

    // -------------------------
    // RIGHT PAGES
    // -------------------------
    if (state.phase != Phase::Opening &&
        state.phase != Phase::Closing &&
        state.phase != Phase::InIdle &&
        state.phase != Phase::InFadeIn &&
        state.phase != Phase::InShiftUp &&
        state.phase != Phase::InShiftDown &&
        state.pageIndex <= pageCount - 1) {

        int offset = pageOffset(true);
        int suffixSize = menuWidth + (menuWidth / 9);

        int extra = (state.phase == Phase::ShiftRight || state.phase == Phase::ShiftLeft) ? 1 : 0;

        for (int i = state.pageIndex + 1 - extra; i < pageCount; ++i) {
            drawPage(
                offset + menuPosX + suffixSize,
                menuPosY,
                menuWidth,
                menuHeight,
                state.menu.pages[i].label.data(),
                false,
                255,
                font,
                fontSize
            );
            suffixSize += (menuWidth / 9);
        }
    }

    // -------------------------
    // BACKGROUND
    // -------------------------
    drawGradientRect(
        menuPosX,
        menuPosY,
        menuWidth,
        menuHeight,
        IM_COL32(255, 255, 255, 255),
        IM_COL32(255, 255, 255, 255),
        true
    );

    // -------------------------
    // OPTIONS (MAIN PAGE)
    // -------------------------
    if (
        state.phase == Phase::Idle ||
        state.phase == Phase::FadeIn ||
        state.phase == Phase::ShiftRight ||
        state.phase == Phase::ShiftLeft ||
        state.phase == Phase::ShiftIn ||
        state.phase == Phase::ShiftUp ||
        state.phase == Phase::ShiftDown
    ) {
        ImDrawList* mainDraw = ImGui::GetForegroundDrawList();

        mainDraw->PushClipRect(
            ImVec2((float)menuPosX, (float)menuPosY),
            ImVec2((float)(menuPosX + menuWidth),
                (float)(menuPosY + menuHeight)),
            true
        );

        float buttonCount = 0.0f;
        int groupIndex = 0;

        for (int i = 0; i < page.options.size(); i++) {
            const auto& opt = page.options[i];
            OptionType type = opt.type;

            int yBase = menuPosY + buttonCount * (menuHeight / 7) + mainButtonOffset;

            if (type == OptionType::Increment ||
                type == OptionType::Decrement ||
                type == OptionType::PointerDisplay) {

                int displayValue = (state.pageIndex == 0)
                    ? state.menuState.playerCount
                    : state.menuState.sensitivity[state.pageIndex - 1];

                drawButton(
                    menuPosX + groupIndex * (menuWidth / 3),
                    yBase,
                    menuWidth,
                    menuHeight,
                    font,
                    fontSize,
                    state.optionIndex == i,
                    globalAlpha,
                    std::to_string(displayValue).c_str(),
                    type,
                    0
                );
                groupIndex = (groupIndex + 1) % 3;
                if (type == OptionType::Increment) {
                    buttonCount++;
                }
                continue;
            }

            if (type == OptionType::Boolean) {
                bool boolValue = (state.pageIndex == 0)
                    ? state.menuState.useKM
                    : state.menuState.invert[state.pageIndex - 1];

                drawButton(
                    menuPosX,
                    yBase,
                    menuWidth,
                    menuHeight,
                    font,
                    fontSize,
                    state.optionIndex == i,
                    globalAlpha,
                    opt.label.c_str(),
                    type,
                    boolValue ? 1 : 0
                );
                buttonCount++;
                continue;
            }

            if (type == OptionType::TeamToggle) {
                drawButton(
                    menuPosX,
                    yBase,
                    menuWidth,
                    menuHeight,
                    font,
                    fontSize,
                    state.optionIndex == i,
                    globalAlpha,
                    opt.label.c_str(),
                    type,
                    state.menuState.teamIndex[state.pageIndex - 1]
                );
                buttonCount++;
                continue;
            }

            if (type == OptionType::Subpage) {
                // if (opt.subOptionType > 0) {
                //     // get the index of the player color from the player index
                //     int colorIndex = state.menuState.playerColors[state.pageIndex - 1].colors[state.menu.pages[state.pageIndex].options[i].subOptionType - 1];
                //     ImU32 color = defaultColors[colorIndex];
                //     ImU32 finalColor = IM_COL32(GetRValue(color), GetGValue(color), GetBValue(color), globalAlpha);
                //     drawButton(
                //         menuPosX,
                //         yBase,
                //         menuWidth,
                //         menuHeight,
                //         font,
                //         fontSize,
                //         state.optionIndex == i,
                //         globalAlpha,
                //         opt.label.c_str(),
                //         type,
                //         0,
                //         finalColor
                //     );
                //     buttonCount++;
                //     continue;
                // } else {
                //     // if not a color type, its a controller type
                //     drawButton(
                //         menuPosX,
                //         yBase,
                //         menuWidth,
                //         menuHeight,
                //         font,
                //         fontSize,
                //         state.optionIndex == i,
                //         globalAlpha,
                //         opt.subOptions[state.menuState.controllerIndex[state.pageIndex - 1]].label.c_str(),
                //         type,
                //         0
                //     );
                //     buttonCount++;
                //     continue;
                // }

                switch(opt.subOptionType) {
                    int colorIndex;
                    ImU32 color;
                    ImU32 finalColor;
                    case 0:
                    drawButton(
                        menuPosX,
                        yBase,
                        menuWidth,
                        menuHeight,
                        font,
                        fontSize,
                        state.optionIndex == i,
                        globalAlpha,
                        opt.subOptions[state.menuState.controllerIndex[state.pageIndex - 1]].label.c_str(),
                        type,
                        0
                    );
                    break;
                    case 1:
                    case 2:
                    case 3:
                    colorIndex = state.menuState.playerColors[state.pageIndex - 1].colors[state.menu.pages[state.pageIndex].options[i].subOptionType - 1];
                    color = defaultColors[colorIndex];
                    finalColor = IM_COL32(GetRValue(color), GetGValue(color), GetBValue(color), globalAlpha);
                    drawButton(
                        menuPosX,
                        yBase,
                        menuWidth,
                        menuHeight,
                        font,
                        fontSize,
                        state.optionIndex == i,
                        globalAlpha,
                        opt.label.c_str(),
                        type,
                        0,
                        finalColor
                    );
                    break;
                    case 4:
                    {
                        std::string stringLabel = "Controller Profile: " + opt.subOptions[state.menuState.controllerProfile[state.pageIndex - 1]].label;
                        drawButton(
                            menuPosX,
                            yBase,
                            menuWidth,
                            menuHeight,
                            font,
                            fontSize,
                            state.optionIndex == i,
                            globalAlpha,
                            stringLabel.c_str(),
                            type,
                            0
                        );
                    }
                    break;
                    default:
                    break;
                }
                buttonCount++;
                continue;
            }

            drawButton(
                menuPosX,
                yBase,
                menuWidth,
                menuHeight,
                font,
                fontSize,
                state.optionIndex == i,
                globalAlpha,
                opt.label.c_str(),
                type,
                0
            );

            buttonCount++;
        }

        mainDraw->PopClipRect();
    }

    // -------------------------
    // SUB OPTIONS (SUB MENU STATE)
    // -------------------------
    if (
        state.phase == Phase::InIdle ||
        state.phase == Phase::InFadeIn ||
        state.phase == Phase::ShiftOut ||
        state.phase == Phase::InShiftUp ||
        state.phase == Phase::InShiftDown
    ) {
        ImDrawList* draw = ImGui::GetForegroundDrawList();

        draw->PushClipRect(
            ImVec2((float)menuPosX, (float)menuPosY),
            ImVec2((float)(menuPosX + menuWidth),
                (float)(menuPosY + menuHeight)),
            true
        );

        float buttonCount = 0.0f;

        const auto& subOpts =
            page.options[state.optionIndex].subOptions;

        for (int i = 0; i < subOpts.size(); i++) {
            const auto& opt = subOpts[i];
            OptionType type = state.menu.pages[state.pageIndex].options[state.optionIndex].subOptions[i].type;
            drawButton(
                menuPosX,
                menuPosY + buttonCount * (menuHeight / 7) + buttonOffset,
                menuWidth,
                menuHeight,
                font,
                fontSize,
                state.subOptionIndex == i,
                globalAlpha,
                opt.label.c_str(),
                type,
                0,
                state.menu.pages[state.pageIndex].options[state.optionIndex].subOptions[i].colorValue
            );

            buttonCount++;
        }
        draw->PopClipRect();
    }
}
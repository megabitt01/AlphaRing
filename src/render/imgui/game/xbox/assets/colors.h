#pragma once

#include <imgui.h>

inline ImU32 defaultColors[30] = {
    IM_COL32(82, 82, 82, 255),       // steel
    IM_COL32(192, 192, 192, 255),    // silver
    IM_COL32(255, 255, 255, 255),    // white
    IM_COL32(150, 75, 0, 255),       // brown
    IM_COL32(210, 180, 140, 255),    // tan
    IM_COL32(219, 202, 178, 255),    // khaki
    IM_COL32(132, 173, 127, 255),    // sage
    IM_COL32(128, 128, 0, 255),      // olive
    IM_COL32(107, 142, 35, 255),     // drab
    IM_COL32(34, 139, 34, 255),      // forest
    IM_COL32(0, 128, 0, 255),        // green
    IM_COL32(72, 209, 204, 255),     // sea foam
    IM_COL32(0, 128, 128, 255),      // teal
    IM_COL32(36, 160, 255, 255),     // aqua
    IM_COL32(71, 218, 255, 255),     // cyan
    IM_COL32(20, 90, 255, 255),      // blue
    IM_COL32(33, 38, 222, 255),      // cobalt
    IM_COL32(176, 224, 230, 255),    // ice
    IM_COL32(238, 130, 238, 255),    // violet
    IM_COL32(222, 158, 247, 255),    // orchid
    IM_COL32(177, 90, 209, 255),     // lavender
    IM_COL32(128, 0, 0, 255),        // maroon
    IM_COL32(178, 34, 34, 255),      // brick
    IM_COL32(255, 192, 203, 255),    // rose
    IM_COL32(183, 65, 14, 255),      // rust
    IM_COL32(255, 127, 80, 255),     // coral
    IM_COL32(255, 218, 185, 255),    // peach
    IM_COL32(255, 215, 0, 255),      // gold
    IM_COL32(255, 255, 0, 255),      // yellow
    IM_COL32(250, 250, 210, 255)     // pale
};

// Names of the 30 player colors, in defaultColors / MenuState order.
inline constexpr const char* k_colorNames[30] = {
    "Steel", "Silver", "White", "Brown", "Tan", "Khaki", "Sage", "Olive", "Drab", "Forest",
    "Green", "Sea Foam", "Teal", "Aqua", "Cyan", "Blue", "Cobalt", "Ice", "Violet", "Orchid",
    "Lavender", "Maroon", "Brick", "Rose", "Rust", "Coral", "Peach", "Gold", "Yellow", "Pale",
};

#pragma once

#include <imgui.h>
#include "./assets/colors.h"
#include <string>
#include <vector>
#include <optional>
#include <iostream>

// option types:
// - 0 = boolean
// - 1 = subpages
// - 2 = positive increment
// - 3 = negative increment
// - 4 = pointer display

enum class OptionType {
    Boolean,
    TeamToggle,
    Subpage,
    SubpageOption,
    Increment,
    Decrement,
    PointerDisplay
};

class Option {
public:
    std::string label;
    OptionType type;
    std::vector<Option> subOptions;
    int subOptionType = 0;
    int value = 0;
    std::optional<ImU32> colorValue;

    std::optional<int*> pointer;
    
    Option(std::string lbl, OptionType t)
        : label(std::move(lbl)), type(t) {}
    
    Option(std::string lbl, OptionType t, int s)
    : label(std::move(lbl)), type(t), subOptionType(s) {}

    Option(std::string lbl, OptionType t, int s, ImU32 c)
        : label(std::move(lbl)), type(t), subOptionType(s), colorValue(c) {}

    // Option(std::string lbl, std::vector<std::string> subs)
    //     : label(std::move(lbl)),
    //       type(OptionType::Subpage),
    //       subOptions(std::move(subs)) {}

    // Option(std::string lbl, int* ptr)
    //     : label(std::move(lbl)),
    //       type(OptionType::PointerDisplay),
    //       pointer(ptr) {}

    void addOption(const Option& opt) {
        subOptions.push_back(opt);
    }
};

class Page {
public:
    std::string label;
    std::vector<Option> options;
    bool visible = false;

    Page() = default;

    explicit Page(std::string lbl, bool vis = false)
        : label(std::move(lbl)), visible(vis) {}

    void addOption(const Option& opt) {
        options.push_back(opt);
    }
};

// Names of the controller slots, in MenuState::controllerIndex order (4 = no controller).
inline constexpr const char* k_controllerNames[5] = {
    "Controller 1", "Controller 2", "Controller 3", "Controller 4", "None",
};

class Menu {
public:
    static constexpr int MAX_PAGES = 5;
    static constexpr int k_controllerOption = 0;
    static constexpr int k_controllerProfileOption = 1;
    // subOptionType of the Splitscreen page's two Boolean options
    static constexpr int k_useKMOption = 0;
    static constexpr int k_usePlayer0ProfileOption = 1;
    Page pages[MAX_PAGES];

    Menu() {
        init();
    }

    // Fills each player's "Controller Profile" subpage with the custom
    // profiles, in MenuState::controllerProfile order.
    void setControllerProfiles(const std::vector<std::string>& names) {
        for (int index = 1; index <= 4; index++) {
            auto& option = pages[index].options[k_controllerProfileOption];
            option.subOptions.clear();
            for (const auto& name : names)
                option.addOption(Option(name, OptionType::SubpageOption, 4));
        }
    }

private:
    void addColorOption(int index, const char* label, int subOptionType) {
        Option option(label, OptionType::Subpage, subOptionType);
        for (int c = 0; c < 30; c++)
            option.addOption(Option(k_colorNames[c], OptionType::SubpageOption, subOptionType, defaultColors[c]));
        pages[index].addOption(option);
    }

    void initPlayer(int index) {
        pages[index].label = "Player " + std::to_string(index);

        pages[index].addOption(
            Option("Controller", OptionType::Subpage));
        for (const char* name : k_controllerNames)
            pages[index].options[k_controllerOption].addOption(Option(name, OptionType::SubpageOption, 0));

        // filled from the custom profiles by setControllerProfiles()
        pages[index].addOption(
            Option("Controller Profile", OptionType::Subpage, 4));

        pages[index].addOption(Option("Reduce Sensitivity", OptionType::Decrement));
        pages[index].addOption(Option("Display", OptionType::PointerDisplay));
        pages[index].addOption(Option("Increase Sensitivity", OptionType::Increment));

        pages[index].addOption(Option("Inverted Look", OptionType::Boolean, 0));

        pages[index].addOption(Option("Red Team", OptionType::TeamToggle, 0));

        addColorOption(index, "Primary Color", 1);
        addColorOption(index, "Secondary Color", 2);
        addColorOption(index, "Tertiary Color", 3);
    }

    void init() {
        pages[0] = Page("Splitscreen", true);

        pages[0].addOption(Option("Remove Player", OptionType::Decrement));
        pages[0].addOption(Option("Display", OptionType::PointerDisplay));
        pages[0].addOption(Option("Add Player", OptionType::Increment));
        pages[0].addOption(Option("Use K/M for Player 1", OptionType::Boolean, k_useKMOption));
        pages[0].addOption(Option("Use Player 1's Armor", OptionType::Boolean, k_usePlayer0ProfileOption));

        for (int i = 1; i <= 4; i++) {
            initPlayer(i);
        }
    }
};

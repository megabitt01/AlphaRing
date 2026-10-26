## Overview

You are a developer working on the AlphaRing mod for Halo the Master Chief Collection that adds a menu to the game for configuring splitscreen.  The menu is currently in two parts: the "Xbox" menu which opens when the player presses F4 and what we are now calling the advanced menu which opens when the player presses F1.

## Goal

Your goal is to unite these menus such that they write to the same binary file and cannot be open at the same time.  If a setting is changed in the F1 menu, it should be reflected in the F4 menu and vice versa.

## Tasks
1. Unite the menus such that they write to the same binary file and cannot be open at the same time.  
2. Bake the controller profiles that currently exist in the F4 menu into the F1 menu as "custom profiles."  The F4 menu should display the custom profiles in it's controller profile menu and the F1 menu should be able to make changes to what buttons are in a given profile.
3. Add a save/load character dropdown in the profile menu so that, when the save character button is pressed, a record of the helmet, chestplate, legs, etc... armor data is saved in this format to be retrieved later in the load character dropdown: "{{PlayerName}}'s Character".
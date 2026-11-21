<img width="1920" height="315" alt="AlphaRing Logo" src="https://github.com/user-attachments/assets/04c51c7b-3b3d-4cde-a094-bc690d5a97fc" />

A Splitscreen Mod for Halo the Master Chief Collection

[![Build status](https://ci.appveyor.com/api/projects/status/o3qbtc7jirw81xmb?svg=true)](https://ci.appveyor.com/project/WinterSquire/alpharing)
[![](https://dcbadge.limes.pink/api/server/https://discord.gg/TUyAnCrpuz)](https://discord.gg/TUyAnCrpuz)

### Showcase

| | |
|--|--|
| Splitscreen in Every Game <br> <img width="1086" height="651" alt="image" src="https://github.com/user-attachments/assets/0b5a5109-f602-424f-a9ec-51e35da832b8" /> | LAN Mutliplayer Support <img width="1569" height="974" alt="image" src="https://github.com/user-attachments/assets/6255730f-e1e7-495b-afb8-152e93d0d070" /> |
| Camera Tool (H3) <br> ![Camera](https://github.com/WinterSquire/AlphaRing/assets/135317392/d359b2e8-5302-430f-be0d-bc065e63f546) | Object Browser (H3) <br> ![Object](https://github.com/WinterSquire/AlphaRing/assets/135317392/0bce1af7-354f-4d9d-92f7-eb2d46d8ae37) |
| 8 Players Campaign <br> ![Splitscreen 8 players](https://github.com/WinterSquire/AlphaRing/assets/135317392/7d9f4281-892a-47e2-8e0c-845a965e5d11) | Splitscreen With [Mod](https://steamcommunity.com/sharedfiles/filedetails/?id=3153235187) (By [Priception](https://steamcommunity.com/id/priception)) <br> ![H4](https://github.com/WinterSquire/AlphaRing/assets/135317392/5359868c-c5db-4300-9805-84c61b0bd8ee) |

### Features
* Splitscreen (all games), side by side in every game, 3-4 players in every game
* Anniversary graphics with 3-4 players in Halo CE and Halo 2 (experimental)
* Per-player HUD: size, area, element positions and colour
* Spawn menus: vehicles, weapons and AI in front of any player, a menu per player on the D-pad
* Players window and hot join: join with A, also in the middle of a mission (experimental)
* "Xbox 360" Menu (controller-friendly, toggled with F4/Left Stick Click + Start)
* Advanced Menu (for greater customization, toggled with F1)
* Camera Tool (H3)
* Object Browser (H3)

See the [Feature Guide](#feature-guide) below for how to use each one.

### Installation
Good news!  We now have a launcher for quickly setting up the mod.  You can find [the repository here](https://github.com/megabitt01/alpharing-launcher-v2).
For a quick guide, watch this YouTube video: https://www.youtube.com/watch?v=E_0IjFBD2pI&t=30s

#### Manual Installation
Make sure you have the latest [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) installed.

Download the latest stable build from the [Releases](https://github.com/kirklandsig/AlphaRing/releases) page.

Place the DLL into the "Halo The Master Chief Collection\mcc\binaries\win64" directory and launch the game with EAC off.

**Xbox app / Game Pass:** place the DLL into `C:\XboxGames\Halo- The Master Chief Collection\Content\mcc\binaries\win64` (or the same path under your Xbox games folder) and launch the game from the "Halo: MCC Anti-Cheat Disabled (Mods and Limited Services)" Start menu entry.

For Running on Steam Deck/Linux, add the following command in the Steam Game Launch Options:
```
WINEDLLOVERRIDES="WTSAPI32=n,b" %command%
```

Works with **any Proton version** (Proton 9.0, Proton Experimental, Proton GE, etc.)

1. **Configure the game**:
   - Right-click MCC → Properties → Compatibility
   - Enable "Force the use of a specific Steam Play compatibility tool"
   - Select any Proton version (Proton 9.0, Experimental, or Proton GE all work)

2. **Set launch options**: Add the following to Steam Launch Options:
   ```
   WINEDLLOVERRIDES="WTSAPI32=n,b" %command%
   ```

3. **Controller Setup (Important for non-Xbox controllers)**:
   - For 8BitDo and other third-party controllers, enable **Steam Input** for the controller
   - Go to Steam → Settings → Controller → Enable "Xbox Configuration Support"
   - This allows Steam to translate your controller inputs to XInput, which MCC and AlphaRing expect
   - Without this, some buttons (like A or stick clicks) may not be detected

> **Note:** Tested on Batocera Linux with the unofficial Batocera add-ons Steam client.

### Usage
"Xbox 360" Menu: `F4` or `Controller Start` + `Left Stick Click`. Advanced Menu: `F1` or `Controller Start` + `Right Stick Click`. Both can be changed in `alpha_ring_menu.cfg`.

To navigate the Advanced Menu using Controller use the `Right Stick` to move the mouse and `RB` to click.

When a menu is open, game input is disabled.

Spawn menu: in a campaign mission of Halo CE, Halo 2, Halo 3 or ODST, each player presses `D-pad Down` on their own controller (see [Spawn Menus](#spawn-menus-halo-ce-halo-2-halo-3-and-odst-campaigns)).

---

## Feature Guide

### Split Screen: Side by Side and 3-4 Players in Every Game
- **Side by side (Left / Right)** works in Halo CE, Halo 2, Halo 3, ODST, Halo 4 and Halo Reach (Reach's is XiaoDanny's). 2 players get two full-height halves. 3 players put player 1 on the left half and players 2 and 3 on the right quarters. 1 and 4 players keep the game's own layout.
- **Turn it on:** Advanced Menu → **Dev Tools** → **Split-screen layout** (above the game tabs): *Top / Bottom* or *Left / Right*. It's one saved choice for every game. Halo 3, ODST, Halo 4 and Reach switch at once; Halo CE and Halo 2 at the next mission start.
- **Crosshair and HUD centred in each view** (Halo 3, ODST, Halo 4, Reach). These games fitted every split-screen view into the whole screen's title-safe area, which pulled each player's crosshair and HUD toward the middle of the screen. Each view now gets its own.
- **3-4 players:**
  - Halo 4 no longer stays black: a 3-4 player mission starts with two players, and the others join about 3 seconds later.
  - ODST's quarters and side-by-side halves use its full HUD (its quarter HUD only has the ammo panel).
  - Halo CE and Halo 2 start 3-4 player missions in Classic graphics, since their Anniversary renderers only draw two views. MCC's own graphics setting is left alone. To keep Anniversary, see the next section.
- **Known limits:**
  - Halo CE and Halo 2 are side by side in Classic graphics only. A side-by-side mission starts in Classic. Back to Anniversary shows the game's own stacked views, and Back to Classic brings side by side back.
  - Halo 4 side by side on a screen wider than 16:9: each half's HUD is centred vertically but not horizontally.

### Anniversary Graphics with 3-4 Players (Halo CE and Halo 2, experimental)
- Both Anniversary renderers only draw two views. With this on, 3-4 player missions keep Anniversary graphics: each frame still draws two views, and the pairs take turns (players 1 and 2, then players 3 and 4), so each view updates at half the frame rate.
- **Turn it on:** Advanced Menu → **Dev Tools** → *Anniversary graphics with 3-4 players (Halo CE, Halo 2; experimental)*, under the layout. It's off by default, saved like the layout, and takes effect at the next mission start. MCC's own graphics setting still decides whether a mission is in Anniversary, and Back switches between Classic and Anniversary in the middle of it, with every view drawn.
- **Three players:** Halo 2 gives player 1 the top half; Halo CE uses quarters and leaves the fourth black.
- **Halo 2 Anniversary with 2 players** draws each view into its half (it came out a quarter of the screen tall before). This doesn't depend on the checkbox.
- **Known limits:**
  - With it off, Back to Anniversary in the middle of a 3-4 player mission doesn't draw every view: Halo 2 is black under the HUD, and Halo CE shows two views.
  - Turned on after Halo CE has loaded in a session, it takes effect the next time Halo CE loads. Until then, Halo CE's 3-4 player missions start in Classic.
  - Side by side still starts in Classic.
  - Halo 2's players 3 and 4 see black until they spawn, as in Classic.

### Per-Player HUD (Halo CE, Halo 2, Halo 3, ODST and Reach)
- **Each player** can resize their whole HUD; move, resize or hide each element (motion tracker, shield/health, weapon/ammo, grenades, crosshair, equipment, messages); and pick a **HUD area**: *Game default*, *Screen edges*, *21:9 box*, *16:9 box* or *4:3 box*.
  - An area moves the HUD's left and right elements to the sides of a centred box of that shape in the player's view. Use it to pull them in from the far edges of an ultrawide or two-monitor view, or to spread Halo CE's 4:3 HUD out to the view's edges.
  - The crosshair can be resized or hidden, but not moved.
- **Colour** (Halo 3, ODST, Reach): shift the HUD toward a hue, with a strength. Whites and enemy reds keep their colour.
- **With the mouse:** Advanced Menu → **HUD**, with a tab per player, *Reset this player* and *Copy to all players*.
- **From a controller:** the spawn menu's **MY HUD** page (D-pad Down, then LB / RB): area, size, colour and Reset.
- **Not saved yet:** changes show up at once and last until MCC closes.
- Messages move in Halo CE and Halo 2, and equipment in Halo 3, ODST and Reach. Halo 2's shield moves with its motion tracker. Halo 4 has no per-player settings.
- **Halo 2 on a screen wider than 16:9:** with MCC's *HUD anchor: Centered* (the default), Halo 2 squeezes every split-screen HUD into a 16:9 box in the middle of the whole screen. Dev Tools → **halo2** → *HUD at screen edges* (off by default) keeps each player's HUD at the edges of their own view, with one player too.
- **Halo 4:** where a split view isn't the shape its HUD layout was made for (a screen that isn't 16:9, or the black bars removed), its HUD fits the view instead of keeping to part of it.

### Spawn Menus (Halo CE, Halo 2, Halo 3 and ODST Campaigns)
Spawn vehicles, weapons, equipment and AI characters in front of any player. Characters can be on their own side, allies or enemies, with a weapon you pick.
- **Open:** in a campaign mission, a player presses **D-pad Down** on their own controller, and a menu opens in that player's part of the screen. While it's open their Spartan holds still; the other players keep playing, or open their own menus.

| Button | In the spawn menu |
|--------|-------------------|
| LB / RB | Vehicles, Weapons, Equipment, Characters, My HUD |
| Up / Down | choose |
| A | spawn in front of you |
| Left / Right | characters: their own side, ally, enemy |
| LT / RT | characters: their usual weapon, or any weapon the level has loaded |
| Y | refresh the list after reaching a new part of the mission |
| B | close |

- **With the mouse:** Advanced Menu → **Spawn**: pick the player, the page and an item (with a search box, and side and weapon for characters). A player on keyboard and mouse uses this window.
- **Halo Reach** has no spawning, so its menu opens on My HUD. **Halo 4** has neither, so D-pad Down goes to the game there.
- No menu opens while the Advanced Menu or the "Xbox 360" Menu is open, and opening either closes the open menus.
- **Change or turn off the button:** add a line to `alpha_ring_menu.cfg` (read when MCC starts): `spawn_menu_controller=DPAD_DOWN`, any button or combo like `BACK+DPAD_DOWN`, or `NONE`.
- **Nothing is saved:** the menus and the Spawn window write no file.
- **Known limits:**
  - Spawned characters borrow an empty squad of the mission, so a mission script waiting on that squad can wait while they live. Kill them if a mission stops progressing.
  - Spawned allies fight but don't follow you.
  - The game keeps running while a menu is open.

### Players Window and Hot Join (experimental)
- **Players window:** Advanced Menu → **Players**. It shows a card per player with their controller (or keyboard and mouse for player 1). While it's open, every controller answers in it directly:
  - At MCC's menus, **A** on a controller nobody plays with adds a player with it, and **B** on a player's controller takes that player out. The players after them move up a slot, each keeping their controller.
  - **Y** on a player's controller, or the button on their card, switches them between Spartan and Elite, from the next game, in Halo 2, Halo 3 and Reach. It's offered in split screen for player 1, and for players 2-4 unless *Use player1's profile* is on.
  - In a game, it shows who's playing, with the controllers they have now. Players join there only with hot join.
- **The same players everywhere:** a join or a leave changes the players and controllers in the shared menu settings, so the "Xbox 360" Menu and the Advanced Menu show it, a game start applies it, and the next time a menu saves, it's written.
- **Hot join:** the **Hot join (experimental)** checkbox at the bottom of the Players window. Then, in a mission, a controller nobody is playing with presses **A** to join, with or without the menu open, and the new player comes in beside a teammate. Works in Halo CE, Halo 2, Halo 3, ODST, Reach and Halo 4. The press a player joins with doesn't reach the game until it's let go.
  - Halo 3, ODST, Reach and Halo 4 take the new player the way they take a controller signing in.
  - Halo 2 brings them in through its co-op respawn. Legendary and the Iron skull turn that off, so there they come in at a starting place.
  - Halo CE puts them on a spot a teammate just walked through. If there's none yet (at a mission's start, say), they come in at a starting place and are moved beside a teammate if such a spot turns up within 10 seconds.
  - In Halo CE and Halo 2, going back to a checkpoint saved before a player joined brings them back the same way, not at the mission's start.
  - Leaving in the middle of a mission works only in Halo CE: the last player who joined presses **B** with the Players window open. The other games end the mission when a player signs out, so players leave at MCC's menus.
  - Halo CE and Halo 2 take the choice as a mission starts, so in their games it's changed at MCC's menus (the checkbox is greyed out in their games). Until such a game ends, the player count changes only by joining and leaving: the "Xbox 360" Menu's count and the Advanced Menu's -/+ are held.
- **One at a time:** while the "Xbox 360" Menu is open, the Players window isn't drawn and hot join doesn't listen. The same goes while the Splitscreen window's **Bind** is waiting for a controller.
- **Not saved:** the Players window writes no file of its own. The hot join choice and each player's species last until MCC closes.
- **Known limits:**
  - Made for campaigns and not tried in multiplayer.
  - A player who joins in the middle of a mission doesn't get their team setting until the next game start.

### Ready for MCC Updates
- AlphaRing carries byte patterns for the addresses it uses. On MCC 1.3528 nothing changes and nothing is searched.
- On another MCC build, each game DLL is searched once as it loads. Anything that can't be found is logged (`[Offsets]` in the log) and just that feature stays off, instead of hooking the wrong code.
- If an address it needs in MCC's own executable can't be found, AlphaRing stays off and MCC runs unmodified.
- Tested with a simulated update, not yet with a real one.

---

## Building from Source

### Prerequisites
- Visual Studio 2022 Build Tools
- CMake 3.27+

### Build Commands
```bash
# First time setup
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe" WTSAPI32.vcxproj -p:Configuration=Release -p:Platform=x64
```

Output: `build/Release/WTSAPI32.dll`

---

## Credits
- **Original AlphaRing:** [WinterSquire](https://github.com/WinterSquire/AlphaRing)
- **Controller Binding and Proton Fixes Fork:** [kirklandsig](https://github.com/kirklandsig/AlphaRing), also for side by side in every game, Anniversary graphics with 3-4 players, the per-player HUD, the spawn menus, the Players window and hot join
- [XiaoDanny](https://github.com/XiaoDanny) for Halo Reach's vertical split, per-player FOV and render-quality controls.
- [Assembly](https://github.com/XboxChaos/Assembly) for the tag group research, and its plugins for the scenario layouts the spawn menus use.
- [Blender](https://github.com/blender/blender) for the bezier curve calculation.
- [Priception](https://github.com/Priception) for adding UI controller support and helping with the interface and crash issue.

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
* Splitscreen (all games)
* "Xbox 360" Menu (controller-friendly, toggled with F4/Left Stick Click + Start)
* Advanced Menu (for greater customization, toggled with F1)
* Camera Tool (H3)
* Object Browser (H3)

### Installation
Good news!  We now have a launcher for quickly setting up the mod.  You can find [the repository here](https://github.com/megabitt01/alpharing-launcher-v2).
For a quick guide, watch this YouTube video: https://www.youtube.com/watch?v=E_0IjFBD2pI&t=30s

#### Manual Installation
Make sure you have the latest [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) installed.

Download the latest stable build from the [Releases](https://github.com/kirklandsig/AlphaRing/releases) page.

Place the DLL into the "Halo The Master Chief Collection\mcc\binaries\win64" directory and launch the game with EAC off.

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
Toggle menu: `F4` or `Controller Back` + `Controller Start`

To navigate using Controller use the `Right Stick` to move the mouse and `RB` to click.

When the menu is open, game input is disabled.

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
- **Controller Binding and Proton Fixes Fork:** [kirklandsig](https://github.com/kirklandsig/AlphaRing)
- [Assembly](https://github.com/XboxChaos/Assembly) for the tag group research.
- [Blender](https://github.com/blender/blender) for the bezier curve calculation.
- [Priception](https://github.com/Priception) for adding UI controller support and helping with the interface and crash issue.

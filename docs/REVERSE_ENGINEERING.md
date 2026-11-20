# Alpha Ring reverse-engineering notebook

This is the shared, agent-neutral source of truth for ongoing reverse-engineering work. It consolidates the durable content from the former `handoff.md` and the raw vertical-splitscreen investigations. Investigation chronology — session-by-session logs, predicted-vs-measured tables, bisect narratives, and disassembly walkthroughs — lives in the private Alpha Ring Notes repo, not here; this document states current mechanism, validated behavior, and open items. Raw evidence (logs, screenshots, capture files) is also archived there. Graphics-hook indices are in [`method_table.txt`](../doc/method_table.txt).

- Last consolidated: **2026-09-19** (condensed toward the project's established documentation style; investigation chronology moved to Alpha Ring Notes)
- Active source target: **MCC 1.3528.0.0** (`VERSION` in `CMakeLists.txt`)
- Research branch: **`vertical-splitscreen`**

Status terms:

- **Known**: supported by current code plus live measurement, a reproducible experiment, or direct binary/decompile evidence.
- **Hypothesis**: plausible interpretation that still needs a discriminating test.
- **Open question**: behavior or mechanism not yet localized.
- **Retired**: tested and ruled out for the stated symptom.

Commit and branch names are provenance, not instructions to reset a working tree.

## Project and runtime map

Alpha Ring is a C++17 DLL-based MCC modding tool. The build creates `WTSAPI32.dll`, installed beside MCC's executable to forward the system WTS API while initializing Alpha Ring. The overlay uses DirectX 11 and ImGui; controller handling uses XInput.

The local/precompiled dependency set includes MinHook (detours), spdlog (logging), nlohmann/json (serialization), Lua, tinyxml2, SDL2/SDL2_mixer, the game structure/offset library, and utility code. `MCC::IsInGame()` is the established session guard. Where shared game state is protected by a critical section, follow the existing lock pattern rather than adding an unsynchronized access path.

| Path | Role |
| --- | --- |
| `CMakeLists.txt` | Active MCC version, sources, dependencies, and `WTSAPI32` target |
| `lib/game/inc/<version>/offset_*.h` | Version-specific MCC/game RVAs and data offsets |
| `src/mcc/module/Module.cpp` | Module lifecycle, Dev Tools UI, and patch coordination |
| `src/mcc/module/entry/haloreach/` | Halo Reach hooks and focused probes |
| `src/mcc/module/patch/` | Runtime patch and splitscreen-config persistence |
| `src/mcc/splitscreen/` | Player/profile and splitscreen UI |
| `src/input/` | XInput wrapper, menu controls, and current configuration code |
| `src/render/d3d11/` | D3D11 hook and GPU-boundary probes |
| `src/log/DebugFlags.h` | Compile-time diagnostic and unfinished-feature gates |

The old handoff referenced `src/mcc/settings/Settings.*`; those files no longer exist. Current configuration work is in `src/input/MenuConfig.*` and the patch/config stores. Documentation under `lib/` is vendor-owned.

## Established baseline behavior

### Controller and menu work

**Known**

- Controller-to-player binding and button-to-action binding exist in `src/mcc/splitscreen/Splitscreen.cpp` and `src/mcc/CGamepadMapping.cpp`.
- XInput supports controller indices 0–3. `src/input/Input.cpp` loads an available XInput DLL, handles the Start+Back menu toggle, moves the UI cursor with the right stick, and maps right shoulder to click while the menu is open.
- New profiles use the Xbox-style defaults documented in `README.md`. This replaced a default-value failure where actions appeared as Left Trigger.

### Player-count boundary

**Known**

- A six-player experiment enlarged profile arrays and loops, but rendering failed because `c_splitscreen_config` contains four view-bound entries and four configuration blocks in `lib/game/src/halo3/render/views/split_screen_config.h`.
- XInput independently exposes only four controller indices.

**Hypothesis**

- More than four rendered local players requires deeper engine/binary work, new view-layout data, and a non-XInput input strategy. The old handoff's “not possible” wording was too strong; the evidence establishes only that simple array/loop expansion is insufficient.

## Halo Reach splitscreen model

### Configuration table and persistence

**Known**

- `c_splitscreen_config::m_config_table` is indexed as `block * 4 + slot`; blocks are 0 = four-player alias, 1 = one player, 2 = two players, 3 = three players, and 4 = four players.
- A slot stores normalized `x0`, `y0`, `x1`, `y1`, plus a resolution/layout selector.
- The game restores its shipped table on level load. `SplitscreenConfigStore::Apply()` reasserts saved fields per frame and avoids writes when bytes already match.
- Returning from a custom vertical layout must clear saved entries 8 and 9. Saving stock values would leave the store fighting later game resets.

Observed resolution variants at 1920×1080:

| `res` | Observed target | Meaning |
| ---: | ---: | --- |
| 0 | 1920×1080 | single player / full screen |
| 1 | 1920×540 | two-player stretched, no bars |
| 2 | 960×540 | quadrant |
| 3 | 1546×540 | shipped two-player pillarboxed layout |

`res = 5` has been used as a custom vertical-layout marker because static analysis showed relevant content/FOV lookup paths fail safe. It is not a render-target variant; masking it to `5 & 3` would alias the wrong surface.

3-player Left/Right places P1 on the full left half and P2/P3 as right-half quadrants (revised once, 2026-09-16, to keep P1 on the same side as 2P Left/Right — controller-to-slot mapping across that revision is unverified). `SplitscreenConfigStore.cpp`'s `kLeftRight3P`/`kStock3P`/`kLeftRight`/`kTopBottom` constants are the authoritative geometry for every supported layout; treat them as source of truth rather than restating coordinates here.

### Black-bar behavior

**Known**

- Halo Reach's two-player bar painter assumes horizontal slots. With a left/right layout, slot 0's computed right bar covers player 2, so bypassing that painter is mandatory for the experiment.
- Vertical configuration and black-bar removal touch the same table entries by different routes. Byte patches apply at load; the persistent store applies later.
- The per-player HUD resolution semantic at `haloreach.dll + 0xD1F710` does not track the live splitscreen table — it is refreshed every HUD update from an unrelated per-player HUD-state field, not copied from the table (see "Resolved: Left/Right HUD fixes" below).
- The `Split-screen layout` combo in `Module.cpp` (Dev Tools, above the game tabs; formerly Reach's `Two-player layout`) is the supported control, and sets the layout for every game (`mcc/splitscreen/LeftRight`); a prior unused `g_verticalSplitToggle` flag that gated nothing was removed 2026-09-17.
- **`ResolveActiveLayout(playerCount)` always returns Native for 1P and 4P**, regardless of the saved Left/Right preference — the preference persists across player counts, but every Left/Right-keyed behavior (bar suppression, HUD semantic override, render-target sizing) must gate on the *active* layout, not the saved one. An earlier version gated on the saved preference alone and forced the 2P pillarbox HUD record onto a full-screen solo slot; fixed by adding this scope check. Any new Left/Right-conditional code must use `ResolveActiveLayout`/`UsesFullHeightLeftRightSlot`, never the raw preference. The other games' equivalent is `LeftRight::Active`, which leaves out Reach's hook gate (`LeftRightAvailable`).
- **Known — current behavior, validated:** the bars-removed/bars-on preference is independent of layout selection and survives Top/Bottom↔Left/Right switching (both directions) and a full MCC restart. Cause of an earlier regression (layout selection silently erasing the user's bar choice) and its fix are covered under `CPatch` below — the same root cause also explains a "toggle inert until a layout round-trip" symptom.

## Vertical-splitscreen investigation

### Render-target sizing

**Known**

- Halo Reach originally sized the two-player render-target family from duplicated constants rather than the live splitscreen table. For variant 3, `FUN_1802663b8` used width `0.805208325` and height `0.5`; the width equals the shipped slot fraction `0.902604163 - 0.097395837`.
- With a custom portrait slot, camera/projection/scissor followed the table while the shared surface remained 1546×540, producing cropped content and black fill.
- `FUN_1802669e8` receives computed dimensions by writable pointer and exposes the descriptor/sub-index needed for correction. The detour takes the maximum two-player slot extent so asymmetric layouts still fit.
- Pyramid allocations must be scaled, not all assigned full slot size. An earlier assignment experiment rendered but inflated dozens of deliberately small buffers by roughly 100 MB and destroyed bloom/exposure pyramid proportions.
- Normalization is per variant: `(1,1)`, `(1,0.5)`, `(0.5,0.5)`, or `(0.805208325,0.5)`. Using variant 3's width for `res = 1` inflated a correct 1920×540 surface to about 2384×540.
- Stock geometry is an exact no-op after rounding.

**Known: the direct-size path must truncate, not round**

Reach computes the variant-3 base with an `int` cast — it truncates. An earlier revision of the direct-size path rounded instead, which substituted a different integer than the game's own wherever the product's fraction reached 0.5 (`3440` wide: native `2769`, rounded `2770`). Bisected to a single commit (`09830ca`, direct child of the last-known-clean `7746277`); fixed by truncating in `8131a31`. Verified under MSVC `/fp:precise`: byte-exact match to Reach at every width/height from 320 to 8192. Direct derivation from the screen dimension (not rescaling an already-truncated base) is preserved — that, not the rounding mode, is what avoids amplified truncation for a custom slot shape. **Do not reintroduce rounding** — see the invariant comment in `splitscreen_rt.cpp`.

**Suspected, not proven**

- The mechanism linking the one-pixel top-level error to visible lighting corruption is unestablished. Two candidate explanations (pyramid-halving propagation; an unwritten frame column) were checked against the evidence 2026-09-19 and neither survives — the pyramid levels are sized independently per level, not halved from the top, and the unwritten-column theory doesn't explain the `1280×720` repro (target equals viewport width there, no unwritten column). What both corrupt cases share is only that the stored target size differed from the value the rest of the frame independently derived. See Alpha Ring Notes for the full elimination and a concrete lead (`FUN_1802663B8`'s pool-entry `+0x3C`/`+0x40` fields, read back as a clamp by `FUN_1802F1A2C`).
- Local repro: `1920×1080`/`2560×1440` cannot reproduce the regression; `1280×720`, `1366×768`, `1440×900`, `1680×1050` can.

**Known: the Left/Right pyramid levels are one pixel short about half the time — arithmetic only, closed**

For a custom (Left/Right) table, `scaled()` recovers each pyramid level by rescaling a value Reach already truncated — the same hazard the top-level fix above removed one level up, uncorrected here. Heights: 50.0% of levels lose a pixel (`scaleY` is bitwise exactly `2.0f`, so `scaled()` just doubles an already-halved integer). Widths: 9.8% lose a pixel. Errors are one-sided, never oversize, max deviation 1px. **Do not transplant the `8131a31` truncation fix here** — truncating `scaled()` makes the width case worse (59.6% wrong instead of 9.8%) because the loss is upstream of the rounding; the only correct fix is recovering the descriptor's divisor and deriving the level directly. A stock table is unaffected (`scaleX`/`scaleY` both bitwise `0x3F800000`).

**Status: closed as arithmetic-only, 2026-09-19.** No run has shown a visual fault from it. Not a release blocker. The `[SplitRT]` probe in `splitscreen_rt.cpp` logs the descriptor's flags word and divisor floats — the only way to recover the pre-halved dimension, since Reach overwrites it before the detour runs and the descriptor table is zero in the DLL's file image until runtime. See the source comment for the exact recovery expression (`floorf(screenH / div + 0.5f)` in float32 — a generic `round()` disagrees at the half-way boundary, which is the whole subject).

**Constants verified against the DLL image (2026-09-19, direct PE read)**

`_DAT_180a8ae58 = 0.8052083253860474` (`0x3F4E2222`), `DAT_180a8ad74 = 0.5`, `DAT_180a8af18 = 1.0f`, `DAT_180a8a9b8 = 1e-4`, `DAT_180a8b560 = 1152.0`, `DAT_180a8b068 = 1.7777778` (16:9). `DAT_180a35c42` reads as **int16** (`720`), not a 32-bit float — confirms the warning in `cui_canvas_scale.cpp` about Ghidra's incorrect 32-bit render of that load.

### Open: Top/Bottom corruption at unusual manually-resized window heights

Reported: stock Top/Bottom shows rendering corruption at some unusual manually-resized window heights (e.g. `1920x673`); others (e.g. `1920x421`) are clean. Not attributed to the Left/Right pyramid mismatch or a `8131a31` recurrence — treat as independent until evidence says otherwise.

**Known: the render-target sizing detour is arithmetically inert here.** Under a stock Top/Bottom table, `scaleX`/`scaleY` are bitwise `1.0f` and `exactW/exactH == topW/topH` at every tested width/height — `splitscreen_rt.cpp` changes nothing at any resolution in this configuration. The two reported heights are also arithmetically indistinguishable on every sizing measure checked (both odd, both give slot 1 one row taller, neither produces a degenerate pyramid level; the *clean* case has more odd pyramid levels than the corrupt one). No sizing-parity theory separates them.

**Suspected:** a resize lifecycle or client-vs-backbuffer aspect question (`FUN_1802881CC`), not sizing arithmetic — see the "graphics descriptor / `GetClientRect`" open item below.

**Deferred past release.** Release verification checks only the settled common modes below.

### Release verification scope

Verify only these before release; arbitrary window-resize sizing is deferred separately.

- `1920x1080` Top/Bottom
- `2560x1440` Top/Bottom
- Normal Left/Right cases

Not release-gating: the Left/Right pyramid mismatch (closed, arithmetic-only) and manually-resized window dimensions.

### Live layout switch and the render-target pool lifecycle

**Known**

- Top/Bottom and Left/Right both select `res = 3`. A fresh match load sizes the shared render-target family from the active layout; switching `TwoPlayerLayout` mid-match updates table/view/HUD state but does not re-invoke RT_CREATE — the old surface is reused and composition breaks until another match loads, unless corrected (below).
- The split-screen targets live in a fixed pool (`RT_POOL_INIT` 0x266F90 / `RT_POOL_RELEASE` 0x2670FC), not a keyed cache — no dirty flag, lazy re-request, or per-entry refcount. Both layouts collide purely on index 3; size is computed only inside `0x2663B8` during pool init.
- Calling RT_CREATE directly is unsafe (leaks COM pointers, misindexes later entries via the global allocation cursor). No per-entry invalidation exists to reuse. `0x2D4220` is a *transient scratch-target* request, not the split-screen pool — do not confuse the two.

**Known — live rebuild implemented and runtime-validated; current behavior**

`SetTwoPlayerLayout` bumps a layout generation only on an actual change. The `UpdateAllPlayerViews` detour, after `SplitscreenConfigStore::Apply()` and before the original, runs Reach's own `RT_POOL_RELEASE`/`RT_POOL_INIT` pair when the generations differ and the scratch-target depth is 0 — the same pair and frame position Reach's own resize path uses. Runtime-confirmed: live Top/Bottom ↔ Left/Right switching is correct in both directions without Alt+Tab, in 2P and 3P. **Open:** whether any subsystem caches pool COM pointers across frames, and whether a one-frame hitch/temporal-effect reset (as on a window resize) is expected — not reported as an artifact in validation.

### GPU viewport and `res` propagation

**Retired.** Neither D3D viewport staleness nor `res` propagation was the distortion mechanism (measured 2026-09-15, prior to the render-target fix); both are superseded by it.

### HUD frame geometry

**Known**

- The frame consumer uses values near `haloreach.dll + 0xD1F790`: x/y inset and half-width/half-height. A width-constrained 16:9 band in a 960×1080 slot covered only y = 270…810, bunching widgets in the middle.
- A full-height frame fixes anchor positions but stretches widget shapes. Frame geometry couples placement and shape; it cannot satisfy both in a portrait slot by itself.
- Radar aspect follows approximately `0.368 × (halfW / halfH)`; shipped Reach draws the radar around 1.264, not as a geometric circle.
- The original portrait frame was asymmetric across the two slots. Re-centering with `(slot extent - 2 × half extent) / 2` produces identical, correct insets for both.
- The HUD basis probe was near-identity with no useful slot dependence; retired as the distortion mechanism.

### CHUD constant layout and widget spreading

**Known**

- `CHUDWidgetVS` table 4 uploads `chud_widget_offset` (element 0, 16 bytes) separately from `chud_widget_transform1/2/3` (element 1, 48 bytes). The transform is a row-major 3×4 matrix with translation isolated in the fourth column, allowing movement without resize — a second lever unavailable in the frame.
- Radar/sprint content occupies the left side (`tx < 500`); radar blips are projected separately and do not follow a moved dish. World-projected elements (e.g. a player nameplate) are identifiable by a nonzero rotation/off-diagonal term and must not receive static-layout spreading.

**Retired 2026-09-19.** A manual widget-spread engine (`hud_spread_*`, multiplicative/additive/derived modes with per-widget tx/ty rule tables) was implemented and validated as functional, but was superseded by the Left/Right canvas-height fit below, which corrects the same distortion at the global-canvas level without per-widget tuning. Removed from the release branch; git history has the full implementation and measured tx/ty cluster data if a per-widget approach is needed again.

### Resolved: Left/Right HUD fixes (semantic + canvas scale)

Two independent defects, both fixed and in production.

**Known — root cause 1 (HUD semantic).** `D1F710`'s HUD-layout semantic has exactly one writer, `UpdatePlayerHudView` (0x2D94BC), which copies it every frame from a separate, persistent per-player field (TLS-rooted context `+0x5DE8 + player_index*0x10D68`) — **not** from the splitscreen table, which correctly holds `res=3` for Left/Right throughout. In the tested configuration that upstream field read `2`, so both the global HUD-layout selector (`FUN_1802d91bc`) and every per-widget lookup (`FUN_1802da0c0`, keyed directly by `D1F710`) ran on the record authored for semantic 2, not Left/Right's actual `res=3` geometry.

**Fix (`hud_layout_probe.cpp`):** at the confirmed call boundary (`UpdatePlayerHudView` → `FUN_1802d91bc`, return RVA `0x2D95DF`), for a slot that is a full-height Left/Right half in the *active* layout (`UsesFullHeightLeftRightSlot` — 2P both slots, 3P player 1 only), overwrite `D1F710` to `3` and call the original selector with semantic `3`. Left at `3` afterward (not restored) so later per-widget lookups in the same update also see it. Gated on the active layout, not the saved preference — see the `ResolveActiveLayout` scope invariant above; an earlier version gated on the saved preference alone and broke solo HUD.

**Known — root cause 2 (canvas scale), independent defect.** Even with the semantic fixed, Left/Right widgets remained visibly stretched. Reach maps every CHUD widget from a reference canvas `(refW', refH)` onto the HUD frame per axis, and corrects the canvas only for the *whole backbuffer's* aspect (`refW' = refW × aspect/(16:9)`) — never for the slot. A full-height Left/Right slot therefore draws the semantic-3 canvas into a frame about 0.53× as wide relative to its height, stretching every widget ~1.9× vertically regardless of backbuffer size.

**Fix (`hud_anchor.cpp`, gate `g_lrHudCanvasFit`):** for the same `UsesFullHeightLeftRightSlot` slots, after `ComputeHudAnchorFrame` runs, set `refH = refW' × halfH/halfW`. The frame itself and `refW'` are untouched, so anchors stay at the slot edges; every canvas consumer (shader, anchors, marker projection) reads the corrected value.

**Validated in game (2026-09-16):** `Sx == Sy` logged exactly for both Left/Right slots at `1920×1080` and `1920×810`; radar/weapon/notification/medal proportions corrected; world-projected markers (nameplates) stay on target; Top/Bottom and 3P quadrants unaffected. **Known limitation:** scoreboard/loadout list layout (`FUN_1802e4a2c`/`FUN_1802e5514`) also reads `refH` and is affected by this fix, but was already broken in Left/Right independently (see "Scoreboard and loadout" below) — not separately regression-tested. Resolutions other than `1920×1080`/`1920×810`/`3840×1080` (dual-monitor) untested.

See Alpha Ring Notes for the full investigation: the superseded frame-crop/portrait-Y-override design attempts, the `D1F6F8`-substitution intermediate approach, Stage-1 predicted-vs-measured radar tables at 5 resolutions, and the `1920×810` runtime trace that isolated root cause 1.

### Loadout screen

**Superseded 2026-09-17** by the CUI resolution-variant fix (see "Scoreboard and loadout" below). Kept as provenance: a vertical `res=3` loadout was invisible while the horizontal one rendered, despite both resolving the correct template ID and reaching the build call with valid pointers — the failure was downstream of template resolution, in mesh submission or a later render-state difference, never fully localized before the CUI variant seam made the investigation moot. See Alpha Ring Notes for the full trace (aspect-crop divergence in `UpdatePlayerHudView`, the `0x2E26F0`/`0x2E24FE` mesh-submission candidates, and the negative capture results).

### Post-HUD-fix remaining Left/Right UI: scoreboard, loadout, death nameplate

**Superseded** for scoreboard and loadout by the CUI variant seam below. The death-screen nameplate's render/template path remains **untraced** — no Ghidra function name or code path has been identified for it at all; this is a genuinely open item if resumed.

### Scoreboard and loadout: shared CUI screen-variant path

**Status: resolved for every full-height Left/Right slot (2P slots 0/1, 3P slot 0); production seam runtime-validated.**

- Scoreboard and loadout share Reach's generic per-window CUI resolution-variant selector at `0x2CA86C` (`GetLoadoutHudTemplateId`, `OFFSET_HALOREACH_PF_SPLITSCREEN_RESOLUTION_RESOURCE`). For `window < 4` it reads `m_config_table[playerCount*4 + window].res`: `3` → `resolution_widescreen_half` (`0x80046`), `2` → `resolution_widescreen_quarter` (`0x80047`), everything else (including 1P's `0`) → base `resolution_widescreen` (`0x80045`).
- A full-height Left/Right slot keeps `res = 3`, which natively selects the half variant authored for the stock wide, short Top/Bottom window — in a full-height half that layout left the loadout invisible.
- **Production rule (`loadout.cpp`):** the detour calls the native selector first. It returns `0x80047` only when the native result is `0x80046` **and** `SplitscreenConfigStore::UsesFullHeightLeftRightSlot(GetSplitscreenPlayerCount(), window)` is true (Left/Right active and that window's entry has `res == 3`: 2P windows 0/1, 3P window 0). Otherwise the native result is returned unchanged. Top/Bottom, 1P, 4P, 3P windows 1/2, and windows 4/5 are all excluded, either by the predicate or because their native result isn't `0x80046`.
- **Known limitation:** at a `3840×1080` backbuffer the CUI screens render but are horizontally stretched — a separate defect, fixed by the CUI canvas-scale correction below.
- **Validated in game (2026-09-17):** 2P Left/Right scoreboard, loadout (including after a mid-match death), and pause/start menu for both players; 2P Top/Bottom as a negative control; live Top/Bottom↔Left/Right layout switching with reopened screens; 3P Left/Right P1 loadout (previously invisible under the 2P-only rule) at `3840×1080`.
- **Not covered:** 1P/4P (excluded by design); other per-player CUI screens (e.g. carnage report) untested; a screen already open during a live layout switch probably keeps its previous variant until reopened; resolutions other than `1920×1080`/`3840×1080` untested.

See Alpha Ring Notes for the full static trace (string_id table, screen entry points, `FUN_1802ef650` layout-application mechanism) and the per-session validation log detail.

### CUI canvas scale: the whole-backbuffer reference space (`3840×1080` stretch)

**Known — mechanism.** Every per-player CUI screen (scoreboard, loadout, per-player pause) draws with `cameraMatrix (slot-derived) × Scale(backbufferW/1152, backbufferH/720)`, built by `FUN_1802f1890` (0x2F1890) and uploaded as shader constant `0x310004` by `FUN_1802f5310`. The camera term is correct and slot-aware (its projection scale is `2·near/width`/`2·near/height` from the render-view stack's slot rectangle, the same rectangle the D3D viewport is set from). The Scale term is not: it is a single global derived from the whole backbuffer with no slot or player-count term. A CUI element authored at `(u, v)` canvas units lands at `pixel_x = u·backbufferW/1152`, `pixel_y = v·backbufferH/720` regardless of which window is drawing.

The ratio `scaleX/scaleY = (W/H)×(720/1152)` is `1.1111` for *any* 16:9 backbuffer — the canvas's own 16:10→16:9 mapping, and the shape tags are authored against, which is why every 16:9 backbuffer renders correctly. At `3840×1080` the ratio is `2.2222` — exactly 2× too wide, matching the reported stretch. Content still fills the slot horizontally in both cases (a 2P Left/Right slot is always `backbufferW/2` wide and the scale tracks `backbufferW`), so the defect never presents as clipping, only stretch.

A second, differently-referenced backbuffer term exists at `0x2F234A` (scratch-target width, 16:9-referenced rather than 16:10) — not covered by this fix; no runtime symptom has been attributed to it.

**Production seam (`cui_canvas_scale.cpp`, gate `g_cuiUltrawideCanvasFit`).** Detours `0x2AAB24` at the confirmed per-player call site (return RVA `0x26CEE9`, `window < 4`, `playerCount >= 2`, `window < playerCount`). Substitutes a 16:9-equivalent width (`backbufferHeight × 16/9`) for `backbufferW` in the scale computation, only when the backbuffer is wider than 16:9. All modified globals — both scale floats and both recompute cache keys — are restored before the draw returns, byte-identical to what Reach would have held.

- **The wider-than-16:9 guard is required, not an optimization.** Without it, a backbuffer *narrower* than 16:9 (16:10, 4:3, a tall window) would be widened rather than corrected — at `1280×1024` the substitution would raise scaleX by ~42%. Every validated case is a no-op at 16:9 and a reduction above it; this failure mode is prevented by construction, not by runtime observation.
- 1P is excluded by design — it's affected by the same mechanism at a wider-than-16:9 backbuffer, but that's stock Reach behavior on every install, not a split-screen regression.
- **Validated in game (2026-09-17):** `1920×1080` 2P/3P Left/Right, 3P, 4P — no change. `3840×1080` 2P Left/Right, 3P Left/Right (all three players), 4P native quadrants — stretch fixed in every case. The 4P result matters beyond 4P: it validates the correction for a native split-screen layout, not only Left/Right, and is why the gate keys on split-screen rather than Left/Right specifically.

**Unknown:** whether aspect-correct content should also be rescaled to *fill* a wider window (it currently renders at its `1920×1080`-equivalent pixel size); behavior at intermediate aspects (a `2560×1061` observation was taken at `player_count=1`, outside the gate, so it measures stock Reach); the respawn/identity strip's vertical placement (untouched by this fix, `scaleY` is unchanged — candidates are a second projection at `0x2F326F` or the `0x2F234A` scratch-target term).

See Alpha Ring Notes for the disassembly this was derived from and the three-pass experiment history that found the correct gate (2P-only → all Left/Right → all split-screen, fixing a 4P miss along the way).

### Reticle and projected markers

**Retired**

- Writes to candidate HUD scale/FOV globals at `0xD1F6F0`/`0xD1F6F4` produced no visible reticle-position change.

**Open questions**

- Which of the five `ProjectHudMarkerToScreen` calls inside `UpdatePlayerHudView` (RVA `0x2E1430`) is the weapon reticle rather than a waypoint/navigation marker?
- Is the error introduced before projection, during per-slot projection, or in a later transform/upload?

**Resume point:** the diagnostic hook that would answer this — `hud_reticle.cpp`, detouring `OFFSET_HALOREACH_PF_PROJECT_HUD_MARKER`, distinguishing the five call sites by return address — was removed from the release branch as out-of-scope diagnostic infrastructure (reticle/third-person work is deferred past this release). It produced no behavior change; fully recoverable verbatim from git history (branch `vertical-splitscreen`) behind `AlphaRing::DebugFlags::g_hudReticle` to resume this question.

### Side-by-side HUD box centring

**Known.** `ComputeHudAnchorFrame` builds each full-height Left/Right half's HUD box against the whole screen's centre line: at 1920x1080 the left half's box starts 128 px in and ends 4 px from its inner edge, the right half's starts 4 px in (yInset 64 vs 80), so both HUDs crowd the middle of the screen, more so at 21:9 (reported by salty). `hud_anchor.cpp` already centred the box for quarter slots (semantic 2); the Left/Right branch (semantic 3) returned after its canvas-height fit, so it now centres too (`CentreHudBox`, gate `g_centreHudBox`). Box-verified in the kirklandsig fork at 1920x1080 and a 1920x822 (21:9) window: each half's compass at its view's centre, identical margins. Reach's motion sensor getting a per-player offset twice is under "Per-player HUD" below.

## Halo Reach ultrawide and dual-monitor viewport path

Static Ghidra/source trace of the stock two-player viewport pipeline, underlying every fix above.

### KNOWN

- **Backbuffer/client dimensions.** `FUN_180250f94` publishes backbuffer W/H to `DAT_180b43a90`/`DAT_180b43a94` at startup/resize. `FUN_18024f48c` separately reads `GetClientRect`, used by the camera projection to correct a client-vs-backbuffer aspect mismatch. The traced path has no monitor enumeration or per-monitor rectangle — a 32:9 panel and two 16:9 monitors exposing one 32:9 backbuffer are indistinguishable here.
- **Viewport construction.** `UpdateAllPlayerViews` (0xC33F8) → `FUN_18026c204` → `ComputeViewportRect_ClampedAndRaw` (0x287C5C), table entry `g_SplitscreenConfigTable[playerCount*4 + slot]` at `0xB43C40 + index*20`. Raw rect per axis: `left = trunc(W*x0)`, `right = trunc(W*x1)`, etc. — independent per slot, no equal-size enforcement. Also publishes normalized destination bounds at `frame+0x478..0x484` (`left/W`, `right/W`, `top/H`, `bottom/H`).
- **Viewport/scissor application.** `SetupPlayerView` (0xC31F4) → `FUN_1802505b0` sets both D3D viewport (`FUN_180253054`, vtable +0x160) and scissor (`FUN_180252c2c`, vtable +0x168) from the rebased rect, caching them at `DAT_184e09f78`/`DAT_184e09f80`. Offscreen passes can instead call `FUN_180274854`, which derives `(0,0,width,height)` from the *bound render target* — so a wrongly-sized RT produces a wrongly-sized offscreen viewport even with a correct table rectangle.
- **Camera/projection.** `FUN_1802884bc`/`FUN_1802881cc` compute the slot's pixel aspect from its table-derived width/height, correcting against `GetClientRect` if it disagrees with the backbuffer. The world-camera path does not require a 16:9 backbuffer and needs no D3D viewport hook for arbitrary table rectangles.
- **HUD/fixed-shape assumptions.** `UpdatePlayerHudView` (0x2D94BC) has a fixed target-shape rule: 4:3 when `res==1`, else 16:9 — the clearest conventional-aspect assumption in the traced path. The stock black-bar painter (0x2C6D84) is layout-specific (horizontal stacking, shared x-bounds, full-height bars) and cannot be reused unchanged for left/right regions. The stock render-target family (`FUN_1802663b8`) is shape-specific for the same reason.
- **Existing Alpha Ring interception:** `SplitscreenConfigStore::Apply()` persists entries 8/9; `splitscreen_rt.cpp` scales the shared target family and truncates the direct top-level derivation (see Render-target sizing); `blackbars.cpp` replaces/bypasses the two-player painter; `hud_anchor.cpp`'s geometry override and the D3D11 probes exist but the screen-dimension publishers and camera projection functions are untouched.

### SUSPECTED

- A dual-monitor mode should use left/right full-height entries so each slot's computed aspect matches one monitor's aspect (confirmed: `3840×1080` → two `1920×1080` slots).
- The `res` field conflates render-target variant, HUD content selection, and loadout/UI resource selection — a full-monitor left/right layout needs 16:9 geometry with `res=3` pillarbox semantics, so dual-monitor support decouples geometry from content selection rather than choosing a different rectangle alone.

### UNKNOWN

- Whether graphics descriptor dimensions and `GetClientRect` stay identical across every fullscreen/borderless/spanning/resize transition (directly relevant to the open manually-resized-window item above).
- Arbitrary unequal-width/height slots, gaps, overlaps, and bezel compensation are untested through final composition.

## Halo Reach split-screen FOV

**Status: complete, runtime-validated for 1P–4P.** Per-player split-screen FOV ships through Reach's own FOV baseline getter (`fov_baseline.cpp`, gate `g_splitscreenFovBaseline`). Checkbox unchecked = native Reach split-screen FOV; checked = that player's Alpha Ring FOV. 1P and VehicleFOV stay native.

### Known: mechanism

- Each local slot has an independent camera record: `slotRec = *(TLS_block+0x688) + slot*0x410`, `cameraInput = slotRec+0x154`. Render-time consumers read only `cameraInput+0x6C` (vertical FOV, derived from `+0x40` horizontal).
- **Writer chain:** `FUN_1800cad04` sets `+0x6C` from `+0x40`, called from `FUN_1800c9cf8` (per-slot observer result build, every tick) and the observer reset. `FUN_1800c9cf8` itself gets `+0x40` from **`FUN_1800c8554(useProfile)`**, Reach's FOV baseline getter, which returns horizontal radians.
- **The split-screen gate:** `FUN_1800c8554`'s MCC-profile branch runs only if local player count `< 2` (`CMP`/`JGE` at `0x1800c8591`), and even then resolves local user 0 only. With 2+ local players every slot — slot 0 included — falls through to the 78° global default. This is why maxing MCC's FOV slider has no split-screen effect natively.
- Camera modes (first-person zoom, orbit/third-person, scripted) all consume `+0x6C` downstream of this baseline; zoom and spring smoothing are layered on top, not replaced by the fix.

### Production seam (`fov_baseline.cpp`)

Detours `FUN_1800c8554`. Always calls the original first; its answer changes only when `useProfile != 0`, a slot context is active (0–3, via thread-local scopes opened by the director/observer per-tick loop detours), and local player count `>= 2`. Per-slot policy: override ON → clamped 70–120° Alpha Ring value; override OFF → native getter result unchanged. `deg × π/180` is the only value returned; nothing downstream is written, so world camera, marker projection, zoom, and screen effects all derive from the one value. VehicleFOV (`FUN_1800c8640`) is not hooked — split-screen vehicles use Reach's native split-screen vehicle FOV.

**Validated in game:** 1P native and live-slider-unaffected; 2P/3P/4P checkbox unchecked = native 78°/native split-screen FOV per slot; checked = independent per-slot values; mixed checked/unchecked in the same match independent; weapon zoom interpolates smoothly; nameplates/waypoints stay locked; vehicles native; settings persist across restart.

### Retired approach: render-side `+0x6C` override

An earlier prototype saved/restored `cameraInput+0x6c` directly around `UPDATE_PLAYER_FRAME`. Three failures, all explained by the writer chain above: (1) wrong mapping — the slider is horizontal degrees at 4:3, `+0x6C` is the already-derived vertical value; (2) choppy zoom — it replaced a value that already included weapon zoom and spring smoothing; (3) nameplate drift — the override only lasted for the world camera, and `ProjectHudMarkerToScreen` re-read the restored native value in the same frame. Do not re-attempt this approach.

### Remaining caveats

- No per-player vehicle FOV (would need the same slot context on `FUN_1800c8640`); split-screen vehicles presumably use Reach's native split-screen vehicle FOV, not the MCC slider — not measured.
- A possible one-time FOV ease at spawn or a camera-mode switch (observer reset / script switch seeding without a slot context) was not specifically tested.
- `FUN_18026fae4` and `FUN_18025d30c` (other `FUN_180287dfc` callers) are unaudited but read the same derived per-slot value.

## Halo Reach split-screen render-quality throttle

**Symptom:** Reach visibly lowers rendering quality as local player count rises, even with MCC's graphics settings maxed.

### Known: mechanism

`FUN_180270258` (0x270258) runs once per frame from `UpdateAllPlayerViews` (inside `OFFSET_HALOREACH_PF_RENDER`). Two composed stages, in order:

1. **Player-count record select.** `GetSplitscreenPlayerCount()` alone selects one 0x50-byte record at index `count-1` and copies it to the live block at `0xCA0240`.
2. **MCC quality-tier post-process.** `FUN_18003aef8` (its only caller) rewrites the live block in place: `field = clamp(field × tierMultiplier, lo, hi)` per category, gated by `DAT_180c1a100`.

The record selector prefers a tag block over a static DLL fallback table (`use_static` byte at `0x4E389B0`, measured `0` at runtime — the fallback table is dead data on this install; the tag path is confirmed live).

### Known: tier multipliers and field table

Each category reads one MCC setting byte in `0x29F5909`–`0x29F5912` (read-only in this DLL, no writer found — pushed in from the MCC host). `0` = low/off (also ORs feature-disable flag bits), `2` = high, else ×1.0 passthrough.

| setting byte | drives |
| --- | --- |
| `0x29F5909` | `anisotropy_level` |
| `0x29F590A` | cpu/gpu light counts and related fields |
| `0x29F590B` | `effect` |
| `0x29F590C` | `shadow_count`, `shadow_quality` |
| `0x29F590D` | `decorator`, `instance`, `object_fade`, `structure_lod` |
| `0x29F590F` | `water` |

Multipliers: high `3.0` (light-count group `10.0`), mid `1.0`, low `0.5` (water `0.3`). `decals` is **not** written by the tier pass at all — passes through from the record unscaled. `shadow_quality` has zero consumers in this DLL (dead field). Field name table (from debug setter `FUN_180270414`): `+0x04 water`, `+0x08 decorator`, `+0x0C effect`, `+0x10 instance`, `+0x14 object_fade`, `+0x20 decals`, `+0x24 structure_lod`, `+0x28 cpu_light_count`, `+0x30 gpu_light_count`, `+0x40 shadow_count`, `+0x44 shadow_quality`, `+0x48 anisotropy_level`.

**Known (measured, Enhanced preset):** at 2+ local players the record is **zero** for water, decorators, decals, dynamic lights, shadows — no multiplier rescues a zero, which is why maxing MCC settings doesn't help. `effect`/`instance`/`object_fade`/`structure_lod` do scale and partly compensate. Record values are context-dependent (differ between menu/lobby and in-map) — do not treat any single capture as the map-independent truth.

**Known:** every live-block consumer reads the live block, never the player count, so the downgrade is one preset switch, not independent overrides (flags word bit-tested in ~14 places). Reach-specific field names also appear in `halo3`/`halo3odst`/`halo4`/`groundhog` (not `halo1`/`halo2`); portability untested. Only the **per-game** MCC quality preset reaches Reach — the global preset alone has no effect (measured).

### Production seam — shipped, default off

5-byte patch at RVA `0x27025E`: `CALL GetSplitscreenPlayerCount` → `MOV EAX, 1`, pinning the selector's player-count input to 1 regardless of actual count. Byte-for-byte no-op at 1P. Shipped as the Dev Tools patch **"Splitscreen Render Quality (force 1P tier)"**, default off, persisted key in `alpha_ring_patches.cfg`.

**Validated in game (2026-09-18):** 2P patch off → zeroed detail record as expected; 2P patch on → 1P-tier record restored, live-block hash byte-identical between forced 2P/3P/4P (confirms the index really is pinned); 4P patch on → user-reported major visual improvement, acceptable performance. The MCC quality tier still composes on top of the forced record — the toggle decides *which* record Reach starts from, the MCC preset still decides how it's scaled.

**Not done:** a forced-2P tier as a middle setting for 3P/4P (deliberately deferred — same 5 bytes, would need mutual exclusion with the 1P patch). Open: a per-game Performance-preset capture (low/mid tier values are currently derived, not measured); frame-cost measurement; portability to other Halo titles.

**Known (2026-09-27): the patch makes a Reach split-screen fault hit sooner.** With the patch on and 4 local players, after repeated deaths and respawns (corpses piling up) players 3/4 saw Spartans and their own first-person weapon black or invisible (flashing on animation changes), and corpses flickered; what P1 looked at changed it. Reproduced on the test box with a real pad at The Package and confirmed by toggling the 5 bytes live: off restored P3/P4 at once, on broke them again. Writing the stock 4P record into the 1P record slot (patch on) also restored them; the 2P record did not. No single field is responsible: the shadow fields or the object/structure budget floats each push P4 black. Player records and unit objects are identical for all four players; `disable_render_state_cache_optimization` has no effect. At 2 local players the same symptoms appear after enough deaths with the patch off, so gating the patch by player count is not a fix: the cause is the per-frame skinning pool (next section), which the forced record fills sooner.

## Halo Reach split screen: later views stop drawing objects as bodies pile up

**Symptom** (XiaoDanny's report, reproduced 2026-09-27): after many deaths in one area, the later split-screen views draw Spartans and first-person weapons black, over-bright or not at all, and bodies flicker. The last view goes first (P4, then P3; P2 at 2 players), and what the earlier players look at changes it.

**Known:**
- It's Reach, not AlphaRing: it still happens with every AlphaRing Reach hook restored to its original bytes live, and with the render-quality patch off (the patch makes it hit sooner).
- It follows the world object count (bodies and dropped weapons pile up at about 6 objects per death). At The Package it breaks from ~440 objects at 4 players (fine at 412) and ~495 at 2 (fine at 466 and below). Measured with a temporary in-DLL harness that kills local player 2 through `unit_kill`'s worker `0x47CAA8(unit, 0, 1, 2, 0)` in bursts of 5 and screenshots each step.
- Reach's own collector holds the pile right at that level. `0x4FF1FC(mode 0)` starts collecting when any of these hold:
  - more than 120 objects are waiting (`[gc+4]`, the imm8 at `0x4FF314`);
  - object memory is low (`0xA990FC` / `0xA990F8`);
  - fewer than 102 free object slots remain.
- It then runs the strategy table at `0xB7393C` (0x28 per entry) while the pressure flags from `0x4FEE78` still hold. For garbage (more than 115 waiting, the imm8 at `0x4FEF10`), `0x4FF0A4` sorts the candidates' spatial groups by population (`0x4FEC3C`) and takes the earliest deadline in the first eligible group (`0x4FEF84`) - biggest pile first, not globally oldest. It works in three passes:
  1. flags 4: past its deadline (`+0x148`) and out of every player's sight (`0x474A2C`);
  2. flags 5: ignoring the deadline;
  3. flags 7: visible ones too.
- For garbage pressure, it stops as soon as 115 or fewer are waiting. So in split screen the pile sits at 115-120 garbage, about 500 objects at The Package, which is above the limit. (Lowering only the 120 live did nothing, because the 115 stop is checked first.)
- `garbage_collect_unsafe` (request word `[[TLS+0x140]+1] = 0x0101`, i.e. flags 3; `garbage_collect_now` is `0x0001`; consumer `0x47B76C` → `0x4FF1FC`) brings every view back at once. It purges every candidate, including weapons dropped a moment ago. TLS is the game thread's block: objects at +0x10, players at +0x18.
- Ruled out as the limit:
  - the render-state cache ("cached object render states", 1024 × 0x270 at the render thread's TLS +0x218, created by `0x256564`) holds 804 after 100 deaths at 4P;
  - no Blam data array is full in the broken state (all ~630 scanned);
  - `0x229558`'s 0x200 cap is a clipping vertex pool.

**The limit: the per-frame skinning pool.** Every skinned object drawn in a frame - in any view, and again for shadows - gets its bone matrices from one pool of `0x35C00` bytes (about 215 KB) at `0xC51C40`, reset every frame (`0x1D3843`):
- The allocator `0x25093C(size, flagged)` rounds the size to 4 and returns `frame << 18 | byte offset`, or -1 once it's full. `mov ecx, 0x35C00` is the limit; unflagged requests also leave the last `0x5000` bytes free. The used count is at `0x4E3888C`, the high-water mark at `0x4E38880`.
- Its callers ask for `bones x 0x30 + 0x48` bytes: `0x24B190` once per object per frame (unflagged, handle cached with the frame number) and `0x27EBE0` (flagged). A Spartan with its weapon takes ~3.7 KB, so the pool holds about 55 of them.
- Its users decode handles as `lea reg, [rip + pool]` + `and reg, 0x3FFFF`: ten `lea`s (one is the startup clear in `0xC390`) and ten masks in `0x24AF80`, `0x27EBE0`, `0x298A94`, `0x2993E8`, `0x299EF0`, `0x2A2400`. The pool is CPU-side only (no GPU buffer of that size; each draw copies its matrices into the `SkinningVS` constants), and nothing reads the frame bits back.
- Measured (temporary harness, 4P, stock collector): the high-water mark climbs with the pile - 136 K at 343 objects, 213 K at 425, the full 220 K at 462 - exactly where P3 and P4 break. A skinned draw that gets -1 is drawn without its bones, which is the black/over-bright/missing body or weapon; the views are drawn in order, so the last ones lose.
- How it was found: the render-state cache (461/1024 in the broken state) and memory diffs of `.data` didn't show it; a scan for bump allocators in the render code (a static counter loaded, advanced, compared and stored back) did - its used count swung between 16 K and 102 K within one healthy frame, against a high-water mark of 102 K.

**Fix (`haloreach/skinning.cpp`).** As `haloreach.dll` loads, a 4 MB buffer is allocated within 2 GB after the module (its users reach it RIP-relative), the ten `lea`s are pointed at it, the ten masks widened to `0x3FFFFF`, the allocator's limit raised to `0x400000` and its handle shift moved from 18 to 22 bits. These are same-length edits, found by scanning the code for `lea`s to the pool and for the masks in their functions' `.pdata` ranges; they're written in an order where every partial state is a working pool (the `lea`s, the handle shift, the masks, then the limit; the instruction cache flushed after each), and undone last first as the module unloads. A build laid out differently (not exactly 10 + 10) is left alone. Box-verified (kirklandsig fork) at 4P with Reach's stock collector (the pile at 115-120 garbage, 480-540 objects): every view drew its first-person weapon and the bodies, with the high-water mark at 329 K. The collector is left as it is, so bodies stay as long as they do in single player; the cost is drawing them (~45 fps at 4P with the full pile on the test box, GPU-bound).

## Per-player HUD (Halo CE, 2, 3, ODST, Reach)

**Known — mechanism.** MCC's host object (`CGameManager`) has three HUD methods the games call while drawing: `+0x2F8` `get_hud_element_anchor(element, int* anchor)`, `+0x300` `get_hud_element_transform(element, float* dx, float* dy, float* scale)` and `+0x308` `transform_hud_color(user, argb)`; MCC's own versions back a feature-flagged HUD editor and the colour-blind filter. Halo CE, 2, 3, ODST and Reach ask for a transform right before placing each HUD element (MCC element ids: 1 motion sensor, 2-5 grenades, 6 shield/health, 7 weapon, 9 crosshair, 11 equipment, 12 messages) and keep the player being drawn in a global; Halo 3, ODST and Reach also pass every HUD colour, with its user, through `+0x308`. `src/mcc/hud` answers for the drawing player, on top of MCC's answer.

| Game | Drawing player | Offset units |
| --- | --- | --- |
| Halo CE | `0x29AF2B8` (int16) | host offsets go to x only, so offsets are added by a hook on `hud_calculate_point` (`0xB56A58`) to each placed point, in the view being drawn (`0x29AF2F0`); scale and hiding still go through the host |
| Halo 2 | `0x165C398` | offset × unit scale (`0xE14F28`) × the element's scale = pixels, in the view's bounds (`0x165C298`, int16 t, l, b, r) |
| Halo 3 | `0xAD317C` | the HUD's virtual canvas (`0xAD3148`, float width, height) |
| ODST | `0xB1AA04` | canvas `0xB1A9D8` |
| Reach | `0xD1F71C` | canvas `0xD1F6F0` |

- **Reach asks for the motion sensor twice.** `UpdatePlayerHudView` (`0x2D94BC`) asks once per view (call at `0x2D96B7`) and bakes the answer into the radar centre the dish and blips draw from (`0xD1F6FC`...); the per-widget lookup `0x2D93A4` (widget anchor string_id → element: `0x80630` motion sensor → 1, `0x80631` → 2 grenades, `0x80632` → 6 shield, `0x80633` → 7 weapon, `0x80634` → 9 crosshair, `0x80635` → 11 equipment) asks again, returning to `0x2D9443`. That second ask is left to MCC, or a dish widget would get its offset and scale twice.
- **The motion sensor is asked for before its view's canvas is set.** Halo 3, ODST and Reach ask for element 1 while they set each view's HUD up, before they store that view's canvas (Halo 3 `0x2ED267`, canvas `0x2ED40C`; ODST `0x3287F5`, `0x328991`; Reach `0x2D96B7`, `0x2D984E`), so the canvas at that ask is still the previous view's - wrong wherever the views differ in size (3 players). The other elements are asked for while the view's widgets are placed (Halo 3 `0x2EDD80`, ODST `0x3292E4`, Reach `0x2D93A4`), after it. `Hud.cpp` keeps each user's canvas from those asks and gives the motion sensor's ask its own view's, as of the frame before. Halo 2 reads its view bounds right after each ask (`0x829490`), so it doesn't have this.
- **Halo 2 and "HUD anchor: Centered"** (the profiles' `LockMaxAspectRatio`, a global at `0x197EE40`, the last loaded profile's). On a screen wider than 16:9, `0x954DD0` insets the whole-screen HUD frame to a centred 16:9 box and `0x7E0A40` splits that box among the players, so every split-screen HUD bunches toward the middle of the screen and the crosshairs leave their views' centres. The embedded patch "HUD at screen edges" (`0x954DF2` `74` → `EB`, off by default) skips the inset - MCC's "HUD anchor: Edge" - with any number of players. Halo 3's `chud_draw_begin` (`0x2ED0D4`) insets each player's own view instead, so it doesn't bunch.

## Anniversary graphics with 3-4 players (Halo CE, Halo 2)

**Known — mechanism.** Both Anniversary (Saber3D) renderers draw split screen as exactly two stacked views: two cameras, render targets with two half-height children, a composite that puts each view's image on its half of the screen, and a good deal of per-view state with two slots. The 3-4 player mode (`module/entry/halo1/anniversary.cpp`, `halo2/anniversary.cpp`; Dev Tools' *Anniversary graphics with 3-4 players*, saved as `anniversary_quad`) keeps that two-view frame and alternates what is in it: players 1 and 2 on one frame, 3 and 4 on the next, so each view updates at half the frame rate. Without the choice, `Splitscreen::ClassicGraphicsScope` clears the Anniversary bit of the game options only while the engine copies them, so a 3-4 player mission starts in Classic and MCC's setting is left alone.
- **Halo CE:** quarter-size children are made beside every split render target as the game makes those (only while the choice is on, so turned on after Halo CE made its targets it takes effect the next time Halo CE loads) and swapped in for each frame; views 3 and 4 get cameras made with the renderer's own AddCamera, kept past its camera count so everything it bounds by that count stays at two views; each view is copied into its quadrant of an image of ours, which goes to the screen before the HUD. Three players get quarters, the fourth left black (the split-screen window hook lays three windows out as four).
- **Halo 2:** the game's glue only splits the screen for exactly two local players, so at its six `== 2` sites (by return address) the local player count answers 2 while the mode runs. Players 3 and 4 get camera copies of ours, each in its player's cell of Halo 2's own grid (with three players, player 1 keeps the top half). The game's composite quad (`0x1D2A00`) leaves no pixels with more than two players, so it's collapsed to a point and each view's image is drawn into its cell by `CellShaders.h`; the other pair's cells are kept in a texture of ours and put back before the HUD. With two players (no quad mode, the choice on or off), each view's image is drawn into its half by the same shaders: under megabitt01's D3D11 composite clip (#23, `render/d3d11/Hook.cpp`) the game's quad already placed the image in its half and the clip's viewport shrank it again, so each view came out a quarter of the screen tall. Until the shaders are made for the device, and for a view with no image, the game's composite runs as it does under the clip.

### Halo 2: each view drawn at its cell's size

Each frame draws two views, as two-player split screen does, and H2A's two-player split screen is itself GPU-bound at ~35 fps on the test box (82% GPU, 1080p); each view was drawn at full width and half height and squeezed into its quarter cell (30 fps with four players). What the renderer draws a view into:
- Its render targets are built by `0xF7750` (set*, name, ... 9 args): a set keeps its count at `+0xC` and 0x30-byte entries at `+0x10` (texture* `+0`, flags `+8`). A split target gets a `_split0` child (flags 0x20000000, full width, half height) that both views are drawn into in turn, and targets with per-view history (`__HALF_F16_SSR_PREV_FRAME`) a `_split1` (0x40000000). 48 such textures at 1080p (`__FULL_8888_n___split0`, `__HALF_*`, `__QUATER_*`, `__PC_Z_BUFFER___split0`, ...).
- The renderer sizes a view by its target, not by the camera's viewport (`camera+0x13C` x, `+0x140` y, `+0x144` width, `+0x148` height; `+0x154` field of view, `+0x158` aspect): shrinking the viewport alone drew the full target with the cell's projection (a cropped, zoomed picture, no speed-up).
- Texture objects: vtable `+0xA0` `bool (texture*, width, height, mips, format, depth, samples, data)` writes the description (`+0x20` width, `+0x22` height, `+0x24` depth, `+0x26` format, `+0x28` samples, int16; `+0x2A` mips, byte) and makes the resources (`+0xB8`) - but only while bit 27 of `+0x98` is clear; once set, a different size is refused (returns false). `+0xC8` releases the GPU resources and views. Reference count at `+0x78`.

**Fix (`QuarterViews`):** every `_split` texture is recorded as `0xF7750` adds it, and on the mode's frames (render thread, before drawing) each is remade at half width: release (`+0xC8`), clear bit 27, `+0xA0` with the new width, bit restored. Leaving the mode puts the widths back; a width the renderer changed itself (a new resolution) is taken as the new original. The listed cameras get their cell's viewport too. Box (kirklandsig fork): 4P at the same spot 30 fps @ 82% GPU before, 60 fps (vsync) @ 69% after, four samples; all four cells drawn whole.

**Texture lifetime.** A reference (`+0x78`) doesn't keep a texture: a set's teardown `0xF8750` drops its reference and calls the manager's remove `0x1A5AC0` (texture*, under the manager's critical section at `0x1AB4A98`), whose delete `0x1A5990` (manager*, index into `+0x220`, count `+0x228`) removes the children (`+0xC0`, `+0xC8`), releases the resources and runs the destructor whatever the count. A hook on `0x1A5990` takes the texture out of both lists first, under the lists' mutex, which the render thread holds while remaking. Nothing under `+0xA0`/`+0xB8`/`+0xC8` takes the manager's lock or deletes (static walk, three calls deep), so the delete thread waiting on the mutex can't deadlock. Box (kirklandsig fork): Save & Quit deleted all 48 tracked textures through the hook (on another thread than the render thread, which does the narrowing), and the mission reloaded in the same process narrowed 48 new ones. Halo CE has the same hazard: its texture manager's delete (`0x1F6BF0`, texture*[] at `+0x1B8`; a render target set's release `0x20B000` gets there) removes a texture's split children (`+0xA8`, `+0xB0`) first, so its hook puts the game's own children back in a parent before the parent goes and forgets any of ours that went.

### Halo 2: each view's exposure

The AHDR keeps, per HDR view (four), an entry at `0x1AB8600 + view * 0x4C` (settings the level loads for views 0-1 - `+0x18` is 150 there and 11 in 2-3, `+0x35` a flag; state: `+0x3C` measured luminance, `+0x40` exposure, `+0x44` adapted luminance, int `+0x48` frames) and two 1x1 staging textures (`0x1AB8758`, texture*[4][2], made by `0x20F670`). The HDR pass `0x210BF0` (tail-called from `0x1D1C60`, the view index being the listed camera's `+0x220` slot) calls the luminance pass `0x2100C0`: the 1x1 luminance (one chain for all views, `0x1AB8730..0x1AB8750`) is copied into `read_back[view][frames & 1]`, the frame count bumped, and `read_back[view][(frames - 1) & 1]` - the frame before's - mapped and read; the adapted luminance moves toward it and sets the exposure. With the pairs alternating, the frame before was the other pair's. Measured (player 3 standing still, player 1 turning between the sky and the ground): player 3's cell 70.6, 53.9, 65.1, 54.6, and player 1's sky blown out (98).

**Fix:** on the second pair's frames the HDR pass hook swaps in that pair's 16-byte adaptation state and views 2-3's read-backs (two views never use them) around the pass. After: player 3 62.4, 62.7, 62.8, 62.6; player 1's sky 87. Four other functions read a view's adapted luminance (`+0x44`, clamped, for effects' brightness): `0x14BD20`, `0x16FBA0`, `0x3CE210`, `0x4FD540`. A temporary probe (1200 quad frames) found none of them on the render thread inside the frame: `0x14BD20` (~650 calls a frame) and `0x16FBA0` (~12) run outside any frame, `0x3CE210` (~500) on worker threads, almost all while the other pair's frame is drawn (the next list being built), `0x4FD540` not at all there. So swapping for the whole frame would hand them the wrong pair; players 3-4's lists keep the first pair's value for those (**open**, minor: visible only when the pairs' exposures differ a lot). Redirecting them would take the list's pair in each reader. (A nearby player's charging plasma weapon lights the world and the other players' guns; that's normal lighting, not a leak between views.)

### Halo CE: why the four views aren't all drawn every frame

Three ways to draw all four each frame were tried or measured:

- **Four cameras in one list** (the pair's two plus the other pair's): crashes in the next commit - prepare frame `0x455170` -> `0x2E2480` -> `0x2B0090` (access violation at `+0x2B00EC` on a garbage refcount pointer). The renderer's light manager (global at `0x1BEA9D0`) keeps per-view light lists for exactly two views: 4 entries (count `+0x158 + view*0x488`, 0x120 bytes each) and 28 (count `+0xA68 + view*0x1F88`), view 1's second block ending where the next array begins (`+0x4978`); the per-view render job (`0x452EF0` -> `0x45A8E0`) indexes it by view. Objects' hidden flags (vtable `+0x328`/`+0x330`) and the first-person models are per slot, two slots too.
- **Rendering the frame twice**, swapping in the other pair between passes: each pass would need its own light lists (made in the commit, main thread), hidden flags and first-person models, all two-slot state the engine fills once per tick - not attempted.
- **Presenting only complete frames** (skip the present after players 1-2, so every shown frame has both pairs fresh): measured with the present skipped on alternate frames - presents dropped to 30/s but the loop stayed at 60 quad frames/s. MCC caps the loop at 60 by itself (its settings are in `SystemSettingsData.bin`; `GameUserSettings.ini`'s `bUseVSync` isn't read - Present still got sync interval 1), so each view would still update at 30 Hz.
- **Two game frames per MCC frame** (2026-09-29): MCC's loop (`0x1243C90`) calls the game's whole frame `0x87F90` (no arguments) through a pointer at `0x1243E34`; calling it twice in quad mode ran 60 game frames/s at 30 loops/s - each call takes the full 16.7 ms, waiting in the job system (`0xC2D50` -> `0xC2560`/`0xC19C0` at the frame's end, `0x899AD`) for MCC's frame. The pacing isn't Present (skipping every other present halves presents, the loop stays at 60) and isn't reachable from config: `Engine.ini` `[SystemSettings] t.MaxFPS`, `rhi.SyncInterval=0`, `r.VSync=0` and `[/Script/Engine.Engine] bSmoothFrameRate=False` changed nothing. MCC's executable carries UE4's RHI frame pacer (`rhi.SyncInterval`, "the RHI vsync thread"), which paces frames without a present - the likely source.

The 4P frame costs 53% GPU at 60 fps on the test box, so drawing both pairs would fit if the engine had four view slots; unlocking MCC's pacing for CE quad mode instead would need about twice the GPU time per shown frame (fine on the test box at best, too much for a Steam Deck).

### Halo CE: players 3-4's cameras and first-person models

Each frame the game thread commits the list the previous sync built (prepare `0x455170` with the commit fields set), then runs the sync `0x89F00` (under the Saber sync lock `0x1BA3AB8`, which the classic HUD pass `0x740B0` also takes: the whole game frame `0xAC29C0`, the first-person hand-off, the objects' sync), while a worker builds the next list and the render thread draws the committed one (render job `0x452740`, a job-graph node queued by `0x87F90`, frame `0x455A10`).
- **What the renderer copies when.** The per-slot first-person state (models' poses, per-view visibility) is taken with the commit, not while the list is built or drawn: with a temporary 0.3 ms stall injected before the sync's first-person hand-off, player 1's gun was in all 1200 frames.
- **Cameras.** The hand-over (`0x89B70`) hands views 0-1 over (`0xB29268` at `0x89D6A`/`0x89D81`), then queues the list build itself: `0x3BBA10` (`0x89DA7`) pushes the scene job `0x1BAA780` onto the worker pool (`0x1C33FA8`) and sets its event. The build (the prepare without the commit fields, `list_camera` `0x2EA720`) copies each view's camera. Views 2-3 handed over after the hand-over returned were too late: a temporary detector counted 2351 of 4200 copies of their cameras made before their hand-over (the frame before's camera; players 3-4's views jumped between two frames). Views 2-3 now go over from a hook on `0xB29268`, right after the game's view 1 and before the queue: 2 of 7200 early, both while player 3 was joining. (Making the build wait for the sync instead cost 0.33 ms of a worker per frame, and a build picked up after the next sync began would have overlapped that sync's resubmission of the same job, which the job system doesn't guard against.)
- **First-person models.** The renderer's first-person models (`0x2B050E8`: entry*, count `+8`; entry 0x20 {int slot, int object (the weapon's or arms' render model tag), model index `+8`, model* `+0x10`, camo `+0x18`, bytes `+0x1C`/`+0x1E` hidden in view 0/1 wanted, `+0x1D`/`+0x1F` applied}) are keyed by (slot, object). `0x7AC60(object, slot)` makes the entry on first use (`0x76170`), copies the pose from the node block of the view whose weapon (`0x1B7AA88` -> nodes `0x1C384A0`) or arms (`0x1B7AA98` -> `0x1C350A0`) the object is (int[4] and 0xD00 x 4), sets the wanted flags (slot 0 in view 0, any other slot in view 1) and the camo from `0xAB16A4(slot)`; its `cmp r15d, 1; ja` (`0x7AE64`) gives slots past 1 no nodes (a memcpy from null). The sync sets every entry's wanted flags hidden, hands over views 0-1's weapon and arms, then applies the changes per view (the model's vtable `+0x328`/`+0x330`). The rest of a first-person gun is addressed by local player index 0-3: its display (`0xB3E4F8` per tick over the four local players -> `0xAB30C0` -> `0x917C0` calls the model's Flash `asSetAmmo`, the model found by `0x91680(object, index)`; its caches, like `0x1B7B788`, have four entries), its events (`0x91440(object, name)`: the local player whose `first_person_weapons` (`0x2D9CD90`, 0x1E94 each) `+8` is the object, then entries of that slot), its camo. So players 3-4's displays and events found no model, and players 1 and 3 shared slot 0's: player 3's assault rifle showed player 1's round count.
- **Fix.** The second pair's syncs hand its models over as slots 2-3 (the `cmp` widened to 3 once the mode first runs, after its bytes are checked, and put back as the module unloads; the hook hands over only an object that is view 2/3's weapon or arms, and a missing view's object is -1), shown in views 0-1: slot 2's wanted flags are moved to view 0. The display, events and camo then find players 3-4's models by their own index. Box (kirklandsig fork): player 3's and player 1's rifles each count their own rounds through firing and reloading, no gun lost in 1200 frames with player 3 unspawned, a two-minute soak, leave/rejoin, Back to Classic and back.

### Back (Classic <-> Anniversary) in the middle of a mission

Both games switch graphics in place when a player presses Back, but their Anniversary renderers only draw two stacked views. With the choice on, the mode is set up for the whole 3-4 player mission, whichever graphics it starts in (`ClassicGraphicsScope`; Halo CE makes its extra cameras as split screen starts, `QuadReady`, without the on-screen test, so a Classic start works too), and runs while Anniversary is on screen: Halo 2's `0xE21280` nonzero and `0x1E8CE73` clear, Halo CE's `0x1B7AA84` (int, nonzero while Anniversary is on screen; `0x745B0` is `bool ()` "Classic", reading it - found by diffing halo1.dll's writable sections across Back presses: 5358 bytes flip with the renderer state, 6 are isolated 0/1 bytes, and this one is read at 42 sites). With the choice off, a 3-4 player mission switched to Anniversary shows what the renderers draw by themselves (Halo 2 black under the HUD, Halo CE two views). Halo CE / Halo 2 side by side is a Classic layout: `LeftRight::OnScreen` and each game's `ClassicShown` ask whether Classic is on screen, so Back to Anniversary shows the game's stacked views (2 players) or, with the choice on, the mode's views (3-4), and Back to Classic restores side by side. Box (kirklandsig fork, where the mode ran whatever the choice): CE and H2 at 2, 3, 4 players, side by side and stacked - Classic -> Anniversary -> Classic drew every view each time.

## Halo 4 side by side: the first-person camera's aspect factor

`0x34EC44` builds the first-person camera from the view's: its field of view is multiplied by `k` (the weapon's scale and zoom), times `S` = screen aspect (`0xE84608`/`0xE8460C`) over the view's (rect `+0x30..+0x36`): `mulss xmm5, xmm1` / `divss xmm5, xmm0` at `0x34ED92`/`0x34ED96`. The world camera (`0x374C84` -> `0x38F658` -> `0x38F3A4`) keeps a fixed vertical field of view. Stock split-screen views are wide (8:3, S = 0.667, keeping the gun's framing); a 960x1080 Left/Right half gives S = 2 - both arms and the whole gun in frame, drawn big. Halo 3 (`0x279BEC`) and Reach (`0x286C6C`) have no S.

**Fix (`halo4/splitscreen.cpp`, `OFFSET_HALO4_VIEWMODEL_ASPECT_DIVIDE`):** while Left/Right is on screen the divide's register byte goes `E8 -> E9` (`divss xmm5, xmm1`: by the screen's aspect, S = 1), after its four bytes are checked, switched from the frame hook before the frame is drawn (`Splitscreen::Frame`, on the same thread as `0x34EC44`), and restored otherwise and as the module unloads. Box (kirklandsig fork): 3 players side by side, the live byte `E9` with the fix and `E8` without in the same scene, the left view's gun at world scale.

## Halo 2 campaign: Save & Quit and players 3-4 at a mission start

### Save & Quit from a modded campaign with 2+ players

**Symptom** (SR388): Save & Quit sits on the level's loading screen forever with the "Halo 2 3/4 Player Coop Fixes" mod and 2+ players. The built-in campaign with 2 players and the mod with 1 player quit fine. Box states: a good quit reports Exiting (10) then 5 to MCC; the hung one reports Loading (0).

**Known:**
- MCC's main thread polls an object's outstanding count (`+0xC8`, set by 0xBA2934, cleared by 0xBA0D20 when the session state machine sees state 5) and waits; Halo 2's main loop keeps running frames (its thread sleeps in the frame limiter 0x6BFAB8), so nothing is deadlocked - the game just never exits.
- At the quit, Halo 2's main-loop quit handling (0x679CD0) asks `0x6A6320` - session object `[0xE80A78]`: `+8 == 1 && byte +0x2C8 != 0`, i.e. "this game came from a Halo 2 lobby" - at `0x679EE0`. True: `0x37040` -> `0x370E0(4)` requests a map change to Halo 2's main-menu map (options type 3), which never loads under MCC; false: `0x36F70` quits to the host.
- Traced with temporary hooks: `0x370E0(4)` fires from 0x679EEE right at the quit (pending map change 0 -> 1, flow state `[0x1A7CA44]` 2); `main_game_change` (0x34E40, quit = null options) isn't called.
- `0x6A6320` has 42 other callers, so it's left alone; only the quit decision is patched.

**Fix:** embedded patch "Save & Quit to MCC" (`OFFSET_HALO2_PF_QUIT_TO_LOBBY_TEST`, `call 0x6A6320` -> `xor eax, eax`, on by default): MCC is always the lobby. Box-verified in the kirklandsig fork: modded campaign, 2 and 4 players, Save & Quit -> Exiting, 5, back to MCC's menu.

### Starting places and the co-op respawn

**Known:**
- Players globals (`PV_RESPAWN` 0xE80A20) hold the int16 local count at `+8` and handle[4] at `+0xC`, both kept by `0x69E4A0`, which maps a player to a local index (count ++/--, player `+0x28` = its index). A mission's first ticks map the players one by one (1 of 4 at the first `players_update`).
- `players_update` (0x6A3910), in a campaign (the session object `[0xE80A78]`: mode `+8` == 1, as `0x6A6310` tests it; not MCC's game options - `copy_game_options` 0x39CE0 memcpys those, 0x2BF30 bytes, to 0x1A840A0, and their `+8` isn't the mode): a player without a unit and with flag 8 at `+6` spawns through `player_spawn` (0x69E580, returns bool) at the best-scored scenario starting location (scenario `+0x100` count, `+0x104` block, 0x34 each; scored by 0x7471D0, 0x747320 checks it); the others wait for the co-op respawn 0x6A1320, which picks a teammate out of combat ("Waiting to respawn (teammate in combat)"), calls `player_spawn` and moves the new unit beside that teammate.
- The built-in missions have two starting places, so players 3 and 4 waited until players 1 and 2 walked off theirs. Delta Halo's are about 1.5 km from the drop pod its intro leaves players 1-2 in: stock, player 3 spawned on player 1's starting place during the intro and was left there, about 490 units from players 1-2.
- Before a waiting player's co-op respawn, `players_update` asks `0x6A6320` (a co-op campaign: session mode `+8` == 1 and byte `+0x2C8`) and `0x6A5D40` (not with the Iron skull, `0x6EEF70(11)`, not on Legendary, session `+0x2C6` == 3). Where they say no, a player without a unit and without flag 8 never spawns, and `0x6A16F0` counts them as dead (players globals `+5`, read by `0x6A0BD0`; `+4` all dead, by `0x6A0BC0`); in a co-op campaign on Legendary or Iron any dead player starts the revert (`0x6A7493`: session `+0x1809` timer).
- Also in `player_spawn`: `player +0x34` holds a unit to take over (`player_set_unit` 0x69E2A0), 0x68CC40 fills the placement's change colours (player `+0x7C`, `+0xB8`), and `[players globals] +0xD8` is a spare unit placed at the location.

**Fix (`halo2/players.cpp`):** a hook on `players_update` clears flag 8 of a local player past the mission's starting locations (index at or past their count) who has no unit yet, so they come in through the co-op respawn beside a teammate, as after a death - only where both tests above say yes. Box: a fresh four-player Delta Halo start put players 3-4 1.8-2.4 units from player 1 at once; with three players, player 3 came in 2.4 units from player 1 at the drop pod once the intro was skipped. A co-op respawn gives what the game gives after a death (no grenades and 120 rounds, against 4 and 180 at a starting place). Without the Legendary/Iron test (the kirklandsig fork up to v2.0.1) the mission reverted to the checkpoint over and over there (box, Legendary 3P: player 3 never spawned, a revert every 30-45 s); with it, player 3 came in at once and nothing reverted.

## Hot join: local players in the middle of a mission

**MCC.** MCC counts local users through `CGameManager::get_xbox_user_id(index)` (true for index < count). When the count rises in the middle of a mission, Halo 3, ODST, Reach and Halo 4 sign the new user in and make the player, who spawns beside a teammate (Halo 4's deferred 3-4 player start relies on the same). When it falls, MCC ends the mission ("Leaving...", state 10 Exiting then 5), so only Halo CE lets players leave. Joins are taken only while the map runs (`CGameManager::running()`: from `set_state` Running until a map loads or the session exits). Resuming a checkpoint saved with more players than are signed in: Halo 3 exits about 1.3 s after Running (also with AlphaRing's split screen off); Reach stays black until the missing players join.

Halo CE and Halo 2 make their local players as a map loads, so a hot-join mission starts with four (`Splitscreen::HotJoinSlots`: `get_xbox_user_id` answers for all four slots) and each game's module holds the game's local player count at the players who joined.

**Joining (`splitscreen/Join.cpp`).** The Players window and hot join change the shared menu state (`MenuSettings::State()`, under `MenuSettings::Mutex()`), which the "Xbox 360" Menu and the Advanced Menu both show: a join puts the new player's controller in place, then the count, published last with a release store (`WriteRelease` on the split-screen settings' plain `int`, `PublishPlayerCount`); the game thread reads the count with acquire loads (`Splitscreen::PlayerCount`: the Halo CE and Halo 2 modules, `active_player_count`), so a reader that sees a new player sees their controller. The pad a player joins with gives the game nothing until its buttons and triggers are let go (`JoinPressHeld`, in `get_key_state`). On a running map the window's state takes each player's controller, and player 1's keyboard and mouse, live from the profiles and split-screen settings, which the Splitscreen window's Input, Bind and "Enable K/M for player1" edit: a rebound controller isn't free, and a join or a leave keeps it. A game start's `MenuSettings::ApplyAll(true)` puts the joined players in place from that state, `ApplySplitscreen(true)` keeping split screen on while hot join is chosen, and the next time a menu saves (`MenuSettings::Save`) it's written to `alpha_ring_menu.bin` with the rest.

**Bind and the pads.** The Splitscreen window's Bind has the controllers while its player's page was drawn waiting this frame or the last, with the overlay shown (`BindPending`): hot join and the Players window act on no press then, and read again from what's held afterwards; a Bind whose player is gone is dropped. Bind skips a controller still holding a press they acted on (a join, a leave, a species: `PressTaken`). That mark and the join press's are let go in `HotJoinPoll` each frame once the controller holds nothing (`LetGoTaken`), since the game's input poll doesn't read the pads while the overlay is shown (with "Disable Input", the default).

**A hot-join Halo CE or Halo 2 game** (`HotJoinHeld`: hot join chosen and the module there, until the session exits) has MCC holding all four local players from the mission's start, and MCC ends it if they go away. So the count changes there only by joining and leaving: the Splitscreen window's -/+ is held, and the "Xbox 360" Menu's player-count Increment/Decrement plays its denied sound and does nothing (`StateMachine::handleOption`).

### Halo CE (`halo1/hotjoin.cpp`)

**Known:**
- Players globals `+0xB4` is the local count, which views, HUD, input and cameras follow live; handle[4] at `+0xB8`, unit[4] at `+0xC8`. The module holds it and `PV_PLAYER_COUNT` at the joined count each `players_update` tick. An unjoined player's `player_spawn` (0xAD4184) is skipped, and a leaver's unit is detached (`player_set_unit(player, -1)`, 0xAD2404) before it's deleted (0xC579D4 skips players' units).
- A player's first spawn (`players_update` 0xAD0720's initial-spawn dispatch, 0xAD0B63 -> `player_spawn` 0xAD4184) is at a scenario starting location: `player_spawn` asks 0xAD39AC (`int16 (player)`: the best-scoring location, -1 if none scores above 0; scenario `+0x354` count, `+0x358` block, 0x34 each, scored by 0xAA7078 times a small random factor) and then 0xAD3940 (`entry* (int16 index)`: point, then facing at `+0xC`) for the entry it spawns at. The built-in missions have two (a30: both in the lifeboat). The co-op respawn (`players_update`, 0xAD0546) spawns a waiting player the same way and then moves their unit beside a teammate (0xAD058F -> 0xAD2FBC, `bool (player, other, -)`, true when it did).
- Players and their globals are game state (allocated through 0xACD75C), so going back to a checkpoint saved before someone joined brings back their record without a unit, and the initial-spawn dispatch puts them at the mission's start (the chooser called at 0xAD436C).

**Joining beside a teammate.** Each joined player's trail is kept: every 15 ticks (half a second), a spot on foot (within 2 units across and 0.25 up or down of the last sample) at least a stride (0.8) from the last spot kept, the newest 8 kept - so standing still doesn't flush them. During the `player_spawn` of a joiner whose placement is armed, 0xAD39AC returns 0 and 0xAD3940 a copy of entry 0 holding the newest trail spot within 10 units of its teammate now and at least 0.6 from every player, facing that teammate, so the body is made there directly. With no such spot yet (nobody has walked near), the joiner comes in at a starting place and is moved onto one as soon as there is one, for up to 300 ticks, with `object_set_position` (0xB359E8: object, point*, forward*, up*; null keeps it) and 0xBA83EC (what `object_teleport` resets after its move: HS evaluate 0xB196D0 -> 0xC57B9C). A player held back at any point in the mission is remembered until the next one starts (`s_held_back`), and whenever they have no unit their placement is armed again, so a checkpoint revert brings them back beside a teammate too; players who were in from the start are left alone. The placement is cleared when the spawn took the offered spot (`t_spot_taken`, set by the 0xAD3940 hook) or when the co-op respawn's move moved them (0xAD2FBC hooked, `OFFSET_HALO1_PF_PLAYER_MOVE_BESIDE`), so a player the game already put beside a teammate isn't moved again. Box (kirklandsig fork): players 2 and 3 appeared 1-2 steps behind player 1 on Halo's opening path.

**The Anniversary picture.** The renderer's split flag (`0x2E3B821`) is set only while the map loads (`0x66DF0`: `setg` on the local count > 1 at `0x6762A`) and applied by `set_split` (`0x4150F0`: adds or drops camera 1 and lays the views out; the only other writer, `0x67AB0`, clears it with a count of 2 around load/unload). A hot-join mission loads with four slots, so it stayed split for one player; the module's per-tick update sets the flag from the held count and applies it through `set_split` (`FollowSplit`).

### Halo 2 (`halo2/hotjoin.cpp`)

The module holds the int16 local count at players globals `+8` at the joined count. The game counts players as it maps them (in no set order over a fresh mission's first ticks) and unmaps them, so the hold follows the mapped handles (`+0xC`), not the first count: until every player who joined is mapped, the game keeps its own count. The players let in are taken once a tick, as `players_update` starts (`s_admitted`), and that one count is used by the flag-8 pass, the `player_spawn` hook (a player not let in doesn't spawn) and `Account`: a player who joins in the middle of a tick is let in on the next, after their record is sent to the co-op respawn - spawning first, they would take the mission's starting place (0x6A3B93). A player held back at any point in the mission is remembered until the next one starts and takes the co-op respawn whenever they have no unit (flag 8 cleared, as for players 3-4 above; on Legendary or with Iron, a starting place): players and their globals are game state (allocated through 0x15A0AF0's allocator), so a checkpoint saved before they joined brings back their record waiting for a starting place, which `players_update`'s dispatch (0x6A3B93 -> `player_spawn` 0x69E580) would honour at the mission's start. Box (kirklandsig fork): joiners 0.8-1.9 units from player 1.

**The players held back keep waiting for a starting place**, as players do before their first spawn: one without a unit who isn't waiting for one is a dead teammate to Halo 2 (`0x6A16F0` sets players globals `+5`), and `game_safe_to_save` (`0x7005C0`, at `0x70067E`) refuses every checkpoint while there is one. Waiting so, they count as alive in Halo 2's "every player is dead" (`+4`), which sends a mission whose players have all died back to its checkpoint, so after each `players_update` tick both bytes are worked out over the players who joined (`Account`). Outside a hot-join mission the hook does exactly what it did.

## Other durable findings

### `CPatch` backup-capture idempotency (generic, not Reach-specific)

**Known — root cause.** `CPatch`'s constructor seeds `m_backup` equal to `m_data` — a placeholder, not real stock, until `apply()` captures live bytes. `CModule::load_module` used to call `apply()` on an enabled patch **twice**: once via `setState()` restoring the saved "on" preference, then again, unconditionally, via `CPatchSet::apply()` sweeping every enabled patch. The first call correctly captured true stock into `m_backup` before overwriting `dst`; the second, redundant call re-captured `m_backup` from `dst` — which by then already held the *patched* bytes — silently replacing the correct stock backup with the patched pattern itself. A later `setState(false)` then "restored" `m_backup`, indistinguishable from the patched bytes, so nothing visibly changed. **This affects any `CPatch` whose saved-enabled state differs from its compile-time default, in any game module** — it produced the Halo Reach symptom "unchecking Remove Black Bar live does nothing" but is not specific to that patch.

**First fix (superseded): an idempotency guard.** `CPatch::apply()` was made idempotent while enabled — if the live target bytes already equalled `m_data`, it returned without writing and without touching `m_backup`. That fixed the double apply above, but `m_backup` still started as the placeholder. A patch that is **on by default and saved off** gets its first `apply()` from `setState(false)` in `load_module`, which wrote the placeholder — the patch bytes — as the "stock" bytes: the patch stayed applied while Dev Tools showed it off, and toggling could not undo it (enabling hit the guard, disabling wrote the placeholder again).

**Fix (generic, from the kirklandsig fork):** the module's own bytes are captured once per load, before anything is written: `CPatchSet::update()` (and `add()`, for a patch.xml entry added while the module is loaded) calls `CPatch::capture()`, which copies them into `m_backup` under an SEH guard, so a patch.xml offset outside the module just stays off. `apply()` never captures — it writes `m_data` when enabled and the captured bytes when disabled, so enabling and disabling can each be repeated in any order — and does nothing until a capture succeeded. `setState(false)` re-applies the other enabled patches, since restoring one patch's bytes also undoes any enabled patch overlapping it (`CPatchSet::clear()` deletes in a second loop for that reason).

### WTS wrapper and graphics tools

- The original wrapper forwarded only the WTS functions MCC itself called. RenderDoc's launch-time injection repeatedly faulted while that short surface was installed — the wrapper was expanded to cover the documented public WTSAPI32 export surface. Tooling compatibility, not game behavior.

### Logging discipline

- Hot hooks execute thousands of times per second. Unfiltered logging has produced a 25,000-line log, a 78,688-line log (last-value filter thrashing on alternating state), and a 164 MB log — plus a watchdog crash from sustained disk I/O.
- Use a bounded distinct-state set keyed by every dimension needed for coverage; a last-value filter fails when states alternate. Do not share one bounded pool across categories if early traffic can starve the target category.
- `src/log/DebugFlags.h` is the central gate. Flood-prone probes default off.

### Offsets across MCC updates (pattern mode)

**Known — mechanism.** Every module address lives in `lib/game/inc/<version>/offset_*.h` as `DefOffset(OFFSET_X, 0x…)`: an `AlphaRing::Offset` object that reads as its number (`module + OFFSET_X`). Plain `#define`s there are not addresses (entry values, counts). When a game module loads, `CModule::load_module` calls `Offsets::Prepare`: a module whose PE timestamp and image size match the build the headers were written for keeps the written values, with no scan. Any other build is scanned once (one pass over its code sections) for the byte patterns in `patterns_<module>.inc`, and each offset is either replaced by what was found or marked not found (value 0, `found()` false); every result goes to the log (`[Offsets]`). MCC's executable is the same, through `Hook::Initialize` (the Windows Store 1.3498 build keeps its own `OFFSET_MCC_WS_*` offsets); in an unknown executable AlphaRing stays off if any of its offsets isn't found, apart from two that nothing uses yet (`Hook.cpp` `Optional`; not the XUID lookup - without its hook a split-screen player changing team freezes the game).

What uses an offset that isn't found stays off: a hook isn't installed, and hooks whose detours reach other offsets are grouped per file (or namespace) with `EntryFeature(name, offsets their detours use)` (the hooks' own targets join automatically) - none of a group is hooked if one is missing, and a group that only works with another goes off with it (`Feature::Requires`: Halo 4's Left/Right needs its HUD fit, Halo CE's Anniversary 3-4 its split-screen hooks, Halo 2's hot join its co-op respawn hook); spawning and the per-player HUD carry a `Feature` per game; embedded patches aren't captured or written, and neither is one whose `needs` (`CPatch`) finds what it relies on missing - Reach's black-bar patches need its painter and CUI-variant hooks, Halo 2's four splitscreen patches go on only all together; `patch.xml` patches (fixed offsets) are off in any unknown build; `INVOKE`/`DefPtr` do nothing for a missing one; other paths check `Found({...})` / `.found()`. Reach's Left/Right layout goes on screen only where every hook that draws it is in (`SplitscreenConfigStore::LeftRightAvailable`); elsewhere a saved Left/Right is kept but the stock split is used. `tools/offsets/check_gates.py` fails when a file uses an offset outside such a gate. Hooks and patches keep a reference to their `Offset`, never a copy of its value: they are made during static initialization, before any lookup.

A pattern is a run of whole instructions with RIP displacements, call/jump targets, image-relative displacements and relocated bytes left out, unique in the module's code, at least 16 fixed bytes, kept inside one function. A function is found by its first instructions and by a call to it; a global by the instructions that address it (two different functions where possible, or a field of it in writable data); patched and hooked mid-function sites by patterns that also cover the code their patch or detour assumes (`gen_patterns.py` `COVER` after the site, `COVER_BEFORE` before it), and a return address by one that takes its call. Up to two per offset: a unique match that disagrees with the other, or lands in the wrong kind of section, makes the offset not found rather than guessed.

**Changing an offset:** edit the header, run `python tools/offsets/gen_patterns.py` against the installed build (it must be the headers' version; needs `capstone`, `pefile`, `numpy`), rebuild, run `build/Release/offset_test.exe` (every pattern of every offset must resolve exactly) and `python tools/offsets/check_gates.py`. A new patched site needs a `COVER` entry, and a mid-function hook whose detour rewrites what the code before it set up a `COVER_BEFORE` one. `python tools/offsets/perturb.py` fakes an update (code grown and shifted between functions with every relative reference fixed up, data moved, patterns broken or duplicated; `--churn F` also scrambles bytes in a fraction of all functions) and checks every offset is found where it moved or reported missing, never wrong. In game, `DebugFlags::g_forceOffsetLookup` runs pattern mode on the known build (every offset looked up and logged "at X (was X)").

**Not covered by patterns:** the Windows Store executable's offsets (no binary to generate from). Struct field offsets, vtable slot indices (`CGameManager::FunctionTable`) and `module_info_t` are layout assumptions a pattern can't check; an update that changes them needs new code regardless.

### Selected active offsets

These are research-facing RVAs for MCC 1.3528.0.0, not a substitute for `lib/game/inc/1.3528.0.0/offset_haloreach.h`.

| Role | RVA |
| --- | ---: |
| Per-player frame update | `0x26C204` |
| Camera basis (calls the projection consumer) | `0x2884BC` |
| Camera-basis projection consumer (`tan(fov*0.5)`) | `0x2881CC` |
| Camera-input record -> global scratch struct populate | `0x287DFC` |
| Camera-basis global scratch struct (`DAT_180c9fae0`) | `0xC9FAE0` |
| Native FOV baseline getter (`<2` local-player gate at `0xC8591`; production seam) | `0xC8554` |
| Native VehicleFOV baseline getter | `0xC8640` |
| Per-slot observer result build (writes `+0x40`, calls `+0x6C` writer) | `0xC9CF8` |
| `cameraInput+0x6C` writer (horizontal → vertical at 4:3) | `0xCAD04` |
| Director update loop / observer update loop | `0xC4458` / `0xC7F98` |
| Director per-slot pre-update / observer per-slot command copy (slot context) | `0xC5040` / `0xC88C8` |
| Reach profile mirror (4 × `0xAB8`; FOV `+0x8C`, VehicleFOV `+0x90`) | `0x2A03C50` |
| Camera aspect rect | `0x287F58` |
| Global HUD-layout subrecord selector (`FUN_1802d91bc`) | `0x2D91BC` |
| Per-widget record selector (`FUN_1802da0c0`) | `0x2DA0C0` |
| View-matrix builder | `0x28AF8C` |
| Inner player-frame update | `0x26C6DC` |
| Render-target pool init | `0x266F90` |
| Render-target pool release | `0x2670FC` |
| Render-target descriptor path | `0x266D60` |
| Transient scratch-target request (not the split-screen pool; see live layout switch) | `0x2D4220` |
| Scratch-target stack depth (`DAT_184e38ca8`) | `0x4E38CA8` |
| Render-target size computation | `0x2663B8` |
| Render-target create consumer | `0x2669E8` |
| CHUD constant upload | `0x271200` |
| CUI window resolution-variant selector (Ghidra name `GetLoadoutHudTemplateId`) | `0x2CA86C` |
| CUI window camera/render setup (Ghidra name `SetupLoadoutBackdropCamera`) | `0x2F307C` |
| CUI window draw entry (per-player call site `0x26CEE4`; windows 4/5 from `0x26FCE5`) | `0x2AAB24` |
| CUI canvas-scale + transform composer (`backbufferW/1152`, `backbufferH/720`) | `0x2F1890` |
| CUI transform upload (transpose to 3×4, shader constant `0x310004`) | `0x2F5310` |
| CUI command-list walker (inlined canvas-scale recompute at `0x2F1B8C`) | `0x2F1A2C` |
| CUI scratch-target width term, backbuffer aspect / (16:9) | `0x2F234A` |
| CUI canvas scale globals (`scaleX`, `scaleY`) | `0xB4BBD8` / `0xB4BBDC` |
| CUI canvas-scale cache keys (last backbuffer W, H) | `0x4E38C8C` / `0x4E38C94` |
| CUI canvas reference constants (`1152.0f` float, `720` **word**) | `0xA8B560` / `0xA35C42` |
| Perspective projection builder (`2·near/width`, `2·near/height`) | `0x383240` |
| Render-view stack array / top index | `0xC878A8` / `0xB43ABC` |
| Render-view push / pop | `0x251C08` / `0x251C50` |
| Per-player view object (`+0x38` = slot rect, `0xC9FB18`) | `0xC9FAE0` |
| Slot-rect → D3D viewport publisher (writes `DAT_184e09f78`) | `0x2505B0` |
| CUI screen open (screen, variant, theme) | `0x2C9EFC` |
| Scoreboard open (`scoreboard`) / loadout open (`player_loadout_menu[_half]`) | `0x2CAACC` / `0x2D3320` |
| Loadout template resolve (generic CUI screen build by variant) | `0x2EEB94` |
| Alternate loadout resolve | `0x2EEC90` |
| Loadout build | `0x2C9E00` |
| Split-screen render-quality throttle select (call site `0xC358E`) | `0x270258` |
| Throttle player-count call (5-byte seam) | `0x27025E` |
| MCC quality-tier post-process (only caller is the throttle tail) | `0x3AEF8` |
| Throttle live block (0x50 bytes) / static fallback table (4 × 0x50) | `0xCA0240` / `0xB43E40` |
| Throttle `use_static` byte (read-only, no writer) | `0x4E389B0` |
| MCC graphics setting bytes (read-only in this DLL) | `0x29F5909`–`0x29F5912` |
| Throttle debug setter / validator | `0x270414` / `0x270730` |

## Investigation priorities

1. **Scoreboard/loadout in Left/Right**: resolved for every full-height Left/Right slot. Open: other per-player CUI screens, resolutions other than `1920×1080`/`3840×1080`.
2. **Ultrawide CUI stretch**: resolved and productionized. Open: fill-vs-proportion question, intermediate-aspect behavior, 1P (excluded by design), respawn/identity-strip vertical placement.
3. **Reticle attribution**: identify which of the five per-slot marker-projection calls is the weapon reticle. Resume via `hud_reticle.cpp` from git history.
4. **Ultrawide rounding**: closed. Mechanism linking the (fixed) 1px error to visible corruption remains unestablished — see Alpha Ring Notes for the eliminated candidates.
5. **Vertical release criteria**: loadout visibility, reticle placement, and ultrawide lighting still need explicit results. Release scope is fixed to `1920x1080`/`2560x1440` Top/Bottom and normal Left/Right — see "Release verification scope".
6. **Left/Right pyramid mismatch**: closed as arithmetic-only, not a release blocker. Do not apply the `8131a31` truncation fix to `scaled()`.
7. **Top/Bottom corruption at manually-resized window heights**: open, deferred past release. Start at the resize lifecycle / `GetClientRect`-vs-backbuffer path, not render-target sizing. Needs a capture with resolution, log, and screenshot.
8. **Split-screen render-quality throttle**: resolved and productionized as a default-off Dev Tools patch. Open: a per-game Performance capture, a forced-2P middle tier, frame-cost measurement, portability to other titles.
9. **Death-screen nameplate**: completely untraced — no code path identified.
10. **Controller-to-slot mapping in 3P Left/Right, drop-in 2P→3P join**: not checked.

For each run, record MCC version, commit/dirty state, display resolution, player count, layout/res value, slot, scenario, probe flags, expected discriminator, and observed result.

## Superseded and contradictory notes

- The old handoff described detached HEAD at `bdad7eb`; consolidation occurred on `vertical-splitscreen`.
- The old `src/mcc/settings/Settings.*` path is gone.
- “More than four players is impossible” exceeded the evidence; simple source array/loop expansion is what failed.
- Render-target allocation, viewport sizing, `res` propagation, and loadout creation were once candidates for the vertical-HUD distortion. They are now fixed or retired for the remaining defects as described above.
- `LNK4098` was formerly described as harmless without qualification. Treat any recurrence as a build-configuration warning to investigate, not a permanently safe condition.

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
- The `Two-player layout` combo in `Module.cpp` is the supported control; a prior unused `g_verticalSplitToggle` flag that gated nothing was removed 2026-09-17.
- **`ResolveActiveLayout(playerCount)` always returns Native for 1P and 4P**, regardless of the saved Left/Right preference — the preference persists across player counts, but every Left/Right-keyed behavior (bar suppression, HUD semantic override, render-target sizing) must gate on the *active* layout, not the saved one. An earlier version gated on the saved preference alone and forced the 2P pillarbox HUD record onto a full-screen solo slot; fixed by adding this scope check. Any new Left/Right-conditional code must use `ResolveActiveLayout`/`UsesFullHeightLeftRightSlot`, never the raw preference.
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

## Other durable findings

### `CPatch` backup-capture idempotency (generic, not Reach-specific)

**Known — root cause.** `CPatch`'s constructor seeds `m_backup` equal to `m_data` — a placeholder, not real stock, until `apply()` captures live bytes. `CModule::load_module` used to call `apply()` on an enabled patch **twice**: once via `setState()` restoring the saved "on" preference, then again, unconditionally, via `CPatchSet::apply()` sweeping every enabled patch. The first call correctly captured true stock into `m_backup` before overwriting `dst`; the second, redundant call re-captured `m_backup` from `dst` — which by then already held the *patched* bytes — silently replacing the correct stock backup with the patched pattern itself. A later `setState(false)` then "restored" `m_backup`, indistinguishable from the patched bytes, so nothing visibly changed. **This affects any `CPatch` whose saved-enabled state differs from its compile-time default, in any game module** — it produced the Halo Reach symptom "unchecking Remove Black Bar live does nothing" but is not specific to that patch.

**Fix (shipped, generic):** `CPatch::apply()` is now idempotent while enabled — if the live target bytes already equal `m_data`, it returns success without writing and without touching `m_backup`. A genuinely differing write (first enable, or a live re-apply immediately after fresh stock bytes were just written) still captures backup and writes exactly as before. No call sites or other subsystems changed. Validated in game: a bars-removed preference now toggles correctly live, cold launch, and across restart.

### WTS wrapper and graphics tools

- The original wrapper forwarded only the WTS functions MCC itself called. RenderDoc's launch-time injection repeatedly faulted while that short surface was installed — the wrapper was expanded to cover the documented public WTSAPI32 export surface. Tooling compatibility, not game behavior.

### Logging discipline

- Hot hooks execute thousands of times per second. Unfiltered logging has produced a 25,000-line log, a 78,688-line log (last-value filter thrashing on alternating state), and a 164 MB log — plus a watchdog crash from sustained disk I/O.
- Use a bounded distinct-state set keyed by every dimension needed for coverage; a last-value filter fails when states alternate. Do not share one bounded pool across categories if early traffic can starve the target category.
- `src/log/DebugFlags.h` is the central gate. Flood-prone probes default off.

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

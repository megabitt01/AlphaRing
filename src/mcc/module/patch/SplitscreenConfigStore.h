#pragma once

// Persists the Splitscreen Config Editor's live-memory table edits
// (c_splitscreen_config::m_config_table, x0/y0/x1/y1/resolution per slot)
// across game restarts - same on-disk pattern as PatchConfig.h, just keyed by
// (table index, field name) instead of (module, patch name) since the table
// itself has no config file section of its own.
namespace AlphaRing::SplitscreenConfigStore {
    // c_splitscreen_config::m_config_table layout in haloreach.dll. Table index
    // is (block * SLOT_COUNT + slot), where block is the player-count variant
    // (0: alias of 4p, 1: 1p, 2: 2p, 3: 3p, 4: 4p).
    constexpr __int64 TABLE_OFFSET = 0xB43C40;
    constexpr int     ENTRY_SIZE   = 20;
    constexpr int     BLOCK_COUNT  = 5;
    constexpr int     SLOT_COUNT   = 4;
    constexpr int     ENTRY_COUNT  = BLOCK_COUNT * SLOT_COUNT;

    enum class TwoPlayerLayout {
        TopBottom = 0,
        LeftRight = 1,
    };

    void Load();

    // Returns true and fills out_value if a saved value exists for this
    // index/field pair; returns false (out_value left untouched) otherwise.
    bool Get(int index, const char* field, float& out_value);

    // Records the value in memory and rewrites the config file immediately.
    void Set(int index, const char* field, float value);

    // Resolves and persists the split layout preference through the two-player
    // entries 8/9 and three-player entries 12-14 (the name predates 3p).
    // TopBottom restores the shipped entries once and removes their saved
    // overrides; LeftRight saves and immediately applies the side-by-side
    // regions (2p: two full-height halves; 3p: player 1 on the full left half,
    // players 2/3 splitting the right half). It never touches the user's
    // black-bar patches or painter setting - after selecting TopBottom the
    // caller re-applies any enabled black-bar patch over the stock bytes.
    //
    // GetTwoPlayerLayout() is the SAVED PREFERENCE only. It stays LeftRight in
    // 1p/4p games, so a game-side hook must not treat it as "Left/Right is on
    // screen now" - use ResolveActiveLayout() for that.
    TwoPlayerLayout GetTwoPlayerLayout();
    void SetTwoPlayerLayout(TwoPlayerLayout layout, __int64 hModule);

    enum class ActiveLayout {
        Native,
        LeftRight,
    };

    // The layout actually in effect for a live local-player count, which the
    // caller must read on the game thread (GetSplitscreenPlayerCount is
    // TLS-rooted and returns 1 outside gameplay). This is the one place that
    // knows which player counts have a Left/Right implementation: 2 and 3.
    ActiveLayout ResolveActiveLayout(int playerCount);

    // True while Left/Right is active and this slot's Left/Right region is a
    // full-height half: both 2p slots and 3p slot 0. 3p slots 1/2 are stock
    // quadrants. Pure layout geometry; HUD (res=3 semantic, canvas fit) and
    // CUI (half -> quarter variant) corrections both key on it.
    bool UsesFullHeightLeftRightSlot(int playerCount, int slot);

    // Incremented only when the published TwoPlayerLayout actually changes,
    // after the new geometry is in the table. Both layouts share res=3, so the
    // render-target pool cannot tell them apart by variant; this generation is
    // what signals that the shared surface needs rebuilding.
    unsigned GetLayoutGeneration();

    // Forgets every saved field for one table index, so Apply() stops
    // re-asserting it and the game's own per-level reset is left to stand.
    // Needed by the layout resolver: switching back to top/bottom
    // has to REMOVE the saved 8.*/9.* entries, not overwrite them with stock
    // values, or the store would keep fighting the game every frame.
    void ClearEntry(int index);

    // Writes every saved value into the live table, skipping any field whose
    // live bytes already match. Safe and cheap to call every frame: in the
    // steady state it is a handful of 4-byte compares and no writes at all.
    //
    // This has to run repeatedly, not once. The game re-initialises
    // m_config_table to its shipped values on every level load (confirmed
    // in-game 2026-09-12: a second match started without re-applying comes up
    // as stock splitscreen), so a one-shot restore at module-load time is
    // erased before the first frame of any match is drawn.
    void Apply(__int64 hModule);
}

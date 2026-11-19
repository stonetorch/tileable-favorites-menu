# TFME 1.1.0 validation

## Baseline and packaging

The existing fork points to `stonetorch/tileable-favorites-menu`, with `origin/main` as its default branch. The workspace started on `dev` at `62839c2`, based on upstream version 1.0.6 at `cefbd9b`. Implementation is on `codex/tfme`. The MIT and bundled dependency notices are included in the staged package. A GitHub search returned no open issues in the upstream repository at the time of validation.

The unmodified x64 Release built successfully with the host's shared CommonLibVR 4.18.0 and vcpkg installation. The original Build-Debug.ps1 also built the extended DLL with those dependencies once the host granted access to the shared vcpkg directory. Its DLL is preserved under `build/release` as `TileableFavoritesMenu-baseline.dll`. Baseline SHA-256 is `3674B47F800E8E1073185821D26BA8AAC21A82D0D36AE348D87AB305F7CE5C89`. TFME uses the existing Scaleform movie and carries the original layout record alongside its versioned grid and recovery records.

## Interaction decisions

- Right-drag measures displacement from the press point in whole cell pitches on each axis. A 10 px drag threshold distinguishes a drag from a click. The grid step changes with 0.60-cell hysteresis. An obstructed resize outlines the last legal rectangle and identifies the collision or boundary. Hold Shift at press to shrink the indicated sides.
- Size copying uses an inside band of up to 24% of the tile dimension, capped at 35% of one cell pitch. Entering one edge band immediately previews the copied dimensions and the adjacent placement. Releasing commits the preview. Corners use ordinary dragging. The adjacent rectangle must fit in the target region.
- Dropping onto one tile sends it to the closest legal rectangle, including space the drag frees. The search uses the tile's current dimensions first and falls back to one cell with its preferred size retained. If the destination page has no free cell, the covered tile follows the normal overflow rules for its category.
- Paint regions is a separate mode. Holding left mouse for 220 ms in the grid starts a rectangular selection, including on occupied cells. Existing Global cells are teal. Changed cells and displaced visible tiles receive outlines in the draft. Apply and Cancel operate on the whole draft.
- Free-rectangle search is top to bottom and left to right. Existing legal positions are reserved before displaced tiles search. Placement respects the complete rectangle and keeps the desired size on the tile if it must drop to 1 ¡Á 1. Explicit organization retries a pinned tile's preferred size at its current location when space permits.
- Category moves carry every page with the category. Category deletion rehomes the affected favorites through current rules. The page cleanup command accepts empty pages only and leaves a category's final page in place. Empty automatic pages are retained until explicitly cleaned up.
- Q/E is consumed during Favorites before the native close and activation handlers. It changes pages during a move drag and keeps the tile attached until release or cancel. Escape cancels a gesture or editor draft; Tab retains close. Short mouse clicks call the upstream action path on release. The up-front keyboard menu, controller selection and game-input branches are preserved alongside the grid controls.

The final Release linked the pinned CommonLibSSE-NG v6.6.3 (2fcadbdf), its OpenVR submodule and MinHook v1.3.4. This local build disabled CommonLib IPO and reused the installed vcpkg packages. The standalone Release test took about 21 ms for 500 incremental insert-and-reconcile operations and about 1.5 ms for 100 unchanged refreshes of those 500 favorites. These timings measure the layout model only.

## Acceptance status

| Scope | Verification |
| --- | --- |
| P0 baseline | Unmodified Release built. Live baseline run pending. |
| P1 grid | Grid sizing and 1 ¡Á 1 placement covered offline. Window presentation at 4K, high DPI and ultrawide pending. |
| P2 gestures | Rectangle growth, bounds, collision, imitation and desired-size recovery covered in model tests. Mouse timing, Shift, preview appearance and cancel in the game pending. |
| P3 regions | Arbitrary masks, cross-region rejection and Global overflow covered in model tests. Mouse selection and visual stability across page changes pending. |
| P4 categories | Routing, multi-route fallback, category reordering/deletion and contiguous page allocation covered offline. Q/E interception and page indicators pending in the game. |
| P5 placement | Existing-page reuse, overflow, zero Page cells, pinned positions, sorting, empty-page cleanup and large inventories covered offline. Save/reload, version rollback and full menu latency pending in the game. |

The available WuBingV4/NewWorld1 installation reports Skyrim SE 1.5.97, SKSE 2.0.20 and SKSE Menu Framework 3.14.0. The selected framework exports the ImGui functions used by the editor. The computer-use tool did not receive access to Mod Organizer. At the user¡¯s direction, the TFME Release DLL was deployed for their in-game test. The previous DLL was backed up. SHA-256 checks confirmed the deployed binary and verified the INI, SWF, mod list, plugin list and load order were preserved. Skyrim AE and VR are enabled at build time. Upstream reports testing the original mod on SE 1.5.97 and AE 1.6.1170. TFME runtime compatibility still needs verification on each platform. [Upstream version and dependency notes](https://www.nexusmods.com/skyrimspecialedition/mods/186393)

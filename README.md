# Tileable Favorites Menu Extended

TFME replaces Skyrim's native/SkyUI favorites list with a mouse-first tile grid. Use Skyrim's normal favorite and unfavorite commands to manage its contents.

## Install

Install the Release archive as a mod in MO2. It replaces Tileable Favorites Menu and uses the same DLL, INI and SWF paths. Keep only one version active. For an upgrade, retain your existing INI to keep preferences such as pause, icons and close-on-selection. Keep the baseline mod and an original Skyrim save and matching SKSE co-save for rollback.

Requires the SKSE and Address Library for your runtime, plus SKSE Menu Framework. Icon mode uses the included SWF. Mesh Rendering Framework is required only for the optional 3D preview mode. See the [upstream compatibility notes](https://www.nexusmods.com/skyrimspecialedition/mods/186393) and the [TFME validation record](docs/TFME-Validation.md).

## Controls

| Input                                                          | Result                                                                           |
|----------------------------------------------------------------|----------------------------------------------------------------------------------|
| Q in gameplay                                                  | Opens Favorites through the native game binding                                  |
| Q or E in Favorites                                            | Previous or next page                                                            |
| Category badge or page dot                                     | First page of that category or the exact page                                    |
| Tab                                                            | Closes Favorites                                                                 |
| Short left click or right click                                | Upstream action, with right click retaining left-hand selection where applicable |
| Left-drag from a tile                                          | Moves the tile and pins its new position                                         |
| Right-drag from a tile                                         | Grows from the edges indicated by the drag direction                             |
| Shift + right-drag                                             | Shrinks from the indicated edges                                                 |
| Edit, then hold a dragged tile near another tile's inside edge | Previews copying the destination tile's dimensions                               |
| Edit, then Paint regions                                       | Click a cell or drag a rectangle to toggle each selected cell between Page and Global |
| Apply preview or Cancel preview                                | Commits or discards the proposed changes                                         |
| Escape during a gesture or edit                                | Cancels the gesture or draft, or exits editing                                   |

Use **Edit** to change window bounds, row and column counts, density, gaps and navigation. Turn off **Fit cells to window** to set the preferred cell size at 1080p. The grid scales down to fit if the panel is smaller than the requested footprint. Hide the settings panel while painting over cells underneath it.

**Categories and pages** manages labels, badge text, category order, matching item types and page sorting. Multiple categories may accept the same item type. New favorites search matching categories in navigation order before creating a page in the first match. **Selected tile** can pin a tile, reset it to 1 × 1 or move it to the page you navigate to. Empty pages can be removed explicitly. **Undo**, **Redo** and **Restore backup** are in the editor.

Layout, navigation preferences and category settings are saved with the current save's SKSE co-save. Save in Skyrim after applying edits. Opening a legacy layout imports its rectangles to the grid where possible and retains the original record for rollback. Restore backup uses the previous serialized TFME layout, reconciled with the active save's current favorites.

## Build

On this workstation, use the existing build script. Close Skyrim and the SKSE launcher before deploying.

```powershell
./scripts/Build-Debug.ps1          # Build Debug
./scripts/Build-Debug.ps1 -Deploy  # Build and deploy Debug to the existing WuBingV4 mod
```

Deployment backs up the previous DLL under `build/deployment-backups`, verifies SHA-256 and replaces only the DLL. `-DeployTo` selects a different existing mod directory. See [the build instructions for agents](AGENTS.md) for shared vcpkg permission errors.

For a portable Release package, configure and build the Release preset from a Visual Studio 2022 x64 developer environment with CMake, Ninja and vcpkg available.

```powershell
cmake --preset release -DOUTPUT_FOLDER=build/staging/TFME
cmake --build --preset release
```

The package uses the checked-in `TileableFavoritesIcons.swf`. The C++ build and packaging need no Flash compiler.

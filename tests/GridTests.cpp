#include "Grid.h"
#include "Types.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <tuple>

using namespace TFM;

void Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

GridItem Item(unsigned id, Category category = Category::spell, std::string name = {}, int value = 0)
{
    return {id, 0, category, std::move(name), value};
}

Placement Tile(const Grid& grid, unsigned id)
{
    const auto* tile = grid.Find({id, 0});
    Check(tile != nullptr, "Missing favorite");
    return *tile;
}

void CheckLayout(const Grid& grid, std::size_t favoriteCount)
{
    Check(grid.tiles.size() == favoriteCount, "Favorite was lost or duplicated");
    std::set<std::pair<unsigned, unsigned>> keys;
    std::set<std::tuple<bool, unsigned, unsigned, int, int>> occupied;
    unsigned missing = 0;
    for (const auto& tile : grid.tiles) {
        Check(keys.emplace(tile.form, tile.instance).second, "Duplicate identity");
        if (!grid.Inside(tile.cells)) {
            ++missing;
            continue;
        }
        const auto loc = tile.location;
        if (!loc.global) {
            Check(loc.group < grid.groups.size() && loc.page < grid.groups[loc.group].pages.size(),
                "Invalid page location");
        }
        for (int y = tile.cells.y; y < tile.cells.y + tile.cells.h; ++y) {
            for (int x = tile.cells.x; x < tile.cells.x + tile.cells.w; ++x) {
                Check(grid.GlobalAt(x, y) == loc.global, "Tile crosses an area boundary");
                Check(occupied.emplace(loc.global, loc.global ? 0 : loc.group, loc.global ? 0 : loc.page, x, y).second,
                    "Overlapping rectangles");
            }
        }
    }
    Check(missing == grid.missing, "Incorrect number of unplaced favorites");
}

void TestRoutingAndPages()
{
    Grid grid;
    grid.SetDimensions(2, 1);
    std::vector items{Item(1), Item(2, Category::scroll), Item(3, Category::shout)};
    grid.Reconcile(items);
    Check(grid.groups[1].pages.size() == 2, "Full page did not get a continuation");
    Check(Tile(grid, 3).location == Location{1, 1, false}, "Shout did not enter the magic category");
    items.push_back(Item(4));
    grid.Reconcile(items);
    Check(grid.groups[1].pages.size() == 2 && Tile(grid, 4).location.page == 1,
        "Existing space must be used before allocating another page");
    items.push_back(Item(5));
    grid.Reconcile(items);
    Check(grid.groups[1].pages.size() == 3 && Tile(grid, 5).location.page == 2, "Missing third page");
    Check(grid.groups[2].name == "Consumables", "Continuation split the category list");
    auto manual = Tile(grid, 1);
    manual.location = {2, 0, false};
    Check(grid.Move(manual), "Manual cross-category placement failed");
    items.push_back(Item(6));
    grid.Reconcile(items);
    Check(Tile(grid, 1).location == manual.location && Tile(grid, 1).cells == manual.cells,
        "Automatic routing overrode a manual destination");
    Check(grid.MoveGroup(2, 1) && Tile(grid, 1).location.group == 1, "Category move did not carry positions");
    Check(grid.DeleteGroup(2), "Could not delete populated category");
    grid.Reflow(items);
    CheckLayout(grid, items.size());
    for (unsigned category = 0; category < kCategoryNames.size(); ++category) {
        Check(std::ranges::any_of(grid.groups, [&](const auto& group) { return group.accepts[category]; }),
            "Deleting a category lost a default destination");
    }
}

void TestGlobalAndShrink()
{
    Grid grid;
    grid.SetDimensions(3, 3);
    grid.Toggle({0, 0, 3, 1});
    grid.Toggle({0, 1, 1, 2});
    std::vector items{Item(1), Item(2), Item(3)};
    grid.Reflow(items);
    auto tile = Tile(grid, 1);
    tile.location.global = true;
    tile.cells = {0, 0, 3, 1};
    tile.wantedWidth = 3;
    Check(grid.Move(tile), "Manual Global placement failed");
    grid.Toggle({1, 0, 2, 1});
    grid.Reflow(items);
    tile = Tile(grid, 1);
    Check(tile.location.global && tile.cells.w == 1 && tile.wantedWidth == 3, "Global failed to shrink with size retained");
    grid.Toggle({0, 0, 1, 3});
    grid.Reflow(items);
    tile = Tile(grid, 1);
    Check(!tile.location.global && tile.location.group == 1 && !grid.notice.empty(), "Global overflow was not routed");
    CheckLayout(grid, items.size());

    grid.Toggle({0, 0, 3, 3});
    const auto pages = grid.PageCount();
    grid.Reflow(items);
    Check(grid.missing == items.size() && grid.PageCount() == pages, "Page-free mask created pages");
    grid.Toggle({0, 0});
    grid.Reflow(items);
    Check(grid.missing == 0 && grid.PageCount() == pages + 2, "Opening Page space did not recover favorites");
    CheckLayout(grid, items.size());
}

void TestResizeImitateAndRestore()
{
    Grid grid;
    grid.SetDimensions(5, 4);
    std::vector items{Item(1), Item(2)};
    grid.Reflow(items);
    auto first = Tile(grid, 1);
    first.cells = {1, 1};
    Check(grid.Move(first), "Move to empty cell failed");
    auto grown = first;
    grown.cells = {0, 1, 2, 2};
    Check(grid.ResizePreview(grown) == grown.cells, "Legal diagonal resize blocked");
    grown.wantedWidth = grown.wantedHeight = 2;
    Check(grid.Move(grown), "Resize commit failed");
    auto second = Tile(grid, 2);
    second.cells = grown.cells;
    second.wantedWidth = second.wantedHeight = 2;
    const auto copy = grid.Imitate(second);
    Check(copy && copy->cells.w == 2 && copy->cells.h == 2 && copy->cells != grown.cells,
        "Size imitation reused the occupied source rectangle");
    auto beyond = grown;
    beyond.cells = {0, 1, 2, 8};
    Check(grid.ResizePreview(beyond) == Cells{0, 1, 2, 3}, "Edge resize did not stop at legal rectangle");

    grid.SetDimensions(5, 1);
    grid.Reflow(items);
    Check(Tile(grid, 1).cells.h == 1 && Tile(grid, 1).wantedHeight == 2,
        "Page shrink changed the expected size");
    items.pop_back();
    grid.SetDimensions(5, 4);
    grid.Reflow(items, Location{1, 0, false});
    Check(Tile(grid, 1).cells.h == 2, "Explicit organize failed to restore a legal footprint");
    CheckLayout(grid, items.size());
}

void TestSortingAndMultipleRoutes()
{
    Grid grid;
    grid.SetDimensions(3, 1);
    grid.groups[0].accepts[static_cast<unsigned>(Category::spell)] = true;
    grid.groups[0].pages[0].sort = Sort::name;
    std::vector items{Item(1, Category::spell, "Bravo", 30), Item(2, Category::spell, "Zulu", 80)};
    grid.Reflow(items);
    auto pinned = Tile(grid, 1);
    Check(grid.Move(pinned), "Could not pin position");
    items.push_back(Item(3, Category::spell, "Alpha", 100));
    items.push_back(Item(4, Category::spell, "Charlie", 10));
    grid.Reconcile(items);
    Check(Tile(grid, 1).cells == pinned.cells && Tile(grid, 3).cells.x < Tile(grid, 2).cells.x,
        "Name sort reordered a pinned tile or ignored a new title");
    Check(Tile(grid, 4).location.group == 1 && grid.groups[0].pages.size() == 1,
        "An existing mapped category with room was skipped");
    grid.groups[0].pages[0].sort = Sort::value;
    grid.Reflow(items, Location{0, 0, false});
    Check(Tile(grid, 3).cells.x < Tile(grid, 2).cells.x, "Value sort failed");
    grid.groups[0].pages[0].sort = Sort::original;
    grid.Reflow(items, Location{0, 0, false});
    Check(Tile(grid, 2).cells.x < Tile(grid, 3).cells.x, "Original order did not return");
    CheckLayout(grid, items.size());
}

void TestLargeRefresh()
{
    Grid grid;
    grid.SetDimensions(7, 4);
    std::vector<GridItem> items;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned id = 1; id <= 500; ++id) {
        items.push_back(Item(id, Category::spell, std::to_string(id)));
        grid.Reconcile(items);
    }
    const auto inserts = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < 100; ++iteration) {
        grid.Reconcile(items);
    }
    const auto refreshes = std::chrono::steady_clock::now();
    std::cout << "500 incremental inserts: "
              << std::chrono::duration<double, std::milli>(inserts - start).count()
              << " ms; 100 unchanged refreshes: "
              << std::chrono::duration<double, std::milli>(refreshes - inserts).count() << " ms\n";
    CheckLayout(grid, items.size());
    items.erase(items.begin() + 30, items.begin() + 200);
    grid.Reconcile(items);
    CheckLayout(grid, items.size());
    const auto oldCount = grid.PageCount();
    Check(!grid.DeleteEmptyPage(Tile(grid, 1).location), "A populated page was deleted");
    Check(grid.DeleteEmptyPage({1, 2, false}) && grid.PageCount() + 1 == oldCount, "Empty-page removal failed");
    CheckLayout(grid, items.size());
}

int main()
{
    TestRoutingAndPages();
    TestGlobalAndShrink();
    TestResizeImitateAndRestore();
    TestSortingAndMultipleRoutes();
    TestLargeRefresh();
    std::cout << "Grid acceptance tests passed\n";
}

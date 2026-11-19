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

void TestAdjacentDrop()
{
    Grid grid;
    grid.SetDimensions(8, 5);
    const Location page{1, 0, false};
    const Placement source{1, 0, {0, 0}, 1, 1, page};
    const Placement target{2, 0, {3, 2, 2, 1}, 2, 1, page};
    grid.tiles = {source, target};
    for (const auto& [edge, expected] : {
        std::pair{TileEdge::left, Cells{1, 2, 2, 1}},
        std::pair{TileEdge::right, Cells{5, 2, 2, 1}},
        std::pair{TileEdge::top, Cells{3, 1, 2, 1}},
        std::pair{TileEdge::bottom, Cells{3, 3, 2, 1}}
    }) {
        const auto drop = grid.AdjacentDrop(source, target, edge);
        Check(drop && drop->tile.cells == expected && drop->tile.wantedWidth == 2 && !drop->displaced,
            "Size imitation did not align with the selected edge");
    }
    grid.Toggle({5, 2});
    Check(!grid.AdjacentDrop(source, target, TileEdge::right), "Size imitation crossed a region boundary");
}

void TestOccupiedDrop()
{
    Grid grid;
    grid.SetDimensions(7, 1);
    const Location page{1, 0, false};
    const Placement source{1, 0, {1, 0, 2, 1}, 2, 1, page};
    const Placement target{2, 0, {5, 0, 2, 1}, 2, 1, page};
    grid.tiles = {source, target, {3, 0, {3, 0}, 1, 1, page}};
    auto incoming = source;
    incoming.cells = target.cells;
    const auto drop = grid.DropPreview(incoming);
    Check(drop && drop->displaced && drop->displaced->cells == source.cells,
        "Displaced tile did not use the vacated rectangle before shrinking");

    auto invalid = *drop->displaced;
    invalid.cells = incoming.cells;
    Check(!grid.Move(TileDrop{drop->tile, invalid}) && Tile(grid, 1).cells == source.cells && !Tile(grid, 2).pinned,
        "Invalid paired move partially changed the grid");
    Check(grid.Move(*drop) && Tile(grid, 1).cells == target.cells &&
        Tile(grid, 2).cells == source.cells && Tile(grid, 2).pinned, "Could not commit the previewed swap");
    CheckLayout(grid, 3);

    auto singleCell = source;
    singleCell.cells = {1, 0};
    grid.tiles = {singleCell, target, {3, 0, {2, 0, 2, 1}, 2, 1, page}};
    auto cover = singleCell;
    cover.cells = {5, 0};
    const auto fullSize = grid.DropPreview(cover);
    Check(fullSize && fullSize->displaced && fullSize->displaced->cells == Cells{0, 0, 2, 1},
        "A nearby free cell took precedence over a full-size rectangle");
    grid.tiles.push_back({4, 0, {0, 0}, 1, 1, page});
    const auto fallback = grid.DropPreview(cover);
    Check(fallback && fallback->displaced && fallback->displaced->cells == Cells{4, 0} &&
        fallback->displaced->wantedWidth == 2, "Fallback did not use the nearest cell and retain the preferred size");

    grid.Toggle({1, 0, 2, 1});
    auto globalSource = source;
    globalSource.location.global = true;
    grid.tiles = {globalSource, target, {3, 0, {3, 0, 2, 1}, 2, 1, page}};
    const auto crossRegion = grid.DropPreview(incoming);
    Check(crossRegion && crossRegion->displaced && crossRegion->displaced->location.global &&
        crossRegion->displaced->cells == source.cells && grid.Move(*crossRegion),
        "Displaced tile did not use the vacated rectangle across the region boundary");
    CheckLayout(grid, 3);
}

void TestCrossPageDrop()
{
    Grid grid;
    grid.SetDimensions(3, 1);
    const Location first{1, 0, false};
    const Location second{1, 1, false};
    const Location third{1, 2, false};
    grid.groups[1].pages.resize(3);
    const Placement source{1, 0, {0, 0, 2, 1}, 2, 1, second};
    const Placement target{2, 0, {0, 0, 2, 1}, 2, 1, third};
    const Placement blocker{3, 0, {2, 0}, 1, 1, third, true};
    grid.tiles = {source, target, blocker};
    std::vector items{Item(1), Item(2), Item(3)};

    auto incoming = source;
    incoming.location = third;
    const auto overflow = grid.DropPreview(incoming);
    Check(overflow && overflow->overflow && overflow->displaced &&
        overflow->displaced->Key() == target.Key(), "A full destination page did not trigger overflow");
    Check(grid.Move(*overflow), "Cross-page drop could not reserve the incoming tile's footprint");
    grid.Reconcile(items);
    Check(Tile(grid, 2).location == first && Tile(grid, 2).cells == target.cells &&
        Tile(grid, 1).location == third && Tile(grid, 3).cells == blocker.cells &&
        grid.groups[1].pages.size() == 3, "Overflow did not honor the target group's existing page order");
    CheckLayout(grid, items.size());

    auto crossCategorySource = source;
    crossCategorySource.location = {0, 0, false};
    grid.groups[1].pages.resize(1);
    grid.tiles = {crossCategorySource, target, blocker};
    grid.tiles[1].location = grid.tiles[2].location = first;
    incoming.location = first;
    const auto newPage = grid.DropPreview(incoming);
    Check(newPage && newPage->overflow && grid.Move(*newPage), "Drop from a different category did not trigger overflow");
    grid.Reconcile(items);
    Check(Tile(grid, 2).location == second && Tile(grid, 2).cells == target.cells &&
        grid.groups[1].pages.size() == 2 && grid.groups[1].pages[1].automatic,
        "A full target category failed to create a continuation page for the displaced tile");
    CheckLayout(grid, items.size());

    crossCategorySource.cells.w = 1;
    grid.tiles = {crossCategorySource, target, blocker};
    grid.tiles[1].location = grid.tiles[2].location = first;
    incoming.cells.w = 1;
    const auto local = grid.DropPreview(incoming);
    Check(local && !local->overflow && local->displaced && local->displaced->location == first &&
        local->displaced->cells == Cells{1, 0} && local->displaced->wantedWidth == 2,
        "A free cell on the destination page did not take priority over overflow");
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
    TestAdjacentDrop();
    TestOccupiedDrop();
    TestCrossPageDrop();
    TestSortingAndMultipleRoutes();
    TestLargeRefresh();
    std::cout << "Grid acceptance tests passed\n";
}

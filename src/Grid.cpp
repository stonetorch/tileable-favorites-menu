#include "Grid.h"
#include "Types.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace TFM
{
    namespace
    {
        unsigned Slot(Location location)
        {
            return location.global ? 0 : 1 + location.group * kPageLimit + location.page;
        }

        std::uint64_t KeyOf(std::uint32_t form, std::uint16_t instance)
        {
            return (static_cast<std::uint64_t>(form) << 16) | instance;
        }

        auto KeyOf(const auto& item) { return KeyOf(item.form, item.instance); }
        auto KeyOf(ItemKey key) { return KeyOf(key.formID, key.uniqueID); }

        bool Overlaps(Cells a, Cells b)
        {
            return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
        }

        class Occupancy
        {
        public:
            explicit Occupancy(const Grid& grid) : grid_(grid) {}

            bool Fits(const Placement& tile) const
            {
                if (!grid_.Inside(tile.cells)) {
                    return false;
                }
                const auto slot = slots_.find(Slot(tile.location));
                const auto [x, y, w, h] = tile.cells;
                for (int row = y; row < y + h; ++row) {
                    for (int column = x; column < x + w; ++column) {
                        const auto cell = row * grid_.settings.columns + column;
                        if (grid_.GlobalAt(column, row) != tile.location.global ||
                            (slot != slots_.end() && slot->second[cell])) {
                            return false;
                        }
                    }
                }
                return true;
            }

            void Add(const Placement& tile)
            {
                auto& cells = slots_[Slot(tile.location)];
                if (cells.empty()) {
                    cells.resize(grid_.mask.size());
                }
                const auto [x, y, w, h] = tile.cells;
                for (int row = y; row < y + h; ++row) {
                    for (int column = x; column < x + w; ++column) {
                        cells[row * grid_.settings.columns + column] = true;
                    }
                }
            }

            bool FindSpace(Placement& tile, bool shrink = true, bool keepPosition = true) const
            {
                if (FindRectangle(tile, keepPosition)) {
                    return true;
                }
                if (!shrink || (tile.cells.w == 1 && tile.cells.h == 1)) {
                    return false;
                }
                tile.cells = {0, 0, 1, 1};
                return FindRectangle(tile, false);
            }

            void FindNearest(Placement tile, Cells area, Cells origin,
                std::optional<Placement>& result, int& distance) const
            {
                for (int y = area.y; y <= area.y + area.h - tile.cells.h; ++y) {
                    for (int x = area.x; x <= area.x + area.w - tile.cells.w; ++x) {
                        const auto dx = x - origin.x;
                        const auto dy = y - origin.y;
                        const auto candidateDistance = dx * dx + dy * dy;
                        tile.cells.x = x;
                        tile.cells.y = y;
                        if (candidateDistance < distance && Fits(tile)) {
                            result = tile;
                            distance = candidateDistance;
                        }
                    }
                }
            }

        private:
            bool FindRectangle(Placement& tile, bool keepPosition) const
            {
                if (keepPosition && Fits(tile)) {
                    return true;
                }
                for (int y = 0; y <= grid_.settings.rows - tile.cells.h; ++y) {
                    for (int x = 0; x <= grid_.settings.columns - tile.cells.w; ++x) {
                        tile.cells.x = x;
                        tile.cells.y = y;
                        if (Fits(tile)) {
                            return true;
                        }
                    }
                }
                return false;
            }

            const Grid& grid_;
            std::unordered_map<unsigned, std::vector<bool>> slots_;
        };

        bool Valid(const Grid& grid, Location loc)
        {
            return loc.global || (loc.group < grid.groups.size() && loc.page < grid.groups[loc.group].pages.size());
        }

        Occupancy OccupiedByOthers(const Grid& grid, const Placement& tile, const Placement* displaced = nullptr)
        {
            Occupancy occupied(grid);
            for (const auto& other : grid.tiles) {
                if (other.Key() != tile.Key() && (!displaced || other.Key() != displaced->Key()) &&
                    (Slot(other.location) == Slot(tile.location) ||
                        (displaced && Slot(other.location) == Slot(displaced->location))) && grid.Inside(other.cells)) {
                    occupied.Add(other);
                }
            }
            return occupied;
        }
    }

    ItemKey Placement::Key() const { return {form, instance}; }
    ItemKey GridItem::Key() const { return {form, instance}; }

    Grid::Grid() :
        mask(settings.columns * settings.rows),
        groups{{"Equipment", "EQ"}, {"Magic", "MA"}, {"Consumables", "CO"}, {"Miscellaneous", "MI"}}
    {
        groups[0].accepts[static_cast<unsigned>(Category::equipment)] = true;
        for (const auto type : {Category::spell, Category::scroll, Category::shout}) {
            groups[1].accepts[static_cast<unsigned>(type)] = true;
        }
        for (const auto type : {Category::potion, Category::poison}) {
            groups[2].accepts[static_cast<unsigned>(type)] = true;
        }
        groups[3].accepts[static_cast<unsigned>(Category::miscellaneous)] = true;
    }

    bool Grid::GlobalAt(int x, int y) const
    {
        return Inside({x, y}) && mask[y * settings.columns + x] != 0;
    }

    bool Grid::Inside(Cells cells) const
    {
        return cells.w > 0 && cells.h > 0 && cells.x >= 0 && cells.y >= 0 &&
            cells.x <= settings.columns - cells.w && cells.y <= settings.rows - cells.h;
    }

    const Placement* Grid::Find(ItemKey key) const
    {
        const auto tile = std::ranges::find_if(tiles, [&](const auto& entry) { return entry.Key() == key; });
        return tile == tiles.end() ? nullptr : &*tile;
    }

    bool Grid::Fits(const Placement& tile) const
    {
        return Valid(*this, tile.location) && OccupiedByOthers(*this, tile).Fits(tile);
    }

    std::optional<Placement> Grid::Imitate(Placement tile) const
    {
        const auto occupied = OccupiedByOthers(*this, tile);
        if (Valid(*this, tile.location) && occupied.FindSpace(tile, false)) {
            return tile;
        }
        return {};
    }

    std::optional<TileDrop> Grid::AdjacentDrop(Placement tile, const Placement& target, TileEdge edge) const
    {
        tile.location = target.location;
        tile.cells = target.cells;
        tile.wantedWidth = tile.cells.w;
        tile.wantedHeight = tile.cells.h;
        switch (edge) {
        case TileEdge::left: tile.cells.x -= tile.cells.w; break;
        case TileEdge::right: tile.cells.x += tile.cells.w; break;
        case TileEdge::top: tile.cells.y -= tile.cells.h; break;
        case TileEdge::bottom: tile.cells.y += tile.cells.h; break;
        }
        return Fits(tile) ? std::optional{TileDrop{tile, {}}} : std::nullopt;
    }

    std::optional<TileDrop> Grid::DropPreview(Placement tile) const
    {
        const auto* source = Find(tile.Key());
        if (!source || !Valid(*this, tile.location) || !Inside(tile.cells)) {
            return {};
        }
        const auto destinationSlot = Slot(tile.location);
        const auto sourceSlot = Slot(source->location);
        const bool reuseSourceRegion = source->location.global != tile.location.global &&
            Inside(source->cells) && Valid(*this, source->location);
        const Placement* displaced = nullptr;
        Occupancy occupied(*this);
        for (const auto& other : tiles) {
            const auto slot = Slot(other.location);
            if ((slot != destinationSlot && !(reuseSourceRegion && slot == sourceSlot)) ||
                other.Key() == tile.Key() || !Inside(other.cells)) {
                continue;
            }
            if (slot == destinationSlot && Overlaps(tile.cells, other.cells)) {
                if (displaced) {
                    return {};
                }
                displaced = &other;
            } else {
                occupied.Add(other);
            }
        }
        if (!occupied.Fits(tile)) {
            return {};
        }
        if (!displaced) {
            return TileDrop{tile, {}};
        }

        occupied.Add(tile);
        const auto nearest = [&](Placement candidate) {
            std::optional<Placement> result;
            int distance = std::numeric_limits<int>::max();
            occupied.FindNearest(candidate, {0, 0, settings.columns, settings.rows}, displaced->cells, result, distance);
            if (reuseSourceRegion) {
                candidate.location = source->location;
                occupied.FindNearest(candidate, source->cells, displaced->cells, result, distance);
            }
            return result;
        };

        auto destination = nearest(*displaced);
        if (!destination && (displaced->cells.w != 1 || displaced->cells.h != 1)) {
            auto singleCell = *displaced;
            singleCell.cells.w = singleCell.cells.h = 1;
            destination = nearest(singleCell);
        }
        if (destination) {
            return TileDrop{tile, destination};
        }
        return TileDrop{tile, *displaced, true};
    }

    std::optional<Cells> Grid::ResizePreview(const Placement& tile) const
    {
        const auto* original = Find(tile.Key());
        if (!original) {
            return {};
        }
        auto candidate = tile;
        const bool extendLeft = tile.cells.x < original->cells.x;
        const bool extendUp = tile.cells.y < original->cells.y;
        candidate.cells = original->cells;
        auto result = candidate.cells;
        while (candidate.cells != tile.cells) {
            auto next = candidate.cells;
            const auto dw = (tile.cells.w > next.w) - (tile.cells.w < next.w);
            const auto dh = (tile.cells.h > next.h) - (tile.cells.h < next.h);
            next.w += dw;
            next.h += dh;
            next.x = extendLeft ? original->cells.x + original->cells.w - next.w :
                std::min(tile.cells.x, next.x + 1);
            next.y = extendUp ? original->cells.y + original->cells.h - next.h :
                std::min(tile.cells.y, next.y + 1);
            candidate.cells = next;
            if (!Fits(candidate)) {
                break;
            }
            result = next;
        }
        return result;
    }

    unsigned Grid::PageCount() const
    {
        unsigned count = 0;
        for (const auto& group : groups) {
            count += static_cast<unsigned>(group.pages.size());
        }
        return count;
    }

    void Grid::Toggle(Cells area)
    {
        if (!Inside(area)) {
            return;
        }
        for (int y = area.y; y < area.y + area.h; ++y) {
            for (int x = area.x; x < area.x + area.w; ++x) {
                mask[y * settings.columns + x] ^= 1;
            }
        }
    }

    void Grid::SetDimensions(int columns, int rows)
    {
        columns = std::clamp(columns, 1, kGridLimit);
        rows = std::clamp(rows, 1, kGridLimit);
        std::vector<std::uint8_t> next(columns * rows);
        for (int y = 0; y < std::min(rows, settings.rows); ++y) {
            for (int x = 0; x < std::min(columns, settings.columns); ++x) {
                next[y * columns + x] = mask[y * settings.columns + x];
            }
        }
        mask = std::move(next);
        settings.columns = columns;
        settings.rows = rows;
    }

    void Grid::EnsureRoutes()
    {
        for (unsigned category = 0; category < kCategoryNames.size(); ++category) {
            if (std::ranges::none_of(groups, [&](const auto& group) { return group.accepts[category]; })) {
                groups.front().accepts[category] = true;
            }
        }
    }

    void Grid::Reflow(std::span<const GridItem> items, std::optional<Location> organize, bool wholeGroup)
    {
        notice.clear();
        missing = 0;
        needsValidation = false;
        const bool hasPageCells = std::ranges::find(mask, 0) != mask.end();
        std::unordered_map<std::uint64_t, const GridItem*> favorites;
        for (const auto& item : items) {
            if (item.form) {
                favorites.try_emplace(KeyOf(item), &item);
            }
        }

        auto previous = std::move(tiles);
        tiles.clear();
        tiles.reserve(favorites.size());
        std::unordered_set<std::uint64_t> seen;
        Occupancy occupied(*this);
        std::vector<Placement> pending;
        std::unordered_set<unsigned> sortPages;

        const auto place = [&](const Placement& tile) {
            occupied.Add(tile);
            tiles.push_back(tile);
        };

        const auto requested = [&](const Placement& tile) {
            return organize && !tile.location.global && tile.location.group == organize->group &&
                (wholeGroup || tile.location.page == organize->page);
        };

        // Reserve all still-legal positions before searching for displaced tiles.
        for (const bool pinned : {true, false}) {
            for (const auto& tile : previous) {
                if (tile.pinned != pinned || !favorites.contains(KeyOf(tile)) || !seen.insert(KeyOf(tile)).second) {
                    continue;
                }
                if (Valid(*this, tile.location) && occupied.Fits(tile) && (pinned || !requested(tile))) {
                    place(tile);
                } else {
                    pending.push_back(tile);
                }
                if (requested(tile)) {
                    sortPages.insert(Slot(tile.location));
                }
            }
        }

        for (const auto& item : items) {
            if (item.form && seen.insert(KeyOf(item)).second) {
                pending.push_back({item.form, item.instance, {}, 1, 1, {kGroupLimit, 0, false}, false});
            }
        }

        for (auto tile : pending) {
            const auto original = tile;
            bool placed = false;
            const auto tryPage = [&](Location loc) {
                tile.location = loc;
                tile.cells = original.cells;
                if (requested(original)) {
                    tile.cells.w = tile.wantedWidth;
                    tile.cells.h = tile.wantedHeight;
                }
                if (!occupied.FindSpace(tile, true, original.pinned || !requested(original))) {
                    return false;
                }
                place(tile);
                if (!tile.pinned && !loc.global) {
                    sortPages.insert(Slot(loc));
                }
                return true;
            };

            if (Valid(*this, original.location)) {
                placed = tryPage(original.location);
            }
            if (!placed && hasPageCells) {
                std::vector<unsigned> destinations;
                if (!original.location.global && original.location.group < groups.size()) {
                    destinations.push_back(original.location.group);
                } else {
                    const auto category = static_cast<unsigned>(favorites.at(KeyOf(tile))->category);
                    for (unsigned g = 0; g < groups.size(); ++g) {
                        if (groups[g].accepts[category]) {
                            destinations.push_back(g);
                        }
                    }
                }

                if (destinations.empty()) {
                    destinations.push_back(0);
                }

                for (const auto g : destinations) {
                    for (unsigned p = 0; p < groups[g].pages.size() && !placed; ++p) {
                        const Location loc{g, p, false};
                        if (loc != original.location) {
                            placed = tryPage(loc);
                        }
                    }
                    if (placed) {
                        break;
                    }
                }

                if (!placed && PageCount() < kPageLimit) {
                    const auto g = destinations.front();
                    auto& pages = groups[g].pages;
                    const auto page = static_cast<unsigned>(pages.size());
                    pages.push_back({pages.back().sort, false, true});
                    placed = tryPage({g, page, false});
                    if (!placed) {
                        pages.pop_back();
                    }
                }
            }
            if (placed && original.location.global && !tile.location.global) {
                if (notice.size() < 400) {
                    if (!notice.empty()) {
                        notice += "\n";
                    }
                    notice += favorites.at(KeyOf(tile))->name + " moved to " + groups[tile.location.group].name +
                        " / " + std::to_string(tile.location.page + 1);
                }

            }
            if (!placed) {
                ++missing;
                // Keep the desired footprint and destination while there is no legal cell.
                // An empty rectangle is never drawn or reserved.
                tile = original;
                tile.cells = {-1, -1, original.cells.w, original.cells.h};
                tiles.push_back(tile);
            }
        }

        if (organize) {
            // An explicit organize may restore a pinned footprint at its existing anchor.
            for (auto& tile : tiles) {
                if (!tile.pinned || !requested(tile) || !Inside(tile.cells) ||
                    (tile.cells.w == tile.wantedWidth && tile.cells.h == tile.wantedHeight)) {
                    continue;
                }
                auto expanded = tile;
                expanded.cells.w = tile.wantedWidth;
                expanded.cells.h = tile.wantedHeight;
                if (Fits(expanded)) {
                    tile.cells = expanded.cells;
                }
            }
        }

        for (unsigned g = 0; g < groups.size(); ++g) {
            for (unsigned p = 0; p < groups[g].pages.size(); ++p) {
                const Location loc{g, p, false};
                if (!sortPages.contains(Slot(loc))) {
                    continue;
                }
                const auto sort = groups[g].pages[p].sort;
                if (sort == Sort::original) {
                    continue;
                }
                std::vector<Placement*> automatic;
                for (auto& tile : tiles) {
                    if (!tile.pinned && tile.location == loc && Inside(tile.cells) &&
                        tile.cells.w == 1 && tile.cells.h == 1) {
                        automatic.push_back(&tile);
                    }
                }
                std::vector<Cells> positions;
                for (const auto* tile : automatic) {
                    positions.push_back(tile->cells);
                }
                std::ranges::sort(positions, {}, [](Cells c) { return std::pair(c.y, c.x); });
                std::ranges::sort(automatic, [&](const Placement* a, const Placement* b) {
                    const auto& left = *favorites.at(KeyOf(*a));
                    const auto& right = *favorites.at(KeyOf(*b));
                    const auto primary = [&](const GridItem& item) {
                        if (sort == Sort::type) {
                            return static_cast<std::int64_t>(item.category);
                        }
                        return sort == Sort::value ? -static_cast<std::int64_t>(item.value) : std::int64_t{0};
                    };
                    return std::tuple(primary(left), std::string_view(left.name), KeyOf(left)) <
                        std::tuple(primary(right), std::string_view(right.name), KeyOf(right));
                });
                for (std::size_t index = 0; index < automatic.size(); ++index) {
                    automatic[index]->cells = positions[index];
                }
            }
        }
    }

    void Grid::Reconcile(std::span<const GridItem> items)
    {
        std::unordered_set<std::uint64_t> keys;
        for (const auto& item : items) {
            if (item.form) {
                keys.insert(KeyOf(item));
            }
        }
        if (needsValidation || keys.size() != tiles.size() ||
            std::ranges::any_of(tiles, [&](const auto& tile) { return !keys.erase(KeyOf(tile)); })) {
            Reflow(items);
        }
    }

    bool Grid::Move(Placement tile)
    {
        return Move(TileDrop{tile, {}});
    }

    bool Grid::Move(const TileDrop& drop)
    {
        const auto& [tile, displaced, overflow] = drop;
        const auto existing = std::ranges::find_if(tiles, [&](const auto& other) { return other.Key() == tile.Key(); });
        if (existing == tiles.end() || !Valid(*this, tile.location)) {
            return false;
        }
        auto occupied = OccupiedByOthers(*this, tile, displaced ? &*displaced : nullptr);
        if (!occupied.Fits(tile)) {
            return false;
        }
        auto other = tiles.end();
        if (displaced) {
            other = std::ranges::find_if(tiles, [&](const auto& entry) { return entry.Key() == displaced->Key(); });
            if (other == tiles.end() || other == existing) {
                return false;
            }
            if (overflow) {
                if (Slot(other->location) != Slot(tile.location) || !Inside(other->cells) ||
                    !Overlaps(tile.cells, other->cells)) {
                    return false;
                }
            } else {
                occupied.Add(tile);
                if (!Valid(*this, displaced->location) || !occupied.Fits(*displaced)) {
                    return false;
                }
            }
        } else if (overflow) {
            return false;
        }
        const auto place = [&](Placement& current, Placement next) {
            next.pinned = true;
            if (!next.location.global) {
                groups[next.location.group].pages[next.location.page].edited = true;
            }
            current = next;
        };
        if (overflow) {
            // Keep the original group and size so reflow can route the displaced tile.
            other->cells.x = other->cells.y = -1;
            ++missing;
            needsValidation = true;
        } else if (displaced) {
            place(*other, *displaced);
        }
        place(*existing, tile);
        return true;
    }

    bool Grid::DeleteGroup(unsigned group)
    {
        if (group >= groups.size() || groups.size() < 2) {
            return false;
        }
        groups.erase(groups.begin() + group);
        for (auto& tile : tiles) {
            if (tile.location.global || tile.location.group >= kGroupLimit) {
                continue;
            }
            if (tile.location.group == group) {
                tile.location = {kGroupLimit, 0, false};
            } else if (tile.location.group > group) {
                --tile.location.group;
            }
        }
        EnsureRoutes();
        return true;
    }

    bool Grid::MoveGroup(unsigned group, unsigned target)
    {
        if (group >= groups.size() || target >= groups.size() || group == target) {
            return false;
        }
        std::swap(groups[group], groups[target]);
        for (auto& tile : tiles) {
            if (!tile.location.global && tile.location.group == group) {
                tile.location.group = target;
            } else if (!tile.location.global && tile.location.group == target) {
                tile.location.group = group;
            }
        }
        return true;
    }

    bool Grid::DeleteEmptyPage(Location loc)
    {
        if (loc.global || !Valid(*this, loc) || groups[loc.group].pages.size() < 2 ||
            std::ranges::any_of(tiles, [&](const auto& tile) { return tile.location == loc; })) {
            return false;
        }
        auto& pages = groups[loc.group].pages;
        pages.erase(pages.begin() + loc.page);
        for (auto& tile : tiles) {
            if (!tile.location.global && tile.location.group == loc.group && tile.location.page > loc.page) {
                --tile.location.page;
            }
        }
        return true;
    }
}

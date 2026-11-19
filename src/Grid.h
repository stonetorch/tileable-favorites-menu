#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace TFM
{
    struct ItemKey;

    enum class Category : std::uint8_t { equipment, potion, poison, spell, scroll, miscellaneous, shout, count };
    enum class Sort : std::uint8_t { original, name, type, value };
    enum class Navigation : std::uint8_t { top, bottom, left, right, floating };

    inline constexpr int kGridLimit = 32;
    inline constexpr unsigned kGroupLimit = 64;
    inline constexpr unsigned kPageLimit = 8192;
    inline constexpr std::array kCategoryNames{
        "Weapons & armor", "Potions", "Poisons", "Spells", "Scrolls", "Miscellaneous", "Shouts"
    };

    struct Cells
    {
        int x{}, y{}, w{1}, h{1};
        friend bool operator==(const Cells&, const Cells&) = default;
    };

    struct GridSettings
    {
        int columns{10}, rows{5};
        int x{8}, y{18}, width{84}, height{68};
        int cellSize{100}, gap{6}, margin{8};
        bool fit{true}, wrap{true};
        Navigation navigation{Navigation::top};
        int navX{8}, navY{10};
    };

    struct Page
    {
        Sort sort{};
        bool edited{};
        bool automatic{};
    };

    struct CategoryGroup
    {
        std::string name;
        std::string icon;
        std::array<bool, kCategoryNames.size()> accepts{};
        std::vector<Page> pages{Page{}};
    };

    struct Location
    {
        unsigned group{}, page{};
        bool global{};
        friend bool operator==(const Location&, const Location&) = default;
    };

    struct Placement
    {
        std::uint32_t form{};
        std::uint16_t instance{};
        Cells cells;
        int wantedWidth{1}, wantedHeight{1};
        Location location;
        bool pinned{};
        [[nodiscard]] ItemKey Key() const;
    };

    struct GridItem
    {
        std::uint32_t form{};
        std::uint16_t instance{};
        Category category{};
        std::string name;
        int value{};
        [[nodiscard]] ItemKey Key() const;
    };

    struct Grid
    {
        GridSettings settings;
        std::vector<std::uint8_t> mask;
        std::vector<CategoryGroup> groups;
        std::vector<Placement> tiles;
        std::string notice;
        unsigned missing{};
        bool needsValidation{};

        Grid();
        [[nodiscard]] bool GlobalAt(int x, int y) const;
        [[nodiscard]] bool Inside(Cells cells) const;
        [[nodiscard]] const Placement* Find(ItemKey key) const;
        [[nodiscard]] bool Fits(const Placement& tile) const;
        [[nodiscard]] std::optional<Placement> Imitate(Placement tile) const;
        [[nodiscard]] std::optional<Cells> ResizePreview(const Placement& tile) const;
        [[nodiscard]] unsigned PageCount() const;
        void Toggle(Cells area);
        void SetDimensions(int columns, int rows);
        void EnsureRoutes();
        void Reflow(std::span<const GridItem> items, std::optional<Location> organize = {}, bool wholeGroup = false);
        void Reconcile(std::span<const GridItem> items);
        bool Move(Placement tile);
        bool DeleteGroup(unsigned group);
        bool MoveGroup(unsigned group, unsigned target);
        bool DeleteEmptyPage(Location page);
    };
}

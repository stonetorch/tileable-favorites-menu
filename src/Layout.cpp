#include "Layout.h"
#include "Config.h"
#include <nlohmann/json.hpp>

namespace TFM
{
    namespace
    {
        constexpr std::size_t kMaximumDepth = 64;

        template <class T>
        bool WriteValue(SKSE::SerializationInterface* serialization, const T& value)
        {
            return serialization->WriteRecordData(std::addressof(value), sizeof(value));
        }

        template <class T>
        bool ReadValue(SKSE::SerializationInterface* serialization, T& value)
        {
            return serialization->ReadRecordData(std::addressof(value), sizeof(value)) == sizeof(value);
        }

        void CalculateNode(
            const LayoutNode* node,
            const Rect& rect,
            float gap,
            std::vector<LeafRect>& leaves)
        {
            if (!node) {
                return;
            }
            if (node->IsLeaf()) {
                leaves.push_back({ node->item, rect });
                return;
            }

            if (node->axis == Axis::kVertical) {
                const auto usable = std::max(0.0f, rect.width - gap);
                const auto firstWidth = usable * node->ratio;
                const Rect first{ rect.x, rect.y, firstWidth, rect.height };
                const Rect second{ rect.x + firstWidth + gap, rect.y, usable - firstWidth, rect.height };
                CalculateNode(node->first.get(), first, gap, leaves);
                CalculateNode(node->second.get(), second, gap, leaves);
            } else {
                const auto usable = std::max(0.0f, rect.height - gap);
                const auto firstHeight = usable * node->ratio;
                const Rect first{ rect.x, rect.y, rect.width, firstHeight };
                const Rect second{ rect.x, rect.y + firstHeight + gap, rect.width, usable - firstHeight };
                CalculateNode(node->first.get(), first, gap, leaves);
                CalculateNode(node->second.get(), second, gap, leaves);
            }
        }

        bool WriteNode(SKSE::SerializationInterface* serialization, const LayoutNode* node)
        {
            const std::uint8_t kind = node->IsLeaf() ? 0U : 1U;
            if (!WriteValue(serialization, kind)) {
                return false;
            }
            if (node->IsLeaf()) {
                return WriteValue(serialization, node->item.formID) &&
                    WriteValue(serialization, node->item.uniqueID);
            }

            const auto axis = static_cast<std::uint8_t>(node->axis);
            return WriteValue(serialization, axis) && WriteValue(serialization, node->ratio) &&
                WriteNode(serialization, node->first.get()) && WriteNode(serialization, node->second.get());
        }

        std::unique_ptr<LayoutNode> ReadNode(SKSE::SerializationInterface* serialization, std::size_t depth, bool& ok)
        {
            if (!ok || depth > kMaximumDepth) {
                ok = false;
                return nullptr;
            }

            std::uint8_t kind = 0;
            if (!ReadValue(serialization, kind) || kind > 1) {
                ok = false;
                return nullptr;
            }

            auto node = std::make_unique<LayoutNode>();
            if (kind == 0) {
                if (!ReadValue(serialization, node->item.formID) || !ReadValue(serialization, node->item.uniqueID)) {
                    ok = false;
                    return nullptr;
                }
                if (node->item.formID != 0) {
                    RE::FormID resolved = 0;
                    if (!serialization->ResolveFormID(node->item.formID, resolved)) {
                        node->item = {};
                    } else {
                        node->item.formID = resolved;
                    }
                }
                return node;
            }

            std::uint8_t axis = 0;
            if (!ReadValue(serialization, axis) || axis > 1 || !ReadValue(serialization, node->ratio)) {
                ok = false;
                return nullptr;
            }
            node->axis = static_cast<Axis>(axis);
            node->ratio = std::isfinite(node->ratio) ? std::clamp(node->ratio, 0.1f, 0.9f) : 0.5f;
            node->first = ReadNode(serialization, depth + 1, ok);
            node->second = ReadNode(serialization, depth + 1, ok);
            if (!ok) {
                return nullptr;
            }
            return node;
        }
    }

    namespace
    {
        using Json = nlohmann::json;

        Category Classify(const FavoriteItem& item)
        {
            switch (item.form->GetFormType()) {
            case RE::FormType::Weapon:
            case RE::FormType::Armor:
            case RE::FormType::Ammo: return Category::equipment;
            case RE::FormType::AlchemyItem:
                if (const auto* alchemy = item.form->As<RE::AlchemyItem>()) {
                    if (alchemy->IsPoison()) {
                        return Category::poison;
                    }
                    if (!alchemy->IsFood()) {
                        return Category::potion;
                    }
                }
                return Category::miscellaneous;
            case RE::FormType::Spell: return Category::spell;
            case RE::FormType::Scroll: return Category::scroll;
            case RE::FormType::Shout: return Category::shout;
            default: return Category::miscellaneous;
            }
        }

        bool WriteGrid(SKSE::SerializationInterface* serialization, const Grid& grid)
        {
            const auto& s = grid.settings;
            Json groups = Json::array();
            for (const auto& group : grid.groups) {
                Json pages = Json::array();
                for (const auto& page : group.pages) {
                    pages.push_back({static_cast<int>(page.sort), page.edited, page.automatic});
                }
                groups.push_back({group.name, group.icon, group.accepts, std::move(pages)});
            }

            Json tiles = Json::array();
            for (const auto& tile : grid.tiles) {
                const auto& c = tile.cells;
                const auto& loc = tile.location;
                tiles.push_back({tile.form, tile.instance, c.x, c.y, c.w, c.h, tile.wantedWidth, tile.wantedHeight,
                    loc.group, loc.page, loc.global, tile.pinned});
            }
            const auto data = Json{
                {"grid", {s.columns, s.rows, s.x, s.y, s.width, s.height, s.cellSize, s.gap, s.margin, s.fit, s.wrap,
                    static_cast<int>(s.navigation), s.navX, s.navY}},
                {"mask", grid.mask}, {"groups", std::move(groups)}, {"tiles", std::move(tiles)}
            }.dump(-1, ' ', false, Json::error_handler_t::replace);
            return serialization->WriteRecordData(data.data(), static_cast<std::uint32_t>(data.size()));
        }

        int ReadInt(const Json& array, unsigned index, int minimum, int maximum)
        {
            const auto& value = array.at(index);
            if (!value.is_number_integer()) {
                throw std::invalid_argument("Expected an integer");
            }
            const auto number = value.get<std::int64_t>();
            if (number < minimum || number > maximum) {
                throw std::out_of_range("Grid field is outside its supported range");
            }
            return static_cast<int>(number);
        }

        Grid ReadGrid(SKSE::SerializationInterface* serialization, const Json& record)
        {
            Grid grid;
            auto& s = grid.settings;
            const auto& g = record.at("grid");
            s.columns = ReadInt(g, 0, 1, kGridLimit);
            s.rows = ReadInt(g, 1, 1, kGridLimit);
            s.x = ReadInt(g, 2, 0, 95);
            s.y = ReadInt(g, 3, 0, 95);
            s.width = ReadInt(g, 4, 5, 100);
            s.height = ReadInt(g, 5, 5, 100);
            s.cellSize = ReadInt(g, 6, 24, 320);
            s.gap = ReadInt(g, 7, 0, 40);
            s.margin = ReadInt(g, 8, 0, 40);
            s.fit = g.at(9).get<bool>();
            s.wrap = g.at(10).get<bool>();
            s.navigation = static_cast<Navigation>(ReadInt(g, 11, 0, 4));
            s.navX = ReadInt(g, 12, 0, 95);
            s.navY = ReadInt(g, 13, 0, 95);
            const auto& mask = record.at("mask");
            if (mask.size() != static_cast<std::size_t>(s.columns * s.rows)) {
                throw std::invalid_argument("Incorrect mask size");
            }
            grid.mask.clear();
            for (unsigned i = 0; i < mask.size(); ++i) {
                grid.mask.push_back(static_cast<std::uint8_t>(ReadInt(mask, i, 0, 1)));
            }

            const auto& groups = record.at("groups");
            if (!groups.is_array() || groups.empty() || groups.size() > kGroupLimit) {
                throw std::invalid_argument("Invalid category list");
            }
            unsigned totalPages = 0;
            grid.groups.clear();
            for (const auto& group : groups) {
                CategoryGroup entry{group.at(0).get<std::string>(), group.at(1).get<std::string>()};
                entry.accepts = group.at(2).get<decltype(entry.accepts)>();
                const auto& pages = group.at(3);
                if (!pages.is_array() || pages.empty() || pages.size() > kPageLimit - totalPages ||
                    entry.name.empty() || entry.name.size() > 63 || entry.icon.size() > 15) {
                    throw std::invalid_argument("Invalid category or page settings");
                }
                totalPages += static_cast<unsigned>(pages.size());
                entry.pages.clear();
                for (const auto& page : pages) {
                    entry.pages.push_back({static_cast<Sort>(ReadInt(page, 0, 0, 3)),
                        page.at(1).get<bool>(), page.at(2).get<bool>()});
                }
                grid.groups.push_back(std::move(entry));
            }
            grid.EnsureRoutes();

            const auto& tiles = record.at("tiles");
            if (!tiles.is_array() || tiles.size() > 65536) {
                throw std::invalid_argument("Invalid tile list");
            }
            for (const auto& t : tiles) {
                RE::FormID resolved{};
                if (!serialization->ResolveFormID(t.at(0).get<RE::FormID>(), resolved) || !resolved) {
                    continue;
                }
                grid.tiles.push_back({
                    resolved, static_cast<std::uint16_t>(ReadInt(t, 1, 0, 65535)),
                    {ReadInt(t, 2, -1, kGridLimit), ReadInt(t, 3, -1, kGridLimit),
                        ReadInt(t, 4, 1, kGridLimit), ReadInt(t, 5, 1, kGridLimit)},
                    ReadInt(t, 6, 1, kGridLimit), ReadInt(t, 7, 1, kGridLimit),
                    {static_cast<unsigned>(ReadInt(t, 8, 0, kGroupLimit)),
                        static_cast<unsigned>(ReadInt(t, 9, 0, kPageLimit - 1)), t.at(10).get<bool>()},
                    t.at(11).get<bool>()
                });
            }
            // Revalidate every rectangle against live favorites on the first refresh.
            grid.needsValidation = true;
            return grid;
        }

        void Remember(std::vector<Grid>& history, const Grid& grid)
        {
            constexpr std::size_t limit = 24;
            if (history.size() >= limit) {
                history.erase(history.begin());
            }
            history.push_back(grid);
        }
    }

    Layout& Layout::GetSingleton()
    {
        static Layout singleton;
        return singleton;
    }

    void Layout::Reconcile(const std::vector<FavoriteItem>& favorites)
    {
        std::scoped_lock lock(mutex_);
        items_.clear();
        items_.reserve(favorites.size());
        for (const auto& favorite : favorites) {
            if (favorite.key.IsValid() && favorite.form) {
                items_.push_back({favorite.key.formID, favorite.key.uniqueID, Classify(favorite),
                    favorite.name, favorite.form->IsInventoryObject() ? favorite.form->GetGoldValue() : 0});
            }
        }
        if (!migrated_ && !items_.empty()) {
            const auto& config = Config::Get();
            if (std::isfinite(config.gap)) {
                grid_.settings.gap = static_cast<int>(std::round(std::clamp(config.gap, 0.0f, 40.0f)));
            }
            if (std::isfinite(config.outerMargin)) {
                grid_.settings.margin = static_cast<int>(std::round(std::clamp(config.outerMargin, 0.0f, 40.0f)));
            }
        }
        if (!migrated_ && legacy_ && !items_.empty()) {
            std::vector<LeafRect> legacyTiles;
            const auto columns = static_cast<float>(grid_.settings.columns);
            const auto rows = static_cast<float>(grid_.settings.rows);
            CalculateNode(legacy_.get(), {0, 0, columns, rows}, 0, legacyTiles);
            for (const auto& leaf : legacyTiles) {
                const auto item = std::ranges::find_if(items_, [&](const auto& entry) { return entry.Key() == leaf.item; });
                if (item == items_.end()) {
                    continue;
                }
                const auto group = std::ranges::find_if(grid_.groups, [&](const auto& entry) {
                    return entry.accepts[static_cast<unsigned>(item->category)];
                });
                const Cells cells{static_cast<int>(leaf.rect.x), static_cast<int>(leaf.rect.y),
                    std::max(1, static_cast<int>(leaf.rect.width)), std::max(1, static_cast<int>(leaf.rect.height))};
                grid_.tiles.push_back({item->form, item->instance, cells, cells.w, cells.h,
                    {static_cast<unsigned>(group - grid_.groups.begin()), 0, false}, true});
            }
            grid_.Reflow(items_);
            grid_.notice = "TFM layout imported. Original layout retained in the SKSE co-save.";
            migrated_ = true;
        } else {
            grid_.Reconcile(items_);
            migrated_ |= !items_.empty();
        }
    }

    Grid Layout::Snapshot() const
    {
        std::scoped_lock lock(mutex_);
        return grid_;
    }

    Grid Layout::Prepare(Grid grid, std::optional<Location> organize, bool wholeGroup) const
    {
        std::scoped_lock lock(mutex_);
        grid.EnsureRoutes();
        grid.Reflow(items_, organize, wholeGroup);
        return grid;
    }

    void Layout::Commit(Grid grid)
    {
        std::scoped_lock lock(mutex_);
        if (grid.missing) {
            grid.Reflow(items_);
        } else {
            grid.Reconcile(items_);
        }
        Remember(undo_, grid_);
        redo_.clear();
        grid_ = std::move(grid);
        migrated_ = true;
    }

    bool Layout::Undo()
    {
        std::scoped_lock lock(mutex_);
        if (undo_.empty()) {
            return false;
        }
        Remember(redo_, grid_);
        grid_ = std::move(undo_.back());
        undo_.pop_back();
        grid_.Reconcile(items_);
        return true;
    }

    bool Layout::Redo()
    {
        std::scoped_lock lock(mutex_);
        if (redo_.empty()) {
            return false;
        }
        Remember(undo_, grid_);
        grid_ = std::move(redo_.back());
        redo_.pop_back();
        grid_.Reconcile(items_);
        return true;
    }

    bool Layout::RestoreBackup()
    {
        std::scoped_lock lock(mutex_);
        if (!backup_ && !previousSave_) {
            return false;
        }
        Remember(undo_, grid_);
        redo_.clear();
        grid_ = backup_ ? *backup_ : *previousSave_;
        grid_.needsValidation = true;
        if (!items_.empty()) {
            grid_.Reflow(items_);
        }
        migrated_ = true;
        return true;
    }

    void Layout::Clear()
    {
        std::scoped_lock lock(mutex_);
        grid_ = Grid{};
        items_.clear();
        undo_.clear();
        redo_.clear();
        legacy_.reset();
        previousSave_.reset();
        backup_.reset();
        migrated_ = false;
    }

    bool Layout::Save(SKSE::SerializationInterface* serialization, std::uint32_t gridRecord,
        std::uint32_t backupRecord, std::uint32_t legacyRecord, std::uint32_t version)
    {
        std::scoped_lock lock(mutex_);
        const bool hasRoot = static_cast<bool>(legacy_);
        const bool legacyWritten = serialization->OpenRecord(legacyRecord, version) &&
            WriteValue(serialization, hasRoot) && (!hasRoot || WriteNode(serialization, legacy_.get()));
        if (!migrated_) {
            return legacyWritten;
        }

        try {
            const auto& recovery = previousSave_ ? *previousSave_ : grid_;
            const bool backupWritten = serialization->OpenRecord(backupRecord, version) &&
                WriteGrid(serialization, recovery);
            if (backupWritten) {
                backup_ = recovery;
            }
            const bool gridWritten = serialization->OpenRecord(gridRecord, version) && WriteGrid(serialization, grid_);
            if (gridWritten) {
                previousSave_ = grid_;
            }
            return legacyWritten && backupWritten && gridWritten;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool Layout::Load(SKSE::SerializationInterface* serialization, std::uint32_t length, bool backup)
    {
        if (length == 0 || length > 16 * 1024 * 1024) {
            return false;
        }
        try {
            std::string data(length, '\0');
            if (serialization->ReadRecordData(data.data(), length) != length) {
                return false;
            }
            auto loaded = ReadGrid(serialization, Json::parse(data));
            std::scoped_lock lock(mutex_);
            if (backup) {
                backup_ = std::move(loaded);
            } else {
                grid_ = std::move(loaded);
                previousSave_ = grid_;
                migrated_ = true;
            }
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool Layout::LoadLegacy(SKSE::SerializationInterface* serialization)
    {
        std::uint8_t hasRoot{};
        if (!ReadValue(serialization, hasRoot) || hasRoot > 1) {
            return false;
        }
        bool ok = true;
        auto loaded = hasRoot ? ReadNode(serialization, 0, ok) : nullptr;
        if (ok) {
            std::scoped_lock lock(mutex_);
            legacy_ = std::move(loaded);
        }
        return ok;
    }
}

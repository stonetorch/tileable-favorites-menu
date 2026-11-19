#pragma once

#include "Grid.h"
#include "Types.h"

namespace TFM
{
    struct LeafRect
    {
        ItemKey item{};
        Rect rect{};
    };

    // TFM 1.0.x split tree, preserved in its original SKSE record for rollback.
    struct LayoutNode
    {
        ItemKey item{};
        Axis axis{Axis::kVertical};
        float ratio{0.5f};
        std::unique_ptr<LayoutNode> first;
        std::unique_ptr<LayoutNode> second;
        [[nodiscard]] bool IsLeaf() const { return !first && !second; }
    };

    class Layout
    {
    public:
        static Layout& GetSingleton();
        void Reconcile(const std::vector<FavoriteItem>& favorites);
        [[nodiscard]] Grid Snapshot() const;
        [[nodiscard]] Grid Prepare(Grid grid, std::optional<Location> organize = {}, bool wholeGroup = false) const;
        void Commit(Grid grid);
        bool Undo();
        bool Redo();
        bool RestoreBackup();
        void Clear();

        bool Save(SKSE::SerializationInterface* serialization, std::uint32_t gridRecord,
            std::uint32_t backupRecord, std::uint32_t legacyRecord, std::uint32_t version);
        bool Load(SKSE::SerializationInterface* serialization, std::uint32_t length, bool backup);
        bool LoadLegacy(SKSE::SerializationInterface* serialization);

    private:
        Grid grid_;
        std::vector<Grid> undo_;
        std::vector<Grid> redo_;
        std::vector<GridItem> items_;
        std::unique_ptr<LayoutNode> legacy_;
        std::optional<Grid> previousSave_;
        std::optional<Grid> backup_;
        bool migrated_{};
        mutable std::mutex mutex_;
    };
}

#pragma once

#include "IconLayer.h"
#include "Layout.h"
#include "SKSEMenuFramework.h"

namespace TFM::UI
{
    struct GridGeometry
    {
        Rect window;
        Rect bounds;
        float stepX{}, stepY{}, gap{};
        [[nodiscard]] Rect RectFor(Cells cells) const;
        [[nodiscard]] Cells CellAt(ImGuiMCP::ImVec2 point) const;
    };

    class GridEditor
    {
    public:
        Grid Draft(const Grid& live) const;
        GridGeometry Geometry(const Grid& grid, ImGuiMCP::ImVec2 screen) const;
        std::vector<LeafRect> Leaves(const Grid& grid, const GridGeometry& geometry) const;
        void DrawControls(const Grid& live, const GridGeometry& geometry, ItemKey selected);
        [[nodiscard]] Rect NavigationRect() const { return navigationRect_; }
        [[nodiscard]] std::span<const IconLayer::GroupIcon> GroupIcons() const { return groupIcons_; }
        void DrawMask(const Grid& live, const Grid& display, const GridGeometry& geometry,
            std::optional<Cells> selection, ImGuiMCP::ImDrawList* drawList) const;
        void PreviewMask(const Grid& live, Cells area);
        void Turn(const Grid& grid, int delta);
        void ClampPage(const Grid& grid);
        void Reset();
        void RevealFavorites(const Grid& grid);
        bool Cancel();
        [[nodiscard]] bool OverControls(ImGuiMCP::ImVec2 point) const;
        [[nodiscard]] bool HasDraft() const { return baseline_.has_value(); }

        Location current;
        bool editing{};
        bool regionMode{};
        std::string feedback;

    private:
        void Stage(Grid grid, std::optional<Location> organize = {}, bool wholeGroup = false);
        void DrawSettings(const Grid& live, ItemKey selected);
        void Apply();

        Grid preview_;
        std::optional<Grid> baseline_;
        std::optional<Location> organize_;
        bool wholeGroup_{};
        bool settingsOpen_{};
        unsigned category_{};
        Rect navigationRect_;
        std::vector<IconLayer::GroupIcon> groupIcons_;
        Rect settingsRect_;
        Rect statusRect_;
    };
}

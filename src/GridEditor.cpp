#include "GridEditor.h"

namespace TFM::UI
{
    namespace ImGui = ImGuiMCP;
    using namespace ImGuiMCP;

    namespace
    {
        constexpr auto kOverlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing;
        constexpr std::array kSortNames{"Original / manual", "Name", "Type", "Value"};
        constexpr std::array kNavNames{"Top", "Bottom", "Left", "Right", "Floating"};

        bool Contains(Rect rect, ImVec2 position)
        {
            return rect.width > 0 && rect.height > 0 && position.x >= rect.x && position.x < rect.Right() &&
                position.y >= rect.y && position.y < rect.Bottom();
        }

        void Outline(ImDrawList* list, Rect rect, ImU32 color, float width)
        {
            ImDrawListManager::AddRect(list, {rect.x, rect.y}, {rect.Right(), rect.Bottom()}, color, 0, 0, width);
        }

        bool TextField(const char* label, std::string& value, auto buffer)
        {
            std::copy_n(value.begin(), std::min(value.size(), buffer.size() - 1), buffer.begin());
            if (ImGui::InputText(label, buffer.data(), buffer.size())) {
                value = buffer.data();
                return true;
            }
            return false;
        }

        bool Slider(const char* label, int& value, int low, int high)
        {
            ImGui::SetNextItemWidth(120);
            return ImGui::SliderInt(label, &value, low, high);
        }
    }

    Rect GridGeometry::RectFor(Cells c) const
    {
        return {bounds.x + stepX * c.x, bounds.y + stepY * c.y,
            stepX * c.w - gap, stepY * c.h - gap};
    }

    Cells GridGeometry::CellAt(ImVec2 point) const
    {
        return {static_cast<int>(std::floor((point.x - bounds.x) / stepX)),
            static_cast<int>(std::floor((point.y - bounds.y) / stepY))};
    }

    GridGeometry GridEditor::Geometry(const Grid& grid, ImVec2 screen) const
    {
        const auto& s = grid.settings;
        Rect panel{screen.x * s.x / 100.0f, screen.y * s.y / 100.0f,
            screen.x * s.width / 100.0f, screen.y * s.height / 100.0f};
        panel.x = std::clamp(panel.x, 0.0f, std::max(0.0f, screen.x - panel.width));
        panel.y = std::clamp(panel.y, 0.0f, std::max(0.0f, screen.y - panel.height));
        const float scale = std::clamp(screen.y / 1080.0f, 0.65f, 4.0f);
        if (s.navigation == Navigation::top || s.navigation == Navigation::bottom) {
            const auto navHeight = std::min(60.0f, std::max(1.0f, screen.y - 1.0f));
            const auto available = std::max(1.0f, screen.y - navHeight);
            panel.height = std::min(panel.height, available);
            panel.y = s.navigation == Navigation::top ?
                std::clamp(panel.y, navHeight, screen.y - panel.height) :
                std::min(panel.y, available - panel.height);
        } else if (s.navigation == Navigation::left || s.navigation == Navigation::right) {
            const auto navWidth = std::min(136.0f, std::max(1.0f, screen.x - 1.0f));
            const auto available = std::max(1.0f, screen.x - navWidth);
            panel.width = std::min(panel.width, available);
            panel.x = s.navigation == Navigation::left ?
                std::clamp(panel.x, navWidth, screen.x - panel.width) :
                std::min(panel.x, available - panel.width);
        }
        const auto margin = std::min(s.margin * scale, std::min(panel.width, panel.height) * 0.2f);
        const auto availableX = std::max(1.0f, panel.width - 2 * margin);
        const auto availableY = std::max(1.0f, panel.height - 2 * margin);
        auto stepX = availableX / s.columns;
        auto stepY = availableY / s.rows;
        if (!s.fit) {
            const float pitch = (s.cellSize + s.gap) * scale;
            stepX = stepY = std::min({stepX, stepY, pitch});
        }
        const auto gap = std::min(s.gap * scale, std::min(stepX, stepY) * 0.25f);
        return {panel, {panel.x + margin, panel.y + margin, stepX * s.columns, stepY * s.rows},
            stepX, stepY, gap};
    }

    Grid GridEditor::Draft(const Grid& live) const
    {
        return baseline_ ? preview_ : live;
    }

    std::vector<LeafRect> GridEditor::Leaves(const Grid& grid, const GridGeometry& geometry) const
    {
        std::vector<LeafRect> leaves;
        for (const auto& tile : grid.tiles) {
            if ((tile.location.global || tile.location == current) && grid.Inside(tile.cells)) {
                leaves.push_back({tile.Key(), geometry.RectFor(tile.cells)});
            }
        }
        return leaves;
    }

    void GridEditor::ClampPage(const Grid& grid)
    {
        current.group = std::min(current.group, static_cast<unsigned>(grid.groups.size() - 1));
        current.page = std::min(current.page, static_cast<unsigned>(grid.groups[current.group].pages.size() - 1));
        category_ = std::min(category_, static_cast<unsigned>(grid.groups.size() - 1));
    }

    void GridEditor::Turn(const Grid& grid, int delta)
    {
        ClampPage(grid);
        int position = static_cast<int>(current.page);
        for (unsigned group = 0; group < current.group; ++group) {
            position += static_cast<int>(grid.groups[group].pages.size());
        }
        const auto count = static_cast<int>(grid.PageCount());
        position += delta;
        position = grid.settings.wrap ? (position % count + count) % count : std::clamp(position, 0, count - 1);
        for (unsigned group = 0; group < grid.groups.size(); ++group) {
            const auto pages = static_cast<int>(grid.groups[group].pages.size());
            if (position < pages) {
                current = {group, static_cast<unsigned>(position), false};
                return;
            }
            position -= pages;
        }
    }

    bool GridEditor::OverControls(ImVec2 point) const
    {
        return Contains(navigationRect_, point) || Contains(settingsRect_, point) || Contains(statusRect_, point);
    }

    void GridEditor::Reset()
    {
        const auto selected = current;
        *this = {};
        current = selected;
    }

    void GridEditor::RevealFavorites(const Grid& grid)
    {
        ClampPage(grid);
        if (std::ranges::any_of(grid.tiles, [&](const auto& tile) {
                return (tile.location.global || tile.location == current) && grid.Inside(tile.cells);
            })) {
            return;
        }
        for (unsigned group = 0; group < grid.groups.size(); ++group) {
            const auto first = std::ranges::min_element(grid.tiles, {}, [&](const auto& tile) {
                return tile.location.group == group && !tile.location.global && grid.Inside(tile.cells) ?
                    tile.location.page : kPageLimit;
            });
            if (first != grid.tiles.end() && first->location.group == group &&
                !first->location.global && grid.Inside(first->cells)) {
                current = first->location;
                return;
            }
        }
    }

    bool GridEditor::Cancel()
    {
        if (!baseline_) {
            return false;
        }
        baseline_.reset();
        organize_.reset();
        wholeGroup_ = false;
        feedback.clear();
        return true;
    }

    void GridEditor::Stage(Grid grid, std::optional<Location> organize, bool wholeGroup)
    {
        organize_ = organize;
        wholeGroup_ = wholeGroup;
        baseline_ = std::move(grid);
        preview_ = Layout::GetSingleton().Prepare(*baseline_, organize_, wholeGroup_);
    }

    void GridEditor::Apply()
    {
        if (baseline_) {
            auto& layout = Layout::GetSingleton();
            auto result = layout.Prepare(*baseline_, organize_, wholeGroup_);
            if (result.missing && std::ranges::find(result.mask, 0) != result.mask.end()) {
                feedback = "Some favorites could not be placed. Cancel or free space before applying.";
                return;
            }
            layout.Commit(std::move(result));
            Cancel();
        }
    }

    void GridEditor::PreviewMask(const Grid& live, Cells area)
    {
        auto candidate = baseline_ ? *baseline_ : live;
        candidate.Toggle(area);
        Stage(std::move(candidate));
    }

    void GridEditor::DrawMask(const Grid& live, const Grid& display, const GridGeometry& geometry,
        std::optional<Cells> selection, ImDrawList* drawList) const
    {
        if (!editing) {
            return;
        }
        for (int y = 0; y < display.settings.rows; ++y) {
            for (int x = 0; x < display.settings.columns; ++x) {
                const auto cell = geometry.RectFor({x, y});
                if (display.GlobalAt(x, y)) {
                    ImDrawListManager::AddRectFilled(drawList, {cell.x, cell.y}, {cell.Right(), cell.Bottom()},
                        IM_COL32(23, 159, 149, 58), 0, 0);
                }
                Outline(drawList, cell, IM_COL32(190, 203, 215, 86), 1);
                if (baseline_ && live.GlobalAt(x, y) != display.GlobalAt(x, y)) {
                    Outline(drawList, cell, IM_COL32(245, 186, 83, 240), 2);
                }
            }
        }
        if (baseline_) {
            for (const auto& tile : live.tiles) {
                if ((!tile.location.global && tile.location != current) || !live.Inside(tile.cells)) {
                    continue;
                }
                const auto next = display.Find(tile.Key());
                if (!next || next->location != tile.location || next->cells != tile.cells) {
                    Outline(drawList, geometry.RectFor(tile.cells), IM_COL32(246, 129, 97, 255), 2);
                }
            }
        }
        if (selection) {
            auto bounds = geometry.RectFor(*selection);
            ImDrawListManager::AddRectFilled(drawList, {bounds.x, bounds.y}, {bounds.Right(), bounds.Bottom()},
                IM_COL32(245, 185, 88, 55), 0, 0);
            Outline(drawList, bounds, IM_COL32(245, 185, 88, 255), 2);
        }
    }

    void GridEditor::DrawControls(const Grid& live, const GridGeometry& geometry, ItemKey selected)
    {
        const auto& grid = baseline_ ? preview_ : live;
        ClampPage(grid);
        const auto screen = ImGui::GetIO()->DisplaySize;
        const auto& s = grid.settings;
        const bool vertical = s.navigation == Navigation::left || s.navigation == Navigation::right;
        const auto& panel = geometry.window;
        const float width = vertical ? 132.0f : std::min(screen.x, std::max(390.0f, panel.width));
        const float height = vertical ? std::min(screen.y, std::max(220.0f, panel.height)) : 58.0f;
        float x = panel.x;
        float y = panel.y - height;
        switch (s.navigation) {
        case Navigation::top: break;
        case Navigation::bottom: y = panel.Bottom(); break;
        case Navigation::left: x -= width; y = panel.y; break;
        case Navigation::right: x = panel.Right(); y = panel.y; break;
        case Navigation::floating: x = screen.x * s.navX / 100; y = screen.y * s.navY / 100; break;
        }
        navigationRect_ = {std::clamp(x, 0.0f, std::max(0.0f, screen.x - width)),
            std::clamp(y, 0.0f, std::max(0.0f, screen.y - height)), width, height};
        ImGui::SetNextWindowPos({navigationRect_.x, navigationRect_.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (ImGui::Begin("##TFME navigation", nullptr, kOverlayFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
            if (ImGui::Button("<##Previous")) {
                Turn(grid, -1);
            }
            ImGui::SameLine();
            if (ImGui::Button(">##Next")) {
                Turn(grid, 1);
            }
            if (!vertical) {
                ImGui::SameLine();
            }
            if (ImGui::Button(editing ? "Done" : "Edit")) {
                editing = !editing;
                settingsOpen_ = editing;
                regionMode = false;
                Cancel();
                feedback.clear();
            }
            if (editing) {
                if (!vertical) {
                    ImGui::SameLine();
                }
                if (ImGui::Button(settingsOpen_ ? "Hide settings" : "Settings")) {
                    settingsOpen_ = !settingsOpen_;
                }
                if (!vertical) {
                    ImGui::SameLine();
                }
                ImGui::Checkbox("Paint regions", &regionMode);
            }
            if (ImGui::BeginChild("##Pages", {0, 0}, 0, vertical ? 0 : ImGuiWindowFlags_HorizontalScrollbar)) {
                for (unsigned g = 0; g < grid.groups.size(); ++g) {
                    const auto& group = grid.groups[g];
                    ImGui::PushID(static_cast<int>(g));
                    if (!vertical && g != 0) {
                        ImGui::SameLine(0, 16);
                    }
                    if (ImGui::SmallButton(group.icon.empty() ? group.name.c_str() : group.icon.c_str())) {
                        current = {g, 0, false};
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", group.name.c_str());
                    }
                    // Keep the strip bounded while retaining exact navigation via the page selector.
                    const auto index = current.group == g ? current.page : 0U;
                    const auto size = static_cast<unsigned>(group.pages.size());
                    const unsigned visible = vertical ? 3 : 5;
                    const auto half = visible / 2;
                    const auto start = std::min(index > half ? index - half : 0U, size > visible ? size - visible : 0U);
                    for (unsigned p = start; p < std::min(size, start + visible); ++p) {
                        ImGui::SameLine(0, 3);
                        ImGui::PushID(static_cast<int>(p));
                        if (ImGui::SmallButton(current == Location{g, p, false} ? "o" : ".")) {
                            current = {g, p, false};
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s / %u", group.name.c_str(), p + 1);
                        }
                        ImGui::PopID();
                    }
                    if (size > visible) {
                        ImGui::SameLine(0, 3);
                        if (ImGui::SmallButton("...")) {
                            ImGui::OpenPopup("Pages");
                        }
                        if (ImGui::BeginPopup("Pages")) {
                            int page = static_cast<int>(index) + 1;
                            if (Slider("Page", page, 1, static_cast<int>(size))) {
                                current = {g, static_cast<unsigned>(page - 1), false};
                            }
                            ImGui::EndPopup();
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();

        settingsRect_ = {};
        if (editing && settingsOpen_) {
            DrawSettings(live, selected);
        }

        statusRect_ = {};
        const auto& display = baseline_ ? preview_ : live;
        if (HasDraft() || display.missing || !display.notice.empty() || !feedback.empty()) {
            const auto yPos = std::clamp(panel.Bottom() + 4, 0.0f, std::max(0.0f, screen.y - 95));
            const auto toastX = settingsOpen_ ? settingsRect_.Right() + 6 : 6;
            ImGui::SetNextWindowPos({std::min(toastX, std::max(0.0f, screen.x - 460)), yPos}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({std::min(screen.x, 450.0f), 0}, ImGuiCond_Always);
            if (ImGui::Begin("##TFME status", nullptr, kOverlayFlags | ImGuiWindowFlags_AlwaysAutoResize | (HasDraft() ? 0 : ImGuiWindowFlags_NoInputs))) {
                if (HasDraft()) {
                    const auto statusPosition = ImGui::GetWindowPos();
                    const auto statusSize = ImGui::GetWindowSize();
                    statusRect_ = {statusPosition.x, statusPosition.y, statusSize.x, statusSize.y};
                    if (ImGui::Button("Apply preview")) {
                        Apply();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel preview")) {
                        Cancel();
                    }
                }
                if (display.missing) {
                    ImGui::TextWrapped("%u favorites could not be placed. Make at least one Page cell and free space.", display.missing);
                }
                if (!display.notice.empty()) {
                    ImGui::TextWrapped("%s", display.notice.c_str());
                }
                if (!feedback.empty()) {
                    ImGui::TextWrapped("%s", feedback.c_str());
                }
            }
            ImGui::End();
        }
    }

    void GridEditor::DrawSettings(const Grid& live, ItemKey selected)
    {
        const auto screen = ImGui::GetIO()->DisplaySize;
        settingsRect_ = {6, 66, std::min(336.0f, screen.x - 12), std::max(1.0f, screen.y - 144)};
        ImGui::SetNextWindowPos({settingsRect_.x, settingsRect_.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({settingsRect_.width, settingsRect_.height}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        if (ImGui::Begin("Layout editor", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing)) {
            auto& layout = Layout::GetSingleton();
            if (!HasDraft()) {
                if (ImGui::Button("Undo")) {
                    layout.Undo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Redo")) {
                    layout.Redo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Restore backup")) {
                    feedback = layout.RestoreBackup() ? "Saved backup restored." : "No saved backup available.";
                }
            }
            auto candidate = baseline_ ? *baseline_ : live;
            auto& s = candidate.settings;
            bool dirty = false;
            std::optional<Location> organize;
            bool wholeGroup = false;
            ImGui::TextWrapped("Drag a tile to move. Right-drag to grow. Shift + right-drag to shrink. Hold near a tile edge to copy its size.");
            if (regionMode) {
                ImGui::TextWrapped("Hold the left button, then drag to toggle each cell in the rectangle. Teal cells stay visible on every page.");
            }

            if (ImGui::CollapsingHeader("Grid and window", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto columns = s.columns;
                auto rows = s.rows;
                if (Slider("Columns", columns, 1, kGridLimit) | Slider("Rows", rows, 1, kGridLimit)) {
                    candidate.SetDimensions(columns, rows);
                    dirty = true;
                }
                dirty |= Slider("Window X (%)", s.x, 0, 95);
                dirty |= Slider("Window Y (%)", s.y, 0, 95);
                dirty |= Slider("Window width (%)", s.width, 5, 100);
                dirty |= Slider("Window height (%)", s.height, 5, 100);
                dirty |= ImGui::Checkbox("Fit cells to window", &s.fit);
                if (!s.fit) {
                    dirty |= Slider("Cell size at 1080p", s.cellSize, 24, 320);
                }
                dirty |= Slider("Gap at 1080p", s.gap, 0, 40);
                dirty |= Slider("Margin at 1080p", s.margin, 0, 40);
                dirty |= ImGui::Checkbox("Wrap Q / E navigation", &s.wrap);
                if (ImGui::BeginCombo("Page controls", kNavNames[static_cast<unsigned>(s.navigation)])) {
                    for (unsigned i = 0; i < kNavNames.size(); ++i) {
                        if (ImGui::Selectable(kNavNames[i], i == static_cast<unsigned>(s.navigation))) {
                            s.navigation = static_cast<Navigation>(i);
                            dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (s.navigation == Navigation::floating) {
                    dirty |= Slider("Controls X (%)", s.navX, 0, 95);
                    dirty |= Slider("Controls Y (%)", s.navY, 0, 95);
                }
            }
            if (ImGui::CollapsingHeader("Categories and pages")) {
                if (ImGui::Button("Add category") && candidate.groups.size() < kGroupLimit &&
                    candidate.PageCount() < kPageLimit) {
                    candidate.groups.push_back({"New category", "+"});
                    category_ = static_cast<unsigned>(candidate.groups.size() - 1);
                    dirty = true;
                }
                if (ImGui::BeginCombo("Category", candidate.groups[category_].name.c_str())) {
                    for (unsigned g = 0; g < candidate.groups.size(); ++g) {
                        ImGui::PushID(static_cast<int>(g));
                        if (ImGui::Selectable(candidate.groups[g].name.c_str(), category_ == g)) {
                            category_ = g;
                            current = {g, 0, false};
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                auto& group = candidate.groups[category_];
                auto name = group.name;
                if (TextField("Name", name, std::array<char, 64>{}) && !name.empty()) {
                    group.name = std::move(name);
                    dirty = true;
                }
                dirty |= TextField("Icon", group.icon, std::array<char, 16>{});
                for (unsigned type = 0; type < kCategoryNames.size(); ++type) {
                    dirty |= ImGui::Checkbox(kCategoryNames[type], &group.accepts[type]);
                }
                if (ImGui::Button("Add page") && candidate.PageCount() < kPageLimit) {
                    group.pages.push_back({group.pages.back().sort, true, false});
                    current = {category_, static_cast<unsigned>(group.pages.size() - 1), false};
                    dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove empty page") && current.group == category_) {
                    dirty |= candidate.DeleteEmptyPage(current);
                }
                if (ImGui::Button("Earlier") && category_ > 0 && candidate.MoveGroup(category_, category_ - 1)) {
                    --category_;
                    current = {category_, 0, false};
                    dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Later") && candidate.MoveGroup(category_, category_ + 1)) {
                    ++category_;
                    current = {category_, 0, false};
                    dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete category") && candidate.DeleteGroup(category_)) {
                    dirty = true;
                }

                ClampPage(candidate);
                auto& page = candidate.groups[current.group].pages[current.page];
                ImGui::Text("%s / %u", candidate.groups[current.group].name.c_str(), current.page + 1);
                if (ImGui::BeginCombo("Current page sort", kSortNames[static_cast<unsigned>(page.sort)])) {
                    for (unsigned sort = 0; sort < kSortNames.size(); ++sort) {
                        if (ImGui::Selectable(kSortNames[sort], static_cast<unsigned>(page.sort) == sort)) {
                            page.sort = static_cast<Sort>(sort);
                            page.edited = true;
                            organize = current;
                            dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Organize page")) {
                    organize = current;
                    dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Organize category")) {
                    organize = current;
                    wholeGroup = true;
                    dirty = true;
                }
            }
            if (const auto* selectedTile = candidate.Find(selected);
                selectedTile && ImGui::CollapsingHeader("Selected tile", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto tile = *selectedTile;
                if (ImGui::Button("Reset to 1 x 1")) {
                    tile.cells.w = tile.cells.h = tile.wantedWidth = tile.wantedHeight = 1;
                    dirty |= candidate.Move(tile);
                }
                if (!tile.pinned && ImGui::Button("Pin position")) {
                    dirty |= candidate.Move(tile);
                }
                if (tile.location != current && ImGui::Button("Move here")) {
                    tile.location = current;
                    auto destination = candidate.Imitate(tile);
                    if (!destination) {
                        tile.cells.w = tile.cells.h = 1;
                        destination = candidate.Imitate(tile);
                        if (destination) {
                            feedback = "The tile will use 1 x 1 on this page. Its preferred size was kept.";
                        }
                    }
                    if (destination) {
                        dirty |= candidate.Move(*destination);
                    } else {
                        feedback = "This page has no free cell.";
                    }
                }
            }
            if (dirty) {
                Stage(std::move(candidate), organize ? organize : organize_, organize ? wholeGroup : wholeGroup_);
            }
        }
        ImGui::End();
    }
}

#include "UI.h"

#include "Actions.h"
#include "Config.h"
#include "Favorites.h"
#include "IconLayer.h"
#include "GridEditor.h"
#include "PreviewCache.h"
#include "SKSEMenuFramework.h"
#include "Theme.h"
#include "logger.h"

namespace TFM::UI
{
    namespace ImGui = ImGuiMCP;
    using namespace ImGuiMCP;

    namespace
    {
        SKSEMenuFramework::Model::WindowInterface* window = nullptr;
        std::unique_ptr<SKSEMenuFramework::Model::InputEvent> inputRegistration;
        std::unique_ptr<SKSEMenuFramework::Model::Event> frameworkEventRegistration;
        std::unique_ptr<SKSEMenuFramework::Model::HudElement> meshPrewarmRegistration;
        ItemKey focusedItem{};
        GridEditor gridEditor;
        std::atomic_int pendingPageTurn{};
        std::atomic_bool pendingCancel{};
        std::atomic_bool canCancelEdit{};
        std::atomic_bool enteringText{};
        struct DragState
        {
            ItemKey source{};
            ItemKey target{};
            Rect sourceRect{};
            ImGui::ImVec2 pressPosition{};
            ImGui::ImVec2 grabOffset{};
            ImGui::ImVec2 pointerPosition{};
            std::optional<Placement> preview;
            std::optional<Cells> region;
            Cells startCell;
            float held{};
            float edgeHeld{};
            int horizontal{}, vertical{};
            bool blocked{};
            bool frameSelection{};
            bool resize{};
            bool shrink{};
            bool active{};
            bool controller{};
            bool dropRequested{};
        };
        DragState dragState{};
        struct PointerInputFrame
        {
            bool leftPressed{ false };
            bool leftReleased{ false };
            bool rightPressed{ false };
            bool rightReleased{ false };
            bool rightDown{ false };
            bool leftDown{ false };
        };
        PointerInputFrame pointerInput{};
        bool favoritesButtonDown = false;
        bool registered = false;
        bool nativeBlurActive = false;
        bool controllerInputActive = false;
        enum class ActiveInputDevice : std::uint8_t
        {
            kUnknown,
            kKeyboardMouse,
            kController
        };
        std::atomic<ActiveInputDevice> activeInputDevice{ ActiveInputDevice::kUnknown };
        bool centerCursorOnNextFrame = false;
        std::uint8_t scaleformCursorRequestCooldown = 0;
        std::atomic_bool replacementAvailable{ false };
        std::atomic_bool equipmentStateChanged{ false };
        std::atomic_uint64_t inputHotkeyTarget{ 0 };
        std::atomic_uint64_t pendingHotkeyTarget{ 0 };
        std::atomic_int pendingHotkey{ 0 };
        std::atomic<float> controllerStickX{ 0.0f };
        std::atomic<float> controllerStickY{ 0.0f };
        std::atomic_bool pendingControllerDragPress{ false };
        std::atomic_bool pendingControllerDragRelease{ false };
        std::atomic_bool pendingControllerConfirmPress{ false };
        enum class ControllerAction : std::uint8_t
        {
            kNone,
            kConfirm,
            kCancel,
            kDrag,
            kSuppressWindowing
        };
        enum class ControllerEquipRequest : std::uint8_t
        {
            kNone,
            kLeft,
            kRight
        };
        std::atomic<ControllerEquipRequest> pendingControllerEquipRequest{ ControllerEquipRequest::kNone };
        std::optional<Direction> repeatedControllerDirection;
        float controllerDirectionRepeatTimer = 0.0f;
        bool nonBlockingInputFrame = false;
        bool previousMouseLeftDown = false;
        bool previousMouseRightDown = false;
        bool initialMeshRevealComplete = false;
        bool incrementalMeshRevealAllowed = false;
        bool meshPrewarmReady = false;
        bool meshPrewarmRefreshRequested = true;
        std::uint8_t postActivationRefreshFrames = 0;
        float refreshTimer = 0.0f;
        constexpr std::uint8_t kPostActivationRefreshFrames = 12;
        constexpr float kDragThreshold = 10.0f;
        constexpr float kControllerDragDeadzone = 0.2f;
        constexpr float kControllerDragSpeed = 0.75f;
        constexpr float kControllerActivationDeadzone = 0.2f;
        constexpr float kControllerNavigationDeadzone = 0.5f;
        constexpr float kControllerNavigationInitialRepeatDelay = 0.3f;
        constexpr float kControllerNavigationRepeatInterval = 0.1f;
        constexpr float kItemLabelHorizontalPadding = 12.0f;
        constexpr float kDefaultEquipIndicatorSize = 24.0f;
        constexpr float kDefaultHotkeyIndicatorSize = 26.0f;
        constexpr float kDefaultItemIndicatorGap = 6.0f;
        constexpr std::uint8_t kScaleformCursorRetryFrames = 30;

        void ResetControllerInput()
        {
            controllerStickX.store(0.0f, std::memory_order_release);
            controllerStickY.store(0.0f, std::memory_order_release);
            pendingControllerDragPress.store(false, std::memory_order_release);
            pendingControllerDragRelease.store(false, std::memory_order_release);
            pendingControllerConfirmPress.store(false, std::memory_order_release);
            pendingControllerEquipRequest.store(ControllerEquipRequest::kNone, std::memory_order_release);
            repeatedControllerDirection.reset();
            controllerDirectionRepeatTimer = 0.0f;
            pointerInput = {};
            nonBlockingInputFrame = false;
            previousMouseLeftDown = false;
            previousMouseRightDown = false;
        }

        class PlayerEquipEventSink final : public RE::BSTEventSink<RE::TESEquipEvent>
        {
        public:
            static PlayerEquipEventSink& GetSingleton()
            {
                static PlayerEquipEventSink singleton;
                return singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESEquipEvent* event,
                RE::BSTEventSource<RE::TESEquipEvent>*) override
            {
                const auto player = RE::PlayerCharacter::GetSingleton();
                if (event && player && event->actor.get() == player) {
                    equipmentStateChanged.store(true, std::memory_order_release);
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            PlayerEquipEventSink() = default;
        };

        constexpr ImGui::ImU32 kText = Theme::kTextColor;
        constexpr ImGui::ImU32 kHotkeyBackground = IM_COL32(12, 14, 18, 210);
        constexpr float kFocusOutlineThickness = 3.0f;

        [[nodiscard]] ImGui::ImVec2 Min(const Rect& rect) { return { rect.x, rect.y }; }
        [[nodiscard]] ImGui::ImVec2 Max(const Rect& rect) { return { rect.Right(), rect.Bottom() }; }

        [[nodiscard]] std::uint64_t PackItemKey(const ItemKey& key)
        {
            return (static_cast<std::uint64_t>(key.formID) << 16U) | key.uniqueID;
        }

        [[nodiscard]] ItemKey UnpackItemKey(std::uint64_t packed)
        {
            return {
                static_cast<RE::FormID>(packed >> 16U),
                static_cast<std::uint16_t>(packed & 0xFFFFU)
            };
        }

        void SetNativeBlur(bool enabled)
        {
            if (enabled == nativeBlurActive) {
                return;
            }
            const auto blur = RE::UIBlurManager::GetSingleton();
            if (!blur) {
                return;
            }
            if (enabled) {
                blur->IncrementBlurCount();
            } else {
                blur->DecrementBlurCount();
            }
            nativeBlurActive = enabled;
        }

        [[nodiscard]] bool Contains(const Rect& rect, const ImGui::ImVec2& point)
        {
            return point.x >= rect.x && point.x <= rect.Right() &&
                point.y >= rect.y && point.y <= rect.Bottom();
        }

        [[nodiscard]] ImGui::ImVec2 MeasureText(float size, std::string_view text)
        {
            return ImGui::ImFontManger::CalcTextSizeA(
                ImGui::GetFont(),
                size,
                std::numeric_limits<float>::max(),
                0.0f,
                text.data(),
                text.data() + text.size(),
                nullptr);
        }

        [[nodiscard]] std::string BuildItemLabel(
            std::string_view name,
            std::uint32_t count,
            float fontSize,
            float maximumWidth)
        {
            const auto suffix = count > 1 ? std::format(" ({})", count) : std::string{};
            auto visibleName = std::string(name);
            auto label = visibleName + suffix;
            if (MeasureText(fontSize, label).x <= maximumWidth) {
                return label;
            }

            constexpr auto ellipsis = "..."sv;
            while (!visibleName.empty()) {
                auto characterStart = visibleName.size() - 1;
                while (characterStart > 0 &&
                    (static_cast<unsigned char>(visibleName[characterStart]) & 0xC0U) == 0x80U) {
                    --characterStart;
                }
                visibleName.erase(characterStart);
                label = visibleName + ellipsis.data() + suffix;
                if (MeasureText(fontSize, label).x <= maximumWidth) {
                    return label;
                }
            }

            return suffix.empty() ? std::string(ellipsis) : suffix.substr(1);
        }

        struct ItemLabelLayout
        {
            std::string text;
            float x{ 0.0f };
            float y{ 0.0f };
            Rect equipIndicator{};
            Rect hotkeyIndicator{};
            float fontSize{};
        };

        struct RenderTile
        {
            LeafRect leaf{};
            FavoriteItem item;
            Rect preview{};
            ItemLabelLayout label{};
            bool hovered{ false };
            bool focused{ false };
            bool dropTarget{ false };
        };

        struct RenderGhost
        {
            ItemKey key{};
            FavoriteItem item;
            Rect bounds{};
            Rect preview{};
            ItemLabelLayout label{};
        };

        struct RenderScene
        {
            std::vector<RenderTile> tiles;
            std::optional<RenderGhost> ghost;
        };

        [[nodiscard]] ItemLabelLayout CalculateItemLabelLayout(
            const FavoriteItem& item,
            const Rect& bounds,
            const Rect& preview)
        {
            const bool showEquipIndicator = item.equipState != EquipState::kNone;
            const bool showHotkeyIndicator = item.hotkey > 0;
            const auto configuredFontSize = static_cast<float>(Config::Get().textSize);
            const auto fontSize = std::min({configuredFontSize, bounds.height * 0.22f, bounds.width * 0.20f});
            const auto padding = kItemLabelHorizontalPadding * (fontSize / configuredFontSize);
            const auto indicatorScale = fontSize / static_cast<float>(Config::kTextSizeDefault);
            const auto equipIndicatorSize = kDefaultEquipIndicatorSize * indicatorScale;
            const auto hotkeyIndicatorSize = kDefaultHotkeyIndicatorSize * indicatorScale;
            const auto indicatorGap = kDefaultItemIndicatorGap * indicatorScale;
            const auto leadingWidth = showEquipIndicator ? equipIndicatorSize + indicatorGap : 0.0f;
            const auto trailingWidth = showHotkeyIndicator ? indicatorGap + hotkeyIndicatorSize : 0.0f;
            const auto maximumTextWidth = std::max(
                1.0f,
                bounds.width - padding * 2.0f - leadingWidth - trailingWidth);
            auto text = BuildItemLabel(item.name, item.count, fontSize, maximumTextWidth);
            const auto measuredText = MeasureText(fontSize, text);
            const auto groupLeft = bounds.CenterX() - (leadingWidth + measuredText.x + trailingWidth) * 0.5f;
            const auto labelY = preview.Bottom() + 2.0f * (fontSize / configuredFontSize);

            Rect equipIndicator{};
            if (showEquipIndicator) {
                equipIndicator = {
                    groupLeft,
                    labelY + (measuredText.y - equipIndicatorSize) * 0.5f,
                    equipIndicatorSize,
                    equipIndicatorSize
                };
            }
            Rect hotkeyIndicator{};
            if (showHotkeyIndicator) {
                hotkeyIndicator = {
                    groupLeft + leadingWidth + measuredText.x + indicatorGap,
                    labelY + (measuredText.y - hotkeyIndicatorSize) * 0.5f,
                    hotkeyIndicatorSize,
                    hotkeyIndicatorSize
                };
            }
            return {
                std::move(text),
                groupLeft + leadingWidth,
                labelY,
                equipIndicator,
                hotkeyIndicator,
                fontSize
            };
        }

        void DrawText(ImGui::ImDrawList* drawList, float size, float x, float y, ImGui::ImU32 color, std::string_view text)
        {
            ImGui::ImDrawListManager::AddText(
                drawList,
                ImGui::GetFont(),
                size,
                { x, y },
                color,
                text.data(),
                text.data() + text.size());
        }

        void DrawHotkeyIndicator(
            ImGui::ImDrawList* drawList,
            std::int8_t hotkey,
            const Rect& indicator)
        {
            if (hotkey < 1 || hotkey > 8 || indicator.width <= 0.0f || indicator.height <= 0.0f) {
                return;
            }

            const ImGui::ImVec2 center{ indicator.CenterX(), indicator.CenterY() };
            const auto radius = std::max(1.0f, std::min(indicator.width, indicator.height) * 0.46f);
            ImGui::ImDrawListManager::AddCircleFilled(drawList, center, radius, kHotkeyBackground, 20);
            ImGui::ImDrawListManager::AddCircle(
                drawList,
                center,
                radius,
                kText,
                20,
                std::max(1.0f, indicator.height * 0.055f));

            const char character = static_cast<char>('0' + hotkey);
            const std::string_view text{ std::addressof(character), 1 };
            const auto fontSize = std::max(1.0f, indicator.height * 0.68f);
            const auto textSize = MeasureText(fontSize, text);
            DrawText(
                drawList,
                fontSize,
                center.x - textSize.x * 0.5f,
                center.y - textSize.y * 0.5f,
                kText,
                text);
        }

        [[nodiscard]] const LeafRect* FindLeaf(const std::vector<LeafRect>& leaves, const ItemKey& key)
        {
            const auto match = std::ranges::find(leaves, key, &LeafRect::item);
            return match == leaves.end() ? nullptr : std::addressof(*match);
        }

        [[nodiscard]] ItemKey FindNeighbor(
            const std::vector<LeafRect>& leaves,
            const ItemKey& current,
            Direction direction)
        {
            const auto source = FindLeaf(leaves, current);
            if (!source) {
                return leaves.empty() ? ItemKey{} : leaves.front().item;
            }

            ItemKey best{};
            auto bestScore = std::numeric_limits<float>::max();
            bool foundAligned = false;
            const bool horizontal = direction == Direction::kLeft || direction == Direction::kRight;
            for (const auto& candidate : leaves) {
                if (candidate.item == current) {
                    continue;
                }
                const auto dx = candidate.rect.CenterX() - source->rect.CenterX();
                const auto dy = candidate.rect.CenterY() - source->rect.CenterY();
                const bool valid =
                    (direction == Direction::kLeft && dx < -1.0f) ||
                    (direction == Direction::kRight && dx > 1.0f) ||
                    (direction == Direction::kUp && dy < -1.0f) ||
                    (direction == Direction::kDown && dy > 1.0f);
                if (!valid) {
                    continue;
                }

                const auto secondaryOverlap = horizontal ?
                    std::min(source->rect.Bottom(), candidate.rect.Bottom()) -
                        std::max(source->rect.y, candidate.rect.y) :
                    std::min(source->rect.Right(), candidate.rect.Right()) -
                        std::max(source->rect.x, candidate.rect.x);
                const bool aligned = secondaryOverlap > 1.0f;
                // Stay in the current row or column whenever possible; diagonals are a fallback.
                if (aligned && !foundAligned) {
                    foundAligned = true;
                    best = {};
                    bestScore = std::numeric_limits<float>::max();
                } else if (!aligned && foundAligned) {
                    continue;
                }

                const auto primary = horizontal ? std::abs(dx) : std::abs(dy);
                const auto secondary = horizontal ? std::abs(dy) : std::abs(dx);
                const auto score = primary + secondary * 0.45f;
                if (score < bestScore) {
                    bestScore = score;
                    best = candidate.item;
                }
            }
            return best;
        }

        [[nodiscard]] bool UsesNonBlockingInput()
        {
            return window &&
                window->IsOpen.load(std::memory_order_acquire) &&
                !window->BlockUserInput.load(std::memory_order_acquire) &&
                !SKSEMenuFramework::IsAnyBlockingWindowOpened();
        }

        void BeginInputFrame()
        {
            nonBlockingInputFrame = UsesNonBlockingInput();
            if (!nonBlockingInputFrame) {
                pointerInput = {};
                previousMouseLeftDown = false;
                previousMouseRightDown = false;
                return;
            }

            const auto leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            const auto rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            pointerInput = {
                .leftPressed = leftDown && !previousMouseLeftDown,
                .leftReleased = !leftDown && previousMouseLeftDown,
                .rightPressed = rightDown && !previousMouseRightDown,
                .rightReleased = !rightDown && previousMouseRightDown,
                .rightDown = rightDown,
                .leftDown = leftDown
            };
            previousMouseLeftDown = leftDown;
            previousMouseRightDown = rightDown;
        }

        void __stdcall HandleFrameworkEvent(SKSEMenuFramework::Model::EventType eventType)
        {
            if (eventType == SKSEMenuFramework::Model::EventType::kBeforeRender &&
                UsesNonBlockingInput()) {
                ImGui::GetIO()->ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            }
        }

        [[nodiscard]] ControllerAction ResolveControllerAction(
            const RE::ButtonEvent& button,
            const RE::UserEvents& userEvents)
        {
            if (button.GetDevice() != RE::INPUT_DEVICE::kGamepad) {
                return ControllerAction::kNone;
            }

            const auto userEvent = button.QUserEvent();
            if (userEvent == userEvents.accept || userEvent == userEvents.activate) {
                return ControllerAction::kConfirm;
            }
            if (userEvent == userEvents.cancel ||
                userEvent == userEvents.tweenMenu ||
                userEvent == userEvents.pause) {
                return ControllerAction::kCancel;
            }

            const auto controlMap = RE::ControlMap::GetSingleton();
            if (controlMap && controlMap->GetGamePadType() == RE::PC_GAMEPAD_TYPE::kOrbis) {
                switch (static_cast<RE::BSPCOrbisGamepadDevice::Key>(button.GetIDCode())) {
                case RE::BSPCOrbisGamepadDevice::Key::kPS3_A:
                    return ControllerAction::kConfirm;
                case RE::BSPCOrbisGamepadDevice::Key::kPS3_B:
                    return ControllerAction::kCancel;
                case RE::BSPCOrbisGamepadDevice::Key::kPS3_X:
                    return ControllerAction::kSuppressWindowing;
                case RE::BSPCOrbisGamepadDevice::Key::kPS3_Y:
                    return ControllerAction::kDrag;
                default:
                    return ControllerAction::kNone;
                }
            }

            switch (static_cast<RE::BSWin32GamepadDevice::Key>(button.GetIDCode())) {
            case RE::BSWin32GamepadDevice::Key::kA:
                return ControllerAction::kConfirm;
            case RE::BSWin32GamepadDevice::Key::kB:
                return ControllerAction::kCancel;
            case RE::BSWin32GamepadDevice::Key::kX:
                return ControllerAction::kSuppressWindowing;
            case RE::BSWin32GamepadDevice::Key::kY:
                return ControllerAction::kDrag;
            default:
                return ControllerAction::kNone;
            }
        }

        [[nodiscard]] bool IsGameplayPassthroughInput(
            const RE::ButtonEvent& button,
            const RE::UserEvents& userEvents)
        {
            if (button.GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                return false;
            }

            const auto userEvent = button.QUserEvent();
            if (userEvent == userEvents.forward ||
                userEvent == userEvents.back ||
                userEvent == userEvents.strafeLeft ||
                userEvent == userEvents.strafeRight ||
                userEvent == userEvents.move ||
                userEvent == userEvents.run ||
                userEvent == userEvents.toggleRun ||
                userEvent == userEvents.autoMove ||
                userEvent == userEvents.sprint ||
                userEvent == userEvents.sprintStart ||
                userEvent == userEvents.sprintStop) {
                return true;
            }

            return static_cast<RE::BSKeyboardDevice::Key>(button.GetIDCode()) ==
                RE::BSKeyboardDevice::Key::kPrintScreen;
        }

        [[nodiscard]] ImGui::ImVec2 ControllerStickForScreen()
        {
            const ImGui::ImVec2 raw{
                controllerStickX.load(std::memory_order_acquire),
                controllerStickY.load(std::memory_order_acquire)
            };
            if (raw.x != 0.0f || raw.y != 0.0f) {
                return { raw.x, -raw.y };
            }

            return ImGui::GetKeyMagnitude2d(
                ImGuiKey_GamepadLStickLeft,
                ImGuiKey_GamepadLStickRight,
                ImGuiKey_GamepadLStickUp,
                ImGuiKey_GamepadLStickDown);
        }

        [[nodiscard]] std::optional<Direction> ControllerStickDirection()
        {
            const auto stick = ControllerStickForScreen();
            const auto horizontal = std::abs(stick.x);
            const auto vertical = std::abs(stick.y);
            if (std::max(horizontal, vertical) < kControllerNavigationDeadzone) {
                return std::nullopt;
            }

            if (horizontal >= vertical) {
                return stick.x < 0.0f ? Direction::kLeft : Direction::kRight;
            }
            return stick.y < 0.0f ? Direction::kUp : Direction::kDown;
        }

        [[nodiscard]] std::optional<Direction> PressedGamepadDirection()
        {
            if (const auto direction = ControllerStickDirection()) {
                if (direction != repeatedControllerDirection) {
                    repeatedControllerDirection = direction;
                    controllerDirectionRepeatTimer = kControllerNavigationInitialRepeatDelay;
                    return direction;
                }

                controllerDirectionRepeatTimer -= std::clamp(ImGui::GetIO()->DeltaTime, 0.0f, 0.05f);
                if (controllerDirectionRepeatTimer <= 0.0f) {
                    controllerDirectionRepeatTimer += kControllerNavigationRepeatInterval;
                    return direction;
                }
                return std::nullopt;
            }

            repeatedControllerDirection.reset();
            controllerDirectionRepeatTimer = 0.0f;
            return std::nullopt;
        }

        [[nodiscard]] std::int8_t HotkeyNumber(
            const RE::BSFixedString& userEvent,
            const RE::UserEvents& userEvents)
        {
            const std::array<const RE::BSFixedString*, 8> events{
                std::addressof(userEvents.hotkey1),
                std::addressof(userEvents.hotkey2),
                std::addressof(userEvents.hotkey3),
                std::addressof(userEvents.hotkey4),
                std::addressof(userEvents.hotkey5),
                std::addressof(userEvents.hotkey6),
                std::addressof(userEvents.hotkey7),
                std::addressof(userEvents.hotkey8)
            };
            for (std::size_t index = 0; index < events.size(); ++index) {
                if (userEvent == *events[index]) {
                    return static_cast<std::int8_t>(index + 1);
                }
            }
            return -1;
        }

        [[nodiscard]] ControllerEquipRequest ControllerEquipRequestFor(
            const RE::BSFixedString& userEvent,
            const RE::UserEvents& userEvents)
        {
            // The replacement window normally receives gameplay-context events after the
            // vanilla menu closes; Skyrim's item-menu actions are linked to these attack events.
            if (userEvent == userEvents.leftEquip || userEvent == userEvents.leftAttack) {
                return ControllerEquipRequest::kLeft;
            }
            if (userEvent == userEvents.rightEquip || userEvent == userEvents.rightAttack) {
                return ControllerEquipRequest::kRight;
            }
            return ControllerEquipRequest::kNone;
        }

        [[nodiscard]] bool IsCloseMenuInput(
            const RE::ButtonEvent& button,
            const RE::UserEvents& userEvents)
        {
            const auto userEvent = button.QUserEvent();
            if (userEvent == userEvents.cancel ||
                userEvent == userEvents.tweenMenu ||
                userEvent == userEvents.pause) {
                return true;
            }

            if (button.GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                const auto key = static_cast<RE::BSKeyboardDevice::Key>(button.GetIDCode());
                return key == RE::BSKeyboardDevice::Key::kEscape ||
                    key == RE::BSKeyboardDevice::Key::kTab;
            }
            if (button.GetDevice() == RE::INPUT_DEVICE::kGamepad) {
                return ResolveControllerAction(button, userEvents) == ControllerAction::kCancel;
            }
            return false;
        }

        [[nodiscard]] ItemKey HotkeyTarget(const std::vector<LeafRect>& leaves)
        {
            const auto mouse = ImGui::GetIO()->MousePos;
            const auto hovered = std::ranges::find_if(leaves, [&](const LeafRect& leaf) {
                return Contains(leaf.rect, mouse);
            });
            return hovered != leaves.end() ? hovered->item : focusedItem;
        }

        void RefreshFavorites()
        {
            auto& favorites = Favorites::GetSingleton();
            favorites.Refresh();
            Layout::GetSingleton().Reconcile(favorites.Items());
            PreviewCache::GetSingleton().Reconcile(favorites.Items());
            IconLayer::Reconcile(favorites.Items());
            if (!favorites.Find(focusedItem)) {
                focusedItem = favorites.Items().empty() ? ItemKey{} : favorites.Items().front().key;
            }
            if (dragState.source.IsValid() && !favorites.Find(dragState.source)) {
                dragState = {};
            }
        }

        void __stdcall PrewarmMeshes()
        {
            if (!replacementAvailable.load(std::memory_order_acquire) || !window || window->IsOpen ||
                Config::Get().previewMode != Config::PreviewMode::kMeshes ||
                (meshPrewarmReady && !meshPrewarmRefreshRequested)) {
                return;
            }
            const auto ui = RE::UI::GetSingleton();
            if (!RE::PlayerCharacter::GetSingleton() || !ui || ui->IsApplicationMenuOpen()) {
                return;
            }

            if (std::exchange(meshPrewarmRefreshRequested, false)) {
                RefreshFavorites();
            }

            auto& cache = PreviewCache::GetSingleton();
            cache.SetVisible(false);
            cache.BeginFrame();
            meshPrewarmReady = cache.Prewarm(Favorites::GetSingleton().Items());
        }

        [[nodiscard]] Rect PreviewAreaFor(const Rect& bounds)
        {
            const auto padding = std::min({8.0f, bounds.width * 0.08f, bounds.height * 0.08f});
            const auto labelHeight = std::min(
                std::clamp(bounds.height * 0.24f, 48.0f, 76.0f), bounds.height / 3.0f);
            return {
                bounds.x + padding,
                bounds.y + padding,
                bounds.width - 2.0f * padding,
                bounds.height - labelHeight - 1.5f * padding
            };
        }

        [[nodiscard]] std::optional<Rect> DragGhostRect()
        {
            if (!dragState.active || dragState.resize || dragState.frameSelection) {
                return std::nullopt;
            }
            constexpr float kMaximumGhostWidth = 320.0f;
            constexpr float kMaximumGhostHeight = 150.0f;
            const auto scale = std::min({
                1.0f,
                kMaximumGhostWidth / std::max(1.0f, dragState.sourceRect.width),
                kMaximumGhostHeight / std::max(1.0f, dragState.sourceRect.height)
            });
            return Rect{
                dragState.pointerPosition.x - dragState.grabOffset.x * scale,
                dragState.pointerPosition.y - dragState.grabOffset.y * scale,
                dragState.sourceRect.width * scale,
                dragState.sourceRect.height * scale
            };
        }

        [[nodiscard]] RenderScene BuildRenderScene(
            const std::vector<LeafRect>& leaves,
            bool trackHover,
            bool showFocus)
        {
            RenderScene scene;
            scene.tiles.reserve(leaves.size());
            for (const auto& leaf : leaves) {
                const auto item = Favorites::GetSingleton().Find(leaf.item);
                if (!item) {
                    continue;
                }
                const auto preview = PreviewAreaFor(leaf.rect);
                scene.tiles.push_back({
                    .leaf = leaf,
                    .item = *item,
                    .preview = preview,
                    .label = CalculateItemLabelLayout(*item, leaf.rect, preview),
                    .hovered = trackHover && ImGui::IsMouseHoveringRect(Min(leaf.rect), Max(leaf.rect), true),
                    .focused = showFocus && leaf.item == focusedItem,
                    .dropTarget = dragState.active && dragState.target == leaf.item
                });
            }

            if (const auto bounds = DragGhostRect()) {
                const auto item = Favorites::GetSingleton().Find(dragState.source);
                if (item) {
                    const auto preview = PreviewAreaFor(*bounds);
                    scene.ghost = RenderGhost{
                        .key = dragState.source,
                        .item = *item,
                        .bounds = *bounds,
                        .preview = preview,
                        .label = CalculateItemLabelLayout(*item, *bounds, preview)
                    };
                }
            }
            return scene;
        }

        void SubmitScaleformLayout(
            const ImGui::ImVec2& screen,
            Rect panel,
            const RenderScene& scene,
            const Config::Settings& settings)
        {
            std::optional<IconLayer::DragGhost> dragGhost;
            if (scene.ghost) {
                dragGhost = IconLayer::DragGhost{
                    .key = scene.ghost->key,
                    .bounds = scene.ghost->bounds,
                    .preview = scene.ghost->preview,
                    .equipIndicator = scene.ghost->label.equipIndicator
                };
            }

            std::vector<IconLayer::Tile> iconTiles;
            iconTiles.reserve(scene.tiles.size());
            for (const auto& tile : scene.tiles) {
                iconTiles.push_back({
                    .key = tile.leaf.item,
                    .bounds = tile.leaf.rect,
                    .preview = tile.preview,
                    .hovered = tile.hovered || tile.focused,
                    .dropTarget = tile.dropTarget,
                    .equipState = tile.item.equipState,
                    .equipIndicator = tile.label.equipIndicator
                });
            }

            IconLayer::SubmitLayout(
                screen.x,
                screen.y,
                panel,
                iconTiles,
                dragGhost,
                settings.backdropAlpha,
                settings.tileAlpha,
                settings.tileStyle == Config::TileStyle::kFramed,
                settings.borderScale,
                settings.frameScale);
        }

        void DrawCachedPreview(
            ImGui::ImDrawList* drawList,
            const FavoriteItem& item,
            const Rect& area,
            ImGui::ImU32 tint = IM_COL32_WHITE)
        {
            const auto& cache = PreviewCache::GetSingleton();
            const auto preview = cache.Get(item);
            if (!preview) {
                return;
            }

            const auto imageSize = std::max(1.0f, std::min(area.width, area.height));
            const ImGui::ImVec2 imageMin{
                area.CenterX() - imageSize * 0.5f,
                area.CenterY() - imageSize * 0.5f
            };
            const ImGui::ImVec2 imageMax{ imageMin.x + imageSize, imageMin.y + imageSize };
            ImGui::ImDrawListManager::AddImage(
                drawList,
                reinterpret_cast<ImGui::ImTextureID>(preview.texture),
                imageMin,
                imageMax,
                { preview.placement.u0, preview.placement.v0 },
                { preview.placement.u1, preview.placement.v1 },
                tint);
        }

        void Activate(const FavoriteItem* item, Actions::Hand hand)
        {
            if (!item || !Actions::Activate(*item, hand)) {
                return;
            }
            if (Config::Get().closeOnSelection) {
                Close();
                return;
            }
            RefreshFavorites();
            postActivationRefreshFrames = kPostActivationRefreshFrames;
        }

        void StartControllerDrag(const std::vector<LeafRect>& leaves)
        {
            if (leaves.empty() || dragState.source.IsValid() || gridEditor.HasDraft() || gridEditor.regionMode) {
                return;
            }
            const auto source = FindLeaf(leaves, focusedItem);
            if (!source) {
                return;
            }

            dragState.source = focusedItem;
            dragState.sourceRect = source->rect;
            dragState.grabOffset = {
                source->rect.width * 0.5f,
                source->rect.height * 0.5f
            };
            dragState.pointerPosition = {
                source->rect.CenterX(),
                source->rect.CenterY()
            };
            dragState.active = true;
            dragState.controller = true;
        }

        void UpdateControllerDragPointer()
        {
            auto stick = ControllerStickForScreen();
            const auto magnitude = std::sqrt(stick.x * stick.x + stick.y * stick.y);
            if (magnitude > kControllerDragDeadzone) {
                const auto normalizedMagnitude = std::clamp(
                    (magnitude - kControllerDragDeadzone) / (1.0f - kControllerDragDeadzone),
                    0.0f,
                    1.0f);
                const auto scale = normalizedMagnitude / magnitude;
                stick.x *= scale;
                stick.y *= scale;

                const auto screen = ImGui::GetIO()->DisplaySize;
                const auto deltaTime = std::clamp(ImGui::GetIO()->DeltaTime, 0.0f, 0.05f);
                const auto speed = std::max(screen.x, screen.y) * kControllerDragSpeed;
                dragState.pointerPosition.x += stick.x * speed * deltaTime;
                dragState.pointerPosition.y += stick.y * speed * deltaTime;
                dragState.pointerPosition.x = std::clamp(dragState.pointerPosition.x, 0.0f, screen.x);
                dragState.pointerPosition.y = std::clamp(dragState.pointerPosition.y, 0.0f, screen.y);
            }
        }

        int Quantize(float distance, float step, int previous)
        {
            const auto units = std::clamp(distance / step, -float(kGridLimit), float(kGridLimit));
            if (std::abs(units - previous) <= 0.60f) {
                return previous;
            }
            return static_cast<int>(std::round(units));
        }

        Cells Resized(Cells cells, int dx, int dy, bool shrink)
        {
            const auto change = [shrink](int& position, int& size, int delta) {
                if (shrink) {
                    const int removed = std::min(std::abs(delta), size - 1);
                    if (delta < 0) {
                        position += removed;
                    }
                    size -= removed;
                } else {
                    position += std::min(0, delta);
                    size += std::abs(delta);
                }
            };
            change(cells.x, cells.w, dx);
            change(cells.y, cells.h, dy);
            return cells;
        }

        void UpdateTilePreview(const Grid& live, const GridGeometry& geometry, const std::vector<LeafRect>& leaves)
        {
            const auto* source = live.Find(dragState.source);
            if (!source) {
                return;
            }
            auto tile = *source;
            dragState.preview.reset();
            dragState.blocked = false;
            const auto pointer = dragState.pointerPosition;
            if (dragState.resize) {
                dragState.horizontal = Quantize(pointer.x - dragState.pressPosition.x, geometry.stepX, dragState.horizontal);
                dragState.vertical = Quantize(pointer.y - dragState.pressPosition.y, geometry.stepY, dragState.vertical);
                tile.cells = Resized(tile.cells, dragState.horizontal, dragState.vertical, dragState.shrink);
                const auto valid = live.ResizePreview(tile);
                dragState.blocked = !valid || *valid != tile.cells;
                if (valid) {
                    tile.cells = *valid;
                    if (tile.cells != source->cells) {
                        tile.wantedWidth = tile.cells.w;
                        tile.wantedHeight = tile.cells.h;
                    }
                    dragState.preview = tile;
                }
                return;
            }

            if (gridEditor.OverControls(pointer) || !Contains(geometry.bounds, pointer)) {
                dragState.target = {};
                dragState.edgeHeld = 0;
                dragState.blocked = true;
                return;
            }

            const auto anchor = geometry.CellAt({
                pointer.x - dragState.grabOffset.x + geometry.stepX * 0.5f,
                pointer.y - dragState.grabOffset.y + geometry.stepY * 0.5f
            });
            tile.cells.x = anchor.x;
            tile.cells.y = anchor.y;
            const auto hoveredCell = geometry.CellAt(pointer);
            tile.location = gridEditor.current;
            tile.location.global = live.GlobalAt(hoveredCell.x, hoveredCell.y);

            ItemKey target;
            if (gridEditor.editing) {
                for (const auto& leaf : leaves) {
                    if (leaf.item == source->Key() || !Contains(leaf.rect, pointer)) {
                        continue;
                    }
                    const auto edgeX = std::min(pointer.x - leaf.rect.x, leaf.rect.Right() - pointer.x);
                    const auto edgeY = std::min(pointer.y - leaf.rect.y, leaf.rect.Bottom() - pointer.y);
                    if (edgeX <= std::min(leaf.rect.width * 0.24f, geometry.stepX * 0.35f) ||
                        edgeY <= std::min(leaf.rect.height * 0.24f, geometry.stepY * 0.35f)) {
                        target = leaf.item;
                    }
                    break;
                }
            }
            if (target != dragState.target) {
                dragState.target = target;
                dragState.edgeHeld = 0;
            } else if (target.IsValid()) {
                dragState.edgeHeld += std::max(0.0f, ImGui::GetIO()->DeltaTime);
            }
            if (const auto* other = live.Find(target); other && dragState.edgeHeld >= 0.30f) {
                tile.cells.w = tile.wantedWidth = other->cells.w;
                tile.cells.h = tile.wantedHeight = other->cells.h;
                dragState.preview = live.Imitate(tile);
            } else if (live.Fits(tile)) {
                dragState.preview = tile;
            }
            dragState.blocked = !dragState.preview;
        }

        bool HandlePointer(const Grid& live, const Grid& display, const GridGeometry& geometry,
            const std::vector<LeafRect>& leaves, bool pointerOverGrid)
        {
            const auto mouse = ImGui::GetIO()->MousePos;
            const bool leftPressed = nonBlockingInputFrame ? pointerInput.leftPressed :
                ImGui::IsMouseClicked(ImGuiMouseButton_Left, false);
            const bool rightPressed = nonBlockingInputFrame ? pointerInput.rightPressed :
                ImGui::IsMouseClicked(ImGuiMouseButton_Right, false);
            if (!dragState.source.IsValid() && !dragState.frameSelection && pointerOverGrid) {
                if (gridEditor.regionMode && leftPressed && Contains(geometry.bounds, mouse)) {
                    dragState.frameSelection = true;
                    dragState.startCell = geometry.CellAt(mouse);
                } else if (!gridEditor.regionMode && !gridEditor.HasDraft() && (leftPressed || rightPressed)) {
                    const auto leaf = std::ranges::find_if(leaves, [&](const auto& entry) { return Contains(entry.rect, mouse); });
                    if (leaf != leaves.end()) {
                        focusedItem = leaf->item;
                        dragState.source = leaf->item;
                        dragState.sourceRect = leaf->rect;
                        dragState.pressPosition = mouse;
                        dragState.grabOffset = {mouse.x - leaf->rect.x, mouse.y - leaf->rect.y};
                        dragState.resize = rightPressed && !leftPressed;
                        dragState.shrink = dragState.resize && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                        gridEditor.feedback.clear();
                    }
                }
            }

            const bool released = dragState.resize ?
                (nonBlockingInputFrame ? pointerInput.rightReleased : ImGui::IsMouseReleased(ImGuiMouseButton_Right)) :
                (nonBlockingInputFrame ? pointerInput.leftReleased : ImGui::IsMouseReleased(ImGuiMouseButton_Left));
            if (dragState.frameSelection) {
                dragState.held += std::max(0.0f, ImGui::GetIO()->DeltaTime);
                if (dragState.held >= 0.22f) {
                    auto end = geometry.CellAt(mouse);
                    end.x = std::clamp(end.x, 0, display.settings.columns - 1);
                    end.y = std::clamp(end.y, 0, display.settings.rows - 1);
                    const auto start = dragState.startCell;
                    dragState.region = Cells{std::min(start.x, end.x), std::min(start.y, end.y),
                        std::abs(start.x - end.x) + 1, std::abs(start.y - end.y) + 1};
                }
                if (released) {
                    if (dragState.region) {
                        gridEditor.PreviewMask(live, *dragState.region);
                    }
                    dragState = {};
                }
                return false;
            }

            if (!dragState.source.IsValid()) {
                return false;
            }
            if (dragState.controller) {
                UpdateControllerDragPointer();
            } else {
                dragState.pointerPosition = mouse;
                const auto dx = mouse.x - dragState.pressPosition.x;
                const auto dy = mouse.y - dragState.pressPosition.y;
                dragState.active |= dx * dx + dy * dy >= kDragThreshold * kDragThreshold;
            }
            if (dragState.active) {
                UpdateTilePreview(live, geometry, leaves);
            }

            if (!(dragState.controller ? dragState.dropRequested : released)) {
                return false;
            }
            const auto drag = std::exchange(dragState, {});
            if (drag.active) {
                auto next = live;
                const auto* original = live.Find(drag.source);
                if (drag.preview && original && (drag.preview->cells != original->cells ||
                    drag.preview->location != original->location || drag.preview->wantedWidth != original->wantedWidth ||
                    drag.preview->wantedHeight != original->wantedHeight) && next.Move(*drag.preview)) {
                    auto& layout = Layout::GetSingleton();
                    layout.Commit(std::move(next));
                    if (drag.blocked) {
                        gridEditor.feedback = "Resize stopped at a tile, region boundary, or grid edge.";
                    }
                    return true;
                }
                if (drag.blocked) {
                    gridEditor.feedback = drag.resize ? "Resize blocked by a tile, grid edge, or region boundary." :
                        (drag.target.IsValid() ? "No free rectangle for that size in the target region." :
                            "Drop blocked by a tile, grid edge, or region boundary.");
                }
            } else if (!gridEditor.editing && pointerOverGrid && Contains(drag.sourceRect, mouse)) {
                Activate(Favorites::GetSingleton().Find(drag.source), drag.resize ? Actions::Hand::kLeft : Actions::Hand::kRight);
            }
            return false;
        }

        void DrawGesture(ImGui::ImDrawList* drawList, const GridGeometry& geometry)
        {
            if (!dragState.active) {
                return;
            }
            const auto color = dragState.blocked ? IM_COL32(244, 123, 94, 255) : IM_COL32(93, 227, 199, 255);
            const auto bounds = dragState.preview ? geometry.RectFor(dragState.preview->cells) : dragState.sourceRect;
            ImGui::ImDrawListManager::AddRect(drawList, Min(bounds), Max(bounds), color, 1, 0, 3);
            if (dragState.preview) {
                const auto c = dragState.preview->cells;
                ImGui::SetTooltip("%s%d x %d", dragState.blocked ? "Limit: " :
                    (dragState.target.IsValid() && dragState.edgeHeld >= 0.30f ? "Copy size: " : ""), c.w, c.h);
            } else if (dragState.target.IsValid() && dragState.edgeHeld < 0.30f) {
                ImGui::SetTooltip("Hold to copy tile size");
            } else if (dragState.blocked) {
                ImGui::SetTooltip("No legal placement at the pointer");
            }
        }

        void HandleMenuInput(const std::vector<LeafRect>& leaves)
        {
            const auto dragButtonPressed = pendingControllerDragPress.exchange(false, std::memory_order_acq_rel);
            const auto dragButtonReleased = pendingControllerDragRelease.exchange(false, std::memory_order_acq_rel);
            const auto confirmButtonPressed =
                pendingControllerConfirmPress.exchange(false, std::memory_order_acq_rel) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false);
            const auto equipRequest =
                pendingControllerEquipRequest.exchange(ControllerEquipRequest::kNone, std::memory_order_acq_rel);
            if (dragState.controller) {
                if (dragButtonReleased || confirmButtonPressed) {
                    dragState.dropRequested = true;
                }
                return;
            }
            if (dragButtonPressed) {
                StartControllerDrag(leaves);
                if (dragState.controller && dragButtonReleased) {
                    dragState.dropRequested = true;
                }
                return;
            }
            if (const auto direction = PressedGamepadDirection()) {
                const auto neighbor = FindNeighbor(leaves, focusedItem, *direction);
                if (neighbor.IsValid()) {
                    focusedItem = neighbor;
                }
            }

            if (gridEditor.editing) {
                return;
            }
            if (confirmButtonPressed) {
                Activate(Favorites::GetSingleton().Find(focusedItem), Actions::Hand::kDefault);
                return;
            }
            if (equipRequest == ControllerEquipRequest::kLeft) {
                Activate(Favorites::GetSingleton().Find(focusedItem), Actions::Hand::kLeft);
            } else if (equipRequest == ControllerEquipRequest::kRight) {
                Activate(Favorites::GetSingleton().Find(focusedItem), Actions::Hand::kRight);
            }
        }

        void DrawEmpty(ImGui::ImDrawList* drawList, const Rect& bounds)
        {
            const auto message = "No favorites yet";
            const auto fontSize = static_cast<float>(Config::Get().textSize);
            const auto messageSize = MeasureText(fontSize, message);
            DrawText(
                drawList,
                fontSize,
                bounds.CenterX() - messageSize.x * 0.5f,
                bounds.CenterY() - messageSize.y * 0.5f,
                kText,
                message);
        }

        void DrawItemLabel(ImGui::ImDrawList* drawList, const Rect& bounds, const ItemLabelLayout& label,
            std::int8_t hotkey)
        {
            ImGui::PushClipRect(Min(bounds), Max(bounds), true);
            DrawText(drawList, label.fontSize, label.x, label.y, kText, label.text);
            DrawHotkeyIndicator(drawList, hotkey, label.hotkeyIndicator);
            ImGui::PopClipRect();
        }

        void DrawTile(ImGui::ImDrawList* drawList, const RenderTile& tile)
        {
            const auto& item = tile.item;
            if (Config::Get().previewMode == Config::PreviewMode::kMeshes) {
                DrawCachedPreview(drawList, item, tile.preview);
            }

            DrawItemLabel(drawList, tile.leaf.rect, tile.label, item.hotkey);
            if (tile.focused) {
                ImGui::ImDrawListManager::AddRect(
                    drawList,
                    Min(tile.leaf.rect),
                    Max(tile.leaf.rect),
                    Theme::kFocusColor,
                    Theme::kCornerRadius,
                    0,
                    kFocusOutlineThickness);
            }

            const auto dragSource = dragState.active && dragState.source == item.key;
            if (dragSource) {
                ImGui::ImDrawListManager::AddRectFilled(
                    drawList,
                    Min(tile.leaf.rect),
                    Max(tile.leaf.rect),
                    IM_COL32(5, 7, 10, 172),
                    Theme::kCornerRadius,
                    0);
            }
        }

        void DrawDragOverlay(ImGui::ImDrawList* drawList, const std::optional<RenderGhost>& ghost)
        {
            if (!ghost) {
                return;
            }
            if (Config::Get().previewMode == Config::PreviewMode::kMeshes) {
                DrawCachedPreview(
                    drawList,
                    ghost->item,
                    ghost->preview,
                    IM_COL32(255, 255, 255, 235));
            }
            DrawItemLabel(drawList, ghost->bounds, ghost->label, ghost->item.hotkey);
        }

        void RestoreVanillaAfterPresentationFailure()
        {
            if (!replacementAvailable.exchange(false, std::memory_order_acq_rel)) {
                return;
            }

            logger::error("Tileable favorites presentation failed; restoring the vanilla FavoritesMenu for this session");
            Close();
            favoritesButtonDown = false;
            if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::FavoritesMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kShow, nullptr);
            }
        }

        void ObserveInputEvent(const RE::InputEvent& event)
        {
            if (const auto button = event.AsButtonEvent(); button && !button->IsDown()) {
                return;
            }

            switch (event.GetDevice()) {
            case RE::INPUT_DEVICE::kKeyboard:
            case RE::INPUT_DEVICE::kMouse:
                activeInputDevice.store(ActiveInputDevice::kKeyboardMouse, std::memory_order_release);
                break;
            case RE::INPUT_DEVICE::kGamepad:
                if (const auto thumbstick = event.AsThumbstickEvent(); thumbstick) {
                    const auto magnitude = std::sqrt(
                        thumbstick->xValue * thumbstick->xValue +
                        thumbstick->yValue * thumbstick->yValue);
                    if (magnitude < kControllerActivationDeadzone) {
                        return;
                    }
                }
                activeInputDevice.store(ActiveInputDevice::kController, std::memory_order_release);
                break;
            default:
                break;
            }
        }

        [[nodiscard]] bool IsControllerActive()
        {
            switch (activeInputDevice.load(std::memory_order_acquire)) {
            case ActiveInputDevice::kKeyboardMouse:
                return false;
            case ActiveInputDevice::kController:
                return true;
            case ActiveInputDevice::kUnknown:
                break;
            }

            const auto inputManager = RE::BSInputDeviceManager::GetSingleton();
            return inputManager && inputManager->IsGamepadEnabled();
        }

        void HideScaleformCursor()
        {
            scaleformCursorRequestCooldown = 0;
            if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::CursorMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }

        void UpdateControllerInputMode()
        {
            const auto active = IsControllerActive();
            if (active == controllerInputActive) {
                return;
            }

            controllerInputActive = active;
            if (active) {
                centerCursorOnNextFrame = false;
                HideScaleformCursor();
            } else {
                scaleformCursorRequestCooldown = 0;
                centerCursorOnNextFrame = true;
            }
        }

        [[nodiscard]] bool SynchronizeScaleformCursor()
        {
            const auto ui = RE::UI::GetSingleton();
            if (!ui) {
                return false;
            }

            const auto cursorMenu = ui->GetMenu<RE::CursorMenu>();
            if (cursorMenu && cursorMenu->OnStack()) {
                scaleformCursorRequestCooldown = 0;
                if (cursorMenu->uiMovie) {
                    const auto mouse = ImGui::GetIO()->MousePos;
                    cursorMenu->uiMovie->NotifyMouseState(mouse.x, mouse.y, 0);
                    return true;
                }
                return false;
            }

            if (ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
                return false;
            }
            if (scaleformCursorRequestCooldown > 0) {
                --scaleformCursorRequestCooldown;
                return false;
            }

            if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::CursorMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kShow, nullptr);
                scaleformCursorRequestCooldown = kScaleformCursorRetryFrames;
            }
            return false;
        }

        void __stdcall Render()
        {
            if (!window || !window->IsOpen) {
                return;
            }

            BeginInputFrame();
            UpdateControllerInputMode();
            const bool equipEventReceived = equipmentStateChanged.exchange(false, std::memory_order_acq_rel);
            const bool postActivationRefresh = postActivationRefreshFrames > 0;
            if (postActivationRefresh) {
                --postActivationRefreshFrames;
            }
            bool hotkeyChanged = false;
            if (const auto number = pendingHotkey.exchange(0, std::memory_order_acq_rel); number > 0) {
                const auto target = UnpackItemKey(pendingHotkeyTarget.load(std::memory_order_acquire));
                if (target.IsValid() && Favorites::GetSingleton().AssignHotkey(target, static_cast<std::int8_t>(number))) {
                    focusedItem = target;
                    hotkeyChanged = true;
                }
            }
            refreshTimer += std::max(0.0f, ImGui::GetIO()->DeltaTime);
            if (hotkeyChanged || equipEventReceived || postActivationRefresh || refreshTimer >= 0.5f) {
                refreshTimer = 0.0f;
                RefreshFavorites();
            }

            auto& previewCache = PreviewCache::GetSingleton();

            const auto screen = ImGui::GetIO()->DisplaySize;
            if (!controllerInputActive && centerCursorOnNextFrame) {
                centerCursorOnNextFrame = false;
                ImGui::TeleportMousePos({ screen.x * 0.5f, screen.y * 0.5f });
            }
            if (controllerInputActive || SynchronizeScaleformCursor()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            }
            const auto& settings = Config::Get();
            previewCache.BeginFrame();

            if (screen.x < 2 || screen.y < 2) {
                return;
            }
            auto& layout = Layout::GetSingleton();
            auto live = layout.Snapshot();
            if (pendingCancel.exchange(false, std::memory_order_acq_rel)) {
                if (dragState.source.IsValid() || dragState.frameSelection) {
                    dragState = {};
                } else if (!gridEditor.Cancel()) {
                    gridEditor.editing = false;
                    gridEditor.regionMode = false;
                }
            }
            auto display = gridEditor.Draft(live);
            gridEditor.ClampPage(display);
            if (const auto turn = pendingPageTurn.exchange(0, std::memory_order_acq_rel); turn != 0) {
                dragState = {};
                gridEditor.Turn(display, turn);
            }
            const auto geometry = gridEditor.Geometry(display, screen);
            const auto bounds = geometry.window;
            auto leaves = gridEditor.Leaves(display, geometry);
            if (controllerInputActive && !FindLeaf(leaves, focusedItem)) {
                focusedItem = leaves.empty() ? ItemKey{} : leaves.front().item;
            }
            inputHotkeyTarget.store(gridEditor.editing ? 0 : PackItemKey(HotkeyTarget(leaves)), std::memory_order_release);

            bool layoutSubmitted = false;
            std::optional<RenderScene> renderScene;
            auto presentationState = IconLayer::GetPresentationState();
            if (presentationState == IconLayer::PresentationState::kFailed) {
                RestoreVanillaAfterPresentationFailure();
                return;
            }
            if (presentationState != IconLayer::PresentationState::kReady) {
                renderScene = BuildRenderScene(leaves, false, controllerInputActive);
                SubmitScaleformLayout(screen, bounds, *renderScene, settings);
                layoutSubmitted = true;
                presentationState = IconLayer::GetPresentationState();
                if (presentationState == IconLayer::PresentationState::kFailed) {
                    RestoreVanillaAfterPresentationFailure();
                    return;
                }
            }
            if (settings.previewMode == Config::PreviewMode::kMeshes) {
                std::vector<FavoriteItem> visibleFavorites;
                visibleFavorites.reserve(leaves.size());
                for (const auto& leaf : leaves) {
                    if (const auto* item = Favorites::GetSingleton().Find(leaf.item)) {
                        visibleFavorites.push_back(*item);
                    }
                }
                const bool meshesReady = previewCache.Update(visibleFavorites);
                if (!initialMeshRevealComplete) {
                    if (!meshesReady && !incrementalMeshRevealAllowed) {
                        return;
                    }
                    initialMeshRevealComplete = true;
                }
            }
            if (presentationState != IconLayer::PresentationState::kReady) {
                return;
            }

            ImGui::SetNextWindowPos({ 0.0f, 0.0f }, ImGuiCond_Always);
            ImGui::SetNextWindowSize(screen, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);
            // Keep the drawing surface full-screen for drag ghosts without covering the control windows' hit areas.
            constexpr auto flags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            if (ImGui::Begin("##TileableFavoritesMenu", nullptr, flags)) {
                const auto drawList = ImGui::GetWindowDrawList();
                HandleMenuInput(leaves);
                if (IsOpen()) {
                    const auto mouse = ImGui::GetIO()->MousePos;
                    const bool pointerOverGrid = Contains(geometry.bounds, mouse) && !gridEditor.OverControls(mouse) &&
                        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !ImGui::IsAnyItemActive() &&
                        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
                    if (HandlePointer(live, display, geometry, leaves, pointerOverGrid)) {
                        live = layout.Snapshot();
                        leaves = gridEditor.Leaves(live, geometry);
                    }
                    if (!renderScene) {
                        const bool trackHover = !controllerInputActive && pointerOverGrid;
                        renderScene = BuildRenderScene(leaves, trackHover, controllerInputActive);
                    }
                    if (!layoutSubmitted) {
                        SubmitScaleformLayout(screen, bounds, *renderScene, settings);
                    }
                    if (renderScene->tiles.empty()) {
                        DrawEmpty(drawList, bounds);
                    } else {
                        for (const auto& tile : renderScene->tiles) {
                            DrawTile(drawList, tile);
                            if (!IsOpen()) {
                                break;
                            }
                        }
                    }
                    if (IsOpen()) {
                        gridEditor.DrawMask(live, display, geometry, dragState.region, drawList);
                        DrawDragOverlay(drawList, renderScene->ghost);
                        DrawGesture(drawList, geometry);
                        if (!dragState.active) {
                            const auto hover = std::ranges::find_if(renderScene->tiles, [](const auto& tile) {
                                return tile.hovered && !tile.label.text.starts_with(tile.item.name);
                            });
                            if (hover != renderScene->tiles.end()) {
                                ImGui::SetTooltip("%s", hover->item.name.c_str());
                            }
                        }
                    }
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
            if (IsOpen()) {
                const auto page = gridEditor.current;
                gridEditor.DrawControls(live, geometry, focusedItem);
                if (gridEditor.current != page) {
                    dragState = {};
                }
                enteringText.store(ImGui::GetIO()->WantTextInput, std::memory_order_release);
                canCancelEdit.store(gridEditor.editing || dragState.source.IsValid() || dragState.frameSelection,
                    std::memory_order_release);
            }
        }

        bool __stdcall ProcessInput(RE::InputEvent* event)
        {
            if (!event) {
                return false;
            }
            ObserveInputEvent(*event);
            if (!window || !replacementAvailable.load(std::memory_order_acquire)) {
                return false;
            }

            const auto menuOpen = window->IsOpen.load(std::memory_order_acquire);
            const auto nonBlockingInput = menuOpen && UsesNonBlockingInput();
            if (menuOpen && event->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
                if (const auto thumbstick = event->AsThumbstickEvent(); thumbstick && thumbstick->IsLeft()) {
                    if (nonBlockingInput) {
                        return false;
                    }
                    controllerStickX.store(thumbstick->xValue, std::memory_order_release);
                    controllerStickY.store(thumbstick->yValue, std::memory_order_release);
                    return true;
                }
            }
            if (nonBlockingInput && event->AsMouseMoveEvent()) {
                return true;
            }
            if (nonBlockingInput && event->AsCharEvent()) {
                return true;
            }

            const auto button = event->AsButtonEvent();
            const auto userEvents = RE::UserEvents::GetSingleton();
            if (!button || !userEvents) {
                return false;
            }

            if (menuOpen) {
                if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                    const auto key = static_cast<RE::BSKeyboardDevice::Key>(button->GetIDCode());
                    if (key == RE::BSKeyboardDevice::Key::kQ || key == RE::BSKeyboardDevice::Key::kE) {
                        if (button->IsDown() && !enteringText.load(std::memory_order_acquire)) {
                            pendingPageTurn.fetch_add(key == RE::BSKeyboardDevice::Key::kQ ? -1 : 1,
                                std::memory_order_release);
                        }
                        if (key == RE::BSKeyboardDevice::Key::kQ && button->IsUp()) {
                            favoritesButtonDown = false;
                        }
                        return true;
                    }
                    if (key == RE::BSKeyboardDevice::Key::kEscape && canCancelEdit.load(std::memory_order_acquire)) {
                        if (button->IsDown()) {
                            pendingCancel.store(true, std::memory_order_release);
                        }
                        return true;
                    }
                }
                if (IsCloseMenuInput(*button, *userEvents)) {
                    if (button->IsDown()) {
                        Close();
                    }
                    return true;
                }

                if (nonBlockingInput && button->GetDevice() == RE::INPUT_DEVICE::kMouse) {
                    return true;
                }

                const auto controllerAction = ResolveControllerAction(*button, *userEvents);
                if (controllerAction == ControllerAction::kDrag) {
                    if (button->IsDown()) {
                        pendingControllerDragPress.store(true, std::memory_order_release);
                    } else if (button->IsUp()) {
                        pendingControllerDragRelease.store(true, std::memory_order_release);
                    }
                    return true;
                }

                if (button->QUserEvent() != userEvents->favorites &&
                    controllerAction == ControllerAction::kConfirm) {
                    if (button->IsDown()) {
                        pendingControllerConfirmPress.store(true, std::memory_order_release);
                    }
                    return true;
                }

                if (button->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
                    const auto equipRequest = ControllerEquipRequestFor(button->QUserEvent(), *userEvents);
                    if (equipRequest != ControllerEquipRequest::kNone) {
                        if (button->IsDown()) {
                            pendingControllerEquipRequest.store(equipRequest, std::memory_order_release);
                        }
                        return true;
                    }
                }

                const auto number = HotkeyNumber(button->QUserEvent(), *userEvents);
                if (number > 0) {
                    if (button->IsDown()) {
                        pendingHotkeyTarget.store(inputHotkeyTarget.load(std::memory_order_acquire), std::memory_order_release);
                        pendingHotkey.store(number, std::memory_order_release);
                    }
                    return true;
                }

                if (controllerAction == ControllerAction::kSuppressWindowing) {
                    return true;
                }

                if (nonBlockingInput && button->QUserEvent() != userEvents->favorites) {
                    if (button->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
                        return true;
                    }
                    if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                        return !IsGameplayPassthroughInput(*button, *userEvents);
                    }
                }
            }

            if (button->QUserEvent() != userEvents->favorites) {
                return false;
            }

            if (!IsOpen()) {
                if (button->IsDown()) {
                    favoritesButtonDown = true;
                } else if (button->IsUp()) {
                    favoritesButtonDown = false;
                }

                // Let Skyrim's FavoritesHandler decide whether the vanilla menu may open.
                // VanillaMenuSink replaces it with this menu only after that succeeds.
                return false;
            }

            if (button->IsDown()) {
                if (!favoritesButtonDown) {
                    favoritesButtonDown = true;
                    Close();
                }
                return true;
            }
            if (button->IsUp()) {
                favoritesButtonDown = false;
                return true;
            }
            return IsOpen();
        }

        class VanillaMenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static VanillaMenuSink& GetSingleton()
            {
                static VanillaMenuSink singleton;
                return singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::MenuOpenCloseEvent* event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!event || !window || !replacementAvailable.load(std::memory_order_acquire)) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (!event->opening) {
                    meshPrewarmRefreshRequested = true;
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (event->menuName != "FavoritesMenu") {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (IconLayer::HandleFavoritesMenuOpened()) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                Open();
                if (IconLayer::HandleFavoritesMenuOpened()) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
                    queue->AddMessage(RE::FavoritesMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kForceHide, nullptr);
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            VanillaMenuSink() = default;
        };
    }

    bool Register()
    {
        if (registered) {
            return window != nullptr;
        }
        registered = true;

        if (Config::Get().previewMode == Config::PreviewMode::kMeshes &&
            !PreviewCache::GetSingleton().IsFrameworkAvailable()) {
            Config::UsePreviewModeForSession(Config::PreviewMode::kIcons);
            logger::warn("Mesh Rendering Framework is unavailable; using flat icons for this session");
        }

        if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
            logger::error("SKSE Menu Framework is unavailable; vanilla FavoritesMenu will remain active");
            return false;
        }

        if (!IconLayer::Register()) {
            logger::error("Scaleform tile layer registration failed; vanilla FavoritesMenu will remain active");
            return false;
        }

        window = SKSEMenuFramework::AddWindow(Render, Config::Get().pauseGameWhileOpen);
        if (!window) {
            logger::error("SKSE Menu Framework rejected the tileable favorites window; vanilla fallback remains active");
            return false;
        }
        inputRegistration.reset(SKSEMenuFramework::AddInputEvent(ProcessInput));
        frameworkEventRegistration.reset(SKSEMenuFramework::AddEvent(HandleFrameworkEvent, 0.0f));
        meshPrewarmRegistration.reset(SKSEMenuFramework::AddHudElement(PrewarmMeshes));
        if (const auto skyrimUI = RE::UI::GetSingleton()) {
            skyrimUI->AddEventSink<RE::MenuOpenCloseEvent>(std::addressof(VanillaMenuSink::GetSingleton()));
        }
        if (const auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESEquipEvent>(std::addressof(PlayerEquipEventSink::GetSingleton()));
        }
        replacementAvailable.store(true, std::memory_order_release);
        logger::info("Tileable favorites window registered (menu framework {:.2f})", SKSEMenuFramework::GetMenuFrameworkVersion());
        return true;
    }

    void Open()
    {
        if (!window || !replacementAvailable.load(std::memory_order_acquire)) {
            return;
        }
        window->BlockUserInput.store(Config::Get().pauseGameWhileOpen, std::memory_order_release);
        const auto wasOpen = window->IsOpen.load();
        if (!wasOpen) {
            controllerInputActive = IsControllerActive();
            centerCursorOnNextFrame = !controllerInputActive;
            scaleformCursorRequestCooldown = 0;
        }
        PreviewCache::GetSingleton().SetVisible(Config::Get().previewMode == Config::PreviewMode::kMeshes);
        IconLayer::SetActive(true, Config::Get().previewMode == Config::PreviewMode::kIcons);
        RefreshFavorites();
        if (!wasOpen) {
            gridEditor.RevealFavorites(Layout::GetSingleton().Snapshot());
            const auto useMeshes = Config::Get().previewMode == Config::PreviewMode::kMeshes;
            initialMeshRevealComplete = !useMeshes;
            incrementalMeshRevealAllowed = useMeshes && std::ranges::any_of(
                Favorites::GetSingleton().Items(),
                [](const FavoriteItem& item) {
                    return static_cast<bool>(PreviewCache::GetSingleton().Get(item));
                });
        }
        dragState = {};
        ResetControllerInput();
        postActivationRefreshFrames = 0;
        equipmentStateChanged.store(false, std::memory_order_release);
        pendingHotkey.store(0, std::memory_order_release);
        inputHotkeyTarget.store(PackItemKey(focusedItem), std::memory_order_release);
        refreshTimer = 0.0f;
        window->IsOpen = true;
        if (!wasOpen) {
            if (controllerInputActive) {
                HideScaleformCursor();
            }
            SetNativeBlur(true);
        }
    }

    void Close()
    {
        const auto wasOpen = window && window->IsOpen.exchange(false);
        gridEditor.Reset();
        pendingPageTurn.store(0, std::memory_order_release);
        pendingCancel.store(false, std::memory_order_release);
        canCancelEdit.store(false, std::memory_order_release);
        enteringText.store(false, std::memory_order_release);
        IconLayer::SetActive(false, false);
        PreviewCache::GetSingleton().SetVisible(false);
        dragState = {};
        ResetControllerInput();
        centerCursorOnNextFrame = false;
        initialMeshRevealComplete = false;
        incrementalMeshRevealAllowed = false;
        postActivationRefreshFrames = 0;
        pendingHotkey.store(0, std::memory_order_release);
        inputHotkeyTarget.store(0, std::memory_order_release);
        if (wasOpen) {
            HideScaleformCursor();
            SetNativeBlur(false);
        }
    }

    void ApplyPreviewSettings()
    {
        PreviewCache::GetSingleton().Clear();
        const auto useIcons = Config::Get().previewMode == Config::PreviewMode::kIcons;
        initialMeshRevealComplete = useIcons;
        incrementalMeshRevealAllowed = false;
        PreviewCache::GetSingleton().SetVisible(IsOpen() && !useIcons);
        IconLayer::SetActive(IsOpen(), useIcons);
        meshPrewarmReady = false;
        meshPrewarmRefreshRequested = true;
        if (IsOpen()) {
            RefreshFavorites();
        }
    }

    bool IsOpen()
    {
        return window && window->IsOpen;
    }
}

// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Viewport/CnaSceneRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <utility>
#include <unordered_map>
#include <unordered_set>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"
#include "CNA/Editor/Scene/Tilemap.hpp"
#include "CNA/Editor/Scene/EditorIcons.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Scene/TransformGizmos.hpp"
#include "CNA/Editor/Viewport/CnaModelPass.hpp"
#include "CNA/Editor/Viewport/CnaUiRenderer.hpp"

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace CNA::Editor
{
    namespace
    {
        /** @brief Background behind the scene, distinct from the editor chrome around it. */
        const Xna::Color kBackground{24, 24, 27, 255};

        /** @brief Behind a model thumbnail: lighter than the viewport, so a dark model still reads. */
        const Xna::Color kThumbnailBackground{52, 52, 60, 255};
        const Xna::Color kGridMinor{45, 45, 52, 255};
        const Xna::Color kGridMajor{62, 62, 72, 255};
        const Xna::Color kAxis{92, 74, 74, 255};
        const Xna::Color kSelection{255, 158, 46, 255};

        /** @brief Icon colours. One per kind, so a glance is enough to tell them apart. */
        const Xna::Color kIconFrame{92, 92, 104, 255};
        const Xna::Color kIconCamera{124, 190, 255, 255};
        const Xna::Color kIconLight{250, 218, 110, 255};
        const Xna::Color kIconAudio{150, 220, 168, 255};
        const Xna::Color kIconModel{198, 168, 240, 255};

        /** @brief Returns the mark colour for @p kind. */
        const Xna::Color& iconColor(EditorIconKind kind)
        {
            switch (kind)
            {
                case EditorIconKind::Camera: return kIconCamera;
                case EditorIconKind::Light: return kIconLight;
                case EditorIconKind::AudioSource: return kIconAudio;
                case EditorIconKind::Model: return kIconModel;
                case EditorIconKind::None: break;
            }
            return kIconFrame;
        }

        /** @brief Gizmo arm colours: the X/Y/Z-is-red/green/blue convention every editor shares. */
        const Xna::Color kGizmoX{226, 62, 62, 255};
        const Xna::Color kGizmoY{104, 204, 92, 255};
        const Xna::Color kGizmoCenter{236, 226, 96, 255};

        /** @brief The rotate ring, in the blue every editor gives the Z axis. */
        const Xna::Color kGizmoRing{104, 164, 240, 255};

        /** @brief Drawn where a sprite's texture is missing, so the entity stays visible. */
        const Xna::Color kMissingTexture{200, 60, 140, 255};

        Xna::Color toXnaColor(const EditorColor& color)
        {
            return Xna::Color(static_cast<int>(color.r), static_cast<int>(color.g),
                              static_cast<int>(color.b), static_cast<int>(color.a));
        }

        XnaGraphics::SpriteEffects toSpriteEffects(const std::string& name)
        {
            if (name == "FlipHorizontally") { return XnaGraphics::SpriteEffects::FlipHorizontally; }
            if (name == "FlipVertically") { return XnaGraphics::SpriteEffects::FlipVertically; }
            if (name == "FlipBoth")
            {
                return static_cast<XnaGraphics::SpriteEffects>(
                    static_cast<int>(XnaGraphics::SpriteEffects::FlipHorizontally)
                    | static_cast<int>(XnaGraphics::SpriteEffects::FlipVertically));
            }
            return XnaGraphics::SpriteEffects::None;
        }
    }

    struct CnaSceneRenderer::Impl
    {
        XnaGraphics::GraphicsDevice* device = nullptr;
        const AssetDatabase* assets = nullptr;
        const ComponentRegistry* components = nullptr;

        std::unique_ptr<XnaGraphics::RenderTarget2D> target;
        std::unique_ptr<XnaGraphics::SpriteBatch> spriteBatch;

        /** @brief A 1x1 white texture: every line, outline and placeholder is a stretched quad. */
        std::unique_ptr<XnaGraphics::Texture2D> pixel;

        std::unordered_map<Uuid, std::unique_ptr<XnaGraphics::Texture2D>> textures;

        /**
         * @brief Assets whose load already failed.
         *
         * Remembered so a broken asset costs one attempt rather than one per frame. An editor that
         * retries a missing file sixty times a second stalls on exactly the project that most needs
         * opening.
         */
        std::unordered_set<Uuid> failedTextures;

        int targetWidth = 0;
        int targetHeight = 0;

        /** @brief The square target model thumbnails are drawn into (ED-406). */
        std::unique_ptr<XnaGraphics::RenderTarget2D> thumbnailTarget;
        int thumbnailExtent = 0;

        /** @brief Makes sure the thumbnail target is @p extent square, with depth. */
        void ensureThumbnailTarget(int extent)
        {
            if (thumbnailTarget != nullptr && thumbnailExtent == extent) { return; }

            thumbnailTarget = std::make_unique<XnaGraphics::RenderTarget2D>(
                *device, extent, extent, false, XnaGraphics::SurfaceFormat::Color,
                XnaGraphics::DepthFormat::Depth24Stencil8);
            thumbnailExtent = extent;
        }

        /** @brief Whether the current target carries a depth buffer, which only the 3D view needs. */
        bool targetHasDepth = false;

        /** @brief The solid model pass (ED-402). Initialised with the renderer, drawn in 3D only. */
        CnaModelPass modelPass;

        /** @brief Ensures the offscreen target matches the requested size. */
        /**
         * @brief Makes sure the offscreen target is @p width by @p height, with depth if asked.
         *
         * @param withDepth The 3D view needs one and the 2D view does not. Sprites sort by draw
         *        order, so the two-argument `RenderTarget2D` constructor -- documented as "no
         *        depth buffer" -- is exactly right for them; models sort per pixel, and without a
         *        depth buffer a crate draws its own back faces through its front.
         *
         * One target that is recreated when the requirement changes, rather than two kept side by
         * side. Switching between the 2D and 3D view is something a user does seconds apart, not
         * per frame, and two targets would be two things to keep the same size as the panel.
         */
        void ensureTarget(int width, int height, bool withDepth = false)
        {
            if (target != nullptr && targetWidth == width && targetHeight == height
                && targetHasDepth == withDepth)
            {
                return;
            }

            if (withDepth)
            {
                target = std::make_unique<XnaGraphics::RenderTarget2D>(
                    *device, width, height, false, XnaGraphics::SurfaceFormat::Color,
                    XnaGraphics::DepthFormat::Depth24Stencil8);
            }
            else
            {
                target = std::make_unique<XnaGraphics::RenderTarget2D>(*device, width, height);
            }

            targetWidth = width;
            targetHeight = height;
            targetHasDepth = withDepth;
        }

        /** @brief Returns the texture for @p assetId, loading it once, or nullptr. */
        XnaGraphics::Texture2D* resolveTexture(const Uuid& assetId, SceneRenderStats& stats)
        {
            if (!assetId.isValid() || assets == nullptr) { return nullptr; }

            if (const auto found = textures.find(assetId); found != textures.end())
            {
                return found->second.get();
            }
            if (failedTextures.count(assetId) > 0) { return nullptr; }

            const AssetRecord* record = assets->find(assetId);
            if (record == nullptr)
            {
                failedTextures.insert(assetId);
                return nullptr;
            }

            try
            {
                auto texture = std::make_unique<XnaGraphics::Texture2D>(
                    assets->resolvePath(record->sourcePath), *device);
                XnaGraphics::Texture2D* raw = texture.get();
                textures.emplace(assetId, std::move(texture));
                ++stats.texturesLoaded;
                return raw;
            }
            catch (const std::exception&)
            {
                // A texture that will not load is a project problem, not an editor crash: the
                // sprite falls back to a placeholder so the entity is still visible and selectable.
                failedTextures.insert(assetId);
                return nullptr;
            }
        }

        /** @brief Draws an axis-aligned rectangle in screen space, using the 1x1 white texture. */
        void drawRect(const Xna::Rectangle& rect, const Xna::Color& color)
        {
            spriteBatch->Draw(*pixel, rect, std::nullopt, color, 0.0f,
                              Xna::Vector2{0.0f, 0.0f}, XnaGraphics::SpriteEffects::None, 0.0f);
        }

        /**
         * @brief Draws a line of @p thickness pixels from @p from to @p to.
         *
         * The 1x1 texture again, but stretched *and rotated*: SpriteBatch has no line primitive,
         * and a rotated quad is the only way to draw an arm that does not lie along a screen axis
         * -- which is every arm once the gizmo follows the entity's own rotation.
         */
        void drawLine(const EditorVector2& from, const EditorVector2& to, float thickness,
                      const Xna::Color& color)
        {
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float length = std::hypot(dx, dy);
            if (length <= 0.0f) { return; }

            // Origin at the middle of the left edge, so the quad grows along the line and is
            // centred across it -- a thick line then straddles the line rather than hanging below.
            spriteBatch->Draw(*pixel, Xna::Vector2{from.x, from.y}, std::nullopt, color,
                              std::atan2(dy, dx), Xna::Vector2{0.0f, 0.5f},
                              Xna::Vector2{length, thickness}, XnaGraphics::SpriteEffects::None, 0.0f);
        }

        /** @brief Draws a one-pixel outline just outside @p rect. */
        void drawOutline(const Xna::Rectangle& rect, const Xna::Color& color, int thickness)
        {
            drawRect(Xna::Rectangle{rect.X - thickness, rect.Y - thickness,
                                    rect.Width + thickness * 2, thickness}, color);
            drawRect(Xna::Rectangle{rect.X - thickness, rect.Y + rect.Height,
                                    rect.Width + thickness * 2, thickness}, color);
            drawRect(Xna::Rectangle{rect.X - thickness, rect.Y, thickness, rect.Height}, color);
            drawRect(Xna::Rectangle{rect.X + rect.Width, rect.Y, thickness, rect.Height}, color);
        }

        /** @brief Draws a diamond centred on (@p cx, @p cy), as rows of the 1x1 white texture. */
        void drawDiamond(int cx, int cy, int radius, const Xna::Color& color)
        {
            for (int row = -radius; row <= radius; ++row)
            {
                const int halfWidth = radius - std::abs(row);
                drawRect(Xna::Rectangle{cx - halfWidth, cy + row, halfWidth * 2 + 1, 1}, color);
            }
        }

        /**
         * @brief Draws a triangle whose vertical edge is at @p x, widening towards @p direction.
         *
         * @param direction +1 for a triangle pointing right, -1 for one pointing left.
         */
        void drawTriangleFrom(int x, int cy, int height, int length, int direction,
                              const Xna::Color& color)
        {
            for (int step = 0; step < length; ++step)
            {
                const int half = std::max(1, height * (length - step) / (2 * length));
                drawRect(Xna::Rectangle{x + step * direction, cy - half, 1, half * 2}, color);
            }
        }

        /**
         * @brief Draws one entity icon: a shared badge frame plus a per-kind mark.
         *
         * The frame is common on purpose -- it makes the icons read as one family of editor
         * artefacts rather than as scene content, which matters because they sit in the same
         * picture as the game's own sprites.
         */
        void drawEditorIcon(const EditorIconPlacement& icon, bool selected)
        {
            const int cx = static_cast<int>(std::round(icon.center.x));
            const int cy = static_cast<int>(std::round(icon.center.y));
            const int extent = static_cast<int>(kEditorIconExtent);
            const Xna::Color& color = iconColor(icon.kind);

            const Xna::Rectangle badge{cx - extent, cy - extent, extent * 2, extent * 2};
            drawRect(badge, Xna::Color(20, 20, 24, 190));
            drawOutline(badge, selected ? kSelection : kIconFrame, selected ? 2 : 1);

            switch (icon.kind)
            {
                case EditorIconKind::Camera:
                    // A body with a lens flaring out of it: the shape of every camera icon since
                    // the first one, and recognisable at thirteen pixels.
                    drawRect(Xna::Rectangle{cx - 8, cy - 5, 9, 10}, color);
                    drawTriangleFrom(cx + 2, cy, 14, 6, +1, color);
                    break;

                case EditorIconKind::Light:
                    drawDiamond(cx, cy, 5, color);
                    // Four rays, so it reads as emitting rather than as a solid object.
                    drawRect(Xna::Rectangle{cx - 1, cy - 9, 2, 3}, color);
                    drawRect(Xna::Rectangle{cx - 1, cy + 7, 2, 3}, color);
                    drawRect(Xna::Rectangle{cx - 9, cy - 1, 3, 2}, color);
                    drawRect(Xna::Rectangle{cx + 7, cy - 1, 3, 2}, color);
                    break;

                case EditorIconKind::AudioSource:
                    drawRect(Xna::Rectangle{cx - 8, cy - 3, 4, 6}, color);
                    drawTriangleFrom(cx - 1, cy, 14, 5, -1, color);
                    drawRect(Xna::Rectangle{cx + 4, cy - 4, 2, 8}, color);
                    drawRect(Xna::Rectangle{cx + 7, cy - 6, 2, 12}, color);
                    break;

                case EditorIconKind::Model:
                    // Two offset squares: the cheapest drawing that reads as a box rather than a
                    // rectangle, and it needs no diagonal.
                    drawOutline(Xna::Rectangle{cx - 7, cy - 3, 10, 10}, color, 1);
                    drawOutline(Xna::Rectangle{cx - 3, cy - 7, 10, 10}, color, 1);
                    break;

                case EditorIconKind::None:
                    break;
            }
        }

        /** @brief Returns @p point advanced by @p distance along the unit direction @p axis. */
        static EditorVector2 along(const EditorVector2& point, const EditorVector2& axis, float distance)
        {
            return EditorVector2{point.x + axis.x * distance, point.y + axis.y * distance};
        }

        /**
         * @brief Draws an arrowhead of @p length pixels ending at @p tip, pointing along @p axis.
         *
         * A stack of shortening crossbars rather than a polygon: SpriteBatch has no triangle
         * primitive, and at twelve pixels the stepping is invisible while costing a handful of
         * quads. The crossbars are drawn along the axis's perpendicular, so the head follows a
         * rotated arm exactly as the shaft does.
         */
        void drawArrowHead(const EditorVector2& tip, const EditorVector2& axis, float length,
                           float halfWidth, const Xna::Color& color)
        {
            const EditorVector2 perpendicular{-axis.y, axis.x};

            for (float step = 0.0f; step < length; step += 1.0f)
            {
                const float half = std::max(1.0f, halfWidth * (length - step) / length);
                const EditorVector2 center = along(tip, axis, -step);
                drawLine(along(center, perpendicular, -half), along(center, perpendicular, half), 1.0f,
                         color);
            }
        }

        /** @brief Draws a square of half-extent @p extent centred at @p center, rotated by @p axis. */
        void drawHandleSquare(const EditorVector2& center, const EditorVector2& axis, float extent,
                              const Xna::Color& color)
        {
            const EditorVector2 perpendicular{-axis.y, axis.x};

            // Filled by drawing rows across it: the handle is out at the end of an arm where it
            // hides nothing, and a solid square reads as "grab here" far better than an outline.
            for (float offset = -extent; offset <= extent; offset += 1.0f)
            {
                const EditorVector2 center2 = along(center, perpendicular, offset);
                drawLine(along(center2, axis, -extent), along(center2, axis, extent), 1.0f, color);
            }
        }

        /**
         * @brief Draws the translate gizmo: two arms with arrowheads and a centre square.
         *
         * Every dimension comes from the layout, which is the same object the hit-test uses, so
         * what the user sees and what the user can grab cannot drift apart -- including the arm
         * directions, which follow the entity's rotation in local space.
         */
        void drawTranslateGizmo(const TranslateGizmoLayout& layout)
        {
            constexpr float kArmThickness = 3.0f;
            constexpr float kHeadLength = 12.0f;
            constexpr float kHeadHalfWidth = 6.0f;

            // The arms start outside the centre square, so the square reads as a separate handle --
            // which it is: it is the one that moves on both axes at once.
            const float armStart = layout.centerExtent + 2.0f;
            const float armEnd = layout.axisLength - kHeadLength;

            const std::array<std::pair<EditorVector2, const Xna::Color*>, 2> arms{
                std::pair{layout.xAxis, &kGizmoX}, std::pair{layout.yAxis, &kGizmoY}};

            for (const auto& [axis, color] : arms)
            {
                if (armEnd > armStart)
                {
                    drawLine(along(layout.origin, axis, armStart), along(layout.origin, axis, armEnd),
                             kArmThickness, *color);
                }
                drawArrowHead(along(layout.origin, axis, layout.axisLength), axis, kHeadLength,
                              kHeadHalfWidth, *color);
            }

            // Outlined rather than filled: the centre handle sits exactly over the entity's origin,
            // and a filled square that size would hide the very sprite being positioned.
            const int extent = static_cast<int>(std::round(layout.centerExtent));
            drawOutline(Xna::Rectangle{static_cast<int>(std::round(layout.origin.x)) - extent,
                                       static_cast<int>(std::round(layout.origin.y)) - extent,
                                       extent * 2, extent * 2},
                        kGizmoCenter, 2);
        }

        /**
         * @brief Draws the rotate gizmo: a ring, a mark showing the current angle, and a hub.
         *
         * The ring is a fan of short chords. Sixty-four of them is smooth at any radius the layout
         * uses and, unlike a stepped circle of rectangles, stays smooth when the ring is large.
         *
         * The mark is what makes the gizmo readable: a bare circle cannot show that anything
         * happened, so rotating a symmetrical sprite would give no feedback at all.
         */
        void drawRotateGizmo(const RotateGizmoLayout& layout)
        {
            constexpr int kSegments = 64;
            constexpr float kTwoPi = 6.28318530717958647692f;

            for (int segment = 0; segment < kSegments; ++segment)
            {
                const float from = kTwoPi * static_cast<float>(segment) / kSegments;
                const float to = kTwoPi * static_cast<float>(segment + 1) / kSegments;
                drawLine(layout.getPointAt(from), layout.getPointAt(to), 2.0f, kGizmoRing);
            }

            // A spoke out to the ring plus a blob on it: the spoke says which way is "zero degrees
            // for this entity", the blob is what the eye tracks while dragging.
            const EditorVector2 mark = layout.getPointAt(layout.angle);
            drawLine(layout.origin, mark, 1.0f, kGizmoRing);
            drawDiamond(static_cast<int>(std::round(mark.x)), static_cast<int>(std::round(mark.y)), 5,
                        kGizmoCenter);

            // A small hub, so the pivot the turn happens about is visible rather than implied.
            drawDiamond(static_cast<int>(std::round(layout.origin.x)),
                        static_cast<int>(std::round(layout.origin.y)), 3, kGizmoCenter);
        }

        /**
         * @brief Draws the scale gizmo: two arms ending in solid squares, and a centre square.
         *
         * Squares rather than arrowheads, deliberately. The arrowhead means "this points
         * somewhere"; the square means "this end goes in and out", which is the whole difference
         * between moving a thing and resizing it -- and the only cue distinguishing the two gizmos
         * at a glance, since both are a pair of arms on the same origin.
         */
        void drawScaleGizmo(const ScaleGizmoLayout& layout)
        {
            constexpr float kArmThickness = 3.0f;

            const float armStart = layout.centerExtent + 2.0f;
            const float armEnd = layout.axisLength - layout.handleExtent;

            const std::array<std::pair<EditorVector2, const Xna::Color*>, 2> arms{
                std::pair{layout.xAxis, &kGizmoX}, std::pair{layout.yAxis, &kGizmoY}};

            for (const auto& [axis, color] : arms)
            {
                if (armEnd > armStart)
                {
                    drawLine(along(layout.origin, axis, armStart), along(layout.origin, axis, armEnd),
                             kArmThickness, *color);
                }
                drawHandleSquare(along(layout.origin, axis, layout.axisLength), axis,
                                 layout.handleExtent, *color);
            }

            drawOutline(Xna::Rectangle{static_cast<int>(std::round(layout.origin.x))
                                           - static_cast<int>(std::round(layout.centerExtent)),
                                       static_cast<int>(std::round(layout.origin.y))
                                           - static_cast<int>(std::round(layout.centerExtent)),
                                       static_cast<int>(std::round(layout.centerExtent)) * 2,
                                       static_cast<int>(std::round(layout.centerExtent)) * 2},
                        kGizmoCenter, 2);
        }

        void drawGrid(const EditorCamera2D& camera, SceneRenderStats& stats)
        {
            // Roughly 90 pixels between minor lines: dense enough to judge distance, sparse enough
            // not to become a texture.
            const float spacing = chooseGridSpacing(camera.getZoom(), kGridTargetPixels);
            if (spacing <= 0.0f) { return; }

            const WorldBounds2D visible = camera.getVisibleBounds();
            const float firstX = std::floor(visible.min.x / spacing) * spacing;
            const float firstY = std::floor(visible.min.y / spacing) * spacing;

            // Bounded so that a pathological zoom cannot ask for a million draw calls.
            constexpr int kMaxLines = 400;

            int drawn = 0;
            for (float x = firstX; x <= visible.max.x && drawn < kMaxLines; x += spacing, ++drawn)
            {
                const int screenX = static_cast<int>(std::round(camera.worldToScreen(EditorVector2{x, 0.0f}).x));
                // Every fifth line is emphasised, and the axis itself more so again -- without
                // that the grid reads as texture rather than as a measurable scale.
                const bool isMajor = std::fabs(std::fmod(x / spacing, 5.0f)) < 0.001f;
                const bool isAxis = std::fabs(x) < spacing * 0.001f;
                drawRect(Xna::Rectangle{screenX, 0, 1, targetHeight},
                         isAxis ? kAxis : (isMajor ? kGridMajor : kGridMinor));
                ++stats.gridLines;
            }

            drawn = 0;
            for (float y = firstY; y <= visible.max.y && drawn < kMaxLines; y += spacing, ++drawn)
            {
                const int screenY = static_cast<int>(std::round(camera.worldToScreen(EditorVector2{0.0f, y}).y));
                const bool isMajor = std::fabs(std::fmod(y / spacing, 5.0f)) < 0.001f;
                const bool isAxis = std::fabs(y) < spacing * 0.001f;
                drawRect(Xna::Rectangle{0, screenY, targetWidth, 1},
                         isAxis ? kAxis : (isMajor ? kGridMajor : kGridMinor));
                ++stats.gridLines;
            }
        }
    };

    CnaSceneRenderer::CnaSceneRenderer() : impl_(std::make_unique<Impl>()) {}

    CnaSceneRenderer::~CnaSceneRenderer() { shutdown(); }

    void CnaSceneRenderer::initialize(XnaGraphics::GraphicsDevice& device,
                                      const AssetDatabase& assets,
                                      const ComponentRegistry& components)
    {
        impl_->device = &device;
        impl_->assets = &assets;
        impl_->components = &components;
        impl_->spriteBatch = std::make_unique<XnaGraphics::SpriteBatch>(device);

        impl_->pixel = std::make_unique<XnaGraphics::Texture2D>(device, 1, 1);
        const Xna::Color white(255, 255, 255, 255);
        impl_->pixel->SetData(&white, 1);

        // Here rather than on first use, so a build that cannot construct a PbrEffect has found
        // that out before the first frame instead of during it (ED-402).
        impl_->modelPass.initialize(device, assets);
    }

    void CnaSceneRenderer::shutdown()
    {
        impl_->textures.clear();
        impl_->failedTextures.clear();
        impl_->pixel.reset();
        impl_->spriteBatch.reset();
        impl_->target.reset();
        impl_->device = nullptr;
        impl_->assets = nullptr;
    }

    void CnaSceneRenderer::invalidateTexture(const Uuid& assetId)
    {
        if (!assetId.isValid())
        {
            impl_->textures.clear();
            impl_->failedTextures.clear();
            return;
        }

        impl_->textures.erase(assetId);

        // Clearing the failure memory matters as much as clearing the texture: a load that failed
        // because the file was missing must get another chance once it comes back, or restoring
        // the file would look like it did nothing.
        impl_->failedTextures.erase(assetId);
    }

    XnaGraphics::Texture2D* CnaSceneRenderer::getOrLoadTexture(const Uuid& assetId)
    {
        if (impl_->device == nullptr) { return nullptr; }

        SceneRenderStats ignored;
        return impl_->resolveTexture(assetId, ignored);
    }

    EditorVector2 CnaSceneRenderer::getSpriteSize(const Uuid& assetId) const
    {
        const auto found = impl_->textures.find(assetId);
        if (found == impl_->textures.end()) { return EditorVector2{}; }
        return EditorVector2{static_cast<float>(found->second->getWidthProperty()),
                             static_cast<float>(found->second->getHeightProperty())};
    }

    SpriteSizeProvider CnaSceneRenderer::makeSizeProvider() const
    {
        return [this](const Uuid& assetId) { return getSpriteSize(assetId); };
    }

    UiTextureId CnaSceneRenderer::shareWithUi(CnaUiRenderer& uiRenderer)
    {
        if (impl_->target == nullptr) { return kUiTextureNone; }
        return uiRenderer.adoptTexture(*impl_->target);
    }

    SceneRenderStats CnaSceneRenderer::render(const SceneDocument& scene,
                                              const EditorCamera2D& camera,
                                              int width,
                                              int height,
                                              const std::vector<Uuid>& selection,
                                              GizmoMode gizmoMode,
                                              GizmoSpace gizmoSpace,
                                              const AnimationPreview& preview)
    {
        return renderPasses(scene, camera, width, height, selection, gizmoMode, gizmoSpace, preview,
                            true, true);
    }

    SceneRenderStats CnaSceneRenderer::renderGameView(const SceneDocument& scene,
                                                      const EditorCamera2D& camera,
                                                      int width,
                                                      int height)
    {
        static const std::vector<Uuid> kNothingSelected;
        return renderPasses(scene, camera, width, height, kNothingSelected, GizmoMode::None,
                            GizmoSpace::World, AnimationPreview{}, false, false);
    }

    void CnaSceneRenderer::renderWireframe(const std::vector<WireSegment>& segments, int width, int height)
    {
        lastStats_ = SceneRenderStats{};

        if (impl_->device == nullptr || impl_->spriteBatch == nullptr) { return; }
        if (width <= 0 || height <= 0) { return; }

        impl_->ensureTarget(width, height);
        impl_->device->SetRenderTarget(impl_->target.get());
        impl_->device->Clear(kBackground);

        impl_->spriteBatch->Begin(XnaGraphics::SpriteSortMode::Deferred,
                                  XnaGraphics::BlendState::AlphaBlend);

        for (const WireSegment& segment : segments)
        {
            impl_->drawLine(segment.from, segment.to, segment.thickness,
                            Xna::Color{segment.color.r, segment.color.g, segment.color.b,
                                       segment.color.a});
        }

        impl_->spriteBatch->End();

        // Counted as grid lines because that is what nearly all of them are, and a viewport that
        // reported zero of everything while visibly drawing would be lying about its own work.
        lastStats_.gridLines = segments.size();

        impl_->device->SetRenderTarget(nullptr);
    }

    ModelPassStats CnaSceneRenderer::renderScene3D(const SceneModelBatch& models,
                                                   const SceneSpriteBatch3D& sprites,
                                                   const std::vector<WireSegment>& segments,
                                                   int width, int height)
    {
        lastStats_ = SceneRenderStats{};
        ModelPassStats modelStats;
        modelStats.effect = impl_->modelPass.getEffectName();

        if (impl_->device == nullptr || impl_->spriteBatch == nullptr) { return modelStats; }
        if (width <= 0 || height <= 0) { return modelStats; }

        // With depth, unlike every other pass this class runs.
        impl_->ensureTarget(width, height, true);
        impl_->device->SetRenderTarget(impl_->target.get());

        // The depth buffer as well as the colour, which the one-argument `Clear` does not touch.
        // Leaving it means the first model's depth test runs against whatever the last frame -- or
        // nothing at all -- left there, and the honest description of the result is "the models
        // are drawn and then rejected": no error, no draw call missing, nothing on screen.
        impl_->device->Clear(XnaGraphics::ClearOptions::Target | XnaGraphics::ClearOptions::DepthBuffer,
                             kBackground, 1.0f, 0);

        // Models first, then the lines over them. Not the other way round and not interleaved: the
        // wireframe is the editor's *overlay* -- grid, gizmo, selection outline -- and an overlay
        // that a model could occlude would leave a user unable to see the handle they are dragging
        // whenever it passed behind geometry. It is drawn with the depth test off for that reason.
        modelStats = impl_->modelPass.render(models);

        // Sprites after the opaque models and before the overlay: transparency has to be blended
        // against what is already there, so it cannot go first, and it is scene content rather
        // than editor chrome, so it cannot go last.
        const ModelPassStats spriteStats = impl_->modelPass.renderSprites(
            sprites, models,
            [this, &modelStats](const Uuid& assetId)
            {
                // The renderer's own texture cache, which already holds every sprite the 2D view
                // has drawn. A second cache here would load the whole project twice.
                (void)modelStats;
                return getOrLoadTexture(assetId);
            });

        modelStats.spritesDrawn = spriteStats.spritesDrawn;
        modelStats.trianglesDrawn += spriteStats.trianglesDrawn;
        modelStats.missingTextures += spriteStats.missingTextures;

        impl_->spriteBatch->Begin(XnaGraphics::SpriteSortMode::Deferred,
                                  XnaGraphics::BlendState::AlphaBlend);

        for (const WireSegment& segment : segments)
        {
            impl_->drawLine(segment.from, segment.to, segment.thickness,
                            Xna::Color{segment.color.r, segment.color.g, segment.color.b,
                                       segment.color.a});
        }

        impl_->spriteBatch->End();

        lastStats_.gridLines = segments.size();
        lastStats_.missingTextures = modelStats.missingTextures;
        lastStats_.spritesDrawn = modelStats.spritesDrawn;

        impl_->device->SetRenderTarget(nullptr);
        return modelStats;
    }

    XnaGraphics::Texture2D* CnaSceneRenderer::renderModelThumbnail(const Uuid& assetId,
                                                                   const MeshData& mesh, int extent)
    {
        if (impl_->device == nullptr || extent <= 0 || mesh.isEmpty()) { return nullptr; }

        impl_->ensureThumbnailTarget(extent);
        if (impl_->thumbnailTarget == nullptr) { return nullptr; }

        // A camera framing this model and nothing else, from three-quarters on. Head-on is the one
        // angle at which a cube and a flat square are the same picture, and telling those apart is
        // the entire job of a thumbnail.
        EditorCamera3D camera;
        camera.setViewportSize(EditorVector2{static_cast<float>(extent), static_cast<float>(extent)});
        camera.orbit(35.0f, 25.0f);
        camera.frame(WorldBounds3D{mesh.boundsMin, mesh.boundsMax});

        SceneModelBatch batch;
        batch.viewProjection = camera.getViewProjectionMatrix();
        batch.view = camera.getViewMatrix();
        batch.projection =
            multiply(camera.getProjectionMatrix(), createScale(EditorVector3{1.0f, -1.0f, 1.0f}));

        ModelDraw draw;
        draw.modelId = assetId;
        draw.mesh = &mesh;

        // Default lighting rather than the open scene's: a thumbnail is a picture of the *asset*,
        // and one that changed because somebody moved a lamp in the level they happen to have open
        // would be a picture of something else.
        draw.lighting = EffectLighting{};
        batch.draws.push_back(draw);

        // No previous target is saved and restored, because every caller of this sets its own
        // before drawing anything -- the viewport passes do it on entry, and a thumbnail drawn in
        // the middle of the UI pass is drawn before the UI renderer binds the back buffer again.
        impl_->device->SetRenderTarget(impl_->thumbnailTarget.get());
        impl_->device->Clear(XnaGraphics::ClearOptions::Target | XnaGraphics::ClearOptions::DepthBuffer,
                             kThumbnailBackground, 1.0f, 0);
        impl_->modelPass.render(batch);
        impl_->device->SetRenderTarget(nullptr);

        return impl_->thumbnailTarget.get();
    }

    void CnaSceneRenderer::invalidateModel(const Uuid& assetId)
    {
        impl_->modelPass.invalidateModel(assetId);
    }

    const std::string& CnaSceneRenderer::getModelEffectName() const
    {
        return impl_->modelPass.getEffectName();
    }

    SceneRenderStats CnaSceneRenderer::renderPasses(const SceneDocument& scene,
                                                    const EditorCamera2D& camera,
                                                    int width,
                                                    int height,
                                                    const std::vector<Uuid>& selection,
                                                    GizmoMode gizmoMode,
                                                    GizmoSpace gizmoSpace,
                                                    const AnimationPreview& preview,
                                                    bool editorOverlays,
                                                    bool offscreen)
    {
        SceneRenderStats stats;
        lastStats_ = stats;

        if (impl_->device == nullptr || impl_->spriteBatch == nullptr) { return stats; }
        if (width <= 0 || height <= 0) { return stats; }

        XnaGraphics::GraphicsDevice& device = *impl_->device;

        if (offscreen)
        {
            impl_->ensureTarget(width, height);
            device.SetRenderTarget(impl_->target.get());
            device.Clear(kBackground);
        }

        // Pass 1: the grid, beneath everything. An editor artefact, so the game view has none --
        // and skipping the pass is the whole of "the player must not draw editor chrome", because
        // the separation is structural rather than a filter applied to a shared list.
        if (editorOverlays)
        {
            impl_->spriteBatch->Begin(XnaGraphics::SpriteSortMode::Deferred,
                                      XnaGraphics::BlendState::AlphaBlend);
            impl_->drawGrid(camera, stats);
            impl_->spriteBatch->End();
        }

        // Pass 2: the game's own content. BackToFront honours SpriteRenderer.layerDepth, which is
        // XNA's own convention (0 front, 1 back) and the one the game will see at run time.
        impl_->spriteBatch->Begin(XnaGraphics::SpriteSortMode::BackToFront,
                                  XnaGraphics::BlendState::NonPremultiplied);

        const SpriteSizeProvider sizeProvider = makeSizeProvider();

        for (const EditorEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { ++stats.spritesSkipped; continue; }

            const EditorComponent* sprite = entity.findComponent(BuiltinComponentIds::kSpriteRenderer);
            if (sprite == nullptr) { continue; }

            const std::optional<WorldTransform> world = computeWorldTransform(scene, entity.getId());
            if (!world) { ++stats.spritesSkipped; continue; }

            // An animation drives the sprite it sits beside: its sheet replaces the texture and its
            // current frame replaces the source rectangle. That is what makes the viewport show the
            // same frame the inspector's preview does -- and which frame *is* current comes in as a
            // parameter, because playback is editor state and must never travel in a scene.
            const EditorComponent* animation =
                entity.findComponent(BuiltinComponentIds::kSpriteAnimation);

            SpriteAnimationClip clip;
            if (animation != nullptr)
            {
                clip = readSpriteAnimationClip(
                    *animation,
                    impl_->components != nullptr
                        ? impl_->components->find(BuiltinComponentIds::kSpriteAnimation)
                        : nullptr);
            }
            const bool animated = animation != nullptr && !clip.isEmpty();

            // Frame zero for every animated entity except the one being previewed. An editor that
            // played every clip at once would be unreadable, and one that showed nothing until a
            // preview started would hide the art.
            const std::size_t framePosition =
                animated && preview.isActive() && preview.entityId == entity.getId()
                    ? std::min(preview.position, clip.frames.size() - 1)
                    : 0;

            const Uuid textureId =
                animated
                    ? animation->getProperty(SpriteAnimationKeys::kSheet).get<PropertyValue::AssetReference>().id
                    : sprite->getProperty("texture").get<PropertyValue::AssetReference>().id;
            XnaGraphics::Texture2D* texture = impl_->resolveTexture(textureId, stats);

            const EditorVector2 screenPosition =
                camera.worldToScreen(EditorVector2{world->position.x, world->position.y});
            const EditorVector2 origin = sprite->getProperty("origin").get<EditorVector2>();
            const EditorColor tint = sprite->getProperty("tint").get<EditorColor>();
            const float layerDepth = sprite->getProperty("layerDepth").get<float>(0.5f);
            const float rotation = zRotationOf(world->rotation);

            if (texture == nullptr)
            {
                // No texture: draw the placeholder at the bounds the picker uses, so what the user
                // clicks and what the user sees are the same rectangle.
                ++stats.missingTextures;
                const std::optional<WorldBounds2D> bounds =
                    computeEntityBounds2D(scene, entity.getId(), sizeProvider);
                if (!bounds) { continue; }

                const EditorVector2 topLeft = camera.worldToScreen(bounds->min);
                const EditorVector2 bottomRight = camera.worldToScreen(bounds->max);
                impl_->drawRect(Xna::Rectangle{static_cast<int>(topLeft.x), static_cast<int>(topLeft.y),
                                               static_cast<int>(bottomRight.x - topLeft.x),
                                               static_cast<int>(bottomRight.y - topLeft.y)},
                                kMissingTexture);
                ++stats.spritesDrawn;
                continue;
            }

            std::optional<Xna::Rectangle> sourceRectangle;
            const EditorRectangle source = animated ? clip.getFrameRectangle(framePosition)
                                                    : sprite->getProperty("sourceRectangle").get<EditorRectangle>();
            if (!source.isEmpty())
            {
                sourceRectangle = Xna::Rectangle{source.x, source.y, source.width, source.height};
            }

            // The camera's zoom folds into the sprite's scale, so one SpriteBatch::Draw covers both
            // the entity's own scale and the view's -- no separate view matrix needed, and the
            // result is exactly what the game would draw at that scale.
            const float scaleX = world->scale.x * camera.getZoom();
            const float scaleY = world->scale.y * camera.getZoom();

            impl_->spriteBatch->Draw(*texture,
                                     Xna::Vector2{screenPosition.x, screenPosition.y},
                                     sourceRectangle,
                                     toXnaColor(tint),
                                     rotation,
                                     Xna::Vector2{origin.x, origin.y},
                                     Xna::Vector2{scaleX, scaleY},
                                     toSpriteEffects(sprite->getProperty("spriteEffects")
                                                         .get<PropertyValue::EnumValue>().name),
                                     layerDepth);
            ++stats.spritesDrawn;
        }

        // Tilemaps share the content pass, so a tile and a sprite at the same layer depth sort
        // against each other exactly as they will at run time.
        for (const EditorEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { continue; }

            const EditorComponent* tilemap = entity.findComponent(BuiltinComponentIds::kTilemap);
            if (tilemap == nullptr) { continue; }

            const std::optional<WorldTransform> world = computeWorldTransform(scene, entity.getId());
            if (!world) { continue; }

            const ComponentDescriptor* descriptor =
                impl_->components != nullptr ? impl_->components->find(BuiltinComponentIds::kTilemap)
                                             : nullptr;

            const TilemapGrid grid = readTilemapGrid(*tilemap, descriptor);
            if (grid.isEmpty()) { continue; }

            const int tileWidth = static_cast<int>(
                tilemap->getPropertyOrDefault(TilemapKeys::kTileWidth, descriptor).get<std::int64_t>(0));
            const int tileHeight = static_cast<int>(
                tilemap->getPropertyOrDefault(TilemapKeys::kTileHeight, descriptor).get<std::int64_t>(0));
            const int sheetColumns = static_cast<int>(
                tilemap->getPropertyOrDefault(TilemapKeys::kSheetColumns, descriptor).get<std::int64_t>(1));
            if (tileWidth <= 0 || tileHeight <= 0 || sheetColumns <= 0) { continue; }

            const Uuid textureId =
                tilemap->getProperty(TilemapKeys::kTileSet).get<PropertyValue::AssetReference>().id;
            XnaGraphics::Texture2D* sheet = impl_->resolveTexture(textureId, stats);
            if (sheet == nullptr) { continue; }

            // Only the cells the viewport can actually show. A 200x200 map is forty thousand draw
            // calls a frame otherwise, nearly all of them offscreen.
            const TileCoordinate first = worldToTile(*world, tileWidth, tileHeight,
                                                     camera.screenToWorld(EditorVector2{0.0f, 0.0f}));
            const TileCoordinate last =
                worldToTile(*world, tileWidth, tileHeight,
                            camera.screenToWorld(EditorVector2{static_cast<float>(width),
                                                                static_cast<float>(height)}));

            const int minX = std::max(0, std::min(first.x, last.x));
            const int maxX = std::min(grid.columns - 1, std::max(first.x, last.x));
            const int minY = std::max(0, std::min(first.y, last.y));
            const int maxY = std::min(grid.rows - 1, std::max(first.y, last.y));

            const float scaleX = world->scale.x * camera.getZoom();
            const float scaleY = world->scale.y * camera.getZoom();

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const std::int64_t tile = grid.at(x, y);
                    if (tile < 0) { continue; }

                    const int sheetX = static_cast<int>(tile % sheetColumns) * tileWidth;
                    const int sheetY = static_cast<int>(tile / sheetColumns) * tileHeight;

                    const EditorVector2 cell = camera.worldToScreen(EditorVector2{
                        world->position.x + static_cast<float>(x * tileWidth) * world->scale.x,
                        world->position.y + static_cast<float>(y * tileHeight) * world->scale.y});

                    impl_->spriteBatch->Draw(*sheet,
                                             Xna::Vector2{cell.x, cell.y},
                                             Xna::Rectangle{sheetX, sheetY, tileWidth, tileHeight},
                                             toXnaColor(EditorColor{}),
                                             0.0f,
                                             Xna::Vector2{0.0f, 0.0f},
                                             Xna::Vector2{scaleX, scaleY},
                                             XnaGraphics::SpriteEffects::None,
                                             0.9f);
                    ++stats.tilesDrawn;
                }
            }
        }
        impl_->spriteBatch->End();

        // Pass 3: the editor's overlay. Separate from the content pass on purpose -- selection
        // outlines are editor artefacts and must never become part of the scene.
        if (!editorOverlays)
        {
            if (offscreen) { device.SetRenderTarget(nullptr); }
            lastStats_ = stats;
            return stats;
        }

        impl_->spriteBatch->Begin(XnaGraphics::SpriteSortMode::Deferred,
                                  XnaGraphics::BlendState::AlphaBlend);

        // Icons first, so a selection outline or the gizmo lands on top of one rather than under.
        for (const EditorIconPlacement& icon : collectEditorIcons(scene, camera))
        {
            const bool selected =
                std::find(selection.begin(), selection.end(), icon.entityId) != selection.end();

            // The badge outline is the *only* selection feedback an icon entity gets: a camera has
            // no bounds, so the outline pass below finds nothing to draw around it.
            impl_->drawEditorIcon(icon, selected);
            ++stats.iconsDrawn;
        }

        for (const Uuid& selectedId : selection)
        {
            const std::optional<WorldBounds2D> bounds =
                computeEntityBounds2D(scene, selectedId, sizeProvider);
            if (!bounds) { continue; }

            const EditorVector2 topLeft = camera.worldToScreen(bounds->min);
            const EditorVector2 bottomRight = camera.worldToScreen(bounds->max);
            impl_->drawOutline(Xna::Rectangle{static_cast<int>(topLeft.x), static_cast<int>(topLeft.y),
                                              static_cast<int>(bottomRight.x - topLeft.x),
                                              static_cast<int>(bottomRight.y - topLeft.y)},
                               kSelection, 2);
        }

        // The gizmo goes on the *primary* selection only. Drawing one per selected entity would
        // put several overlapping manipulators on screen with no way to tell which one a press
        // would grab. Manipulating a whole multi-selection needs a shared pivot first, and is a
        // separate piece of work (plan.md ED-200's multi-select).
        if (!selection.empty())
        {
            const Uuid& gizmoTarget = selection.front();

            // With several entities selected the gizmo sits on their shared pivot -- the average of
            // their positions -- and manipulates all of them about it. The layout is still computed
            // for the primary selection, so its arms follow that entity's rotation in local space;
            // only the origin moves.
            const std::optional<EditorVector2> pivot =
                selection.size() > 1 ? computeSelectionPivot(scene, selection) : std::nullopt;

            switch (gizmoMode)
            {
                case GizmoMode::Translate:
                    if (auto layout = computeTranslateGizmoLayout(scene, camera, gizmoTarget, gizmoSpace))
                    {
                        if (pivot) { placeGizmoAt(*layout, camera, *pivot); }
                        impl_->drawTranslateGizmo(*layout);
                    }
                    break;

                case GizmoMode::Rotate:
                    if (auto layout = computeRotateGizmoLayout(scene, camera, gizmoTarget))
                    {
                        if (pivot) { placeGizmoAt(*layout, camera, *pivot); }
                        impl_->drawRotateGizmo(*layout);
                    }
                    break;

                case GizmoMode::Scale:
                    if (auto layout = computeScaleGizmoLayout(scene, camera, gizmoTarget))
                    {
                        if (pivot) { placeGizmoAt(*layout, camera, *pivot); }
                        impl_->drawScaleGizmo(*layout);
                    }
                    break;

                case GizmoMode::None:
                    break;
            }
        }
        impl_->spriteBatch->End();

        if (offscreen) { device.SetRenderTarget(nullptr); }

        lastStats_ = stats;
        return stats;
    }
}

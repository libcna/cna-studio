// SPDX-License-Identifier: MS-PL
/**
 * @file CnaUiRenderer.cpp
 * @brief The editor UI, drawn with the same public API a CNA game has.
 *
 * See CnaUiRenderer.hpp for the full mapping from what an immediate-mode UI needs to what CNA
 * exposes. The short version: nothing here is backend-specific, nothing here is internal, and the
 * whole file would compile unchanged against any CNA build.
 */

#include "CNA/Editor/Viewport/CnaUiRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "CNA/GraphicsBackendType.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace CNA::Editor
{
    namespace
    {
        /**
         * @brief Unpacks a UiVertex colour into a CNA Color.
         *
         * UiVertex stores R in the lowest byte through A in the highest, matching Dear ImGui's
         * default IM_COL32 layout.
         */
        Xna::Color unpackColor(std::uint32_t rgba)
        {
            return Xna::Color(static_cast<int>(rgba & 0xFFu),
                              static_cast<int>((rgba >> 8) & 0xFFu),
                              static_cast<int>((rgba >> 16) & 0xFFu),
                              static_cast<int>((rgba >> 24) & 0xFFu));
        }
    }

    struct CnaUiRenderer::Impl
    {
        XnaGraphics::GraphicsDevice* device = nullptr;
        std::unique_ptr<XnaGraphics::BasicEffect> effect;
        std::unordered_map<UiTextureId, std::unique_ptr<XnaGraphics::Texture2D>> textures;

        /**
         * @brief Textures owned by somebody else -- the scene viewport's render target.
         *
         * Kept apart from `textures` so shutdown() never deletes a texture it does not own.
         */
        std::unordered_map<UiTextureId, XnaGraphics::Texture2D*> borrowedTextures;

        /**
         * @brief Ids for borrowed textures, counting down from the top.
         *
         * Allocated from the opposite end of the range Dear ImGui's own ids come from, so the two
         * namespaces cannot collide however many atlases the UI grows.
         */
        UiTextureId nextBorrowedId = ~UiTextureId{0};

        /** @brief The single slot borrowed textures reuse. See adoptTexture(). */
        UiTextureId borrowedSlot = kUiTextureNone;

        /** @brief Ids minted for keyed borrows, so a key keeps the same id across frames. */
        std::unordered_map<Uuid, UiTextureId> borrowedByKey;

        /** @brief Scratch buffers, reused across frames so a UI frame allocates nothing. */
        std::vector<XnaGraphics::VertexPositionColorTexture> vertexScratch;
        std::vector<std::uint16_t> indexScratch;
        std::vector<Xna::Color> pixelScratch;

        /** @brief Applies @p request, creating, updating or releasing a texture. */
        void applyTextureRequest(const UiTextureRequest& request, UiRenderStats& stats)
        {
            if (device == nullptr) { return; }

            if (request.action == UiTextureAction::Destroy)
            {
                if (textures.erase(request.texture) > 0) { ++stats.texturesDestroyed; }
                return;
            }

            if (request.pixels == nullptr) { return; }

            // CNA's Texture2D::SetData takes Color, not raw bytes, so the RGBA source is widened
            // into the scratch buffer once per upload. Font atlas uploads are rare -- creation
            // plus the occasional glyph row -- so this is not a per-frame cost.
            const std::size_t pixelCount =
                static_cast<std::size_t>(request.updateWidth) * static_cast<std::size_t>(request.updateHeight);
            // resize(n) rather than assign(n, value) would be the natural spelling, but CNA's
            // Color has no default constructor -- unlike XNA's, which is a struct and therefore
            // default-constructible. Reported as a CNA gap (docs/SPIKE-IMGUI-CNA.md, gap G-01).
            pixelScratch.assign(pixelCount, Xna::Color(0, 0, 0, 0));
            for (int row = 0; row < request.updateHeight; ++row)
            {
                const std::uint8_t* source = request.pixels + static_cast<std::ptrdiff_t>(row) * request.pitch;
                for (int column = 0; column < request.updateWidth; ++column)
                {
                    const std::uint8_t* texel = source + static_cast<std::ptrdiff_t>(column) * 4;
                    pixelScratch[static_cast<std::size_t>(row) * request.updateWidth + column] =
                        Xna::Color(texel[0], texel[1], texel[2], texel[3]);
                }
            }

            if (request.action == UiTextureAction::Create)
            {
                auto texture = std::make_unique<XnaGraphics::Texture2D>(*device, request.width, request.height);
                texture->SetData(pixelScratch.data(), static_cast<int>(pixelCount));
                textures[request.texture] = std::move(texture);
                ++stats.texturesCreated;
                return;
            }

            const auto found = textures.find(request.texture);
            if (found == textures.end()) { return; }

            const Xna::Rectangle region{request.updateX, request.updateY,
                                        request.updateWidth, request.updateHeight};
            found->second->SetData(0, &region, pixelScratch.data(), 0, static_cast<int>(pixelCount));
            ++stats.texturesUpdated;
        }
    };

    CnaUiRenderer::CnaUiRenderer() : impl_(std::make_unique<Impl>()) {}

    CnaUiRenderer::~CnaUiRenderer() { shutdown(); }

    void CnaUiRenderer::initialize(XnaGraphics::GraphicsDevice& device)
    {
        impl_->device = &device;

        // BasicEffect is XNA's own textured, vertex-coloured, unlit effect -- exactly what an
        // immediate-mode UI draws with. Using it rather than authoring a shader is what keeps
        // this renderer backend-independent: every CNA backend already has to provide it.
        impl_->effect = std::make_unique<XnaGraphics::BasicEffect>(device);
        impl_->effect->setLightingEnabledProperty(false);
        impl_->effect->setTextureEnabledProperty(true);
        impl_->effect->VertexColorEnabled = true;
        impl_->effect->setWorldProperty(Xna::Matrix::getIdentityProperty());
        impl_->effect->setViewProperty(Xna::Matrix::getIdentityProperty());
    }

    void CnaUiRenderer::shutdown()
    {
        // Borrowed entries are pointers to somebody else's textures; dropping the map is all that
        // may be done to them.
        impl_->borrowedTextures.clear();
        impl_->borrowedByKey.clear();
        impl_->borrowedSlot = kUiTextureNone;
        impl_->textures.clear();
        impl_->effect.reset();
        impl_->device = nullptr;
    }

    std::size_t CnaUiRenderer::getTextureCount() const
    {
        return impl_->textures.size() + impl_->borrowedTextures.size();
    }

    UiTextureId CnaUiRenderer::adoptTexture(XnaGraphics::Texture2D& texture)
    {
        // One slot per renderer instance rather than one per call: the scene target is re-created
        // on every panel resize, and minting a fresh id each time would grow the map without bound
        // and leave the UI referencing ids whose texture had already gone.
        if (impl_->borrowedSlot == kUiTextureNone) { impl_->borrowedSlot = impl_->nextBorrowedId--; }
        impl_->borrowedTextures[impl_->borrowedSlot] = &texture;
        return impl_->borrowedSlot;
    }

    UiTextureId CnaUiRenderer::adoptTexture(const Uuid& key, XnaGraphics::Texture2D& texture)
    {
        // The id is minted once per key and reused. Minting per call would give the same row a
        // different texture id every frame, which defeats every cache the UI has.
        UiTextureId& id = impl_->borrowedByKey[key];
        if (id == kUiTextureNone) { id = impl_->nextBorrowedId--; }

        impl_->borrowedTextures[id] = &texture;
        return id;
    }

    void CnaUiRenderer::releaseAdoptedTexture(const Uuid& key)
    {
        const auto found = impl_->borrowedByKey.find(key);
        if (found == impl_->borrowedByKey.end()) { return; }

        impl_->borrowedTextures.erase(found->second);
        impl_->borrowedByKey.erase(found);
    }

    std::string CnaUiRenderer::getBackendName()
    {
        // Compile-time, because CNA resolves its backend at compile time -- there is exactly one
        // in this binary and it cannot change. See ANALYSIS.md finding F-01.
        return std::string{CNA::getCurrentGraphicsBackendName()};
    }

    UiRenderStats CnaUiRenderer::render(const UiDrawData& drawData)
    {
        UiRenderStats stats = applyTextureRequests(drawData);
        const UiRenderStats geometry = renderGeometry(drawData);

        stats.drawCalls = geometry.drawCalls;
        stats.triangles = geometry.triangles;
        stats.clippedAway = geometry.clippedAway;

        lastStats_ = stats;
        return stats;
    }

    UiRenderStats CnaUiRenderer::applyTextureRequests(const UiDrawData& drawData)
    {
        UiRenderStats stats;
        if (impl_->device == nullptr) { return stats; }

        for (const UiTextureRequest& request : drawData.textureRequests)
        {
            impl_->applyTextureRequest(request, stats);
        }
        return stats;
    }

    UiRenderStats CnaUiRenderer::renderGeometry(const UiDrawData& drawData)
    {
        UiRenderStats stats;

        if (impl_->device == nullptr || impl_->effect == nullptr) { return stats; }

        XnaGraphics::GraphicsDevice& device = *impl_->device;

        const float framebufferWidth = drawData.displayWidth * drawData.framebufferScaleX;
        const float framebufferHeight = drawData.displayHeight * drawData.framebufferScaleY;
        if (framebufferWidth <= 0.0f || framebufferHeight <= 0.0f) { return stats; }

        // Everything the UI needs from the device, captured so it can be put back afterwards. A
        // caller that draws a scene after the UI must not silently inherit the UI's own state.
        const XnaGraphics::BlendState previousBlend = device.getBlendStateProperty();
        const XnaGraphics::DepthStencilState previousDepth = device.getDepthStencilStateProperty();
        const XnaGraphics::RasterizerState previousRasterizer = device.getRasterizerStateProperty();
        const Xna::Rectangle previousScissor = device.getScissorRectangleProperty();

        device.setBlendStateProperty(XnaGraphics::BlendState::NonPremultiplied);
        device.setDepthStencilStateProperty(XnaGraphics::DepthStencilState::None);

        XnaGraphics::RasterizerState uiRasterizer = XnaGraphics::RasterizerState::CullNone;
        uiRasterizer.setScissorTestEnableProperty(true);
        device.setRasterizerStateProperty(uiRasterizer);

        device.getSamplerStatesProperty()[0] = XnaGraphics::SamplerState::LinearClamp;

        // Pixel-space projection: x runs left-to-right, y runs *top-to-bottom*, which is why the
        // top and bottom arguments are swapped relative to a world-space orthographic camera.
        impl_->effect->setProjectionProperty(Xna::Matrix::CreateOrthographicOffCenter(
            drawData.displayX, drawData.displayX + drawData.displayWidth,
            drawData.displayY + drawData.displayHeight, drawData.displayY,
            0.0f, 1.0f));

        for (const UiDrawList& list : drawData.lists)
        {
            if (list.commands.empty() || list.vertices.empty()) { continue; }

            impl_->vertexScratch.clear();
            impl_->vertexScratch.reserve(list.vertices.size());
            for (const UiVertex& vertex : list.vertices)
            {
                impl_->vertexScratch.push_back(XnaGraphics::VertexPositionColorTexture{
                    Xna::Vector3{vertex.x, vertex.y, 0.0f},
                    unpackColor(vertex.rgba),
                    Xna::Vector2{vertex.u, vertex.v}});
            }

            for (const UiDrawCommand& command : list.commands)
            {
                if (command.indexCount == 0) { continue; }

                const UiClipRect clip =
                    command.clipRect.clampTo(drawData.displayX + drawData.displayWidth,
                                             drawData.displayY + drawData.displayHeight);
                if (clip.isEmpty())
                {
                    // Fully clipped: skipping is not merely an optimisation. A zero-area scissor
                    // rectangle is rejected outright by some graphics APIs.
                    ++stats.clippedAway;
                    continue;
                }

                device.setScissorRectangleProperty(Xna::Rectangle{
                    static_cast<int>(clip.left * drawData.framebufferScaleX),
                    static_cast<int>(clip.top * drawData.framebufferScaleY),
                    static_cast<int>(std::ceil((clip.right - clip.left) * drawData.framebufferScaleX)),
                    static_cast<int>(std::ceil((clip.bottom - clip.top) * drawData.framebufferScaleY))});

                XnaGraphics::Texture2D* texture = nullptr;
                if (const auto owned = impl_->textures.find(command.texture); owned != impl_->textures.end())
                {
                    texture = owned->second.get();
                }
                else if (const auto borrowed = impl_->borrowedTextures.find(command.texture);
                         borrowed != impl_->borrowedTextures.end())
                {
                    texture = borrowed->second;
                }

                if (texture == nullptr)
                {
                    // A command naming a texture that was never created would otherwise sample
                    // whatever happens to be bound. Skipping loses that one command; drawing it
                    // would show garbage across the whole UI.
                    continue;
                }
                impl_->effect->setTextureProperty(texture);
                impl_->effect->Apply();

                // The vertex offset is folded into the pointer rather than passed separately,
                // because CNA's DrawUserIndexedPrimitives takes a vertex *offset in vertices*
                // relative to the array it is given -- the two conventions agree only when the
                // base is applied here.
                const XnaGraphics::VertexPositionColorTexture* vertices =
                    impl_->vertexScratch.data() + command.vertexOffset;
                const std::size_t availableVertices = impl_->vertexScratch.size() - command.vertexOffset;

                device.DrawUserIndexedPrimitives(
                    XnaGraphics::PrimitiveType::TriangleList,
                    vertices, 0, static_cast<int>(availableVertices),
                    list.indices.data() + command.indexOffset, 0,
                    static_cast<int>(command.indexCount / 3));

                ++stats.drawCalls;
                stats.triangles += command.indexCount / 3;
            }
        }

        device.setScissorRectangleProperty(previousScissor);
        device.setRasterizerStateProperty(previousRasterizer);
        device.setDepthStencilStateProperty(previousDepth);
        device.setBlendStateProperty(previousBlend);

        return stats;
    }
}

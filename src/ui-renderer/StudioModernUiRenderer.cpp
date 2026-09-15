// SPDX-License-Identifier: MS-PL
/**
 * @file StudioModernUiRenderer.cpp
 * @brief The Studio UI, drawn through CNA's modern graphics API.
 */

#include "CNA/Studio/UiRenderer/StudioModernUiRenderer.hpp"

#include "CNA/Studio/UiRenderer/StudioUiShaderSource.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#ifdef CNA_CNAEXT
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#endif

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace CNA::Studio
{
    namespace
    {
        /** @brief Unpacks a UiVertex colour into a CNA Color. R is the lowest byte. */
        Xna::Color unpackColor(std::uint32_t rgba)
        {
            return Xna::Color(static_cast<int>(rgba & 0xFFu),
                              static_cast<int>((rgba >> 8) & 0xFFu),
                              static_cast<int>((rgba >> 16) & 0xFFu),
                              static_cast<int>((rgba >> 24) & 0xFFu));
        }

        /**
         * @brief Rounds a required size up to the next power of two, with a floor.
         *
         * Powers of two rather than exact fits: a UI whose geometry oscillates by one quad either
         * side of the current capacity would otherwise reallocate on alternate frames for ever.
         */
        std::size_t growTo(std::size_t required, std::size_t floorSize)
        {
            std::size_t size = floorSize;
            while (size < required) { size *= 2; }
            return size;
        }

        /** @brief The smallest buffers worth allocating: roughly a shell frame's worth. */
        constexpr std::size_t kInitialVertices = 4096;
        constexpr std::size_t kInitialIndices = 6144;
    } // namespace

    struct StudioModernUiRenderer::Impl
    {
        XnaGraphics::GraphicsDevice* device = nullptr;
        std::unique_ptr<XnaGraphics::ShaderEffect> effect;

        std::unique_ptr<XnaGraphics::DynamicVertexBuffer> vertexBuffer;
        std::unique_ptr<XnaGraphics::DynamicIndexBuffer> indexBuffer;
        std::size_t vertexCapacity = 0;
        std::size_t indexCapacity = 0;
        std::size_t growths = 0;

        std::string diagnostic;
        bool usable = false;

        std::unordered_map<UiTextureId, std::unique_ptr<XnaGraphics::Texture2D>> textures;

        /** @brief Textures owned by somebody else -- the scene viewport's render target. */
        std::unordered_map<UiTextureId, XnaGraphics::Texture2D*> borrowedTextures;
        UiTextureId nextBorrowedId = ~UiTextureId{0};
        UiTextureId borrowedSlot = kUiTextureNone;
        std::unordered_map<Uuid, UiTextureId> borrowedByKey;

        /** @brief Scratch buffers, reused across frames so a UI frame allocates nothing. */
        std::vector<XnaGraphics::VertexPositionColorTexture> vertexScratch;
        std::vector<Xna::Color> pixelScratch;

        /** @brief Builds the shader package and compiles it. Sets `usable` and `diagnostic`. */
        void compileShader()
        {
#ifdef CNA_CNAEXT
            std::vector<::CNA::Graphics::ShaderCodeEXT> variants;
            variants.reserve(studioUiShaderVariants().size());
            for (const StudioUiShaderVariant& variant : studioUiShaderVariants())
            {
                const ::CNA::ShaderLanguageEXT language =
                    variant.dialect == StudioShaderDialect::GlslDesktop
                        ? ::CNA::ShaderLanguageEXT::GlslDesktop
                        : ::CNA::ShaderLanguageEXT::GlslEs;
                variants.emplace_back(language,
                                      variant.isVertex ? ::CNA::ShaderStageEXT::Vertex
                                                       : ::CNA::ShaderStageEXT::Fragment,
                                      "main", std::string{variant.label},
                                      std::string{variant.source});
            }

            try
            {
                const ::CNA::Graphics::ShaderPackageEXT package{
                    std::move(variants),
                    {::CNA::ShaderStageEXT::Vertex, ::CNA::ShaderStageEXT::Fragment}};

                // Asked before compiling, and kept either way: which dialect a renderer selected is
                // the first question about a UI that draws slightly wrong, and it is not otherwise
                // knowable from outside CNA.
                const ::CNA::Graphics::ShaderPackageSelectionEXT selection = package.selectFor(*device);
                diagnostic = selection.getDiagnostic();
                if (!selection.isUsable())
                {
                    usable = false;
                    return;
                }

                effect = std::make_unique<XnaGraphics::ShaderEffect>(*device, package);
                usable = effect->IsEffectValid();
                if (!usable)
                {
                    // The compile error rather than the selection summary: by this point the
                    // language was chosen and the thing that failed is the source.
                    diagnostic = effect->GetCompileErrorEXT();
                    effect.reset();
                }
            }
            catch (const std::exception& error)
            {
                // CNA throws for a package it will not accept and for a renderer that refuses the
                // selected language. Both mean this backend cannot draw, and both are things a
                // host has to be able to report rather than crash on -- a renderer being unable to
                // run the modern UI is a supported outcome, not a defect.
                usable = false;
                effect.reset();
                diagnostic = std::string{"the UI shader was refused: "} + error.what();
            }
#else
            // Unreachable in a build that can select this backend, because STUDIO-02070's modern
            // host profile requires the engine layer. Stated rather than omitted: a translation
            // unit that silently compiles to nothing is one somebody later wonders about.
            usable = false;
            diagnostic = "this Studio was built against a CNA without the CNAEXT engine layer "
                         "(-DCNA_CNAEXT=OFF), so there is no ShaderPackageEXT to compile.";
#endif
        }

        /** @brief Makes sure the buffers hold @p vertices and @p indices. Returns whether it grew. */
        bool ensureCapacity(std::size_t vertices, std::size_t indices)
        {
            const bool needVertices = vertices > vertexCapacity;
            const bool needIndices = indices > indexCapacity;
            if (!needVertices && !needIndices) { return false; }

            if (needVertices)
            {
                vertexCapacity = growTo(vertices, std::max(vertexCapacity * 2, kInitialVertices));
                vertexBuffer = std::make_unique<XnaGraphics::DynamicVertexBuffer>(
                    *device, XnaGraphics::VertexPositionColorTexture::getVertexDeclarationStatic(),
                    static_cast<int>(vertexCapacity), XnaGraphics::BufferUsage::WriteOnly);
            }
            if (needIndices)
            {
                indexCapacity = growTo(indices, std::max(indexCapacity * 2, kInitialIndices));
                indexBuffer = std::make_unique<XnaGraphics::DynamicIndexBuffer>(
                    *device, XnaGraphics::IndexElementSize::SixteenBits,
                    static_cast<int>(indexCapacity), XnaGraphics::BufferUsage::WriteOnly);
            }
            ++growths;
            return true;
        }

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

            const std::size_t pixelCount = static_cast<std::size_t>(request.updateWidth)
                                         * static_cast<std::size_t>(request.updateHeight);
            pixelScratch.assign(pixelCount, Xna::Color(0, 0, 0, 0));
            for (int row = 0; row < request.updateHeight; ++row)
            {
                const std::uint8_t* source =
                    request.pixels + static_cast<std::ptrdiff_t>(row) * request.pitch;
                for (int column = 0; column < request.updateWidth; ++column)
                {
                    const std::uint8_t* texel = source + static_cast<std::ptrdiff_t>(column) * 4;
                    pixelScratch[static_cast<std::size_t>(row) * request.updateWidth + column] =
                        Xna::Color(texel[0], texel[1], texel[2], texel[3]);
                }
            }
            stats.textureBytesUploaded += pixelCount * 4;

            if (request.action == UiTextureAction::Create)
            {
                auto texture =
                    std::make_unique<XnaGraphics::Texture2D>(*device, request.width, request.height);
                texture->SetData(pixelScratch.data(), static_cast<int>(pixelCount));
                textures[request.texture] = std::move(texture);
                ++stats.texturesCreated;
                return;
            }

            const auto found = textures.find(request.texture);
            if (found == textures.end()) { return; }

            const Xna::Rectangle region{request.updateX, request.updateY,
                                        request.updateWidth, request.updateHeight};
            found->second->SetData(0, &region, pixelScratch.data(), 0,
                                   static_cast<int>(pixelCount));
            ++stats.texturesUpdated;
        }

        /** @brief Finds an owned or borrowed texture by id, or null. */
        [[nodiscard]] XnaGraphics::Texture2D* find(UiTextureId id) const
        {
            if (const auto owned = textures.find(id); owned != textures.end())
            {
                return owned->second.get();
            }
            if (const auto borrowed = borrowedTextures.find(id); borrowed != borrowedTextures.end())
            {
                return borrowed->second;
            }
            return nullptr;
        }
    };

    StudioModernUiRenderer::StudioModernUiRenderer() : impl_(std::make_unique<Impl>()) {}

    StudioModernUiRenderer::~StudioModernUiRenderer() { shutdown(); }

    void StudioModernUiRenderer::initialize(XnaGraphics::GraphicsDevice& device)
    {
        impl_->device = &device;
        impl_->compileShader();
    }

    void StudioModernUiRenderer::shutdown()
    {
        impl_->borrowedTextures.clear();
        impl_->borrowedByKey.clear();
        impl_->borrowedSlot = kUiTextureNone;
        impl_->textures.clear();
        impl_->vertexBuffer.reset();
        impl_->indexBuffer.reset();
        impl_->vertexCapacity = 0;
        impl_->indexCapacity = 0;
        impl_->effect.reset();
        impl_->usable = false;
        impl_->device = nullptr;
    }

    bool StudioModernUiRenderer::isUsable() const { return impl_->usable; }

    const std::string& StudioModernUiRenderer::shaderDiagnostic() const
    {
        return impl_->diagnostic;
    }

    std::size_t StudioModernUiRenderer::vertexCapacity() const { return impl_->vertexCapacity; }
    std::size_t StudioModernUiRenderer::indexCapacity() const { return impl_->indexCapacity; }
    std::size_t StudioModernUiRenderer::bufferGrowths() const { return impl_->growths; }

    std::size_t StudioModernUiRenderer::getTextureCount() const
    {
        return impl_->textures.size() + impl_->borrowedTextures.size();
    }

    UiTextureId StudioModernUiRenderer::adoptTexture(XnaGraphics::Texture2D& texture)
    {
        if (impl_->borrowedSlot == kUiTextureNone)
        {
            impl_->borrowedSlot = impl_->nextBorrowedId--;
        }
        impl_->borrowedTextures[impl_->borrowedSlot] = &texture;
        return impl_->borrowedSlot;
    }

    UiTextureId StudioModernUiRenderer::adoptTexture(const Uuid& key,
                                                     XnaGraphics::Texture2D& texture)
    {
        UiTextureId& id = impl_->borrowedByKey[key];
        if (id == kUiTextureNone) { id = impl_->nextBorrowedId--; }

        impl_->borrowedTextures[id] = &texture;
        return id;
    }

    void StudioModernUiRenderer::releaseAdoptedTexture(const Uuid& key)
    {
        const auto found = impl_->borrowedByKey.find(key);
        if (found == impl_->borrowedByKey.end()) { return; }

        impl_->borrowedTextures.erase(found->second);
        impl_->borrowedByKey.erase(found);
    }

    UiRenderStats StudioModernUiRenderer::applyTextureRequests(const UiDrawData& drawData)
    {
        UiRenderStats stats;
        if (impl_->device == nullptr) { return stats; }

        for (const UiTextureRequest& request : drawData.textureRequests)
        {
            impl_->applyTextureRequest(request, stats);
        }
        return stats;
    }

    UiRenderStats StudioModernUiRenderer::renderGeometry(const UiDrawData& drawData)
    {
        UiRenderStats stats;

        // Not an error path worth logging per frame: a host that fell back already said so once,
        // and a backend that draws nothing draws nothing quietly rather than filling a console.
        if (impl_->device == nullptr || !impl_->usable || impl_->effect == nullptr)
        {
            return stats;
        }

        XnaGraphics::GraphicsDevice& device = *impl_->device;

        const float framebufferWidth = drawData.displayWidth * drawData.framebufferScaleX;
        const float framebufferHeight = drawData.displayHeight * drawData.framebufferScaleY;
        if (framebufferWidth <= 0.0f || framebufferHeight <= 0.0f) { return stats; }

        // Everything the UI takes from the device, so a caller drawing a scene afterwards does not
        // silently inherit the UI's state.
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

        // Pixel space: x left-to-right, y *top-to-bottom*, which is why top and bottom are swapped
        // relative to a world-space orthographic camera.
        const Xna::Matrix projection = Xna::Matrix::CreateOrthographicOffCenter(
            drawData.displayX, drawData.displayX + drawData.displayWidth,
            drawData.displayY + drawData.displayHeight, drawData.displayY,
            0.0f, 1.0f);

        // Applied *before* the uniform is set, and that order is the whole of it: CNA's uniform
        // setters write to the program that is currently bound, and binding is what Apply() does.
        // Setting the projection first sends it to whatever program the last caller left bound --
        // which, on a frame where a scene was drawn before the UI, is a real program, silently
        // taking a matrix meant for this one. The symptom is a UI that renders nothing at all.
        impl_->effect->Apply();

        // Column-major, because CNA's renderers pass the array straight to the graphics API with
        // no transpose, and XNA's Matrix is row-major. The wrong order is not a wrong-looking UI:
        // every vertex lands outside the clip volume and the frame comes out empty.
        float projectionColumns[16] = {};
        projection.ToColumnMajor(projectionColumns);

        // Through the uniform rather than through setProjectionProperty: ShaderEffect stores the
        // IEffectMatrices properties and uploads nothing on its own, so a shader with its own
        // uniform name has to be told. The name is shared with the source in one header, because
        // the failure when they disagree is a black window rather than a compile error.
        //
        // Once per frame, not once per command. A GL program object retains its uniforms across
        // re-binds, so the value set here survives every Apply() below -- and a mat4 uploaded per
        // draw call would be a state change per draw call, which is what this backend exists to
        // avoid.
        impl_->effect->SetUniformMat4(std::string{kStudioUiProjectionUniform}.c_str(),
                                      projectionColumns);

        UiTextureId boundTexture = kUiTextureNone;
        Xna::Rectangle appliedScissor{-1, -1, -1, -1};

        for (const UiDrawList& list : drawData.lists)
        {
            if (list.commands.empty() || list.vertices.empty()) { continue; }

            if (impl_->ensureCapacity(list.vertices.size(), list.indices.size()))
            {
                // Counted rather than logged. Growth is expected at start-up and after a resize,
                // and a log line per growth would be noise on exactly the frames a user is already
                // watching; a count is what a benchmark asserts on.
            }
            if (impl_->vertexBuffer == nullptr || impl_->indexBuffer == nullptr) { continue; }

            impl_->vertexScratch.clear();
            impl_->vertexScratch.reserve(list.vertices.size());
            for (const UiVertex& vertex : list.vertices)
            {
                impl_->vertexScratch.push_back(XnaGraphics::VertexPositionColorTexture{
                    Xna::Vector3{vertex.x, vertex.y, 0.0f},
                    unpackColor(vertex.rgba),
                    Xna::Vector2{vertex.u, vertex.v}});
            }

            // Discard rather than None: the driver hands back fresh memory instead of waiting for
            // the previous frame's draws to finish reading the old, which is the difference between
            // one upload and one pipeline stall.
            impl_->vertexBuffer->SetData(
                impl_->vertexScratch.data(), 0, static_cast<int>(impl_->vertexScratch.size()),
                XnaGraphics::SetDataOptions::Discard);
            impl_->indexBuffer->SetData(
                list.indices.data(), 0, static_cast<int>(list.indices.size()),
                XnaGraphics::SetDataOptions::Discard);

            stats.vertices += list.vertices.size();
            stats.geometryBytesUploaded +=
                impl_->vertexScratch.size() * sizeof(XnaGraphics::VertexPositionColorTexture)
                + list.indices.size() * sizeof(std::uint16_t);

            device.SetVertexBuffer(impl_->vertexBuffer.get());
            device.setIndicesProperty(impl_->indexBuffer.get());

            for (const UiDrawCommand& command : list.commands)
            {
                if (command.indexCount == 0) { continue; }

                const UiClipRect clip =
                    command.clipRect.clampTo(drawData.displayX + drawData.displayWidth,
                                             drawData.displayY + drawData.displayHeight);
                if (clip.isEmpty())
                {
                    // A zero-area scissor rectangle is rejected outright by some graphics APIs, so
                    // skipping is correctness rather than an optimisation.
                    ++stats.clippedAway;
                    continue;
                }

                const Xna::Rectangle scissor{
                    static_cast<int>(clip.left * drawData.framebufferScaleX),
                    static_cast<int>(clip.top * drawData.framebufferScaleY),
                    static_cast<int>(std::ceil((clip.right - clip.left) * drawData.framebufferScaleX)),
                    static_cast<int>(std::ceil((clip.bottom - clip.top) * drawData.framebufferScaleY))};
                if (scissor.X != appliedScissor.X || scissor.Y != appliedScissor.Y
                    || scissor.Width != appliedScissor.Width
                    || scissor.Height != appliedScissor.Height)
                {
                    ++stats.clipChanges;
                    appliedScissor = scissor;
                }
                device.setScissorRectangleProperty(scissor);

                XnaGraphics::Texture2D* texture = impl_->find(command.texture);
                if (texture == nullptr)
                {
                    // A command naming a texture that was never created would otherwise sample
                    // whatever happens to be bound. Skipping loses that one command; drawing it
                    // would show garbage across the whole UI.
                    continue;
                }
                if (command.texture != boundTexture)
                {
                    ++stats.textureChanges;
                    boundTexture = command.texture;
                }
                impl_->effect->SetTexture(0, *texture);
                impl_->effect->Apply();

                device.DrawIndexedPrimitives(
                    XnaGraphics::PrimitiveType::TriangleList,
                    static_cast<int>(command.vertexOffset), 0,
                    static_cast<int>(list.vertices.size() - command.vertexOffset),
                    static_cast<int>(command.indexOffset),
                    static_cast<int>(command.indexCount / 3));

                ++stats.drawCalls;
                stats.triangles += command.indexCount / 3;
                stats.indices += command.indexCount;
            }
        }

        device.SetVertexBuffer(nullptr);
        device.setIndicesProperty(nullptr);
        device.setScissorRectangleProperty(previousScissor);
        device.setRasterizerStateProperty(previousRasterizer);
        device.setDepthStencilStateProperty(previousDepth);
        device.setBlendStateProperty(previousBlend);

        lastStats_ = stats;
        return stats;
    }
} // namespace CNA::Studio

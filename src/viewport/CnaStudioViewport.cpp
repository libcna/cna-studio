// SPDX-License-Identifier: MS-PL
/**
 * @file CnaStudioViewport.cpp
 * @brief The CNA-backed scene viewport.
 *
 * A thin adapter: it owns an `StudioCamera2D` (which is CNA-free and lives in cna-studio-scene) and
 * delegates the actual drawing to `CnaSceneRenderer`. Keeping the two apart means the renderer can
 * be reused for asset thumbnails and, later, for the per-backend captures plan.md ED-510 needs,
 * without dragging the viewport's interaction state along with it.
 */

#include "CNA/Studio/Viewport/StudioViewport.hpp"

#include <string>

#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Viewport/CnaSceneRenderer.hpp"
#include "CNA/Studio/UiRenderer/StudioUiRenderBackend.hpp"
#include <iterator>
#include <utility>
#include <vector>

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace CNA::Studio
{
    /**
     * @brief Draws the scene with CNA and hands the result to the UI as a texture.
     *
     * Constructed by the host, which owns the graphics device and the UI renderer the rendered
     * target has to be shared through.
     */
    class CnaStudioViewport final : public StudioViewport
    {
    public:
        CnaStudioViewport(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                          const AssetDatabase& assets,
                          const ComponentRegistry& components,
                          StudioUiRenderBackend& uiRenderer)
            : device_(&device), uiRenderer_(&uiRenderer)
        {
            renderer_.initialize(device, assets, components);
        }

        ~CnaStudioViewport() override { renderer_.shutdown(); }

        [[nodiscard]] const char* getBackendName() const override
        {
            // Compile-time: CNA resolves its backend at compile time, so there is exactly one in
            // this binary (ANALYSIS.md finding F-01).
            static const std::string name =
                std::string{"cna-"} + std::string{CNA::getCurrentGraphicsRendererName()};
            return name.c_str();
        }

        UiTextureId render(const SceneDocument& scene,
                           int width,
                           int height,
                           const std::vector<Uuid>& selection,
                           GizmoMode gizmoMode,
                           GizmoSpace gizmoSpace,
                           const AnimationPreview& preview) override
        {
            if (width <= 0 || height <= 0) { return kUiTextureNone; }

            camera_.setViewportSize(StudioVector2{static_cast<float>(width), static_cast<float>(height)});

            const SceneRenderStats stats =
                renderer_.render(scene, camera_, width, height, selection, gizmoMode, gizmoSpace, preview);
            lastStats_ = ViewportStats{stats.spritesDrawn, stats.spritesSkipped, stats.gridLines,
                                       stats.missingTextures};

            return renderer_.shareWithUi(*uiRenderer_);
        }

        UiTextureId renderScene3D(const SceneModelBatch& models, const SceneSpriteBatch3D& sprites,
                                  const std::vector<WireSegment>& segments, int width,
                                  int height) override
        {
            if (width <= 0 || height <= 0) { return kUiTextureNone; }

            const ModelPassStats modelStats =
                renderer_.renderScene3D(models, sprites, segments, width, height);

            const SceneRenderStats& stats = renderer_.getLastStats();
            lastStats_ = ViewportStats{modelStats.modelsDrawn + modelStats.spritesDrawn,
                                       sprites.skipped, stats.gridLines, modelStats.missingTextures};

            return renderer_.shareWithUi(*uiRenderer_);
        }

        UiTextureId getModelThumbnail(const Uuid& assetId, const MeshData& mesh, int extent) override
        {
            Microsoft::Xna::Framework::Graphics::Texture2D* texture =
                renderer_.renderModelThumbnail(assetId, mesh, extent);
            if (texture == nullptr) { return kUiTextureNone; }

            // Adopted under the asset's own id, exactly as a sprite thumbnail is, so the UI
            // renderer holds one entry per asset and `invalidateAsset` already releases it.
            return uiRenderer_->adoptTexture(assetId, *texture);
        }

        [[nodiscard]] std::string getModelEffectName() const override
        {
            return renderer_.getModelEffectName();
        }

        void invalidateAsset(const Uuid& assetId) override
        {
            // The UI's borrowed entry has to go first: it points at a texture the renderer is
            // about to destroy, and a map holding a dangling pointer is a crash waiting for the
            // next frame that happens to draw that row.
            uiRenderer_->releaseAdoptedTexture(assetId);
            renderer_.invalidateTexture(assetId);

            // And its geometry, if it had any. One entry point rather than two, because the
            // watcher knows an asset changed and not what kind it is -- and a `.gltf` edited with
            // the editor open must be re-uploaded, or the 3D view goes on drawing the old shape
            // while the console reports the change. The mesh cache is dropped separately, in the
            // context; this is the GPU copy of it.
            renderer_.invalidateModel(assetId);
        }

        UiTextureId getAssetThumbnail(const Uuid& assetId) override
        {
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = renderer_.getOrLoadTexture(assetId);
            if (texture == nullptr) { return kUiTextureNone; }
            return uiRenderer_->adoptTexture(assetId, *texture);
        }

        UiTextureId uploadThumbnail(const Uuid& assetId, const std::string& key,
                                    std::uint32_t width, std::uint32_t height,
                                    const std::vector<unsigned char>& rgba) override
        {
            if (device_ == nullptr || uiRenderer_ == nullptr) { return kUiTextureNone; }
            if (width == 0 || height == 0) { return kUiTextureNone; }

            const std::size_t expected =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
            if (rgba.size() != expected) { return kUiTextureNone; }

            // Cached on the key the pixels were made from (STUDIO-35041). This is called once per
            // visible card per draw pass -- forty times a frame, twice a frame -- so re-uploading
            // would be a texture upload per card per pass, which is the one thing a thumbnail was
            // supposed to save. Comparing the pixels instead would cost more than the upload.
            ThumbnailTexture& cached = thumbnails_[assetId];
            if (cached.texture != nullptr && cached.key == key)
            {
                return uiRenderer_->adoptTexture(assetId, *cached.texture);
            }

            try
            {
                // Color has no default constructor (CNA gap G-01), so the buffer is filled rather
                // than sized -- the same shape `writeImageFile` above uses, for the same reason.
                std::vector<Microsoft::Xna::Framework::Color> pixels;
                pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
                for (std::size_t pixel = 0;
                     pixel < static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
                     ++pixel)
                {
                    pixels.emplace_back(static_cast<int>(rgba[pixel * 4 + 0]),
                                        static_cast<int>(rgba[pixel * 4 + 1]),
                                        static_cast<int>(rgba[pixel * 4 + 2]),
                                        static_cast<int>(rgba[pixel * 4 + 3]));
                }

                auto texture = std::make_unique<Microsoft::Xna::Framework::Graphics::Texture2D>(
                    *device_, static_cast<int>(width), static_cast<int>(height));
                texture->SetData(pixels.data(), static_cast<int>(pixels.size()));

                cached.key = key;
                cached.texture = std::move(texture);
                return uiRenderer_->adoptTexture(assetId, *cached.texture);
            }
            catch (const std::exception&)
            {
                // A device that refused the upload is a card that draws its icon, not an editor
                // that stops. Forgotten rather than left half-built, so the next frame tries again
                // rather than returning a texture that was never filled.
                thumbnails_.erase(assetId);
                return kUiTextureNone;
            }
        }

        void releaseThumbnail(const Uuid& assetId) override
        {
            // Asked for when the cache evicts, so a host holding a texture per thumbnail does not
            // end up holding one per thumbnail it has ever seen: a project of a hundred thousand
            // images would otherwise fill a GPU with pictures of folders nobody is in.
            if (uiRenderer_ != nullptr) { uiRenderer_->releaseAdoptedTexture(assetId); }
            thumbnails_.erase(assetId);
        }

        [[nodiscard]] ImageBuffer readImageFile(const std::string& path) const override
        {
            if (device_ == nullptr) { return {}; }

            try
            {
                // Through Texture2D rather than a decoder of our own: the file was written by CNA
                // in another process, and reading it back with the same library is the one way to
                // be sure a difference is in the *picture* rather than in two people's idea of PNG.
                Microsoft::Xna::Framework::Graphics::Texture2D texture{path, *device_};

                const int width = texture.getWidthProperty();
                const int height = texture.getHeightProperty();
                if (width <= 0 || height <= 0) { return {}; }

                const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

                // Color has no default constructor (gap G-01), so the buffer is filled, not sized.
                std::vector<Microsoft::Xna::Framework::Color> pixels(
                    pixelCount, Microsoft::Xna::Framework::Color(0, 0, 0, 255));
                texture.GetData(pixels.data(), static_cast<int>(pixelCount));

                ImageBuffer image;
                image.width = width;
                image.height = height;
                image.pixels.resize(pixelCount * 4);
                for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
                {
                    image.pixels[pixel * 4 + 0] = static_cast<std::uint8_t>(pixels[pixel].getRProperty());
                    image.pixels[pixel * 4 + 1] = static_cast<std::uint8_t>(pixels[pixel].getGProperty());
                    image.pixels[pixel * 4 + 2] = static_cast<std::uint8_t>(pixels[pixel].getBProperty());
                    image.pixels[pixel * 4 + 3] = static_cast<std::uint8_t>(pixels[pixel].getAProperty());
                }
                return image;
            }
            catch (const std::exception&)
            {
                // A file that will not decode is a result, not a crash: the comparison reports
                // that this backend produced nothing readable and carries on with the others.
                return {};
            }
        }

        bool writeImageFile(const std::string& path, const ImageBuffer& image) override
        {
            if (device_ == nullptr || !image.isWellFormed()) { return false; }

            try
            {
                std::vector<Microsoft::Xna::Framework::Color> pixels;
                pixels.reserve(image.getPixelCount());
                for (std::size_t pixel = 0; pixel < image.getPixelCount(); ++pixel)
                {
                    pixels.emplace_back(static_cast<int>(image.pixels[pixel * 4 + 0]),
                                        static_cast<int>(image.pixels[pixel * 4 + 1]),
                                        static_cast<int>(image.pixels[pixel * 4 + 2]),
                                        static_cast<int>(image.pixels[pixel * 4 + 3]));
                }

                Microsoft::Xna::Framework::Graphics::Texture2D texture{*device_, image.width, image.height};
                texture.SetData(pixels.data(), static_cast<int>(pixels.size()));
                texture.SaveAsPng(path);
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        [[nodiscard]] std::vector<ViewportCapability> getBackendCapabilities() const override
        {
            // Asked of the *device*, not derived from the backend name. Several of these vary by
            // driver within one backend -- anisotropic filtering and MSAA especially -- so a table
            // keyed on the backend would confidently report what this machine cannot do.
            static const std::pair<CNA::GraphicsCapability, const char*> kCapabilities[] = {
                {CNA::GraphicsCapability::ThreeD, "3D pipeline"},
                {CNA::GraphicsCapability::DepthStencilBuffer, "Depth/stencil buffer"},
                {CNA::GraphicsCapability::MultiSampleAntiAliasing, "MSAA"},
                {CNA::GraphicsCapability::MultipleRenderTargets, "Multiple render targets"},
                {CNA::GraphicsCapability::AnisotropicFiltering, "Anisotropic filtering"},
                {CNA::GraphicsCapability::WireFrame, "Wireframe fill mode"},
                {CNA::GraphicsCapability::OcclusionQuery, "Occlusion queries"},
                {CNA::GraphicsCapability::CustomEffects, "Custom SpriteBatch effects"},
            };

            std::vector<ViewportCapability> capabilities;
            capabilities.reserve(std::size(kCapabilities));
            for (const auto& [capability, name] : kCapabilities)
            {
                capabilities.push_back(ViewportCapability{name, device_->SupportsCapability(capability)});
            }
            return capabilities;
        }

        [[nodiscard]] StudioVector2 getSpriteSize(const Uuid& assetId) const override
        {
            return renderer_.getSpriteSize(assetId);
        }

        [[nodiscard]] bool isRenderTextureFlippedVertically() const override
        {
            // Compile-time, from the renderer this build was compiled against. Not a runtime
            // probe: CNA fixes its renderer at compile time, so this is a constant, and a probe
            // would cost a render target and a read-back to learn something already known.
            //
            // This is CNA gap G-03 (docs/CNA-GAPS.md) worked around in the one place the
            // architecture allows renderer-specific knowledge to live. When CNA normalises the
            // sampling origin, or publishes the convention as a queryable property, this whole
            // function is deleted in one edit.
            //
            // EasyGL used to be one entry here. It is now a renderer *family* serving several GL
            // profiles, so the GL profiles are listed individually -- which is also why the list
            // must never be assumed complete: STUDIO-02010 re-measures it across CNA's current
            // renderer set rather than inheriting the prototype's two-renderer observation.
            switch (CNA::getCurrentGraphicsRendererType())
            {
                case CNA::GraphicsRendererType::OpenGLES2:
                case CNA::GraphicsRendererType::OpenGLES3:
                case CNA::GraphicsRendererType::OpenGL33:
                case CNA::GraphicsRendererType::WebGL1:
                case CNA::GraphicsRendererType::WebGL2:
                case CNA::GraphicsRendererType::WebGPU:
                case CNA::GraphicsRendererType::Bgfx:
                case CNA::GraphicsRendererType::SdlGpu:
                    return true;
                default:
                    return false;
            }
        }

        UiTextureId renderWireframe(const std::vector<WireSegment>& segments, int width,
                                    int height) override
        {
            if (width <= 0 || height <= 0) { return kUiTextureNone; }

            renderer_.renderWireframe(segments, width, height);

            const SceneRenderStats& stats = renderer_.getLastStats();
            lastStats_ = ViewportStats{stats.spritesDrawn, stats.spritesSkipped, stats.gridLines,
                                       stats.missingTextures};

            return renderer_.shareWithUi(*uiRenderer_);
        }

        [[nodiscard]] StudioCamera2D& getCamera() override { return camera_; }
        [[nodiscard]] const StudioCamera2D& getCamera() const override { return camera_; }

        [[nodiscard]] StudioCamera3D& getCamera3D() override { return camera3D_; }
        [[nodiscard]] const StudioCamera3D& getCamera3D() const override { return camera3D_; }

        [[nodiscard]] ViewportStats getLastStats() const override { return lastStats_; }

    private:
        /** @brief One uploaded thumbnail, and the key its pixels were made from. */
        struct ThumbnailTexture
        {
            std::string key;
            std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture;
        };

        CnaSceneRenderer renderer_;
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device_;
        StudioUiRenderBackend* uiRenderer_;
        StudioCamera2D camera_;
        StudioCamera3D camera3D_;
        ViewportStats lastStats_;

        /** @brief Textures made from `StudioThumbnailCache` pixels. See uploadThumbnail. */
        std::unordered_map<Uuid, ThumbnailTexture> thumbnails_;
    };

    std::unique_ptr<StudioViewport> createCnaStudioViewport(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        const AssetDatabase& assets,
        const ComponentRegistry& components,
        StudioUiRenderBackend& uiRenderer)
    {
        return std::make_unique<CnaStudioViewport>(device, assets, components, uiRenderer);
    }
}

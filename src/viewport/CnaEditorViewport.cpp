// SPDX-License-Identifier: MS-PL
/**
 * @file CnaEditorViewport.cpp
 * @brief The CNA-backed scene viewport.
 *
 * A thin adapter: it owns an `EditorCamera2D` (which is CNA-free and lives in cna-editor-scene) and
 * delegates the actual drawing to `CnaSceneRenderer`. Keeping the two apart means the renderer can
 * be reused for asset thumbnails and, later, for the per-backend captures plan.md ED-510 needs,
 * without dragging the viewport's interaction state along with it.
 */

#include "CNA/Editor/Viewport/EditorViewport.hpp"

#include <string>

#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Viewport/CnaSceneRenderer.hpp"
#include "CNA/Editor/Viewport/CnaUiRenderer.hpp"
#include <iterator>
#include <utility>
#include <vector>

#include "CNA/GraphicsBackendType.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace CNA::Editor
{
    /**
     * @brief Draws the scene with CNA and hands the result to the UI as a texture.
     *
     * Constructed by the host, which owns the graphics device and the UI renderer the rendered
     * target has to be shared through.
     */
    class CnaEditorViewport final : public EditorViewport
    {
    public:
        CnaEditorViewport(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                          const AssetDatabase& assets,
                          const ComponentRegistry& components,
                          CnaUiRenderer& uiRenderer)
            : device_(&device), uiRenderer_(&uiRenderer)
        {
            renderer_.initialize(device, assets, components);
        }

        ~CnaEditorViewport() override { renderer_.shutdown(); }

        [[nodiscard]] const char* getBackendName() const override
        {
            // Compile-time: CNA resolves its backend at compile time, so there is exactly one in
            // this binary (ANALYSIS.md finding F-01).
            static const std::string name =
                std::string{"cna-"} + std::string{CNA::getCurrentGraphicsBackendName()};
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

            camera_.setViewportSize(EditorVector2{static_cast<float>(width), static_cast<float>(height)});

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

        [[nodiscard]] EditorVector2 getSpriteSize(const Uuid& assetId) const override
        {
            return renderer_.getSpriteSize(assetId);
        }

        [[nodiscard]] bool isRenderTextureFlippedVertically() const override
        {
            // Compile-time, from the backend this build was compiled against. Not a runtime probe:
            // CNA fixes its backend at compile time, so this is a constant, and a probe would
            // cost a render target and a read-back to learn something already known.
            switch (CNA::getCurrentGraphicsBackendType())
            {
                case CNA::GraphicsBackendType::EasyGL:
                case CNA::GraphicsBackendType::WebGPU:
                case CNA::GraphicsBackendType::Bgfx:
                case CNA::GraphicsBackendType::SdlGpu:
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

        [[nodiscard]] EditorCamera2D& getCamera() override { return camera_; }
        [[nodiscard]] const EditorCamera2D& getCamera() const override { return camera_; }

        [[nodiscard]] EditorCamera3D& getCamera3D() override { return camera3D_; }
        [[nodiscard]] const EditorCamera3D& getCamera3D() const override { return camera3D_; }

        [[nodiscard]] ViewportStats getLastStats() const override { return lastStats_; }

    private:
        CnaSceneRenderer renderer_;
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device_;
        CnaUiRenderer* uiRenderer_;
        EditorCamera2D camera_;
        EditorCamera3D camera3D_;
        ViewportStats lastStats_;
    };

    std::unique_ptr<EditorViewport> createCnaEditorViewport(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        const AssetDatabase& assets,
        const ComponentRegistry& components,
        CnaUiRenderer& uiRenderer)
    {
        return std::make_unique<CnaEditorViewport>(device, assets, components, uiRenderer);
    }
}

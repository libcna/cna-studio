// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Viewport/CnaModelPass.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <optional>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "CNA/Editor/Assets/AssetDatabase.hpp"

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace CNA::Editor
{
    namespace
    {
        /**
         * @brief The cull mode that matches what the importer produces, after the Y mirror.
         *
         * Named rather than written at the call site because the reasoning is two steps and both
         * are easy to get backwards: the importer winds triangles counter-clockwise seen from
         * *outside in world space*, the view-projection mirrors Y, and mirroring reverses apparent
         * winding -- so an outward face reaches the rasteriser clockwise, and culling the
         * counter-clockwise ones keeps exactly the faces that should be seen. If models ever come
         * out hollow, this constant is the first thing to try, and the header says so too.
         */
        constexpr XnaGraphics::CullMode kOutwardFaces = XnaGraphics::CullMode::CullCounterClockwiseFace;

        /**
         * @brief Whether to draw through `PbrEffect`. **False, and the reason is a measurement.**
         *
         * The owner chose PBR with a `BasicEffect` fallback, and the PBR path below is written and
         * complete. It is off because on the one backend this repository can photograph -- EASYGL
         * under Xvfb -- `PbrEffect` draws *nothing at all*: it constructs without throwing, accepts
         * every parameter, reports its draw calls, and puts no pixels on screen, while
         * `BasicEffect` renders the same geometry, matrices and lights correctly. That is CNA gap
         * G-05 in NEXT.md, along with the two things ruled out on the way (backface culling, and
         * an unbound base-colour sampler, which is a real second bug in `FillGpuDrawParams`).
         *
         * So this is not a preference between two working paths -- it is one working path and one
         * that has been verified not to. Flipping this to `true` is how the PBR path is re-tested
         * when CNA's changes; nothing else has to move.
         */
        constexpr bool kPreferPbrEffect = false;

        /**
         * @brief Converts one of the editor's matrices to XNA's.
         *
         * Field for field and nothing else, because `EditorMatrix` was written to mirror
         * `Microsoft::Xna::Framework::Matrix` exactly (ED-400). This function existing at all is
         * the price of `cna-editor-core` not being allowed to name a CNA type -- not a conversion
         * of conventions, which is what makes it safe to read past.
         */
        Xna::Matrix toXna(const EditorMatrix& matrix)
        {
            Xna::Matrix result;
            result.M11 = matrix.m11; result.M12 = matrix.m12; result.M13 = matrix.m13; result.M14 = matrix.m14;
            result.M21 = matrix.m21; result.M22 = matrix.m22; result.M23 = matrix.m23; result.M24 = matrix.m24;
            result.M31 = matrix.m31; result.M32 = matrix.m32; result.M33 = matrix.m33; result.M34 = matrix.m34;
            result.M41 = matrix.m41; result.M42 = matrix.m42; result.M43 = matrix.m43; result.M44 = matrix.m44;
            return result;
        }

        Xna::Vector3 toXna(const EditorVector3& vector)
        {
            return Xna::Vector3{vector.x, vector.y, vector.z};
        }
    }

    struct CnaModelPass::Impl
    {
        XnaGraphics::GraphicsDevice* device = nullptr;
        const AssetDatabase* assets = nullptr;

        /**
         * @brief Whichever effect this build got. Exactly one of the two is non-null.
         *
         * Two members rather than a base-class pointer: `IEffectMatrices` and `IEffectLights` are
         * the shared interfaces and they do not cover the material properties, which are where
         * the two effects genuinely differ. A pass that reached them through the interfaces alone
         * would be a PBR pass that could not set metallic.
         */
        std::unique_ptr<XnaGraphics::PbrEffect> pbr;
        std::unique_ptr<XnaGraphics::BasicEffect> basic;
        std::string effectName{"none"};

        /** @brief One upload per model asset, drawn once per entity that names it. */
        struct GpuPart
        {
            std::unique_ptr<XnaGraphics::VertexBuffer> vertices;
            std::unique_ptr<XnaGraphics::IndexBuffer> indices;
            int vertexCount = 0;
            int triangleCount = 0;
            int materialIndex = -1;

            /**
             * @brief The mesh part's name, kept so ED-410's per-part overrides can be matched.
             *
             * Copied at upload rather than looked up in the `MeshData` at draw time: the buffers
             * are cached per asset and the parts that produced them may have been skipped, so the
             * index into `mesh.parts` and the index into `parts` here are not the same number.
             */
            std::string name;
        };

        struct GpuModel
        {
            std::vector<GpuPart> parts;
        };

        std::unordered_map<Uuid, std::unique_ptr<GpuModel>> models;

        /** @brief Textures a material named, resolved by path against the database. */
        std::unordered_map<std::string, std::shared_ptr<XnaGraphics::Texture2D>> textures;
        std::unordered_set<std::string> failedTextures;

        /**
         * @brief One white texel, bound wherever a PBR material names no base-colour texture.
         *
         * Not decoration and not a placeholder for a missing file -- it works around a real gap in
         * CNA (G-05). `PbrEffect::FillGpuDrawParams` sets `textureEnabled = true` unconditionally
         * while binding `texture0` only when a texture was given, so a material with a base-colour
         * *factor* and no map -- which is most hand-authored glTF, including this repository's own
         * example crate -- reaches the shader sampling an unbound texture and renders black.
         * `BasicEffect` has a `TextureEnabled` flag and does not have the problem.
         *
         * White is also the arithmetically correct answer rather than a trick: glTF multiplies the
         * base-colour factor by the base-colour texture, and the identity for that multiply is 1.
         */
        std::shared_ptr<XnaGraphics::Texture2D> whiteTexel;

        /** @brief Uploads @p mesh, or returns what was uploaded before. */
        GpuModel* resolveModel(const Uuid& assetId, const MeshData& mesh, ModelPassStats& stats)
        {
            if (const auto found = models.find(assetId); found != models.end())
            {
                return found->second.get();
            }

            auto model = std::make_unique<GpuModel>();

            for (const MeshPart& part : mesh.parts)
            {
                if (part.vertices.empty() || part.indices.size() < 3) { continue; }

                // The copy `MeshData` was shaped to make cheap: field for field, so this is a
                // conversion of types rather than of meaning (see MeshData.hpp's third decision).
                std::vector<XnaGraphics::VertexPositionNormalTexture> vertices;
                vertices.reserve(part.vertices.size());
                for (const MeshVertex& vertex : part.vertices)
                {
                    XnaGraphics::VertexPositionNormalTexture converted;
                    converted.Position = toXna(vertex.position);
                    converted.Normal = toXna(vertex.normal);
                    converted.TextureCoordinate = Xna::Vector2{vertex.texCoord.x, vertex.texCoord.y};
                    vertices.push_back(converted);
                }

                GpuPart gpuPart;
                gpuPart.vertexCount = static_cast<int>(vertices.size());
                gpuPart.triangleCount = static_cast<int>(part.indices.size() / 3);
                gpuPart.materialIndex = part.materialIndex;
                gpuPart.name = part.name;

                try
                {
                    gpuPart.vertices = std::make_unique<XnaGraphics::VertexBuffer>(
                        *device, XnaGraphics::VertexPositionNormalTexture::getVertexDeclarationStatic(),
                        gpuPart.vertexCount, XnaGraphics::BufferUsage::WriteOnly);
                    gpuPart.vertices->SetData(vertices.data(), gpuPart.vertexCount);

                    // Thirty-two-bit indices throughout rather than sixteen where they would fit.
                    // A cube would fit in sixteen and a scanned prop would not, and a pass that
                    // chose per model would have two paths where the rare one is the one that
                    // breaks. The memory is a few hundred kilobytes on a model worth drawing.
                    gpuPart.indices = std::make_unique<XnaGraphics::IndexBuffer>(
                        *device, XnaGraphics::IndexElementSize::ThirtyTwoBits,
                        static_cast<int>(part.indices.size()), XnaGraphics::BufferUsage::WriteOnly);
                    gpuPart.indices->SetData(part.indices.data(),
                                             static_cast<int>(part.indices.size()));
                }
                catch (const std::exception&)
                {
                    // A buffer that will not allocate is a device problem, not a document one.
                    // Drop the part and keep the rest: half a model drawn is more use than a
                    // viewport that stops, and the counters below show something went wrong.
                    continue;
                }

                ++stats.buffersCreated;
                model->parts.push_back(std::move(gpuPart));
            }

            GpuModel* raw = model.get();
            models.emplace(assetId, std::move(model));
            return raw;
        }

        /** @brief Returns the texture @p relativePath names, resolved through the database. */
        XnaGraphics::Texture2D* resolveTexture(const std::string& relativePath, ModelPassStats& stats)
        {
            if (relativePath.empty() || assets == nullptr) { return nullptr; }

            if (const auto found = textures.find(relativePath); found != textures.end())
            {
                return found->second.get();
            }
            if (failedTextures.count(relativePath) > 0) { return nullptr; }

            // The importer hands over a path relative to the model file and says so: it has no
            // database and must not pretend to. Resolving it is this side's job, and the database
            // is what knows where the project's assets are.
            const AssetRecord* record = assets->findByPath(relativePath);
            if (record == nullptr)
            {
                failedTextures.insert(relativePath);
                ++stats.missingTextures;
                return nullptr;
            }

            try
            {
                auto texture = std::make_shared<XnaGraphics::Texture2D>(
                    assets->resolvePath(record->sourcePath), *device);
                XnaGraphics::Texture2D* raw = texture.get();
                textures.emplace(relativePath, std::move(texture));
                return raw;
            }
            catch (const std::exception&)
            {
                failedTextures.insert(relativePath);
                ++stats.missingTextures;
                return nullptr;
            }
        }

        /**
         * @brief Applies @p environment's fog to whichever effect this build has.
         *
         * Linear between a start and an end distance, which is the whole of what `IEffectFog`
         * offers and therefore the whole of what this editor can promise. Both effects implement
         * that interface, so this is the one place in the pass that needs no branch on which one
         * was constructed.
         */
        void applyFog(const SceneEnvironment& environment)
        {
            XnaGraphics::IEffectFog* fog = pbr != nullptr
                                               ? static_cast<XnaGraphics::IEffectFog*>(pbr.get())
                                               : static_cast<XnaGraphics::IEffectFog*>(basic.get());
            if (fog == nullptr) { return; }

            fog->setFogEnabledProperty(environment.fogEnabled);
            if (!environment.fogEnabled) { return; }

            fog->setFogColorProperty(
                toXna(EditorVector3{static_cast<float>(environment.fogColor.r) / 255.0f,
                                    static_cast<float>(environment.fogColor.g) / 255.0f,
                                    static_cast<float>(environment.fogColor.b) / 255.0f}));
            fog->setFogStartProperty(environment.fogStart);
            fog->setFogEndProperty(environment.fogEnd);
        }

        /** @brief Applies @p lighting to whichever effect this build has. */
        void applyLighting(const EffectLighting& lighting)
        {
            XnaGraphics::IEffectLights* lights =
                pbr != nullptr ? static_cast<XnaGraphics::IEffectLights*>(pbr.get())
                               : static_cast<XnaGraphics::IEffectLights*>(basic.get());
            if (lights == nullptr) { return; }

            if (lighting.useDefaultLighting)
            {
                // XNA's own three-point rig. Deliberately not an approximation of it written here:
                // the point of calling the framework's is that a CNA scene and an XNA one with no
                // lights in them look the same.
                lights->EnableDefaultLighting();
                return;
            }

            lights->setLightingEnabledProperty(true);
            lights->setAmbientLightColorProperty(toXna(lighting.ambientColor));

            XnaGraphics::DirectionalLight* slots[3] = {&lights->getDirectionalLight0Property(),
                                                       &lights->getDirectionalLight1Property(),
                                                       &lights->getDirectionalLight2Property()};

            for (std::size_t i = 0; i < 3; ++i)
            {
                const bool used = i < lighting.lightCount;
                slots[i]->setEnabledProperty(used);
                if (!used) { continue; }

                slots[i]->setDirectionProperty(toXna(lighting.lights[i].direction));
                slots[i]->setDiffuseColorProperty(toXna(lighting.lights[i].diffuseColor));
                slots[i]->setSpecularColorProperty(toXna(lighting.lights[i].specularColor));
            }
        }

        /** @brief Applies @p material, or a neutral default when the part named none. */
        void applyMaterial(const MeshData& mesh, int materialIndex, ModelPassStats& stats,
                           const std::optional<MeshMaterial>& override,
                           const std::string& partName,
                           const std::vector<std::pair<std::string, MeshMaterial>>& partMaterials)
        {
            // The per-part list first, because it is the more specific answer: the single override
            // means "this whole model" and the list means "except these parts" (ED-410).
            for (const auto& [name, material] : partMaterials)
            {
                if (name == partName)
                {
                    applyResolvedMaterial(material, stats);
                    return;
                }
            }

            // An override replaces every part's material, which is the only thing one material can
            // mean for a model of several parts.
            if (override.has_value())
            {
                applyResolvedMaterial(*override, stats);
                return;
            }

            const bool named = materialIndex >= 0
                               && static_cast<std::size_t>(materialIndex) < mesh.materials.size();

            // glTF says an unnamed material means the *default* material, so a part with none is
            // drawn white rather than skipped -- MeshData.hpp's note on `materialIndex` says the
            // same thing to every consumer.
            applyResolvedMaterial(named ? mesh.materials[static_cast<std::size_t>(materialIndex)]
                                        : MeshMaterial{},
                                  stats);
        }

        /** @brief Sets @p material on whichever effect this build has. */
        void applyResolvedMaterial(const MeshMaterial& material, ModelPassStats& stats)
        {
            XnaGraphics::Texture2D* diffuse = resolveTexture(material.diffuseTexturePath, stats);

            if (pbr != nullptr)
            {
                // See `whiteTexel`: PbrEffect has no texture-enabled flag and always samples.
                if (diffuse == nullptr) { diffuse = whiteTexel.get(); }

                pbr->setDiffuseColorProperty(toXna(material.diffuseColor));
                pbr->setEmissiveFactorProperty(toXna(material.emissiveColor));
                pbr->setAlphaProperty(material.alpha);
                pbr->setMetallicFactorProperty(material.metallic);
                pbr->setRoughnessFactorProperty(material.roughness);
                // No texture-enabled flag on this one, unlike BasicEffect: a null texture is
                // how PbrEffect is told there is none.
                pbr->setTextureProperty(diffuse);
                pbr->setNormalMapProperty(resolveTexture(material.normalTexturePath, stats));
                pbr->setMetallicRoughnessMapProperty(
                    resolveTexture(material.metallicRoughnessTexturePath, stats));
                pbr->setEmissiveMapProperty(resolveTexture(material.emissiveTexturePath, stats));
                return;
            }

            if (basic == nullptr) { return; }

            basic->setDiffuseColorProperty(toXna(material.diffuseColor));
            basic->setEmissiveColorProperty(toXna(material.emissiveColor));
            basic->setSpecularColorProperty(toXna(material.specularColor));
            basic->setSpecularPowerProperty(material.specularPower);
            basic->setAlphaProperty(material.alpha);
            basic->setTextureProperty(diffuse);
            basic->setTextureEnabledProperty(diffuse != nullptr);
        }

        /**
         * @brief Sets a sprite's tint and texture, unlit.
         *
         * Unlit deliberately: a sprite's art already has its lighting painted into it, which is
         * what a 2D game *is*, and running it through the same directional rig as the models would
         * darken every sprite by an amount that depends on where the sun happens to be. The tint
         * goes in as the emissive colour so it survives lighting being off.
         */
        void applySpriteMaterial(const EditorColor& tint, XnaGraphics::Texture2D* texture)
        {
            const EditorVector3 colour{static_cast<float>(tint.r) / 255.0f,
                                       static_cast<float>(tint.g) / 255.0f,
                                       static_cast<float>(tint.b) / 255.0f};
            const float alpha = static_cast<float>(tint.a) / 255.0f;

            if (pbr != nullptr)
            {
                pbr->setLightingEnabledProperty(false);
                pbr->setDiffuseColorProperty(toXna(colour));
                pbr->setEmissiveFactorProperty(toXna(colour));
                pbr->setAlphaProperty(alpha);
                pbr->setTextureProperty(texture);
                pbr->setNormalMapProperty(nullptr);
                pbr->setMetallicRoughnessMapProperty(nullptr);
                pbr->setEmissiveMapProperty(nullptr);
                return;
            }

            if (basic == nullptr) { return; }

            basic->setLightingEnabledProperty(false);
            basic->setDiffuseColorProperty(toXna(colour));
            basic->setEmissiveColorProperty(toXna(colour));
            basic->setSpecularColorProperty(toXna(EditorVector3{0.0f, 0.0f, 0.0f}));
            basic->setAlphaProperty(alpha);
            basic->setTextureProperty(texture);
            basic->setTextureEnabledProperty(true);
        }

        /** @brief Sets the world/view/projection the effect draws @p world with. */
        void applyMatrices(const EditorMatrix& world, const EditorMatrix& view,
                           const EditorMatrix& projection)
        {
            XnaGraphics::IEffectMatrices* matrices =
                pbr != nullptr ? static_cast<XnaGraphics::IEffectMatrices*>(pbr.get())
                               : static_cast<XnaGraphics::IEffectMatrices*>(basic.get());
            if (matrices == nullptr) { return; }

            // The view and the projection go in separately, rather than the product as the
            // projection with an identity view. That shortcut works for BasicEffect and renders
            // PbrEffect's specular against an eye at the origin: it recovers the camera position
            // by *inverting the view matrix*, so an identity view is a camera claiming to be at
            // (0, 0, 0). The batch carries both forms from one camera and a test pins their
            // product, which is what keeps this from becoming two answers to one question.
            matrices->setWorldProperty(toXna(world));
            matrices->setViewProperty(toXna(view));
            matrices->setProjectionProperty(toXna(projection));
        }

        /** @brief Applies the effect's current state to the device. */
        void applyEffect()
        {
            if (pbr != nullptr) { pbr->Apply(); }
            else if (basic != nullptr) { basic->Apply(); }
        }
    };

    CnaModelPass::CnaModelPass() : impl_(std::make_unique<Impl>()) {}
    CnaModelPass::~CnaModelPass() = default;

    void CnaModelPass::initialize(XnaGraphics::GraphicsDevice& device, const AssetDatabase& assets)
    {
        impl_->device = &device;
        impl_->assets = &assets;

        try
        {
            impl_->whiteTexel = std::make_shared<XnaGraphics::Texture2D>(device, 1, 1);
            const Xna::Color white(255, 255, 255, 255);
            impl_->whiteTexel->SetData(&white, 1);
        }
        catch (const std::exception&)
        {
            impl_->whiteTexel.reset();
        }

        // PbrEffect first, because the owner chose it; BasicEffect when it cannot be had. Trying
        // and catching rather than asking is deliberate -- there is no capability flag for "this
        // backend can compile the PBR shader", and a wrong guess either way is a black viewport.
        if (kPreferPbrEffect)
        {
            try
            {
                impl_->pbr = std::make_unique<XnaGraphics::PbrEffect>(device);
                impl_->effectName = "PbrEffect";
                return;
            }
            catch (const std::exception&)
            {
                impl_->pbr.reset();
            }
        }

        try
        {
            impl_->basic = std::make_unique<XnaGraphics::BasicEffect>(device);
            impl_->effectName = "BasicEffect";
        }
        catch (const std::exception&)
        {
            impl_->basic.reset();
            impl_->effectName = "none";
        }
    }

    void CnaModelPass::shutdown()
    {
        impl_->models.clear();
        impl_->textures.clear();
        impl_->failedTextures.clear();
        impl_->whiteTexel.reset();
        impl_->pbr.reset();
        impl_->basic.reset();
        impl_->effectName = "none";
        impl_->device = nullptr;
        impl_->assets = nullptr;
    }

    void CnaModelPass::invalidateModel(const Uuid& assetId)
    {
        if (!assetId.isValid())
        {
            impl_->models.clear();
            // The textures go too. A model reimported because its file changed may name different
            // ones, and a remembered failure must not outlive the file coming back -- the same
            // rule `CnaSceneRenderer::invalidateTexture` follows and for the same reason.
            impl_->textures.clear();
            impl_->failedTextures.clear();
            return;
        }

        impl_->models.erase(assetId);
    }

    bool CnaModelPass::isReady() const
    {
        return impl_->device != nullptr && (impl_->pbr != nullptr || impl_->basic != nullptr);
    }

    const std::string& CnaModelPass::getEffectName() const { return impl_->effectName; }

    ModelPassStats CnaModelPass::renderSprites(
        const SceneSpriteBatch3D& sprites, const SceneModelBatch& batch,
        const std::function<XnaGraphics::Texture2D*(const Uuid&)>& resolveTexture)
    {
        ModelPassStats stats;
        stats.effect = impl_->effectName;

        if (!isReady() || sprites.quads.empty() || !resolveTexture) { return stats; }

        XnaGraphics::GraphicsDevice& device = *impl_->device;

        // Depth *tested* so a model in front of a sprite hides it, depth *writes off* so a
        // sprite's own transparent corners do not punch a hole later sprites cannot draw through.
        device.setDepthStencilStateProperty(XnaGraphics::DepthStencilState::DepthRead);
        device.setBlendStateProperty(XnaGraphics::BlendState::AlphaBlend);
        device.getSamplerStatesProperty()[0] = XnaGraphics::SamplerState::PointClamp;

        // Both sides. A sprite is a flat thing with no inside, and half of them would otherwise
        // vanish the moment the camera orbited past their plane -- which is the one thing a user
        // orbiting a 2D scene will certainly do.
        XnaGraphics::RasterizerState rasterizer;
        rasterizer.setCullModeProperty(XnaGraphics::CullMode::None);
        device.setRasterizerStateProperty(rasterizer);

        // One quad at a time, in the order the batch sorted them. Batching by texture would be
        // faster and would also reorder them, and back-to-front order is the whole reason a
        // transparent pass looks right.
        for (const SpriteQuad3D& quad : sprites.quads)
        {
            XnaGraphics::Texture2D* texture = resolveTexture(quad.textureId);
            if (texture == nullptr)
            {
                ++stats.missingTextures;
                continue;
            }

            std::array<XnaGraphics::VertexPositionNormalTexture, 4> vertices{};
            for (std::size_t i = 0; i < 4; ++i)
            {
                vertices[i].Position = toXna(quad.corners[i]);

                // Facing back along -Z, which is where the unrotated camera is. A sprite is lit by
                // nothing in particular -- see the emissive setting below -- so this exists to be
                // a well-defined normal rather than to be shaded by.
                vertices[i].Normal = Xna::Vector3{0.0f, 0.0f, -1.0f};
                vertices[i].TextureCoordinate =
                    Xna::Vector2{quad.texCoords[i].x, quad.texCoords[i].y};
            }

            const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};

            impl_->applyMatrices(EditorMatrix{}, batch.view, batch.projection);

            // Sprites are fogged too. A sprite that stayed crisp in a scene where the models faded
            // would look like it was floating in front of the fog rather than standing in it.
            impl_->applyFog(batch.environment);
            impl_->applySpriteMaterial(quad.tint, texture);
            impl_->applyEffect();

            // DrawUserIndexedPrimitives rather than a cached buffer per sprite: the corners move
            // whenever the entity does, so a buffer would be rewritten every frame anyway, and one
            // per entity is a GPU allocation per entity for four vertices.
            device.DrawUserIndexedPrimitives(XnaGraphics::PrimitiveType::TriangleList,
                                             vertices.data(), 0, 4, indices.data(), 0, 2);

            ++stats.spritesDrawn;
            stats.trianglesDrawn += 2;
        }

        device.setDepthStencilStateProperty(XnaGraphics::DepthStencilState::None);
        device.setBlendStateProperty(XnaGraphics::BlendState::AlphaBlend);
        return stats;
    }

    ModelPassStats CnaModelPass::render(const SceneModelBatch& batch)
    {
        ModelPassStats stats;
        stats.effect = impl_->effectName;

        if (!isReady() || batch.draws.empty()) { return stats; }

        XnaGraphics::GraphicsDevice& device = *impl_->device;

        // Depth on and opaque. The target this draws into is created with a depth buffer for
        // exactly this state to use; without it a model's own back faces punch through its front.
        device.setDepthStencilStateProperty(XnaGraphics::DepthStencilState::Default);
        device.setBlendStateProperty(XnaGraphics::BlendState::Opaque);
        device.getSamplerStatesProperty()[0] = XnaGraphics::SamplerState::LinearWrap;

        XnaGraphics::RasterizerState rasterizer;
        rasterizer.setCullModeProperty(kOutwardFaces);
        device.setRasterizerStateProperty(rasterizer);

        for (const ModelDraw& draw : batch.draws)
        {
            if (draw.mesh == nullptr) { continue; }

            Impl::GpuModel* model = impl_->resolveModel(draw.modelId, *draw.mesh, stats);
            if (model == nullptr || model->parts.empty()) { continue; }

            impl_->applyMatrices(draw.world, batch.view, batch.projection);
            impl_->applyLighting(draw.lighting);
            impl_->applyFog(batch.environment);

            for (const Impl::GpuPart& part : model->parts)
            {
                impl_->applyMaterial(*draw.mesh, part.materialIndex, stats, draw.materialOverride,
                                     part.name, draw.partMaterials);
                impl_->applyEffect();

                device.SetVertexBuffer(part.vertices.get());
                device.setIndicesProperty(part.indices.get());
                device.DrawIndexedPrimitives(XnaGraphics::PrimitiveType::TriangleList, 0, 0,
                                             part.vertexCount, 0, part.triangleCount);

                stats.trianglesDrawn += static_cast<std::size_t>(part.triangleCount);
            }

            ++stats.modelsDrawn;
        }

        // Left as the caller found it. The wireframe drawn over this is a `SpriteBatch` pass, and
        // a SpriteBatch that inherits a depth test compares against depths no sprite ever wrote --
        // which is how an overlay comes to be invisible for reasons nothing in its own code shows.
        device.setDepthStencilStateProperty(XnaGraphics::DepthStencilState::None);

        return stats;
    }
}

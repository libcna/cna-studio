// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Runtime/SceneLoader.hpp
 * @brief Turns a `.cnascene` into entities a shipped game can draw. Header-only.
 *
 * This is what a *game* includes. The editor does not use it; `cna-player` does not use it either,
 * because a player is an editor-side process that already has the whole document model. This exists
 * for the case the editor cannot help with: a built game, on a user's machine, with no editor
 * anywhere near it.
 *
 * The design and the reasoning behind it are in docs/DESIGN-SCENE-LOADER.md (ED-250 / Q-02). The
 * short version: the loader ships from this repository rather than from CNA, so that CNA never has
 * to know what a `.cnascene` is, and so that moving it into CNA later stays a relocation rather
 * than a rewrite.
 *
 * **What this is not.** Not an entity-component system, not an asset pipeline, and not a game loop.
 * It hands back a flat vector of plain structures with their world transforms already composed, and
 * a `draw()` that puts the sprites on screen. Everything else is the game's business. The moment
 * this grows an update loop or a component registry, the editor has become an engine.
 *
 * **Dependencies.** CNA's public API, and `cna-editor-core` for the JSON reader and `Uuid`. Both
 * are MS-PL. A second JSON reader was considered so the header could stand entirely alone, and
 * rejected: two implementations of one format drift, and a scene that loads in the editor but not
 * in the game is the worst failure this design can produce.
 *
 * @code
 * #include "CNA/Editor/Runtime/SceneLoader.hpp"
 *
 * namespace Runtime = CNA::Editor::Runtime;
 *
 * // Once, after the graphics device exists.
 * Runtime::SceneLoadResult level = Runtime::loadScene("Scenes/Level01.cnascene", getGraphicsDeviceProperty());
 * if (!level.succeeded) { std::cerr << level.errorMessage; }
 *
 * // Every frame.
 * spriteBatch.Begin(SpriteSortMode::BackToFront, BlendState::NonPremultiplied);
 * level.scene.draw(spriteBatch);
 * spriteBatch.End();
 * @endcode
 */

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Editor/Core/Json.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor::Runtime
{
    namespace Xna = Microsoft::Xna::Framework;
    namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

    /** @brief A sprite to draw, as authored in the editor. */
    struct SceneSprite
    {
        /** @brief Asset id of the texture. Nil when the slot was left empty. */
        Uuid textureId;

        /** @brief Sub-rectangle of the texture, or absent for the whole thing. */
        std::optional<Xna::Rectangle> sourceRectangle;

        Xna::Vector2 origin{0.0f, 0.0f};
        Xna::Color tint{255, 255, 255, 255};

        /** @brief XNA's convention: 0 is front, 1 is back. */
        float layerDepth = 0.5f;

        XnaGraphics::SpriteEffects effects = XnaGraphics::SpriteEffects::None;
    };

    /** @brief One entity from the scene, with its world transform already composed. */
    struct SceneEntity
    {
        Uuid id;
        std::string name;
        Uuid parentId;
        bool enabled = true;

        /** @brief World-space transform, with every ancestor applied. */
        Xna::Vector3 position{0.0f, 0.0f, 0.0f};
        Xna::Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
        Xna::Vector3 scale{1.0f, 1.0f, 1.0f};

        /** @brief The sprite to draw, when the entity has a SpriteRenderer. */
        std::optional<SceneSprite> sprite;

        /**
         * @brief Every component's raw properties, keyed by component type id.
         *
         * Components the loader does not interpret are carried rather than discarded, so a game can
         * read its own component types out of a scene the editor happily round-trips. The loader
         * acts only on `CNA.Transform` and `CNA.SpriteRenderer`; everything else is data.
         */
        std::map<std::string, JsonValue> components;

        /** @brief Returns the Z-axis rotation in radians, which is what SpriteBatch takes. */
        [[nodiscard]] float getSpriteRotation() const
        {
            return std::atan2(2.0f * (rotation.W * rotation.Z + rotation.X * rotation.Y),
                              1.0f - 2.0f * (rotation.Y * rotation.Y + rotation.Z * rotation.Z));
        }
    };

    struct SceneLoadResult;

    inline SceneLoadResult loadSceneFromJson(const JsonValue& document,
                                             XnaGraphics::GraphicsDevice& device,
                                             const std::string& assetRoot);

    /**
     * @brief A loaded scene: its entities, and the textures they reference.
     *
     * Owns its textures, so it must outlive any drawing that uses them. Move-only for the same
     * reason -- copying would duplicate GPU resources nobody asked for.
     */
    class LoadedScene
    {
    public:
        LoadedScene() = default;

        LoadedScene(const LoadedScene&) = delete;
        LoadedScene& operator=(const LoadedScene&) = delete;
        LoadedScene(LoadedScene&&) noexcept = default;
        LoadedScene& operator=(LoadedScene&&) noexcept = default;

        [[nodiscard]] const std::string& getName() const { return name_; }
        [[nodiscard]] const std::vector<SceneEntity>& getEntities() const { return entities_; }

        /** @brief Returns the entity with @p id, or nullptr. */
        [[nodiscard]] const SceneEntity* findEntity(const Uuid& id) const
        {
            const auto found = std::find_if(entities_.begin(), entities_.end(),
                                            [&](const SceneEntity& entity) { return entity.id == id; });
            return found == entities_.end() ? nullptr : &*found;
        }

        /** @brief Returns the first entity named @p name, or nullptr. */
        [[nodiscard]] const SceneEntity* findEntity(std::string_view name) const
        {
            const auto found = std::find_if(entities_.begin(), entities_.end(),
                                            [&](const SceneEntity& entity) { return entity.name == name; });
            return found == entities_.end() ? nullptr : &*found;
        }

        /** @brief Returns the texture for @p assetId, or nullptr when it is not loaded. */
        [[nodiscard]] const XnaGraphics::Texture2D* findTexture(const Uuid& assetId) const
        {
            const auto found = textures_.find(assetId);
            return found == textures_.end() ? nullptr : found->second.get();
        }

        /** @brief Returns how many texture assets failed to load. */
        [[nodiscard]] std::size_t getMissingTextureCount() const { return missingTextures_; }

        /**
         * @brief Draws every enabled sprite into @p spriteBatch.
         *
         * The batch must already be in a Begin/End pair. `SpriteSortMode::BackToFront` is what the
         * editor's own viewport uses, and is what makes `layerDepth` mean what the inspector said
         * it meant; any other sort mode will draw the same sprites in a different order.
         *
         * @param offset Added to every position, so a scene can be drawn somewhere other than at
         *        the origin without editing it.
         * @return The number of sprites drawn.
         */
        std::size_t draw(XnaGraphics::SpriteBatch& spriteBatch,
                         const Xna::Vector2& offset = Xna::Vector2{0.0f, 0.0f}) const
        {
            std::size_t drawn = 0;

            for (const SceneEntity& entity : entities_)
            {
                if (!entity.enabled || !entity.sprite) { continue; }

                const XnaGraphics::Texture2D* texture = findTexture(entity.sprite->textureId);
                if (texture == nullptr) { continue; }

                spriteBatch.Draw(*texture,
                                 Xna::Vector2{entity.position.X + offset.X, entity.position.Y + offset.Y},
                                 entity.sprite->sourceRectangle,
                                 entity.sprite->tint,
                                 entity.getSpriteRotation(),
                                 entity.sprite->origin,
                                 Xna::Vector2{entity.scale.X, entity.scale.Y},
                                 entity.sprite->effects,
                                 entity.sprite->layerDepth);
                ++drawn;
            }

            return drawn;
        }

    private:
        friend SceneLoadResult loadSceneFromJson(const JsonValue&, XnaGraphics::GraphicsDevice&,
                                                 const std::string&);

        std::string name_;
        std::vector<SceneEntity> entities_;
        std::unordered_map<Uuid, std::unique_ptr<XnaGraphics::Texture2D>> textures_;
        std::size_t missingTextures_ = 0;
    };

    /** @brief What loadScene() produced, and why it failed if it did. */
    struct SceneLoadResult
    {
        bool succeeded = false;
        std::string errorMessage;

        /** @brief Warnings that did not stop the load, such as a texture that would not open. */
        std::vector<std::string> warnings;

        LoadedScene scene;
    };

    /** @brief The format version this header understands. Matches the editor's writer. */
    inline constexpr int kSceneFormatVersion = 1;

    namespace Detail
    {
        inline Xna::Vector3 readVector3(const JsonValue& json, const Xna::Vector3& fallback)
        {
            const JsonValue::Array& values = json.getElements();
            if (values.size() < 3) { return fallback; }
            return Xna::Vector3{values[0].asFloat(), values[1].asFloat(), values[2].asFloat()};
        }

        inline Xna::Vector2 readVector2(const JsonValue& json, const Xna::Vector2& fallback)
        {
            const JsonValue::Array& values = json.getElements();
            if (values.size() < 2) { return fallback; }
            return Xna::Vector2{values[0].asFloat(), values[1].asFloat()};
        }

        inline Xna::Quaternion readQuaternion(const JsonValue& json)
        {
            const JsonValue::Array& values = json.getElements();
            if (values.size() < 4) { return Xna::Quaternion{0.0f, 0.0f, 0.0f, 1.0f}; }
            return Xna::Quaternion{values[0].asFloat(), values[1].asFloat(), values[2].asFloat(),
                                   values[3].asFloat()};
        }

        inline Xna::Color readColor(const JsonValue& json)
        {
            const JsonValue::Array& values = json.getElements();
            if (values.size() < 4) { return Xna::Color(255, 255, 255, 255); }
            return Xna::Color(values[0].asInt(255), values[1].asInt(255), values[2].asInt(255),
                              values[3].asInt(255));
        }

        /**
         * @brief Returns the directory a scene's assets are resolved against.
         *
         * A project puts its scenes in `Scenes/` and its assets in `Assets/`, both directly under
         * the project root, so the scene's grandparent is the root. A scene somewhere else needs
         * the root passed explicitly, which is why loadScene() takes it.
         */
        inline std::string defaultAssetRoot(const std::string& scenePath)
        {
            const std::filesystem::path path{scenePath};
            const std::filesystem::path parent = path.parent_path();
            return parent.empty() ? std::string{"."} : parent.parent_path().generic_string();
        }

        /**
         * @brief Maps every asset id under @p assetRoot to the file it names.
         *
         * Built once per load by reading the `.cnaasset` sidecars, which is the same index the
         * editor keeps in memory. Doing it per texture instead would walk the tree once per sprite.
         */
        inline std::unordered_map<Uuid, std::string> indexAssets(const std::string& assetRoot)
        {
            std::unordered_map<Uuid, std::string> index;

            std::error_code errorCode;
            const std::filesystem::path root{assetRoot.empty() ? std::string{"."} : assetRoot};
            if (!std::filesystem::exists(root, errorCode)) { return index; }

            constexpr std::string_view kSidecarExtension = ".cnaasset";

            std::filesystem::recursive_directory_iterator iterator{
                root, std::filesystem::directory_options::skip_permission_denied, errorCode};
            if (errorCode) { return index; }

            for (const std::filesystem::directory_entry& entry : iterator)
            {
                if (!entry.is_regular_file(errorCode)) { errorCode.clear(); continue; }

                const std::string path = entry.path().generic_string();
                if (path.size() <= kSidecarExtension.size()) { continue; }
                if (path.compare(path.size() - kSidecarExtension.size(), kSidecarExtension.size(),
                                 kSidecarExtension) != 0)
                {
                    continue;
                }

                std::ifstream stream{entry.path(), std::ios::binary};
                if (!stream) { continue; }

                std::ostringstream contents;
                contents << stream.rdbuf();

                const JsonParseResult parsed = Json::parse(contents.str());
                if (!parsed.succeeded) { continue; }

                const Uuid id = Uuid::parse(parsed.value["id"].asString());
                if (!id.isValid()) { continue; }

                // The sidecar sits beside the file it describes, named after it, so the source path
                // is the sidecar's own path with the extension taken off. That is one fewer thing
                // to keep in sync than storing the path inside the sidecar.
                index.emplace(id, path.substr(0, path.size() - kSidecarExtension.size()));
            }

            return index;
        }

        inline XnaGraphics::SpriteEffects readEffects(const std::string& name)
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

        /** @brief Rotates @p vector by @p rotation. Mirrors SceneTransform.hpp's `rotate`. */
        inline Xna::Vector3 rotate(const Xna::Quaternion& rotation, const Xna::Vector3& vector)
        {
            const float x = rotation.X;
            const float y = rotation.Y;
            const float z = rotation.Z;
            const float w = rotation.W;

            const float tx = 2.0f * (y * vector.Z - z * vector.Y);
            const float ty = 2.0f * (z * vector.X - x * vector.Z);
            const float tz = 2.0f * (x * vector.Y - y * vector.X);

            return Xna::Vector3{vector.X + w * tx + (y * tz - z * ty),
                                vector.Y + w * ty + (z * tx - x * tz),
                                vector.Z + w * tz + (x * ty - y * tx)};
        }

        /** @brief Multiplies two rotations: @p a followed by @p b. Mirrors SceneTransform.hpp. */
        inline Xna::Quaternion multiply(const Xna::Quaternion& a, const Xna::Quaternion& b)
        {
            return Xna::Quaternion{a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
                                   a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
                                   a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
                                   a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z};
        }
    }

    /**
     * @brief Loads a scene from an already-parsed document.
     *
     * Separated from loadScene() so a game that has its scenes packed into an archive can hand over
     * the JSON without first writing it to a temporary file.
     *
     * @param assetRoot Directory that texture paths are resolved against.
     */
    inline SceneLoadResult loadSceneFromJson(const JsonValue& document,
                                             XnaGraphics::GraphicsDevice& device,
                                             const std::string& assetRoot)
    {
        SceneLoadResult result;

        const int version = document["formatVersion"].asInt(0);
        if (version > kSceneFormatVersion)
        {
            // Refused rather than guessed at. A newer file may use fields whose absence would be
            // silently wrong -- a scene that half-loads is worse than one that does not.
            result.errorMessage = "scene format version " + std::to_string(version)
                                + " is newer than this build understands ("
                                + std::to_string(kSceneFormatVersion) + ")";
            return result;
        }

        const JsonValue& entities = document["entities"];
        if (!entities.isArray())
        {
            result.errorMessage = "scene has no 'entities' array";
            return result;
        }

        result.scene.name_ = document["name"].asString();

        // Pass one: read every entity's *local* transform, keyed by id.
        struct Local
        {
            Xna::Vector3 position{0.0f, 0.0f, 0.0f};
            Xna::Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
            Xna::Vector3 scale{1.0f, 1.0f, 1.0f};
        };
        std::unordered_map<Uuid, Local> locals;
        std::unordered_map<Uuid, Uuid> parents;

        for (const JsonValue& source : entities.getElements())
        {
            SceneEntity entity;
            entity.id = Uuid::parse(source["id"].asString());
            entity.name = source["name"].asString();
            entity.parentId = Uuid::parse(source["parent"].asString());
            entity.enabled = source["enabled"].asBoolean(true);

            const JsonValue& components = source["components"];
            for (const auto& [typeId, properties] : components.getMembers())
            {
                entity.components.emplace(typeId, properties);
            }

            Local local;
            if (const JsonValue& transform = components["CNA.Transform"]; !transform.isNull())
            {
                local.position = Detail::readVector3(transform["position"], local.position);
                local.rotation = Detail::readQuaternion(transform["rotation"]);
                local.scale = Detail::readVector3(transform["scale"], local.scale);
            }

            if (const JsonValue& sprite = components["CNA.SpriteRenderer"]; !sprite.isNull())
            {
                SceneSprite drawn;
                drawn.textureId = Uuid::parse(sprite["texture"].asString());
                drawn.origin = Detail::readVector2(sprite["origin"], drawn.origin);
                drawn.tint = Detail::readColor(sprite["tint"]);
                drawn.layerDepth = static_cast<float>(sprite["layerDepth"].asNumber(0.5));
                drawn.effects = Detail::readEffects(sprite["spriteEffects"].asString());

                // An all-zero source rectangle means "the whole texture", which is how the editor
                // writes an unset one.
                const JsonValue::Array& region = sprite["sourceRectangle"].getElements();
                if (region.size() >= 4 && region[2].asInt(0) > 0 && region[3].asInt(0) > 0)
                {
                    drawn.sourceRectangle = Xna::Rectangle{region[0].asInt(0), region[1].asInt(0),
                                                           region[2].asInt(0), region[3].asInt(0)};
                }

                entity.sprite = drawn;
            }

            if (entity.id.isValid())
            {
                locals.emplace(entity.id, local);
                parents.emplace(entity.id, entity.parentId);
            }

            result.scene.entities_.push_back(std::move(entity));
        }

        // Pass two: compose each entity's world transform by walking to the root. Composed exactly
        // the way SceneTransform.hpp does it, because a game whose sprites sit somewhere other than
        // where the editor drew them is a bug nobody sees until they compare two screenshots.
        for (SceneEntity& entity : result.scene.entities_)
        {
            std::vector<Uuid> chain;
            Uuid current = entity.id;
            for (std::size_t step = 0; step <= locals.size(); ++step)
            {
                if (!current.isValid() || locals.find(current) == locals.end()) { break; }
                chain.push_back(current);

                const auto parent = parents.find(current);
                if (parent == parents.end()) { break; }
                current = parent->second;
            }

            Xna::Vector3 position{0.0f, 0.0f, 0.0f};
            Xna::Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
            Xna::Vector3 scale{1.0f, 1.0f, 1.0f};

            for (auto step = chain.rbegin(); step != chain.rend(); ++step)
            {
                const Local& local = locals.at(*step);

                const Xna::Vector3 scaled{local.position.X * scale.X, local.position.Y * scale.Y,
                                          local.position.Z * scale.Z};
                const Xna::Vector3 offset = Detail::rotate(rotation, scaled);

                position = Xna::Vector3{position.X + offset.X, position.Y + offset.Y,
                                        position.Z + offset.Z};
                rotation = Detail::multiply(rotation, local.rotation);
                scale = Xna::Vector3{scale.X * local.scale.X, scale.Y * local.scale.Y,
                                     scale.Z * local.scale.Z};
            }

            entity.position = position;
            entity.rotation = rotation;
            entity.scale = scale;
        }

        // Pass three: load each referenced texture once. A texture that will not open is a warning
        // rather than a failure -- a game that refuses to start because one sprite is missing is
        // less useful than one that starts with a hole in it and says so.
        const std::unordered_map<Uuid, std::string> assets = Detail::indexAssets(assetRoot);

        for (const SceneEntity& entity : result.scene.entities_)
        {
            if (!entity.sprite || !entity.sprite->textureId.isValid()) { continue; }
            if (result.scene.textures_.count(entity.sprite->textureId) > 0) { continue; }

            const auto found = assets.find(entity.sprite->textureId);
            if (found == assets.end())
            {
                ++result.scene.missingTextures_;
                result.warnings.push_back("no asset found for texture "
                                          + entity.sprite->textureId.toString());
                continue;
            }

            try
            {
                result.scene.textures_.emplace(
                    entity.sprite->textureId,
                    std::make_unique<XnaGraphics::Texture2D>(found->second, device));
            }
            catch (const std::exception& error)
            {
                ++result.scene.missingTextures_;
                result.warnings.push_back("cannot load '" + found->second + "': " + error.what());
            }
        }

        result.succeeded = true;
        return result;
    }

    /**
     * @brief Loads a scene from a `.cnascene` file.
     *
     * @param scenePath Path to the scene file.
     * @param device The graphics device textures are created on.
     * @param assetRoot Directory the asset sidecars are searched under. Defaults to the scene's
     *        own parent directory's parent, which is where a project's `Assets` folder sits
     *        relative to its `Scenes` folder.
     */
    inline SceneLoadResult loadScene(const std::string& scenePath,
                                     XnaGraphics::GraphicsDevice& device,
                                     const std::string& assetRoot = {})
    {
        SceneLoadResult result;

        std::ifstream stream{scenePath, std::ios::binary};
        if (!stream)
        {
            result.errorMessage = "cannot open '" + scenePath + "'";
            return result;
        }

        std::ostringstream contents;
        contents << stream.rdbuf();

        const JsonParseResult parsed = Json::parse(contents.str());
        if (!parsed.succeeded)
        {
            result.errorMessage = "'" + scenePath + "': " + parsed.errorMessage;
            return result;
        }

        return loadSceneFromJson(parsed.value, device,
                                 assetRoot.empty() ? Detail::defaultAssetRoot(scenePath) : assetRoot);
    }
}

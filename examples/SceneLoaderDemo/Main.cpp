// SPDX-License-Identifier: MS-PL
/**
 * @file Main.cpp
 * @brief What a shipped game does with a scene the editor produced (plan.md ED-250).
 *
 * Doubles as the loader's integration test. It is not a graphical demo -- it opens a real graphics
 * device, loads `examples/HelloSprites`, checks the entities came back with the transforms the
 * editor drew them at, draws one frame, and reports. Run with `--verbose` to see the entity list.
 *
 * The point of keeping this in the repository rather than in a test file is that it is also the
 * documentation: a reader who wants to know how to use the loader should find a program that
 * compiles, not a snippet in a comment.
 */

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"

#include "CNA/Editor/Runtime/SceneLoader.hpp"

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;
namespace Runtime = CNA::Editor::Runtime;

namespace
{
    bool nearlyEqual(float a, float b, float tolerance = 0.01f)
    {
        return std::fabs(a - b) <= tolerance;
    }

    /** @brief Checks the loaded scene against what the editor's own transform code produces. */
    int verify(const Runtime::LoadedScene& scene, bool verbose)
    {
        int failures = 0;
        const auto check = [&](bool condition, const std::string& what) {
            if (condition) { return; }
            std::cerr << "FAIL: " << what << "\n";
            ++failures;
        };

        check(scene.getName() == "Level01", "scene name is Level01");

        // Five: a camera, two sprites, the Crate that ED-405 added so the 3D view has a real model
        // in it, and the Key Light that ED-404 added so it has something lighting that model. The
        // count is asserted rather than ignored because this demo exists to prove a shipped game
        // reads the same scene the editor wrote -- an entity the loader silently dropped is exactly
        // the failure it is here to catch, and a count that was allowed to drift would catch none.
        check(scene.getEntities().size() == 5, "five entities");

        // A light carries no sprite and no model, which makes it the third shape of entity this
        // loader has to survive. It is here because a loader that assumed every entity draws
        // something would have failed on it in a game rather than in a test.
        const Runtime::SceneEntity* light = scene.findEntity(std::string_view{"Key Light"});
        check(light != nullptr, "Key Light is present");
        if (light != nullptr) { check(!light->sprite.has_value(), "Key Light has no sprite"); }

        // A model renderer carries no sprite, so a loader that assumed every entity had one would
        // fail here rather than in a game.
        const Runtime::SceneEntity* crate = scene.findEntity(std::string_view{"Crate"});
        check(crate != nullptr, "Crate is present");
        if (crate != nullptr)
        {
            check(nearlyEqual(crate->position.X, 400.0f), "Crate world X is 400");
            check(nearlyEqual(crate->position.Y, 340.0f), "Crate world Y is 340");
            check(!crate->sprite.has_value(), "Crate has no sprite");
        }

        const Runtime::SceneEntity* player = scene.findEntity(std::string_view{"Player"});
        check(player != nullptr, "Player is present");
        if (player != nullptr)
        {
            check(nearlyEqual(player->position.X, 100.0f), "Player world X is 100");
            check(nearlyEqual(player->position.Y, 220.0f), "Player world Y is 220");
            check(player->sprite.has_value(), "Player has a sprite");
        }

        // The child's world position is the parent's plus its own offset scaled by the parent --
        // 100 + 40 and 220 + 8, since the parent is unscaled. Getting this wrong is the bug that
        // makes a shipped game's sprites sit somewhere other than where the editor drew them.
        const Runtime::SceneEntity* weapon = scene.findEntity(std::string_view{"Weapon"});
        check(weapon != nullptr, "Weapon is present");
        if (weapon != nullptr)
        {
            check(nearlyEqual(weapon->position.X, 140.0f), "Weapon world X is 140");
            check(nearlyEqual(weapon->position.Y, 228.0f), "Weapon world Y is 228");
            check(nearlyEqual(weapon->scale.X, 1.5f), "Weapon world scale is 1.5");
            check(weapon->parentId == player->id, "Weapon's parent is Player");
        }

        // A camera is carried but not interpreted beyond being data the game can read.
        const Runtime::SceneEntity* camera = scene.findEntity(std::string_view{"Main Camera"});
        check(camera != nullptr, "Main Camera is present");
        if (camera != nullptr)
        {
            check(!camera->sprite.has_value(), "Main Camera has no sprite");
            check(camera->components.count("CNA.Camera") == 1, "Main Camera carries its component");
        }

        check(scene.getMissingTextureCount() == 0, "every texture loaded");

        if (verbose)
        {
            for (const Runtime::SceneEntity& entity : scene.getEntities())
            {
                std::cout << "  " << entity.name << " at (" << entity.position.X << ", "
                          << entity.position.Y << ") scale " << entity.scale.X
                          << (entity.sprite ? "  [sprite]" : "") << "\n";
            }
        }

        return failures;
    }

    /**
     * @brief A minimal game that loads a scene and draws it once.
     *
     * This is the whole of what the loader asks of a game: a device, a SpriteBatch, and one call.
     */
    class DemoGame final : public Xna::Game
    {
    public:
        DemoGame(std::string scenePath, std::string assetRoot, bool verbose)
            : scenePath_(std::move(scenePath)), assetRoot_(std::move(assetRoot)), verbose_(verbose)
        {
            graphics_ = std::make_unique<Xna::GraphicsDeviceManager>(this);
        }

        [[nodiscard]] int getFailureCount() const { return failures_; }

    protected:
        void LoadContent() override
        {
            spriteBatch_ = std::make_unique<XnaGraphics::SpriteBatch>(getGraphicsDeviceProperty());

            Runtime::SceneLoadResult loaded =
                Runtime::loadScene(scenePath_, getGraphicsDeviceProperty(), assetRoot_);

            for (const std::string& warning : loaded.warnings)
            {
                std::cerr << "warning: " << warning << "\n";
            }

            if (!loaded.succeeded)
            {
                std::cerr << "FAIL: " << loaded.errorMessage << "\n";
                ++failures_;
                Exit();
                return;
            }

            failures_ += verify(loaded.scene, verbose_);
            scene_ = std::move(loaded.scene);
        }

        void Draw(const Xna::GameTime& gameTime) override
        {
            (void)gameTime;
            getGraphicsDeviceProperty().Clear(Xna::Color(30, 30, 40, 255));

            // BackToFront is what makes layerDepth mean what the inspector said it meant.
            spriteBatch_->Begin(XnaGraphics::SpriteSortMode::BackToFront,
                                XnaGraphics::BlendState::NonPremultiplied);
            const std::size_t drawn = scene_.draw(*spriteBatch_);
            spriteBatch_->End();

            if (drawn != 2)
            {
                std::cerr << "FAIL: expected two sprites drawn, got " << drawn << "\n";
                ++failures_;
            }
            else if (verbose_)
            {
                std::cout << "  drew " << drawn << " sprites\n";
            }

            Exit();
        }

    private:
        std::string scenePath_;
        std::string assetRoot_;
        bool verbose_ = false;
        int failures_ = 0;

        std::unique_ptr<Xna::GraphicsDeviceManager> graphics_;
        std::unique_ptr<XnaGraphics::SpriteBatch> spriteBatch_;
        Runtime::LoadedScene scene_;
    };
}

int main(int argc, char** argv)
{
    std::string scenePath = "examples/HelloSprites/Scenes/Level01.cnascene";
    std::string assetRoot;
    bool verbose = false;

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument{argv[index] != nullptr ? argv[index] : ""};
        if (argument == "--verbose") { verbose = true; }
        else if (argument.rfind("--scene=", 0) == 0) { scenePath = argument.substr(8); }
        else if (argument.rfind("--assets=", 0) == 0) { assetRoot = argument.substr(9); }
    }

    try
    {
        DemoGame game{scenePath, assetRoot, verbose};
        game.Run();

        if (game.getFailureCount() > 0)
        {
            std::cerr << "scene-loader-demo: " << game.getFailureCount() << " check(s) failed\n";
            return 1;
        }

        std::cout << "scene-loader-demo: scene loaded, verified and drawn\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "scene-loader-demo: " << error.what() << "\n";
        return 2;
    }
}

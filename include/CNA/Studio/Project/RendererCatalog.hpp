// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/RendererCatalog.hpp
 * @brief Studio's classification of CNA's renderers and platform implementations.
 *
 * `plan.md` STUDIO-02030, STUDIO-02031, STUDIO-02041, STUDIO-29001, STUDIO-29002.
 *
 * ### Renderer and platform are separate axes
 *
 * Current CNA splits them, and so does this: `CNA_GRAPHICS_RENDERER` chooses how pixels are
 * produced, `CNA_PLATFORM` chooses where the window, events and input come from. The prototype
 * modelled one flat "backend" concept, which cannot express "SDL3 windowing with a Vulkan
 * renderer" and cannot express that the headless platform provides no window handle for a GPU
 * renderer to draw into.
 *
 * ### Studio host support is a stricter question than "does it run a game"
 *
 * Two independent things (see `docs/ARCHITECTURE.md` §3):
 *
 * - the **Studio host renderer** draws CNA Studio's own UI and 3D viewport;
 * - the **game target renderer** draws the user's game, in the player or the packaged build.
 *
 * A renderer that cannot host Studio must still be offered as a game target. The classification
 * below answers only the first question; it must never be used to restrict the second.
 *
 * ### Why this table exists at all, and what replaces it
 *
 * It is a **static classification**, and static classification is not the goal: the goal is
 * capability-driven eligibility, asking the live device through
 * `GraphicsDevice::GetRendererCapabilityProfileEXT()` (`STUDIO-02020`). That answer is only
 * available once a device exists, and Studio needs to answer "which renderers could host me" in a
 * build dialog, in `--list-renderers`, and on a machine that has none of them installed. So this
 * table is the *offline* answer, and it is deliberately the only place in Studio that holds one --
 * `STUDIO-02030`'s guard test fails when CNA registers an identity this table does not classify.
 */

#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief Whether a renderer can host the Studio UI, only preview a game, or only ship one. */
    enum class RendererHostSupport
    {
        /**
         * @brief Can host CNA Studio itself.
         *
         * Requires the modern capability set Studio's UI and 3D viewport need: a real 3D pipeline,
         * render targets, texture sampling, configurable blending, scissor clipping, depth/stencil
         * and dynamic GPU buffers. Being able to draw a triangle is not the bar.
         */
        StudioHost,

        /**
         * @brief Fine for a game, not for Studio's own UI.
         *
         * Either 2D-only -- so it cannot draw the 3D viewport at all -- or from a feature era
         * below what the Studio UI needs, or too slow to be interactive. All of these remain
         * perfectly good targets for Play, Build and Package.
         */
        PreviewOnly,

        /** @brief Ships games only: no window, or no interactive presentation by design. */
        RuntimeOnly
    };

    /** @brief One renderer CNA can be built with. */
    struct RendererInfo
    {
        /** @brief The value CNA's `CNA_GRAPHICS_RENDERER` takes, e.g. `"VULKAN"`. */
        std::string_view cnaIdentity;
        /** @brief Lower-case name used on the command line and in `.cnaproject`. */
        std::string_view commandLineName;
        /** @brief Name shown to a user. */
        std::string_view displayName;
        /** @brief Whether this renderer can host Studio. */
        RendererHostSupport hostSupport = RendererHostSupport::RuntimeOnly;
        /** @brief Why the classification is what it is; shown in the renderer configuration UI. */
        std::string_view note;
    };

    /** @brief Whether a platform implementation exists in CNA today. */
    enum class PlatformStatus
    {
        /** @brief Built and selectable. */
        Implemented,
        /**
         * @brief Recognised by CNA's build but not implemented; selecting it is a hard error.
         *
         * Studio lists these so that a user who asks for one is told it does not exist yet,
         * rather than being silently given the default.
         */
        Reserved
    };

    /** @brief One platform implementation CNA can be built with. */
    struct PlatformInfo
    {
        /** @brief The value CNA's `CNA_PLATFORM` takes, e.g. `"SDL3"`. */
        std::string_view cnaIdentity;
        /** @brief Lower-case name used on the command line and in `.cnaproject`. */
        std::string_view commandLineName;
        /** @brief Name shown to a user. */
        std::string_view displayName;
        /** @brief Whether CNA implements it. */
        PlatformStatus status = PlatformStatus::Reserved;
        /** @brief Whether it can provide the window a Studio host renderer needs. */
        bool canHostStudio = false;
        /** @brief Context for a user choosing a target. */
        std::string_view note;
    };

    /**
     * @brief A renderer name that existed in an older CNA and no longer does.
     *
     * These are real: CNA's renderer registry was reorganised, and names the CNA Editor prototype
     * wrote into `.cnaproject` files no longer identify anything. `"EASYGL"` is the clearest case
     * -- it became a *family* serving five GL profiles rather than a renderer identity of its own,
     * so a project that names it is naming something that cannot be built.
     *
     * A project naming one is migrated to @ref replacement and **told so**. Silently substituting
     * would change which renderer a user's game ships on without telling them, and silently
     * failing would make an old project look corrupt.
     */
    struct RendererAlias
    {
        /** @brief The name as older projects spell it. */
        std::string_view legacyName;
        /** @brief The current identity to use instead; empty when the renderer is simply gone. */
        std::string_view replacement;
        /** @brief What happened, in a sentence a user can act on. */
        std::string_view reason;
    };

    /**
     * @brief Returns every legacy renderer name Studio can migrate.
     * @return The alias table.
     */
    [[nodiscard]] const std::vector<RendererAlias>& getLegacyRendererAliases();

    /**
     * @brief Finds a legacy renderer name, case-insensitively.
     * @param name Name as an older project spells it.
     * @return The alias, or nullptr when the name was never a CNA renderer.
     */
    [[nodiscard]] const RendererAlias* findLegacyRendererAlias(std::string_view name);

    /**
     * @brief Returns every renderer identity Studio knows how to classify.
     * @return The renderer catalogue, in CNA's own declaration order.
     */
    [[nodiscard]] const std::vector<RendererInfo>& getKnownRenderers();

    /**
     * @brief Finds a renderer by its CNA identity or its command-line name, case-insensitively.
     * @param name Identity or command-line name.
     * @return The entry, or nullptr when Studio does not know it.
     */
    [[nodiscard]] const RendererInfo* findRenderer(std::string_view name);

    /**
     * @brief Returns every platform implementation Studio knows how to classify.
     * @return The platform catalogue.
     */
    [[nodiscard]] const std::vector<PlatformInfo>& getKnownPlatforms();

    /**
     * @brief Finds a platform by its CNA identity or command-line name, case-insensitively.
     * @param name Identity or command-line name.
     * @return The entry, or nullptr when Studio does not know it.
     */
    [[nodiscard]] const PlatformInfo* findPlatform(std::string_view name);

    /**
     * @brief The CNA renderer identities this catalogue was audited against.
     *
     * The snapshot `STUDIO-02030`'s guard test compares the catalogue with. When Studio is built
     * against a real CNA checkout, the same test additionally compares against CNA's live
     * inventory, which is what actually catches a renderer added upstream.
     *
     * @return The audited identity list.
     */
    [[nodiscard]] const std::vector<std::string_view>& getAuditedCnaRendererIdentities();

    /** @brief The CNA commit `getAuditedCnaRendererIdentities()` was read from. */
    [[nodiscard]] std::string_view getAuditedCnaCommit();

    /** @brief The CNA branch the audit was taken from. */
    [[nodiscard]] std::string_view getAuditedCnaBranch();
} // namespace CNA::Studio

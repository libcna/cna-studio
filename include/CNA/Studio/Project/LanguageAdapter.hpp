// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/LanguageAdapter.hpp
 * @brief The seam between the Studio model and the language a project is written in.
 *
 * `plan.md` STUDIO-02080 … STUDIO-02087. The decision and the alternatives that were rejected are
 * in `docs/ARCHITECTURE.md` §13.
 *
 * ### What this exists to prevent
 *
 * CNA is a framework with several language bindings. CNA Studio is C++-first and, for a long time
 * yet, C++-only: the C++ workflow is the one that has to be excellent, and nothing here is a
 * promise that a second one is coming soon. But *C++-first* and *C++ welded into the Studio model*
 * are different architectures, and only the first is recoverable. The failure this seam is built
 * against is the one that arrives by accident, a call at a time:
 *
 * ```
 * if (language == Cpp) { ... } else if (language == ...) { ... }
 * ```
 *
 * spread through the Project Hub, the Build panel, the export command and the packaging workflow —
 * none of which has any business knowing what a compiler is. By the time a second binding is
 * actually wanted, that shape is not refactorable; it is a rewrite of every panel that grew one.
 *
 * ### What is deliberately *not* here
 *
 * Not a plugin framework, not a registration macro, not a discovery protocol, not a speculative
 * adapter for a binding whose contract nobody has read. One interface, one implementation, and a
 * registry that is an ordinary object somebody constructs. Every method below exists because a
 * concrete caller in this repository needed it — the boundaries are the ones already in the code,
 * not ones imagined for a future language.
 *
 * ### The invariant it has to keep
 *
 * > CNA is the framework. CNA Studio is the authoring environment. A project authored by Studio
 * > remains a normal project for CNA or one of its supported bindings, and builds and runs without
 * > CNA Studio.
 *
 * An adapter therefore *drives* its language's native build system and never replaces it. The C++
 * adapter runs the project's own CMake, exactly as Studio always has.
 */

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/Studio/Project/BuildRunner.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/ProjectPackaging.hpp"

namespace CNA::Studio
{
    /**
     * @brief A language's identity, and the facts generic Studio code is allowed to know about it.
     *
     * Everything here is either shown to a user or used to *locate* a file. Nothing here says how
     * anything is built, which is the whole point: a panel that can read this struct still cannot
     * learn what a compiler is from it.
     */
    struct StudioLanguageDescriptor
    {
        /**
         * @brief Stable id, written into `.cnaproject` and never translated.
         *
         * Lower case and ASCII, e.g. `"cpp"`. A project file is a thing people commit, diff and
         * read on another machine, so this is an identifier rather than a display name.
         */
        std::string id;

        /** @brief What a user is shown, e.g. `"C++"`. */
        std::string displayName;

        /** @brief The build system this language drives, e.g. `"CMake"`. Shown, never branched on. */
        std::string toolchainName;

        /**
         * @brief Project-relative directory holding hand-written source.
         *
         * Named by the adapter because it is a convention of the language's ecosystem rather than
         * of Studio, and because the rule that hand-written code is never overwritten needs a
         * subtree to be true *of*.
         */
        std::string sourceDirectory;

        /**
         * @brief Project-relative directory holding generated source, and nothing else.
         *
         * Separate from @ref sourceDirectory on purpose. Generation that writes into the same tree
         * as hand-written code is generation that eventually overwrites some of it, and no amount
         * of care at each call site fixes that; a directory the tool owns outright does.
         */
        std::string generatedDirectory;

        /** @brief Extensions of files a user edits, with the dot, e.g. `{".cpp", ".hpp"}`. */
        std::vector<std::string> sourceFileExtensions;
    };

    /** @brief One file an adapter writes into a project it is creating. */
    struct StudioProjectFile
    {
        /** @brief Path relative to the project root, forward slashes. */
        std::string pathInProject;

        /** @brief The file's contents. */
        std::string contents;
    };

    /**
     * @brief What a language is told about a project it is being asked to scaffold.
     *
     * Deliberately the *project's* facts and no template's: a template contributes content — scenes,
     * assets, a starting world — and the adapter contributes the things that make the directory a
     * buildable project in its language. Mixing the two would make every template carry a copy of
     * one language's build file, which is exactly how a template stops being data.
     */
    struct StudioProjectScaffold
    {
        /** @brief The project's name, as the user typed it. */
        std::string projectName;

        /** @brief Absolute path of the directory the project is created in. */
        std::string rootPath;

        /** @brief Which Studio features the project opts into. */
        ProjectKind kind = ProjectKind::CnaNative;

        /** @brief Project-relative path of the scene the game starts on. May be empty. */
        std::string startupScene;

        /** @brief Project-relative directory holding scenes. */
        std::string sceneDirectory = "Scenes";

        /** @brief Project-relative directory holding assets. */
        std::string assetDirectory = "Assets";

        /** @brief The renderer identity the project's first target profile names, e.g. `"OPENGLES3"`. */
        std::string renderer;
    };

    /**
     * @brief Whether a language's toolchain is usable on this machine, and what to say when not.
     *
     * Asked *before* anything is offered rather than after something fails. Somebody who installed
     * an authoring environment and not a compiler is the common case, and the failure they would
     * otherwise get is a wall of build-system output that says nothing they can act on.
     */
    struct StudioToolchainReport
    {
        /** @brief Whether a build can be attempted at all. */
        bool available = false;

        /** @brief Absolute path of the toolchain driver that was found. Empty when none was. */
        std::string toolchainPath;

        /** @brief One line naming what is missing and what to do. Empty when @ref available. */
        std::string problem;
    };

    /**
     * @brief Everything Studio needs a language to do, and nothing it does not.
     *
     * Implementations are stateless and are shared: an adapter answers questions about a project
     * handed to it, and holds no project of its own. That is what lets one instance serve the
     * editor, the headless preview and a test at the same time.
     */
    class StudioLanguageAdapter
    {
    public:
        virtual ~StudioLanguageAdapter() = default;

        /** @brief This language's identity. */
        [[nodiscard]] virtual const StudioLanguageDescriptor& descriptor() const = 0;

        /**
         * @brief Whether this project is one this language can author.
         *
         * A language may not support every project kind — an XNA-style project has a hand-written
         * game loop and a binding without one cannot offer it. Answered by the adapter rather than
         * by a table in the Project Hub, because the Hub would have to be edited for every language
         * and would then be the place the answer is wrong.
         *
         * @param kind The project kind.
         * @return True when a project of that kind can be created and built in this language.
         */
        [[nodiscard]] virtual bool supportsProjectKind(ProjectKind kind) const = 0;

        /**
         * @brief Looks for this language's toolchain.
         *
         * @param preferredPath An explicit toolchain path from preferences, or empty to search.
         * @return What was found, or why nothing was.
         */
        [[nodiscard]] virtual StudioToolchainReport probeToolchain(
            std::string_view preferredPath) const = 0;

        /**
         * @brief Returns why @p project cannot be built, or an empty string when it can.
         *
         * @param project The open project.
         * @param toolchain The result of @ref probeToolchain.
         * @return A sentence a user can act on, or empty.
         */
        [[nodiscard]] virtual std::string describeBuildProblem(
            const Project& project, const StudioToolchainReport& toolchain) const = 0;

        /**
         * @brief Returns the commands that build @p project, in the order they must run.
         *
         * Pure: no filesystem writes, no process, no clock. That is what makes the interesting part
         * of a build — which options are passed, and in which order — testable on a machine with no
         * compiler, and what lets the Build panel show the exact command line before running it.
         *
         * @param project The open project.
         * @param toolchain The result of @ref probeToolchain.
         * @return The job, or one with no steps when @ref describeBuildProblem has something to say.
         */
        [[nodiscard]] virtual StudioBuildJob planBuild(
            const Project& project, const StudioToolchainReport& toolchain) const = 0;

        /**
         * @brief Returns the files that make @p scaffold a buildable project in this language.
         *
         * The build file, the entry point, and whatever else the language's ecosystem requires.
         * Content — scenes, assets, the project file itself — is not this adapter's business and is
         * written by the caller.
         *
         * Deterministic: the same scaffold produces the same bytes, because a project generator
         * whose output churns is one whose first commit is unreviewable.
         *
         * @param scaffold What is being created.
         * @return The files, in a stable order.
         */
        [[nodiscard]] virtual std::vector<StudioProjectFile> projectFiles(
            const StudioProjectScaffold& scaffold) const = 0;

        /**
         * @brief Writes @p project out as a standalone project that does not know Studio exists.
         *
         * @param project The project to export.
         * @param request Where to write, and under what name.
         * @return The files written, or the reason nothing was.
         */
        [[nodiscard]] virtual StudioExportResult exportStandalone(
            const Project& project, const StudioExportRequest& request) const = 0;

        /**
         * @brief Returns the commands that build an exported project by hand, one per line.
         *
         * So that `--export` can print them, and so that the thing printed is written by whoever
         * knows what the exported build file says rather than by the command-line parser.
         *
         * @param directory The exported project's directory.
         * @return Shell command lines, in order.
         */
        [[nodiscard]] virtual std::vector<std::string> standaloneBuildInstructions(
            std::string_view directory) const = 0;
    };

    /**
     * @brief The languages a build of Studio can author in.
     *
     * An ordinary object, constructed by whoever assembles Studio and handed to what needs it —
     * not a locator and not a singleton (`docs/ARCHITECTURE.md` §10.1). Studio's own architecture
     * guard would refuse it if it were.
     */
    class StudioLanguageRegistry
    {
    public:
        /**
         * @brief Adds @p adapter, replacing any with the same id.
         * @param adapter The adapter. A null pointer is ignored.
         */
        void add(std::shared_ptr<const StudioLanguageAdapter> adapter);

        /**
         * @brief Returns the adapter with @p id, or null.
         * @param id A language id, e.g. `"cpp"`.
         */
        [[nodiscard]] const StudioLanguageAdapter* find(std::string_view id) const;

        /**
         * @brief Returns the adapter for @p project's language, or null when it is not registered.
         *
         * Null is the honest answer for a project written in a language this build of Studio does
         * not implement: better an editor that says so than one that quietly builds it as something
         * else.
         *
         * @param project The project.
         */
        [[nodiscard]] const StudioLanguageAdapter* forProject(const Project& project) const;

        /** @brief Every registered adapter, in registration order. */
        [[nodiscard]] const std::vector<std::shared_ptr<const StudioLanguageAdapter>>& all() const
        {
            return adapters_;
        }

        /** @brief The id a new project gets when the user expresses no preference. */
        [[nodiscard]] std::string defaultLanguageId() const;

        /** @brief Whether anything is registered. */
        [[nodiscard]] bool empty() const { return adapters_.empty(); }

    private:
        std::vector<std::shared_ptr<const StudioLanguageAdapter>> adapters_;
    };

    /**
     * @brief Returns a registry holding every language this build of Studio implements.
     *
     * By value, and freshly built each call: a factory rather than an accessor. Handing out a
     * reference to one shared registry is how a seam becomes a service locator, and Studio has a
     * guard test that refuses exactly that shape.
     */
    [[nodiscard]] StudioLanguageRegistry studioBuiltInLanguages();
}

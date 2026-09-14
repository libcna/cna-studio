// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Plugins/Plugin.hpp
 * @brief The plugin manifest, the C ABI entry point, and the services a plugin may use.
 *
 * A plugin is a manifest plus a shared library. The manifest is read *before* the library is
 * loaded, which is what lets the editor refuse an incompatible plugin without ever running its
 * code -- an ABI mismatch that reaches `dlopen` is a crash, not an error message.
 *
 * The entry point is deliberately `extern "C"`: a C++ symbol would bind the plugin to the exact
 * compiler and standard library the editor was built with, which no plugin author can be expected
 * to match. See ANALYSIS.md decision D-11.
 */

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Editor
{
    class ComponentRegistry;
    class EditorContext;

    /** @brief The plugin API version this build exposes. Bumped on any breaking change. */
    inline constexpr int kEditorPluginApiVersion = 1;

    /** @brief A parsed `plugin.json` manifest. */
    struct PluginManifest
    {
        /** @brief Reverse-DNS identity, e.g. "org.openeggbert.mc3". Must be unique. */
        std::string id;

        std::string name;
        std::string version;
        std::string description;
        std::string author;

        /** @brief The kEditorPluginApiVersion the plugin was built against. */
        int editorApiVersion = 0;

        /** @brief Library file name, relative to the manifest, e.g. "libmc3-editor-plugin.so". */
        std::string library;

        /** @brief Ids of plugins that must load first. */
        std::vector<std::string> dependencies;

        /** @brief Absolute path of the directory the manifest was read from. */
        std::string directory;

        /**
         * @brief Parses a manifest file.
         * @param path Path to the `plugin.json`.
         * @param[out] errorMessage Set when parsing fails.
         * @return The manifest, or std::nullopt on failure.
         */
        static std::optional<PluginManifest> loadFromFile(const std::string& path, std::string* errorMessage);

        /** @brief Returns true when @p editorApiVersion matches this build exactly. */
        [[nodiscard]] bool isCompatible() const { return editorApiVersion == kEditorPluginApiVersion; }
    };

    /**
     * @brief What a loaded plugin can do.
     *
     * A plugin implements this and returns one from its entry point. The methods are called at
     * well-defined points, and a plugin that throws out of any of them is unloaded and reported
     * rather than allowed to destabilise the editor.
     */
    class EditorPlugin
    {
    public:
        virtual ~EditorPlugin() = default;

        /**
         * @brief Called once after loading, before any document is opened.
         *
         * The usual work is registering component descriptors, asset importers, panels and menu
         * commands against @p context.
         */
        virtual void initialize(EditorContext& context) = 0;

        /** @brief Called once before unloading. Must remove everything initialize() registered. */
        virtual void shutdown(EditorContext& context) = 0;

        /** @brief Returns the plugin's manifest id, for diagnostics. */
        [[nodiscard]] virtual std::string getId() const = 0;
    };

    /**
     * @brief The signature the plugin library must export as `cnaEditorCreatePlugin`.
     *
     * The returned object is owned by the caller and destroyed through
     * `cnaEditorDestroyPlugin`, so that allocation and deallocation both happen inside the
     * plugin's own runtime -- mixing allocators across a shared-library boundary is the classic
     * way to make a plugin system crash only in release builds.
     */
    using CreatePluginFunction = EditorPlugin* (*)(int editorApiVersion);

    /** @brief The signature the plugin library must export as `cnaEditorDestroyPlugin`. */
    using DestroyPluginFunction = void (*)(EditorPlugin*);

    /** @brief One entry in the plugin host's registry, whether or not the library loaded. */
    struct LoadedPlugin
    {
        PluginManifest manifest;

        /** @brief False when the plugin was rejected; @c error then says why. */
        bool loaded = false;
        std::string error;

        /**
         * @brief True once the library is open and `initialize` has run (ED-411).
         *
         * Distinct from `loaded`, which means "passed validation and would load". A plugin can
         * pass every check and still fail to open -- a missing dependency of its *own*, a symbol
         * it does not export -- and the two failures need different things from a user: one is a
         * manifest to fix, the other is a build to fix.
         */
        bool active = false;
    };

    /**
     * @brief Discovers, validates and loads plugins (ED-017, ED-411).
     *
     * Discovery and validation were built first and deliberately: an ABI mismatch that reaches
     * `dlopen` is a crash rather than an error message (D-11), so the manifest is read and checked
     * *before* any of the plugin's code runs. ED-411 added the loading behind that gate, so the
     * only thing loading can get wrong is the loading.
     *
     * **Unload order is load-bearing and is the one thing here that crashes when reversed.** A
     * plugin's `shutdown` runs first, so everything it registered is removed while its code is
     * still mapped; then the object is destroyed *through the plugin's own* destroy function, so
     * the allocation and the deallocation happen in the same runtime; and only then is the library
     * closed. Closing first unmaps the code that the destructor was about to run.
     */
    class PluginHost
    {
    public:
        // Both defined in the .cpp rather than defaulted here: the member holding open libraries
        // is a `vector<unique_ptr<Open>>`, and `Open` is deliberately incomplete in this header --
        // it names a platform library handle, which nothing including this file should have to.
        PluginHost();
        ~PluginHost();

        // Holds open library handles, which are not copyable and whose lifetime is the host's.
        PluginHost(const PluginHost&) = delete;
        PluginHost& operator=(const PluginHost&) = delete;

        /**
         * @brief Scans @p directory for immediate subdirectories containing a `plugin.json`.
         * @return One entry per manifest found, in dependency order where it can be determined.
         */
        std::vector<LoadedPlugin> discover(const std::string& directory);

        /**
         * @brief Opens and initialises every plugin `discover` accepted (ED-411).
         *
         * In the order `discover` returned, which is dependency order: a plugin that registers a
         * component another plugin extends has to have registered it first.
         *
         * A plugin that will not open, does not export the entry points, refuses the API version
         * or throws out of `initialize` is reported and left inactive; the rest still load. One
         * bad plugin disabling every other one is the failure mode that makes people stop using
         * plugins at all.
         *
         * @return How many became active.
         */
        std::size_t loadAll(EditorContext& context);

        /**
         * @brief Shuts down and closes every active plugin, in reverse load order.
         *
         * Reverse, for the reason dependency order exists at all: a plugin that depends on another
         * must let go of it before that other one is unmapped.
         */
        void unloadAll(EditorContext& context);

        /**
         * @brief Unloads @p pluginId, then loads it again from the same manifest.
         *
         * The whole of hot-reload, and honest about what it is: the plugin's state is gone and its
         * registrations are made afresh. Anything the editor held that the plugin owned -- a
         * descriptor, a panel -- is invalid across the call, which is why `shutdown` is required to
         * remove all of it rather than encouraged to.
         *
         * @return True when the plugin was found and came back active.
         */
        bool reload(EditorContext& context, const std::string& pluginId);

        /** @brief Returns everything discover() found on its most recent call. */
        [[nodiscard]] const std::vector<LoadedPlugin>& getPlugins() const { return plugins_; }

        /** @brief How many plugins are open and initialised right now. */
        [[nodiscard]] std::size_t getActiveCount() const;

    private:
        struct Open;

        std::vector<LoadedPlugin> plugins_;

        /** @brief One entry per *active* plugin, in load order. */
        std::vector<std::unique_ptr<Open>> open_;

        /** @brief Opens and initialises the plugin at @p index, filling in its error on failure. */
        bool activate(EditorContext& context, std::size_t index);

        /** @brief Shuts down, destroys and closes @p entry. Never throws. */
        void deactivate(EditorContext& context, Open& entry);
    };
}

// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Plugins/Plugin.hpp"

#include "CNA/Editor/EditorContext.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif



#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "CNA/Editor/Core/Json.hpp"

namespace CNA::Editor
{
    std::optional<PluginManifest> PluginManifest::loadFromFile(const std::string& path, std::string* errorMessage)
    {
        std::ifstream stream{path, std::ios::binary};
        if (!stream)
        {
            if (errorMessage != nullptr) { *errorMessage = "cannot open '" + path + "'"; }
            return std::nullopt;
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        const JsonParseResult parsed = Json::parse(buffer.str());
        if (!parsed.succeeded)
        {
            if (errorMessage != nullptr) { *errorMessage = parsed.errorMessage; }
            return std::nullopt;
        }

        PluginManifest manifest;
        manifest.id = parsed.value["id"].asString();
        if (manifest.id.empty())
        {
            if (errorMessage != nullptr) { *errorMessage = "manifest has no 'id'"; }
            return std::nullopt;
        }

        manifest.name = parsed.value["name"].asString(manifest.id);
        manifest.version = parsed.value["version"].asString("0.0.0");
        manifest.description = parsed.value["description"].asString();
        manifest.author = parsed.value["author"].asString();
        manifest.editorApiVersion = parsed.value["editorApiVersion"].asInt(0);
        manifest.library = parsed.value["library"].asString();

        for (const JsonValue& dependency : parsed.value["dependencies"].getElements())
        {
            manifest.dependencies.push_back(dependency.asString());
        }

        manifest.directory = std::filesystem::path{path}.parent_path().generic_string();
        return manifest;
    }

    std::vector<LoadedPlugin> PluginHost::discover(const std::string& directory)
    {
        plugins_.clear();

        std::error_code errorCode;
        if (!std::filesystem::is_directory(directory, errorCode)) { return plugins_; }

        std::vector<PluginManifest> manifests;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{directory, errorCode})
        {
            if (!entry.is_directory(errorCode)) { continue; }

            const std::string manifestPath = (entry.path() / "plugin.json").generic_string();
            if (!std::filesystem::exists(manifestPath, errorCode)) { continue; }

            std::string manifestError;
            if (std::optional<PluginManifest> manifest = PluginManifest::loadFromFile(manifestPath, &manifestError))
            {
                manifests.push_back(std::move(*manifest));
            }
            else
            {
                LoadedPlugin rejected;
                rejected.manifest.directory = entry.path().generic_string();
                rejected.error = "malformed plugin.json: " + manifestError;
                plugins_.push_back(std::move(rejected));
            }
        }

        // Sort by id first so that the dependency pass below is deterministic; two plugins with no
        // relationship must always come out in the same order, or a build is not reproducible.
        std::sort(manifests.begin(), manifests.end(),
                  [](const PluginManifest& lhs, const PluginManifest& rhs) { return lhs.id < rhs.id; });

        // Kahn-style ordering. A plugin whose dependency is missing or circular is emitted at the
        // end and marked with an error rather than dropped, so the user can see why it is inactive.
        std::unordered_set<std::string> satisfied;
        std::vector<PluginManifest> pending = std::move(manifests);
        bool madeProgress = true;
        while (madeProgress && !pending.empty())
        {
            madeProgress = false;
            for (auto iterator = pending.begin(); iterator != pending.end();)
            {
                const bool ready = std::all_of(iterator->dependencies.begin(), iterator->dependencies.end(),
                                               [&](const std::string& dependency) {
                                                   return satisfied.count(dependency) > 0;
                                               });
                if (!ready) { ++iterator; continue; }

                LoadedPlugin entry;
                entry.manifest = *iterator;
                if (!entry.manifest.isCompatible())
                {
                    entry.error = "built against editor API version "
                                + std::to_string(entry.manifest.editorApiVersion) + ", this editor speaks "
                                + std::to_string(kEditorPluginApiVersion);
                }
                else if (entry.manifest.library.empty())
                {
                    entry.error = "manifest declares no 'library'";
                }
                else if (!std::filesystem::exists(
                             std::filesystem::path{entry.manifest.directory} / entry.manifest.library, errorCode))
                {
                    entry.error = "library '" + entry.manifest.library + "' is missing";
                }
                else
                {
                    // Passed every check that can be made without running the plugin's code, which
                    // is what `loaded` means here and why it is not the same as `active`: opening
                    // the library is `loadAll`'s job and can still fail for reasons a manifest
                    // cannot show -- a dependency of the plugin's own, a symbol it forgot to
                    // export. Splitting the two is what lets a user be told which of those it was.
                    entry.loaded = true;
                    entry.error.clear();
                }

                satisfied.insert(entry.manifest.id);
                plugins_.push_back(std::move(entry));
                iterator = pending.erase(iterator);
                madeProgress = true;
            }
        }

        for (PluginManifest& manifest : pending)
        {
            LoadedPlugin entry;
            entry.manifest = std::move(manifest);
            entry.error = "unsatisfied or circular dependencies";
            plugins_.push_back(std::move(entry));
        }

        return plugins_;
    }
}

namespace CNA::Editor
{
    namespace
    {
        /**
         * @brief A shared library, opened and closed (ED-411).
         *
         * The only platform-specific code in the editor outside the viewport, and it is this small
         * on purpose: two functions and a handle. `dlopen` and `LoadLibrary` differ in spelling and
         * in how they report an error, and in nothing else this needs.
         */
        class DynamicLibrary
        {
        public:
            DynamicLibrary() = default;

            ~DynamicLibrary() { close(); }

            DynamicLibrary(const DynamicLibrary&) = delete;
            DynamicLibrary& operator=(const DynamicLibrary&) = delete;

            /** @brief Opens @p path, or returns false and sets @p error. */
            bool open(const std::string& path, std::string& error)
            {
                close();

#if defined(_WIN32)
                handle_ = static_cast<void*>(::LoadLibraryA(path.c_str()));
                if (handle_ == nullptr)
                {
                    error = "LoadLibrary failed with error " + std::to_string(::GetLastError());
                    return false;
                }
#else
                // RTLD_LOCAL so a plugin's symbols do not leak into the global namespace and
                // silently satisfy another plugin's undefined reference -- which turns "plugin B
                // forgot to link something" into "plugin B works until plugin A is disabled".
                handle_ = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
                if (handle_ == nullptr)
                {
                    const char* reported = ::dlerror();
                    error = reported != nullptr ? reported : "dlopen failed";
                    return false;
                }
#endif
                return true;
            }

            /** @brief Returns the address of @p name, or nullptr. */
            [[nodiscard]] void* symbol(const char* name) const
            {
                if (handle_ == nullptr) { return nullptr; }
#if defined(_WIN32)
                return reinterpret_cast<void*>(
                    ::GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
                return ::dlsym(handle_, name);
#endif
            }

            void close()
            {
                if (handle_ == nullptr) { return; }
#if defined(_WIN32)
                ::FreeLibrary(static_cast<HMODULE>(handle_));
#else
                ::dlclose(handle_);
#endif
                handle_ = nullptr;
            }

            [[nodiscard]] bool isOpen() const { return handle_ != nullptr; }

        private:
            void* handle_ = nullptr;
        };
    }

    /** @brief One plugin that is open: its library, its object, and how to destroy it. */
    struct PluginHost::Open
    {
        std::string id;
        DynamicLibrary library;

        EditorPlugin* plugin = nullptr;
        DestroyPluginFunction destroy = nullptr;

        /** @brief Index into `plugins_`, so a failure can be reported against the right entry. */
        std::size_t index = 0;
    };

    PluginHost::PluginHost() = default;

    PluginHost::~PluginHost()
    {
        // Deliberately *not* calling shutdown here: that needs an EditorContext, and a destructor
        // has none. Closing the libraries without it would run each plugin's destructor against a
        // context that may already be gone. So the handles are simply released, and the editor is
        // expected to call unloadAll while it still has a context -- which the application does.
        open_.clear();
    }

    std::size_t PluginHost::getActiveCount() const { return open_.size(); }

    bool PluginHost::activate(EditorContext& context, std::size_t index)
    {
        if (index >= plugins_.size()) { return false; }

        LoadedPlugin& entry = plugins_[index];
        if (!entry.loaded) { return false; }

        auto opened = std::make_unique<Open>();
        opened->id = entry.manifest.id;
        opened->index = index;

        const std::filesystem::path path =
            std::filesystem::path{entry.manifest.directory} / entry.manifest.library;

        std::string error;
        if (!opened->library.open(path.string(), error))
        {
            entry.error = "could not open '" + path.string() + "': " + error;
            entry.active = false;
            return false;
        }

        auto create =
            reinterpret_cast<CreatePluginFunction>(opened->library.symbol("cnaEditorCreatePlugin"));
        opened->destroy =
            reinterpret_cast<DestroyPluginFunction>(opened->library.symbol("cnaEditorDestroyPlugin"));

        if (create == nullptr || opened->destroy == nullptr)
        {
            // Both or neither. A library exporting only the creator would leak every plugin object
            // it ever made, and the leak would be invisible until a hot-reload loop found it.
            entry.error = "does not export cnaEditorCreatePlugin and cnaEditorDestroyPlugin";
            entry.active = false;
            return false;
        }

        try
        {
            opened->plugin = create(kEditorPluginApiVersion);
        }
        catch (const std::exception& thrown)
        {
            entry.error = std::string{"threw while being created: "} + thrown.what();
            entry.active = false;
            return false;
        }

        if (opened->plugin == nullptr)
        {
            // The entry point's own way of refusing: it is handed the editor's API version and may
            // decide it cannot work with it, which is a cleaner refusal than the manifest check
            // because the plugin is the one that knows what it needs.
            entry.error = "refused this editor's plugin API version";
            entry.active = false;
            return false;
        }

        try
        {
            opened->plugin->initialize(context);
        }
        catch (const std::exception& thrown)
        {
            entry.error = std::string{"threw out of initialize(): "} + thrown.what();
            entry.active = false;

            // Destroyed through its own destroy function even though it never finished starting:
            // it was allocated in the plugin's runtime and has to be freed there.
            opened->destroy(opened->plugin);
            return false;
        }

        entry.active = true;
        entry.error.clear();
        open_.push_back(std::move(opened));
        return true;
    }

    void PluginHost::deactivate(EditorContext& context, Open& entry)
    {
        // Order matters and reversing it crashes. shutdown() first, while the plugin's code is
        // still mapped and its registrations can be removed; then destroy through the plugin's own
        // function, so allocation and deallocation happen in one runtime; then close the library.
        if (entry.plugin != nullptr)
        {
            try
            {
                entry.plugin->shutdown(context);
            }
            catch (const std::exception&)
            {
                // Swallowed on purpose. A plugin that throws on the way out has already failed,
                // and letting that escape would take the editor down during shutdown -- when
                // there is nothing left to save the situation with.
            }

            if (entry.destroy != nullptr) { entry.destroy(entry.plugin); }
            entry.plugin = nullptr;
        }

        // Whatever the plugin registered and its own shutdown did not remove (ED-412). A panel or
        // a menu command outliving its library is a `std::function` whose code has been unmapped,
        // and it fails on the next frame that draws it rather than at the moment of the mistake --
        // so the host takes them away rather than trusting every plugin to.
        context.getPluginExtensions().removeAllFrom(entry.id);

        entry.library.close();

        if (entry.index < plugins_.size()) { plugins_[entry.index].active = false; }
    }

    std::size_t PluginHost::loadAll(EditorContext& context)
    {
        for (std::size_t index = 0; index < plugins_.size(); ++index)
        {
            if (plugins_[index].active) { continue; }
            activate(context, index);
        }

        return open_.size();
    }

    void PluginHost::unloadAll(EditorContext& context)
    {
        // Reverse load order, for the reason dependency order exists: a plugin that depends on
        // another has to let go before that other one is unmapped.
        for (auto entry = open_.rbegin(); entry != open_.rend(); ++entry)
        {
            deactivate(context, **entry);
        }

        open_.clear();
    }

    bool PluginHost::reload(EditorContext& context, const std::string& pluginId)
    {
        for (auto entry = open_.begin(); entry != open_.end(); ++entry)
        {
            if ((*entry)->id != pluginId) { continue; }

            deactivate(context, **entry);
            open_.erase(entry);
            break;
        }

        for (std::size_t index = 0; index < plugins_.size(); ++index)
        {
            if (plugins_[index].manifest.id != pluginId) { continue; }
            return activate(context, index);
        }

        return false;
    }
}

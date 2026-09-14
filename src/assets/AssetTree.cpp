// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Assets/AssetTree.hpp"

#include <algorithm>
#include <cctype>

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns @p text lower-cased, for the filter's case-insensitive comparison. */
        std::string toLower(std::string_view text)
        {
            std::string lowered;
            lowered.reserve(text.size());
            for (const char character : text)
            {
                lowered.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }
            return lowered;
        }

        /** @brief Returns the folder at @p path under @p root, creating it and its parents. */
        AssetFolder& folderAt(AssetFolder& root, std::string_view path)
        {
            AssetFolder* current = &root;
            std::size_t start = 0;

            while (start < path.size())
            {
                const std::size_t slash = path.find('/', start);
                const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
                const std::string_view name = path.substr(start, end - start);

                if (!name.empty())
                {
                    const auto found = std::find_if(current->folders.begin(), current->folders.end(),
                                                    [&](const AssetFolder& folder) {
                                                        return folder.name == name;
                                                    });
                    if (found != current->folders.end())
                    {
                        current = &*found;
                    }
                    else
                    {
                        AssetFolder child;
                        child.name = std::string{name};
                        child.path = current->path.empty()
                                         ? child.name
                                         : current->path + "/" + child.name;
                        current->folders.push_back(std::move(child));
                        current = &current->folders.back();
                    }
                }

                if (slash == std::string_view::npos) { break; }
                start = end + 1;
            }

            return *current;
        }

        /** @brief Orders sub-folders by name, depth first, so the tree reads the same every frame. */
        void sortFolders(AssetFolder& folder)
        {
            std::sort(folder.folders.begin(), folder.folders.end(),
                      [](const AssetFolder& left, const AssetFolder& right) {
                          return left.name < right.name;
                      });
            for (AssetFolder& child : folder.folders) { sortFolders(child); }
        }
    }

    std::size_t AssetFolder::getTotalAssetCount() const
    {
        std::size_t total = assets.size();
        for (const AssetFolder& child : folders) { total += child.getTotalAssetCount(); }
        return total;
    }

    std::string assetFileName(std::string_view path)
    {
        const std::size_t slash = path.find_last_of('/');
        return std::string{slash == std::string_view::npos ? path : path.substr(slash + 1)};
    }

    std::string assetDirectory(std::string_view path)
    {
        const std::size_t slash = path.find_last_of('/');
        return slash == std::string_view::npos ? std::string{} : std::string{path.substr(0, slash)};
    }

    std::string joinAssetPath(std::string_view directory, std::string_view fileName)
    {
        // A leading slash would look absolute and resolve somewhere else entirely, so a file at the
        // project root keeps its bare name.
        if (directory.empty()) { return std::string{fileName}; }
        return std::string{directory} + "/" + std::string{fileName};
    }

    AssetFolder buildAssetTree(const AssetDatabase& assets, std::string_view filter)
    {
        const std::string needle = toLower(filter);

        AssetFolder root;
        for (const AssetRecord* record : assets.getAll())
        {
            if (!needle.empty() && toLower(record->sourcePath).find(needle) == std::string::npos)
            {
                continue;
            }

            // Only folders that survive the filter are created at all, so a filtered tree never
            // contains an empty folder -- one that told the user nothing about where a match is.
            folderAt(root, assetDirectory(record->sourcePath)).assets.push_back(record->id);
        }

        // getAll() is already ordered by source path, so the assets in each folder come out ordered
        // too; only the folders themselves need sorting.
        sortFolders(root);
        return root;
    }
}

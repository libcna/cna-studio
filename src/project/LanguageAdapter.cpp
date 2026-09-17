// SPDX-License-Identifier: MS-PL
/**
 * @file LanguageAdapter.cpp
 * @brief The language registry, and the one list that says which languages this build implements.
 *
 * `plan.md` STUDIO-02080, STUDIO-02084.
 */

#include "CNA/Studio/Project/LanguageAdapter.hpp"

#include <algorithm>

namespace CNA::Studio
{
    void StudioLanguageRegistry::add(std::shared_ptr<const StudioLanguageAdapter> adapter)
    {
        if (!adapter) { return; }

        const std::string& id = adapter->descriptor().id;
        const auto existing = std::find_if(adapters_.begin(), adapters_.end(),
            [&](const std::shared_ptr<const StudioLanguageAdapter>& registered) {
                return registered->descriptor().id == id;
            });

        // Replaced rather than appended, so that a host wanting to substitute an adapter for a test
        // gets one of it rather than two -- the same rule the action registry follows, and for the
        // same reason: a list with two entries under one id has a first one that silently wins.
        if (existing != adapters_.end()) { *existing = std::move(adapter); return; }
        adapters_.push_back(std::move(adapter));
    }

    const StudioLanguageAdapter* StudioLanguageRegistry::find(std::string_view id) const
    {
        const auto found = std::find_if(adapters_.begin(), adapters_.end(),
            [&](const std::shared_ptr<const StudioLanguageAdapter>& adapter) {
                return adapter->descriptor().id == id;
            });
        return found == adapters_.end() ? nullptr : found->get();
    }

    const StudioLanguageAdapter* StudioLanguageRegistry::forProject(const Project& project) const
    {
        // A project that names no language at all is one written before `.cnaproject` carried the
        // key, and every such project is C++ -- that is the only language Studio has ever
        // authored. Resolved through the default rather than by special-casing the empty string
        // here, so there is one place that decides what "unspecified" means.
        const std::string& language = project.getLanguage();
        return find(language.empty() ? defaultLanguageId() : language);
    }

    std::string StudioLanguageRegistry::defaultLanguageId() const
    {
        // The first registered, not a hard-coded "cpp": the default is a property of what this
        // build ships, and a constant here would be a second opinion that a build without the C++
        // adapter would silently get wrong.
        return adapters_.empty() ? std::string{} : adapters_.front()->descriptor().id;
    }
}

// SPDX-License-Identifier: MS-PL
/**
 * @file StudioBuiltInLanguages.cpp
 * @brief The one list of which languages this build of Studio implements.
 *
 * `plan.md` STUDIO-02084.
 *
 * **The single site `STUDIO-02085`'s guard exempts, and it is a whole file so that the exemption
 * cannot quietly grow.** Somewhere has to name the adapters, or nothing is registered and Studio
 * can author nothing; a composition root that names its parts is not the failure that guard exists
 * to catch. What it *is* is the only place in Studio outside `src/project/cpp/` that may include a
 * C++ adapter header — which is checkable precisely because this file holds one function and
 * nothing else to hide behind.
 */

#include "CNA/Studio/Project/LanguageAdapter.hpp"

#include "CNA/Studio/Project/Cpp/CppLanguage.hpp"

namespace CNA::Studio
{
    StudioLanguageRegistry studioBuiltInLanguages()
    {
        // The whole list, and there is one entry. CNA has several language bindings and Studio may
        // grow adapters for them; none is written here on speculation, because an adapter for a
        // binding whose contract nobody has read is a guess that later has to be unpicked rather
        // than extended. What the seam buys is that adding one is an addition.
        StudioLanguageRegistry registry;
        registry.add(makeCppLanguageAdapter());
        return registry;
    }
}

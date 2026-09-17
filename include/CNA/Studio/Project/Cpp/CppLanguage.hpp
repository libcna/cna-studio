// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/Cpp/CppLanguage.hpp
 * @brief The C++ language adapter: the one implementation of the language seam that exists.
 *
 * `plan.md` STUDIO-02083. What it owns, and therefore what the rest of Studio no longer knows:
 *
 * | Responsibility | How this adapter answers it |
 * |----------------|-----------------------------|
 * | Toolchain identity and discovery | CMake, found on the PATH or named in preferences |
 * | Configure and build commands | `cmake -S -B …` then `cmake --build …`, from the target profile |
 * | Creating a project's own files | `CMakeLists.txt` and `Source/Main.cpp` |
 * | Generated-code ownership | `Generated/`, which the tool owns; `Source/` is the user's |
 * | Packaging and standalone export | the exported tree, its build file and its embedded runtime |
 * | Standalone build verification | the commands `--export` prints and CI runs |
 *
 * It reproduces exactly what Studio did before the seam existed. That is deliberate: an
 * abstraction introduced together with a behaviour change is one whose regressions are impossible
 * to attribute.
 */

#include <memory>

#include "CNA/Studio/Project/LanguageAdapter.hpp"

namespace CNA::Studio
{
    /** @brief The id this adapter registers under, and the value written into `.cnaproject`. */
    inline constexpr const char* kCppLanguageId = "cpp";

    /**
     * @brief Returns a new C++ language adapter.
     *
     * By value into a `shared_ptr`, not a reference to a shared instance: an accessor handing out a
     * reference to a Studio type is the shape `STUDIO-02059`'s guard refuses, and the adapter is
     * stateless enough that constructing one costs nothing worth saving.
     */
    [[nodiscard]] std::shared_ptr<const StudioLanguageAdapter> makeCppLanguageAdapter();
}

// SPDX-License-Identifier: MS-PL
/**
 * @file StudioRuntimeSources.cpp
 * @brief Assembles the embedded runtime table from the byte arrays CMake generates.
 */

#include "CNA/Studio/Project/StudioRuntimeSources.hpp"

namespace CNA::Studio::Generated
{
    // Defined by cmake/EmbedBinary.cmake into generated translation units. Declared here rather
    // than in a header because nothing else has any business reading them: the table below is the
    // only supported way to get at the runtime, and a second reader would be a second place to
    // forget a file.
    extern const unsigned char kRuntimeJsonHeader[];
    extern const unsigned long kRuntimeJsonHeaderSize;
    extern const unsigned char kRuntimeJsonSource[];
    extern const unsigned long kRuntimeJsonSourceSize;
    extern const unsigned char kRuntimeUuidHeader[];
    extern const unsigned long kRuntimeUuidHeaderSize;
    extern const unsigned char kRuntimeUuidSource[];
    extern const unsigned long kRuntimeUuidSourceSize;
    extern const unsigned char kRuntimeSceneLoaderHeader[];
    extern const unsigned long kRuntimeSceneLoaderHeaderSize;
}

namespace CNA::Studio
{
    namespace
    {
        std::string_view viewOf(const unsigned char* bytes, unsigned long size)
        {
            return std::string_view{reinterpret_cast<const char*>(bytes),
                                    static_cast<std::string_view::size_type>(size)};
        }
    }

    std::vector<StudioRuntimeSource> studioRuntimeSources()
    {
        using Generated::kRuntimeJsonHeader;
        using Generated::kRuntimeJsonHeaderSize;
        using Generated::kRuntimeJsonSource;
        using Generated::kRuntimeJsonSourceSize;
        using Generated::kRuntimeSceneLoaderHeader;
        using Generated::kRuntimeSceneLoaderHeaderSize;
        using Generated::kRuntimeUuidHeader;
        using Generated::kRuntimeUuidHeaderSize;
        using Generated::kRuntimeUuidSource;
        using Generated::kRuntimeUuidSourceSize;

        return {
            StudioRuntimeSource{"Runtime/include/CNA/Studio/Core/Json.hpp",
                                viewOf(kRuntimeJsonHeader, kRuntimeJsonHeaderSize), false},
            StudioRuntimeSource{"Runtime/include/CNA/Studio/Core/Uuid.hpp",
                                viewOf(kRuntimeUuidHeader, kRuntimeUuidHeaderSize), false},
            StudioRuntimeSource{"Runtime/include/CNA/Studio/Runtime/SceneLoader.hpp",
                                viewOf(kRuntimeSceneLoaderHeader, kRuntimeSceneLoaderHeaderSize),
                                false},
            StudioRuntimeSource{"Runtime/src/Json.cpp",
                                viewOf(kRuntimeJsonSource, kRuntimeJsonSourceSize), true},
            StudioRuntimeSource{"Runtime/src/Uuid.cpp",
                                viewOf(kRuntimeUuidSource, kRuntimeUuidSourceSize), true},
        };
    }
}

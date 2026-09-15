// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiRenderer/StudioUiShaderSource.hpp
 * @brief The shader the Studio UI is drawn with, in every dialect CNA can select between.
 *
 * `plan.md` STUDIO-04024. Architecture: `docs/UI-RENDER-PATH.md`.
 *
 * ### Why there is more than one source string, and why that is not backend-specific code
 *
 * `STUDIO-02033`'s guard forbids Studio from calling Vulkan, D3D, GL, Metal or WebGPU. Authoring a
 * shader in more than one *shading language* is not that: Studio never chooses which one runs.
 * It hands CNA a `ShaderPackageEXT` holding every variant it has, and
 * `ShaderPackageEXT::selectFor(device)` picks — asking the live renderer which dialects it
 * implements, in CNA's own fixed order, and reporting a deterministic diagnostic listing every
 * candidate it considered when none is usable.
 *
 * The alternative is `ShaderEffect(device, vertexGlsl, fragmentGlsl)`, which takes one GLSL string
 * and would make the Studio UI undrawable on any renderer whose shading language is not GLSL. That
 * is precisely the renderer-specific coupling the guard exists to prevent, arrived at by writing
 * *less* code rather than more.
 *
 * Two dialects today, because they are the two CNA renderers this project can build and run:
 * desktop GLSL for `OPENGL4` and GLSL ES for the EasyGL family and the web. Adding HLSL, MSL or
 * WGSL is adding an entry to `studioUiShaderVariants()` and changes nothing else — no branch, no
 * capability query, no renderer name.
 *
 * ### The vertex contract
 *
 * Attribute locations are the element's index in the `VertexDeclaration`, which is the convention
 * CNA's renderers bind by. `VertexPositionColorTexture` therefore gives:
 *
 * | Location | Element | GLSL |
 * |---------:|---------|------|
 * | 0 | Position | `vec3` |
 * | 1 | Color | `vec4`, normalised from four bytes |
 * | 2 | TextureCoordinate | `vec2` |
 *
 * ### The fragment contract
 *
 * One sampler and one uniform beyond the projection. Every untextured primitive samples the atlas's
 * reserved opaque white texel rather than taking a branch, which is what keeps a shell frame at
 * eleven draw calls: a shader with an `if (textured)` in it would need the uniform set per command,
 * and a uniform set per command is a state change per command.
 */

#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief Which shading language one variant is written in. Mirrors `CNA::ShaderLanguageEXT`. */
    enum class StudioShaderDialect
    {
        GlslDesktop,
        GlslEs
    };

    /** @brief One stage of one dialect. */
    struct StudioUiShaderVariant
    {
        /** @brief The language it is written in. */
        StudioShaderDialect dialect;
        /** @brief Whether this is the vertex or the fragment stage. */
        bool isVertex;
        /** @brief A label for CNA's selection diagnostic, e.g. `"StudioUi.vertex.glsl"`. */
        std::string_view label;
        /** @brief The source text. */
        std::string_view source;
    };

    /**
     * @brief Every shader variant the Studio UI ships.
     *
     * @return The variants, in no meaningful order — `ShaderPackageEXT` states that declaration
     *         order never affects selection, so ordering here would be a decision that does nothing.
     */
    [[nodiscard]] const std::vector<StudioUiShaderVariant>& studioUiShaderVariants();

    /** @brief The uniform the projection matrix is set through. */
    inline constexpr std::string_view kStudioUiProjectionUniform = "uProjection";

    /** @brief The uniform the atlas sampler is bound through. */
    inline constexpr std::string_view kStudioUiTextureUniform = "uTexture";
} // namespace CNA::Studio

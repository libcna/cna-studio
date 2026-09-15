// SPDX-License-Identifier: MS-PL
/**
 * @file StudioUiShaderSource.cpp
 * @brief The Studio UI's shader, in each dialect CNA can select between.
 */

#include "CNA/Studio/UiRenderer/StudioUiShaderSource.hpp"

namespace CNA::Studio
{
    namespace
    {
        // Desktop GLSL. 3.30 rather than 4.10: it is the oldest core profile that has everything
        // this needs, and asking for more than a shader uses is how a program refuses to compile on
        // hardware that would have run it perfectly.
        constexpr std::string_view kVertexGlslDesktop = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uProjection;

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    vColor = aColor;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * vec4(aPosition, 1.0);
}
)GLSL";

        constexpr std::string_view kFragmentGlslDesktop = R"GLSL(#version 330 core
in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 fragColor;

void main()
{
    // Unconditional. Untextured geometry samples the atlas's reserved opaque white texel, so there
    // is no branch and no per-command uniform -- and a uniform set per command would be a state
    // change per command, which is the cost this whole renderer is arranged to avoid.
    fragColor = vColor * texture(uTexture, vTexCoord);
}
)GLSL";

        // GLSL ES 3.00, which is what WebGL 2 and OpenGL ES 3.0 both accept. `precision` is
        // mandatory here and absent above: leaving it out compiles on desktop and fails on ES,
        // which is the single most common way one source string stops being portable.
        constexpr std::string_view kVertexGlslEs = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uProjection;

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    vColor = aColor;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * vec4(aPosition, 1.0);
}
)GLSL";

        constexpr std::string_view kFragmentGlslEs = R"GLSL(#version 300 es
precision mediump float;
in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 fragColor;

void main()
{
    fragColor = vColor * texture(uTexture, vTexCoord);
}
)GLSL";
    } // namespace

    const std::vector<StudioUiShaderVariant>& studioUiShaderVariants()
    {
        static const std::vector<StudioUiShaderVariant> variants = {
            {StudioShaderDialect::GlslDesktop, true,  "StudioUi.vertex.glsl",      kVertexGlslDesktop},
            {StudioShaderDialect::GlslDesktop, false, "StudioUi.fragment.glsl",    kFragmentGlslDesktop},
            {StudioShaderDialect::GlslEs,      true,  "StudioUi.vertex.glsl-es",   kVertexGlslEs},
            {StudioShaderDialect::GlslEs,      false, "StudioUi.fragment.glsl-es", kFragmentGlslEs},
        };
        return variants;
    }
} // namespace CNA::Studio

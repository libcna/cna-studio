// SPDX-License-Identifier: MS-PL
/**
 * @file StudioUiRendererTests.cpp
 * @brief The UI render backends' contract, and the shader package that only one of them uses.
 *
 * `plan.md` STUDIO-04001, STUDIO-04023, STUDIO-04024.
 *
 * Two layers of coverage, deliberately at different costs. **Here**: everything decidable without a
 * device — the shader package's completeness, the dialects it covers, the vertex contract the
 * source declares. **In CTest**: `CnaStudioUiRenderBackendsAgree`, which runs the real editor twice
 * on a real device and compares the pixels, because whether a `DrawIndexedPrimitives` route lands a
 * triangle where a `DrawUserIndexedPrimitives` route does is not a question a unit test can answer.
 *
 * The split matters: the shader's *text* is checkable on any machine, and the shader's *behaviour*
 * needs a renderer that executes shaders — which CNA's `SOFTWARE` renderer, the one this project's
 * CI builds, is not.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiRenderer/StudioUiShaderSource.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief Whether @p text contains @p needle. */
    bool has(std::string_view text, std::string_view needle)
    {
        return text.find(needle) != std::string_view::npos;
    }

    /** @brief The one variant for a dialect and a stage, or null. */
    const StudioUiShaderVariant* variantFor(StudioShaderDialect dialect, bool vertex)
    {
        for (const StudioUiShaderVariant& variant : studioUiShaderVariants())
        {
            if (variant.dialect == dialect && variant.isVertex == vertex) { return &variant; }
        }
        return nullptr;
    }
}

CNA_STUDIO_TEST(EveryShaderDialectShipsBothStages)
{
    // A package missing one stage of one language is not a compile error and not a missing file:
    // CNA reports that candidate as incomplete and silently selects another, so the renderer the
    // variant was written for falls back to a dialect it may not accept at all. The failure is a
    // host that refuses the UI shader, which reads as the renderer's fault.
    for (const StudioShaderDialect dialect : {StudioShaderDialect::GlslDesktop,
                                              StudioShaderDialect::GlslEs})
    {
        CNA_STUDIO_EXPECT(variantFor(dialect, /*vertex=*/true) != nullptr);
        CNA_STUDIO_EXPECT(variantFor(dialect, /*vertex=*/false) != nullptr);
    }
    CNA_STUDIO_EXPECT_EQ(studioUiShaderVariants().size(), std::size_t{4});
}

CNA_STUDIO_TEST(EveryVariantHasANonEmptyLabelAndSourceAndNoTwoLabelsCollide)
{
    // The label is what CNA's selection diagnostic names, and it is the only thing that
    // distinguishes one refused variant from another in that report.
    std::vector<std::string> labels;
    for (const StudioUiShaderVariant& variant : studioUiShaderVariants())
    {
        CNA_STUDIO_EXPECT(!variant.label.empty());
        CNA_STUDIO_EXPECT(!variant.source.empty());
        labels.emplace_back(variant.label);
    }
    std::sort(labels.begin(), labels.end());
    CNA_STUDIO_EXPECT(std::adjacent_find(labels.begin(), labels.end()) == labels.end());
}

CNA_STUDIO_TEST(EveryVertexShaderDeclaresTheVertexDeclarationsThreeAttributesInOrder)
{
    // CNA's renderers bind attribute location = the element's index in the VertexDeclaration.
    // VertexPositionColorTexture is Position, Color, TextureCoordinate, so a shader that declared
    // them in any other order would read the colour as a position. That is not a compile error and
    // not a crash: it is geometry somewhere else entirely, which looks like a broken layout engine.
    for (const StudioShaderDialect dialect : {StudioShaderDialect::GlslDesktop,
                                              StudioShaderDialect::GlslEs})
    {
        const StudioUiShaderVariant* vertex = variantFor(dialect, /*vertex=*/true);
        CNA_STUDIO_EXPECT(vertex != nullptr);

        CNA_STUDIO_EXPECT(has(vertex->source, "layout(location = 0) in vec3 aPosition;"));
        CNA_STUDIO_EXPECT(has(vertex->source, "layout(location = 1) in vec4 aColor;"));
        CNA_STUDIO_EXPECT(has(vertex->source, "layout(location = 2) in vec2 aTexCoord;"));
    }
}

CNA_STUDIO_TEST(TheUniformNamesTheRendererSetsAreTheOnesTheSourceDeclares)
{
    // The failure this catches is a black window rather than a compile error: a uniform the shader
    // does not declare has no location, CNA's setter finds -1 and does nothing, and every vertex is
    // transformed by an identity matrix straight off the screen. Sharing the names through one
    // header makes that checkable; checking it makes the sharing worth something.
    for (const StudioUiShaderVariant& variant : studioUiShaderVariants())
    {
        if (variant.isVertex)
        {
            CNA_STUDIO_EXPECT(has(variant.source, std::string{"uniform mat4 "}
                                                  + std::string{kStudioUiProjectionUniform}));
        }
        else
        {
            CNA_STUDIO_EXPECT(has(variant.source, std::string{"uniform sampler2D "}
                                                  + std::string{kStudioUiTextureUniform}));
        }
    }
}

CNA_STUDIO_TEST(EveryFragmentShaderSamplesUnconditionallyRatherThanBranching)
{
    // Untextured geometry samples the atlas's reserved opaque white texel. A branch would need a
    // uniform set per command, and a uniform set per command is a state change per command -- which
    // is the cost this backend exists to remove. Asserted because the obvious "improvement" is to
    // add the branch back.
    for (const StudioUiShaderVariant& variant : studioUiShaderVariants())
    {
        if (variant.isVertex) { continue; }
        CNA_STUDIO_EXPECT(has(variant.source, "vColor * texture(uTexture, vTexCoord)"));
        CNA_STUDIO_EXPECT(!has(variant.source, "if ("));
    }
}

CNA_STUDIO_TEST(TheEsVariantsDeclareAPrecisionAndTheDesktopOnesDoNot)
{
    // The single most common way one GLSL string stops being portable: `precision` is mandatory in
    // GLSL ES and absent from desktop GLSL, so a source written for one and handed to the other
    // compiles on exactly one of them. Two variants is the fix; this is what keeps them two.
    CNA_STUDIO_EXPECT(has(variantFor(StudioShaderDialect::GlslEs, true)->source, "precision"));
    CNA_STUDIO_EXPECT(has(variantFor(StudioShaderDialect::GlslEs, false)->source, "precision"));
    CNA_STUDIO_EXPECT(!has(variantFor(StudioShaderDialect::GlslDesktop, true)->source, "precision"));
    CNA_STUDIO_EXPECT(!has(variantFor(StudioShaderDialect::GlslDesktop, false)->source, "precision"));

    CNA_STUDIO_EXPECT(has(variantFor(StudioShaderDialect::GlslEs, true)->source, "#version 300 es"));
    CNA_STUDIO_EXPECT(has(variantFor(StudioShaderDialect::GlslDesktop, true)->source,
                          "#version 330 core"));
}

CNA_STUDIO_TEST(TheShaderNamesNoGraphicsBackendAndNoRendererIdentity)
{
    // STUDIO-02033's guard reads includes and calls; a shader is a string and would pass it. The
    // rule is the same either way: Studio authors shading languages, never renderers.
    for (const StudioUiShaderVariant& variant : studioUiShaderVariants())
    {
        for (const char* forbidden : {"VULKAN", "vkCmd", "D3D", "IDirect3D", "MTL", "wgpu",
                                      "OPENGL4", "SOFTWARE"})
        {
            CNA_STUDIO_EXPECT(!has(variant.source, forbidden));
        }
    }
}


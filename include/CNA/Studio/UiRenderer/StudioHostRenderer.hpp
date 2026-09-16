// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiRenderer/StudioHostRenderer.hpp
 * @brief Which CNA renderer this binary was compiled against.
 *
 * `plan.md` STUDIO-04021, STUDIO-04027. **Layer 3** of the four in `docs/UI-RENDER-PATH.md` — the
 * CNA renderer Studio's own window runs on — and nothing to do with layer 2, the UI's own GPU
 * renderer, or with layer 4, the renderer a built game ships with.
 *
 * ### Why this is its own header
 *
 * It used to be `CnaUiRenderer::getBackendName()`: a static on the *classic UI render backend*,
 * answering a question that backend has no authority over. The status bar, the About box, the
 * host's log line, the player's report and the diagnostic panel all called it — so the player, which
 * draws no editor UI at all, linked the editor's UI renderer for one string.
 *
 * That is exactly the conflation `docs/UI-RENDER-PATH.md` was written to name: "the Studio
 * renderer" meant four things, and two of them were being answered by one function on a class that
 * owns one of them. Separating them is what lets `STUDIO-04027` delete a UI backend without
 * touching what reports the CNA renderer, and it is why the player stopped depending on a UI
 * renderer it never used.
 */

#include <string>

namespace CNA::Studio
{
    /**
     * @brief The CNA graphics renderer this binary was compiled against, by CNA's own name for it.
     *
     * Compile-time, because CNA resolves its renderer at compile time: there is exactly one in this
     * binary and it cannot change (`ANALYSIS.md` finding F-01).
     *
     * @return `"SOFTWARE"`, `"OPENGL4"`, and so on.
     */
    [[nodiscard]] std::string studioHostCnaRendererName();
} // namespace CNA::Studio

// SPDX-License-Identifier: MS-PL
/**
 * @file RendererCatalog.cpp
 * @brief The renderer and platform catalogue, audited against CNA.
 *
 * Audited against libcna/cna branch `next` at commit
 * e05b3d0f026e0926741f89459daf02579240399d, read from `cmake/RendererRegistry.cmake` and
 * `cmake/PlatformSelection.cmake` on 2026-09-14.
 *
 * The host-support column is **Studio's judgement, not CNA's**. CNA has no opinion about whether a
 * renderer can host an editor; the requirement belongs to Studio, so the classification lives here.
 */

#include "CNA/Studio/Project/RendererCatalog.hpp"

#include <algorithm>
#include <cctype>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Case-insensitive comparison, for matching a user's spelling of an identity. */
        bool equalsIgnoreCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size()) { return false; }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                const auto lhs = static_cast<unsigned char>(a[i]);
                const auto rhs = static_cast<unsigned char>(b[i]);
                if (std::tolower(lhs) != std::tolower(rhs)) { return false; }
            }
            return true;
        }

        constexpr auto kHost = RendererHostSupport::StudioHost;
        constexpr auto kPreview = RendererHostSupport::PreviewOnly;
        constexpr auto kRuntime = RendererHostSupport::RuntimeOnly;
    } // namespace

    std::string_view getAuditedCnaCommit() { return "e05b3d0f026e0926741f89459daf02579240399d"; }
    std::string_view getAuditedCnaBranch() { return "next"; }

    const std::vector<RendererInfo>& getKnownRenderers()
    {
        // In CNA's own declaration order, so a diff against cmake/RendererRegistry.cmake reads
        // straight down rather than needing to be sorted first.
        static const std::vector<RendererInfo> renderers{
            {"SDL_RENDERER", "sdlrenderer", "SDL_Renderer", kPreview,
             "2D only. It can draw the UI but not the 3D viewport, so it cannot host Studio "
             "under the 3D-first viewport requirement. An excellent 2D game target."},
            {"OPENGLES2", "opengles2", "OpenGL ES 2.0", kPreview,
             "Below the shader and render-target feature set the Studio UI needs."},
            {"OPENGLES3", "opengles3", "OpenGL ES 3.0", kHost,
             "The reference Studio host on Linux and embedded targets."},
            {"OPENGL33", "opengl33", "OpenGL 3.3 Core", kHost, "Full Studio host support."},
            {"WEBGL1", "webgl1", "WebGL 1", kRuntime,
             "Browser only; there is no desktop Studio process to host."},
            {"WEBGL2", "webgl2", "WebGL 2", kRuntime,
             "Browser only; there is no desktop Studio process to host."},
            {"BGFX", "bgfx", "bgfx", kHost, "Full Studio host support."},
            {"VULKAN", "vulkan", "Vulkan", kHost,
             "Full Studio host support, and the reference target for modern CNAEXT rendering."},
            {"WEBGPU", "webgpu", "WebGPU", kPreview,
             "Capable in principle; native support is still settling. Useful for previewing a "
             "browser build's rendering."},
            {"MAGNUM", "magnum", "Magnum", kHost, "Full Studio host support."},
            {"HEADLESS", "headless", "Headless", kRuntime,
             "No window by definition. Used by Studio's own automated tests."},
            {"SOFTWARE", "software", "Software (CPU rasterizer)", kPreview,
             "Correct but slow. Ideal as a comparison reference, unusable as an interactive host."},
            {"STUB", "stub", "Stub", kRuntime, "Draws nothing. A build and API conformance target."},
            {"PORTABLEGL", "portablegl", "PortableGL", kPreview,
             "A software GL implementation. Same trade as SOFTWARE."},
            {"DIRECTX11", "directx11", "Direct3D 11", kHost, "Windows only. Full Studio host support."},
            {"DIRECTX12", "directx12", "Direct3D 12", kHost, "Windows only. Full Studio host support."},
            {"DIRECT2D", "direct2d", "Direct2D", kPreview, "Windows only, and 2D only."},
            {"CANVAS", "canvas", "HTML Canvas 2D", kRuntime,
             "Browser only; there is no desktop Studio process to host."},
            {"HTML_DOM", "htmldom", "HTML DOM", kRuntime, "Browser only, and not a GPU pipeline."},
            {"SVG_DOM", "svgdom", "SVG DOM", kRuntime, "Browser only, and not a GPU pipeline."},
            {"BLEND2D", "blend2d", "Blend2D", kPreview, "A 2D vector rasterizer. No 3D pipeline."},
            {"FREEDIRECT", "freedirect", "FreeDirect", kPreview,
             "Fixed-function-era feature set. Suitable for a player process."},
            {"DIRECTX9", "directx9", "Direct3D 9", kPreview,
             "Fixed-function-era feature set. Exactly what the separate player process exists for."},
            {"DIRECTX1", "directx1", "DirectX 1 (DirectDraw)", kPreview, "Historical."},
            {"DIRECTX2", "directx2", "DirectX 2", kPreview, "Historical."},
            {"DIRECTX3", "directx3", "DirectX 3", kPreview, "Historical."},
            {"DIRECTX5", "directx5", "DirectX 5", kPreview, "Historical."},
            {"DIRECTX6", "directx6", "DirectX 6", kPreview, "Historical."},
            {"DIRECTX7", "directx7", "DirectX 7", kPreview, "Historical."},
            {"DIRECTX8", "directx8", "DirectX 8", kPreview, "Historical."},
            {"DIRECTX10", "directx10", "Direct3D 10", kPreview,
             "Capable, but superseded on every platform that has it by DIRECTX11."},
            {"SDL_GPU", "sdlgpu", "SDL_GPU", kHost, "Full Studio host support."},
            {"OPENGLES1", "opengles1", "OpenGL ES 1.1", kPreview, "Fixed-function. No shaders."},
            {"OPENGL4", "opengl4", "OpenGL 4", kHost,
             "Full Studio host support, including compute where the driver provides it."},
            {"OPENGL1", "opengl1", "OpenGL 1.x", kPreview, "Fixed-function. No shaders."},
            {"OPENGL2", "opengl2", "OpenGL 2.x", kPreview,
             "Shaders, but below the render-target and buffer feature set the Studio UI needs."},
            {"WICKED", "wicked", "Wicked Engine renderer", kHost, "Full Studio host support."},
            {"SOKOL", "sokol", "sokol_gfx", kHost, "Full Studio host support."},
            {"DILIGENT", "diligent", "Diligent Engine", kHost, "Full Studio host support."},
            {"GLIDE", "glide", "Glide (3dfx)", kPreview, "Historical. A player target only."},
            {"GDI", "gdi", "Windows GDI", kPreview, "Windows only, 2D only, and software."},
            {"LLGL", "llgl", "LLGL", kHost, "Full Studio host support."},
            {"METAL", "metal", "Metal", kHost, "macOS and iOS. Full Studio host support."},
            {"FNA3D", "fna3d", "FNA3D", kPreview,
             "Targets the XNA feature set faithfully, which is below what the Studio UI needs."},
            {"OPENVG", "openvg", "OpenVG", kPreview, "A 2D vector API. No 3D pipeline."},
            {"TINYGL", "tinygl", "TinyGL", kPreview, "A small software GL. Fixed-function."},
            {"IGL", "igl", "IGL", kHost, "Full Studio host support."},
            {"PIXIJS", "pixijs", "PixiJS", kRuntime, "Browser only."},
            {"NANOVG", "nanovg", "NanoVG", kPreview, "A 2D vector renderer. No 3D pipeline."},
            {"RLGL", "rlgl", "rlgl (raylib)", kPreview,
             "An immediate-mode GL abstraction. Capable of the UI, but its feature floor is below "
             "what the Studio viewport needs."},
        };
        return renderers;
    }

    const RendererInfo* findRenderer(std::string_view name)
    {
        for (const RendererInfo& renderer : getKnownRenderers())
        {
            if (equalsIgnoreCase(renderer.cnaIdentity, name)
                || equalsIgnoreCase(renderer.commandLineName, name))
            {
                return &renderer;
            }
        }
        return nullptr;
    }

    const std::vector<PlatformInfo>& getKnownPlatforms()
    {
        // From cmake/PlatformSelection.cmake. The reserved entries are listed on purpose: CNA
        // makes selecting one a hard error rather than falling back, and Studio should be able to
        // say why rather than showing a user an option that does not exist.
        static const std::vector<PlatformInfo> platforms{
            {"SDL3", "sdl3", "SDL 3", PlatformStatus::Implemented, true,
             "CNA's default, and the reference platform for Studio."},
            {"SDL2", "sdl2", "SDL 2", PlatformStatus::Implemented, true,
             "For targets whose SDL 3 support is not yet available."},
            {"HEADLESS", "headless", "Headless", PlatformStatus::Implemented, false,
             "No window, so it provides no surface for a GPU renderer. Used for automated tests."},
            {"TERMINAL", "terminal", "Terminal", PlatformStatus::Implemented, false,
             "POSIX only, built on termios. A game presentation, not a Studio host."},
            {"SDL12", "sdl12", "SDL 1.2", PlatformStatus::Reserved, false,
             "Recognised by CNA's build but not implemented; selecting it is a hard error."},
            {"WIN32", "win32", "Native Win32", PlatformStatus::Reserved, false,
             "Recognised by CNA's build but not implemented; selecting it is a hard error."},
            {"EMSCRIPTEN", "emscripten", "Emscripten", PlatformStatus::Reserved, false,
             "Recognised by CNA's build but not implemented; selecting it is a hard error."},
        };
        return platforms;
    }

    const PlatformInfo* findPlatform(std::string_view name)
    {
        for (const PlatformInfo& platform : getKnownPlatforms())
        {
            if (equalsIgnoreCase(platform.cnaIdentity, name)
                || equalsIgnoreCase(platform.commandLineName, name))
            {
                return &platform;
            }
        }
        return nullptr;
    }

    const std::vector<RendererAlias>& getLegacyRendererAliases()
    {
        // Every entry here was a renderer name in the CNA revision the CNA Editor prototype
        // targeted, and is not one at the audited commit. They are migrated rather than rejected,
        // because a `.cnaproject` written by the prototype is a real file someone still has.
        static const std::vector<RendererAlias> aliases{
            {"easygl", "OPENGLES3",
             "EasyGL is now a renderer *family* serving several GL profiles rather than a renderer "
             "identity of its own, and its GL profile became a runtime value. OPENGLES3 is the "
             "profile that matches what 'easygl' used to select."},
            {"d3d11", "DIRECTX11", "Direct3D renderers were renamed to the DIRECTXnn form."},
            {"d3d12", "DIRECTX12", "Direct3D renderers were renamed to the DIRECTXnn form."},
            {"d3d9", "DIRECTX9", "Direct3D renderers were renamed to the DIRECTXnn form."},
            {"dx3", "DIRECTX3", "Direct3D renderers were renamed to the DIRECTXnn form."},
            {"ascii", "",
             "ASCII is no longer a renderer. The equivalent presentation is now a CNAEXT "
             "post-process effect applied on top of an ordinary renderer."},
        };
        return aliases;
    }

    const RendererAlias* findLegacyRendererAlias(std::string_view name)
    {
        for (const RendererAlias& alias : getLegacyRendererAliases())
        {
            if (equalsIgnoreCase(alias.legacyName, name)) { return &alias; }
        }
        return nullptr;
    }

    const std::vector<std::string_view>& getAuditedCnaRendererIdentities()
    {
        // Transcribed from cmake/RendererRegistry.cmake's identity map at the audited commit, in
        // its declaration order. This is a snapshot, not a derivation: the point is that it can
        // disagree with CNA, and that STUDIO-02030's test says so when it does.
        static const std::vector<std::string_view> identities{
            "SDL_RENDERER", "OPENGLES2", "OPENGLES3", "OPENGL33", "WEBGL1", "WEBGL2", "BGFX",
            "VULKAN", "WEBGPU", "MAGNUM", "HEADLESS", "SOFTWARE", "STUB", "PORTABLEGL",
            "DIRECTX11", "DIRECTX12", "DIRECT2D", "CANVAS", "HTML_DOM", "SVG_DOM", "BLEND2D",
            "FREEDIRECT", "DIRECTX9", "DIRECTX1", "DIRECTX2", "DIRECTX3", "DIRECTX5", "DIRECTX6",
            "DIRECTX7", "DIRECTX8", "DIRECTX10", "SDL_GPU", "OPENGLES1", "OPENGL4", "OPENGL1",
            "OPENGL2", "WICKED", "SOKOL", "DILIGENT", "GLIDE", "GDI", "LLGL", "METAL", "FNA3D",
            "OPENVG", "TINYGL", "IGL", "PIXIJS", "NANOVG", "RLGL",
        };
        return identities;
    }
} // namespace CNA::Studio

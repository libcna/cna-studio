// SPDX-License-Identifier: MS-PL
/**
 * @file CnaCapabilityBridge.cpp
 * @brief Copying a live `RendererCapabilityProfile` into Studio's CNA-free snapshot.
 */

#include "CNA/Studio/Viewport/CnaCapabilityBridge.hpp"

#include <CNA/Platform/CurrentPlatform.hpp>
#include <CNA/Platform/IPlatform.hpp>
#include <CNA/RendererCapabilityProfile.hpp>
#include <Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp>

#include <exception>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Translates CNA's four-state answer into Studio's. */
        StudioCapabilityAnswer translate(::CNA::RendererFeatureSupport support)
        {
            switch (support)
            {
                case ::CNA::RendererFeatureSupport::Supported:
                    return StudioCapabilityAnswer::Supported;
                case ::CNA::RendererFeatureSupport::Restricted:
                    return StudioCapabilityAnswer::Restricted;
                case ::CNA::RendererFeatureSupport::Unsupported:
                    return StudioCapabilityAnswer::Unsupported;
                case ::CNA::RendererFeatureSupport::Unknown:
                    break;
            }
            return StudioCapabilityAnswer::Unknown;
        }
    } // namespace

    std::string getHostPlatformName()
    {
        // Guarded, and the guard is not defensive noise: GetCurrentPlatform() lazily *creates* the
        // build's default platform, which throws when none can be created. A diagnostic function
        // that throws while assembling a diagnostic is the worst possible failure mode for it.
        try
        {
            if (!::CNA::Platform::HasCurrentPlatform()) { return "unknown"; }
            return ::CNA::Platform::GetCurrentPlatform().GetName();
        }
        catch (const std::exception&)
        {
            return "unknown";
        }
    }

    StudioCapabilitySnapshot captureStudioCapabilitySnapshot(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        std::string platformName, bool modernApiAvailable)
    {
        const ::CNA::RendererCapabilityProfile& profile = device.GetRendererCapabilityProfileEXT();

        StudioCapabilitySnapshot snapshot;
        snapshot.setRendererName(std::string{profile.GetRendererName()});
        snapshot.setPlatformName(std::move(platformName));
        snapshot.setModernApiAvailable(modernApiAvailable);

        // Every declared feature, walked through CNA's own span rather than a list written here. A
        // list would have to be updated when CNA adds a feature, and nothing would fail when it was
        // not -- the new feature would simply never appear in a diagnostic.
        for (const ::CNA::RendererFeature feature : ::CNA::AllRendererFeatures())
        {
            const ::CNA::RendererFeatureInfo& info = profile.GetFeature(feature);
            snapshot.setFeature(std::string{::CNA::GetRendererFeatureName(feature)},
                                translate(info.support), info.note);
        }

        for (const ::CNA::RendererLimit limit : ::CNA::AllRendererLimits())
        {
            const ::CNA::RendererLimitValue value = profile.GetLimit(limit);
            // An unknown limit is recorded as absent rather than as zero. Zero is a value, and a
            // requirement comparing against it would read "reports 0" for a renderer that in fact
            // reported nothing at all.
            if (value.known)
            {
                snapshot.setLimit(std::string{::CNA::GetRendererLimitName(limit)}, value.value);
            }
        }
        return snapshot;
    }
} // namespace CNA::Studio

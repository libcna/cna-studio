// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/SpriteAnimation.hpp"

#include <algorithm>

#include "CNA/Editor/Scene/EditorEntity.hpp"

namespace CNA::Editor
{
    float SpriteAnimationClip::getFrameDuration(std::size_t position) const
    {
        if (hasFrameDurations() && position < frameDurations.size() && frameDurations[position] > 0.0f)
        {
            return frameDurations[position];
        }
        return framesPerSecond > 0.0f ? 1.0f / framesPerSecond : 0.0f;
    }

    float SpriteAnimationClip::getDuration() const
    {
        if (isEmpty()) { return 0.0f; }

        float total = 0.0f;
        for (std::size_t position = 0; position < frames.size(); ++position)
        {
            total += getFrameDuration(position);
        }
        return total;
    }

    EditorRectangle SpriteAnimationClip::getFrameRectangle(std::size_t position) const
    {
        if (position >= frames.size() || frameWidth <= 0 || frameHeight <= 0 || sheetColumns <= 0)
        {
            return EditorRectangle{};
        }

        const std::int64_t index = frames[position];
        if (index < 0) { return EditorRectangle{}; }

        const int columns = sheetColumns;
        const int x = static_cast<int>(index % columns) * frameWidth;
        const int y = static_cast<int>(index / columns) * frameHeight;
        return EditorRectangle{x, y, frameWidth, frameHeight};
    }

    SpriteAnimationClip readSpriteAnimationClip(const EditorComponent& component,
                                                const ComponentDescriptor* descriptor)
    {
        SpriteAnimationClip clip;
        clip.frameWidth = static_cast<int>(
            component.getPropertyOrDefault(SpriteAnimationKeys::kFrameWidth, descriptor).get<std::int64_t>(0));
        clip.frameHeight = static_cast<int>(
            component.getPropertyOrDefault(SpriteAnimationKeys::kFrameHeight, descriptor).get<std::int64_t>(0));
        clip.sheetColumns = std::max(1, static_cast<int>(
            component.getPropertyOrDefault(SpriteAnimationKeys::kSheetColumns, descriptor).get<std::int64_t>(1)));
        clip.framesPerSecond =
            component.getPropertyOrDefault(SpriteAnimationKeys::kFramesPerSecond, descriptor).get<float>(0.0f);
        clip.loop = component.getPropertyOrDefault(SpriteAnimationKeys::kLoop, descriptor).get<bool>(true);

        const PropertyValue stored = component.getProperty(SpriteAnimationKeys::kFrames);
        if (stored.getType() == PropertyType::List)
        {
            for (const PropertyValue& item : stored.get<PropertyValue::ListValue>().items)
            {
                clip.frames.push_back(item.get<std::int64_t>(0));
            }
        }

        const PropertyValue durations = component.getProperty(SpriteAnimationKeys::kFrameDurations);
        if (durations.getType() == PropertyType::List)
        {
            for (const PropertyValue& item : durations.get<PropertyValue::ListValue>().items)
            {
                clip.frameDurations.push_back(item.get<float>(0.0f));
            }
        }
        return clip;
    }

    void AnimationPlayback::clampTo(const SpriteAnimationClip& clip)
    {
        if (clip.frames.empty())
        {
            position = 0;
            return;
        }
        position = std::min(position, clip.frames.size() - 1);
    }

    void AnimationPlayback::step(const SpriteAnimationClip& clip, int delta)
    {
        // Stepping is a deliberate look at one frame, so it stops playback rather than fighting it.
        // A step that kept playing would move the frame the user asked for out from under them.
        playing = false;
        elapsed = 0.0f;

        if (clip.frames.empty())
        {
            position = 0;
            return;
        }

        const int count = static_cast<int>(clip.frames.size());
        int next = (static_cast<int>(position) + delta) % count;
        if (next < 0) { next += count; }
        position = static_cast<std::size_t>(next);
    }

    bool AnimationPlayback::advance(const SpriteAnimationClip& clip, float deltaSeconds)
    {
        if (!playing || clip.isEmpty()) { return false; }
        if (clip.getFrameDuration(position) <= 0.0f) { return false; }

        elapsed += deltaSeconds;

        const std::size_t count = clip.frames.size();
        const std::size_t before = position;

        // One frame at a time, because each may be held for a different length. Bounded by the
        // frames a whole clip's worth of time could cover, so a duration list of near-zeroes cannot
        // spin here -- and a long stall still catches up rather than crawling back.
        for (std::size_t guard = 0; guard <= count; ++guard)
        {
            const float held = clip.getFrameDuration(position);
            if (held <= 0.0f || elapsed < held) { break; }

            elapsed -= held;

            if (position + 1 < count)
            {
                ++position;
                continue;
            }

            if (clip.loop)
            {
                position = 0;
                continue;
            }

            // Held on the last frame rather than wrapped, and playback stops: a non-looping clip
            // that quietly restarted would be indistinguishable from a looping one.
            playing = false;
            elapsed = 0.0f;
            break;
        }

        return position != before;
    }
}

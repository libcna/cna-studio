// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/RuntimeBridge/StudioProtocol.hpp"

#include <algorithm>

#include <array>

namespace CNA::Studio
{
    namespace
    {
        struct MessageTypeName
        {
            StudioMessageType type;
            const char* name;
        };

        constexpr std::array<MessageTypeName, 18> kMessageTypeNames{{
            {StudioMessageType::Unknown, "unknown"},
            {StudioMessageType::Hello, "hello"},
            {StudioMessageType::LoadScene, "loadScene"},
            {StudioMessageType::ReloadAsset, "reloadAsset"},
            {StudioMessageType::SetProperty, "setProperty"},
            {StudioMessageType::Pause, "pause"},
            {StudioMessageType::Resume, "resume"},
            {StudioMessageType::StepFrame, "stepFrame"},
            {StudioMessageType::SelectEntity, "selectEntity"},
            {StudioMessageType::Screenshot, "screenshot"},
            {StudioMessageType::Quit, "quit"},
            {StudioMessageType::Input, "input"},
            {StudioMessageType::Ready, "ready"},
            {StudioMessageType::ReportException, "reportException"},
            {StudioMessageType::ReportLog, "reportLog"},
            {StudioMessageType::ReportFrameStats, "reportFrameStats"},
            {StudioMessageType::ScreenshotReady, "screenshotReady"},
            {StudioMessageType::ReportInput, "reportInput"},
        }};
    }

    const char* toString(StudioMessageType type)
    {
        for (const auto& entry : kMessageTypeNames)
        {
            if (entry.type == type) { return entry.name; }
        }
        return "unknown";
    }

    StudioMessageType parseStudioMessageType(std::string_view text)
    {
        for (const auto& entry : kMessageTypeNames)
        {
            if (text == entry.name) { return entry.type; }
        }
        return StudioMessageType::Unknown;
    }

    std::string StudioMessage::encode() const
    {
        JsonValue json = JsonValue::makeObject();
        json.set("type", JsonValue{toString(type)});
        if (requestId != 0) { json.set("requestId", JsonValue{static_cast<std::int64_t>(requestId)}); }
        if (!payload.isNull()) { json.set("payload", payload); }

        // Compact, single-line: the framing is the newline, so the body must not contain one.
        return Json::write(json, false) + "\n";
    }

    std::optional<StudioMessage> StudioMessage::decode(std::string_view line)
    {
        const JsonParseResult parsed = Json::parse(line);
        if (!parsed.succeeded || !parsed.value.isObject()) { return std::nullopt; }

        StudioMessage message;
        message.type = parseStudioMessageType(parsed.value["type"].asString());
        if (message.type == StudioMessageType::Unknown) { return std::nullopt; }

        message.requestId = static_cast<std::uint64_t>(parsed.value["requestId"].asNumber(0.0));
        message.payload = parsed.value["payload"];
        return message;
    }

    StudioMessage StudioMessage::makeHello(const std::string& projectRoot)
    {
        StudioMessage message;
        message.type = StudioMessageType::Hello;
        message.payload = JsonValue::makeObject();
        message.payload.set("protocolVersion", JsonValue{kStudioProtocolVersion});
        message.payload.set("projectRoot", JsonValue{projectRoot});
        return message;
    }

    StudioMessage StudioMessage::makeLoadScene(const std::string& scenePath)
    {
        StudioMessage message;
        message.type = StudioMessageType::LoadScene;
        message.payload = JsonValue::makeObject();
        message.payload.set("scenePath", JsonValue{scenePath});
        return message;
    }

    StudioMessage StudioMessage::makeReloadAsset(const Uuid& assetId)
    {
        StudioMessage message;
        message.type = StudioMessageType::ReloadAsset;
        message.payload = JsonValue::makeObject();
        message.payload.set("assetId", JsonValue{assetId.toString()});
        return message;
    }

    StudioMessage StudioMessage::makeScreenshot(const std::string& path)
    {
        StudioMessage message;
        message.type = StudioMessageType::Screenshot;
        message.payload = JsonValue::makeObject();
        message.payload.set("path", JsonValue{path});
        return message;
    }

    StudioMessage StudioMessage::makeSetProperty(const Uuid& entityId,
                                                 const std::string& componentTypeId,
                                                 const std::string& propertyName,
                                                 const PropertyValue& value)
    {
        StudioMessage message;
        message.type = StudioMessageType::SetProperty;
        message.payload = JsonValue::makeObject();
        message.payload.set("entityId", JsonValue{entityId.toString()});
        message.payload.set("component", JsonValue{componentTypeId});
        message.payload.set("property", JsonValue{propertyName});
        // The type travels with the value here, unlike in a scene file: the player resolves the
        // component's schema from its own registry, which may not match the editor's after a
        // plugin reload, so the wire must be self-describing.
        message.payload.set("valueType", JsonValue{toString(value.getType())});
        message.payload.set("value", value.toJson());
        return message;
    }

    bool PlayerInputSnapshot::isKeyDown(const std::string& key) const
    {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    }

    PlayerInputSnapshot PlayerInputSnapshot::mapToSurface(float width, float height) const
    {
        PlayerInputSnapshot mapped = *this;
        if (!hasPointer() || width <= 0.0f || height <= 0.0f) { return mapped; }

        mapped.mouseX = mouseX * (width / surfaceWidth);
        mapped.mouseY = mouseY * (height / surfaceHeight);
        mapped.surfaceWidth = width;
        mapped.surfaceHeight = height;
        return mapped;
    }

    JsonValue PlayerInputSnapshot::toJson() const
    {
        JsonValue json = JsonValue::makeObject();

        JsonValue held = JsonValue::makeArray();
        for (const std::string& key : keys) { held.append(JsonValue{key}); }
        json.set("keys", held);

        json.set("mouseX", JsonValue{static_cast<double>(mouseX)});
        json.set("mouseY", JsonValue{static_cast<double>(mouseY)});
        json.set("surfaceWidth", JsonValue{static_cast<double>(surfaceWidth)});
        json.set("surfaceHeight", JsonValue{static_cast<double>(surfaceHeight)});
        json.set("left", JsonValue{leftButton});
        json.set("middle", JsonValue{middleButton});
        json.set("right", JsonValue{rightButton});
        json.set("wheel", JsonValue{static_cast<double>(wheel)});
        return json;
    }

    PlayerInputSnapshot PlayerInputSnapshot::fromJson(const JsonValue& value)
    {
        PlayerInputSnapshot snapshot;

        for (const JsonValue& key : value["keys"].getElements())
        {
            // Empty names dropped rather than stored: a held key that is not any key would answer
            // isKeyDown("") true, and "" is what a malformed message yields.
            const std::string name = key.asString();
            if (!name.empty()) { snapshot.keys.push_back(name); }
        }

        snapshot.mouseX = value["mouseX"].asFloat(0.0f);
        snapshot.mouseY = value["mouseY"].asFloat(0.0f);
        snapshot.surfaceWidth = value["surfaceWidth"].asFloat(0.0f);
        snapshot.surfaceHeight = value["surfaceHeight"].asFloat(0.0f);
        snapshot.leftButton = value["left"].asBoolean(false);
        snapshot.middleButton = value["middle"].asBoolean(false);
        snapshot.rightButton = value["right"].asBoolean(false);
        snapshot.wheel = value["wheel"].asFloat(0.0f);
        return snapshot;
    }

    bool operator==(const PlayerInputSnapshot& lhs, const PlayerInputSnapshot& rhs)
    {
        return lhs.keys == rhs.keys && lhs.mouseX == rhs.mouseX && lhs.mouseY == rhs.mouseY
               && lhs.surfaceWidth == rhs.surfaceWidth && lhs.surfaceHeight == rhs.surfaceHeight
               && lhs.leftButton == rhs.leftButton && lhs.middleButton == rhs.middleButton
               && lhs.rightButton == rhs.rightButton && lhs.wheel == rhs.wheel;
    }

    StudioMessage StudioMessage::makeInput(const PlayerInputSnapshot& snapshot)
    {
        StudioMessage message;
        message.type = StudioMessageType::Input;
        message.payload = snapshot.toJson();
        return message;
    }

    StudioMessage StudioMessage::makeReportInput(const PlayerInputSnapshot& snapshot)
    {
        StudioMessage message;
        message.type = StudioMessageType::ReportInput;
        message.payload = snapshot.toJson();
        return message;
    }

    StudioMessage StudioMessage::makeReportLog(const std::string& severity, const std::string& text)
    {
        StudioMessage message;
        message.type = StudioMessageType::ReportLog;
        message.payload = JsonValue::makeObject();
        message.payload.set("severity", JsonValue{severity});
        message.payload.set("text", JsonValue{text});
        return message;
    }

    std::vector<StudioMessage> MessageStreamDecoder::feed(std::string_view bytes)
    {
        buffer_.append(bytes);

        std::vector<StudioMessage> messages;
        std::size_t start = 0;
        while (true)
        {
            const std::size_t newline = buffer_.find('\n', start);
            if (newline == std::string::npos) { break; }

            std::string_view line{buffer_.data() + start, newline - start};
            if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }

            if (!line.empty())
            {
                if (std::optional<StudioMessage> message = StudioMessage::decode(line))
                {
                    messages.push_back(std::move(*message));
                }
                else
                {
                    // A malformed line is counted and skipped rather than closing the connection:
                    // a peer from a newer revision sending something unrecognised must not be able
                    // to kill a play session.
                    ++droppedCount_;
                }
            }
            start = newline + 1;
        }

        buffer_.erase(0, start);
        return messages;
    }

    void MessageStreamDecoder::reset()
    {
        buffer_.clear();
    }
}

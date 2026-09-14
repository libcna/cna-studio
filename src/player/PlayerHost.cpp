// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Player/PlayerHost.hpp"

#include <algorithm>

#include "CNA/Studio/Scene/BuiltinComponents.hpp"

namespace CNA::Studio
{
    const char* toString(PlayState state)
    {
        switch (state)
        {
            case PlayState::Running: return "running";
            case PlayState::Paused: return "paused";
            case PlayState::Stopping: return "stopping";
        }
        return "running";
    }

    PlayerHost::PlayerHost()
    {
        registerBuiltinComponents(components_);
    }

    PlayerHost::~PlayerHost() = default;

    bool PlayerHost::openProject(const std::string& projectPath)
    {
        const ProjectLoadResult loaded = project_.loadFromFile(projectPath);
        if (!loaded.succeeded)
        {
            error_ = loaded.errorMessage;
            return false;
        }

        assets_.setProjectRoot(project_.getRootPath());
        assets_.scan(project_.getAssetDirectory());

        // The startup scene is loaded eagerly so that a player launched without an explicit
        // LoadScene still shows the game rather than an empty window.
        if (project_.getKind() == ProjectKind::CnaNative && !project_.getStartupScene().empty())
        {
            const std::string path = project_.resolvePath(project_.getStartupScene());
            if (scene_.loadFromFile(path, components_).succeeded) { scenePath_ = path; }
        }
        return true;
    }

    StudioMessage PlayerHost::makeScreenshotReply(const ScreenshotRequest& request,
                                                  const std::string& errorMessage)
    {
        StudioMessage reply;
        reply.type = StudioMessageType::ScreenshotReady;
        reply.requestId = request.requestId;
        reply.payload = JsonValue::makeObject();
        reply.payload.set("path", JsonValue{request.path});

        // Both fields, always. A reply carrying only a path would make "written" something the
        // editor has to infer from whether the file happens to be there, which is a different
        // question with its own answers -- a stale file from a previous run, for one.
        reply.payload.set("written", JsonValue{errorMessage.empty()});
        if (!errorMessage.empty()) { reply.payload.set("error", JsonValue{errorMessage}); }
        return reply;
    }

    StudioMessage PlayerHost::makeReady(const std::string& backendName) const
    {
        StudioMessage message;
        message.type = StudioMessageType::Ready;
        message.payload = JsonValue::makeObject();
        message.payload.set("protocolVersion", JsonValue{kStudioProtocolVersion});
        // The backend is what the editor most needs to know: it is fixed at compile time, so this
        // is the only way the editor learns which build it actually got hold of.
        message.payload.set("backend", JsonValue{backendName});
        message.payload.set("project", JsonValue{project_.getName()});
        message.payload.set("scene", JsonValue{scene_.getName()});
        message.payload.set("entityCount", JsonValue{static_cast<std::int64_t>(scene_.getEntityCount())});
        return message;
    }

    void PlayerHost::handle(const StudioMessage& message, Outbox& outbox)
    {
        switch (message.type)
        {
            case StudioMessageType::Hello: {
                const int peerVersion = message.payload["protocolVersion"].asInt(0);
                if (peerVersion != kStudioProtocolVersion)
                {
                    // A version mismatch is reported and the session continues in a degraded
                    // state rather than being torn down: the editor can then tell the user which
                    // build to rebuild, which it could not do from a silently dropped connection.
                    outbox.push_back(StudioMessage::makeReportLog(
                        "error", "protocol version mismatch: Studio speaks " + std::to_string(peerVersion)
                                     + ", this player speaks " + std::to_string(kStudioProtocolVersion)));
                }
                else
                {
                    handshakeComplete_ = true;
                }
                break;
            }

            case StudioMessageType::LoadScene:
                handleLoadScene(message, outbox);
                break;

            case StudioMessageType::SetProperty:
                handleSetProperty(message, outbox);
                break;

            case StudioMessageType::ReloadAsset:
                handleReloadAsset(message, outbox);
                break;

            case StudioMessageType::Pause:
                playState_ = PlayState::Paused;
                pendingSteps_ = 0;
                outbox.push_back(StudioMessage::makeReportLog("info", "paused"));
                break;

            case StudioMessageType::Resume:
                playState_ = PlayState::Running;
                pendingSteps_ = 0;
                outbox.push_back(StudioMessage::makeReportLog("info", "resumed"));
                break;

            case StudioMessageType::StepFrame:
                // Stepping while already running is meaningless, and honouring it would make the
                // game jump a frame ahead of where the user is looking.
                if (playState_ == PlayState::Paused) { ++pendingSteps_; }
                break;

            case StudioMessageType::SelectEntity:
                highlightedEntity_ = Uuid::parse(message.payload["entityId"].asString());
                break;

            case StudioMessageType::Input:
                handleInput(message, outbox);
                break;

            case StudioMessageType::Quit:
                playState_ = PlayState::Stopping;
                break;

            case StudioMessageType::Screenshot: {
                // Queued, not answered. The capture belongs to the CNA-linked main loop, which owns
                // the device and the back buffer; replying here would tell the editor a file exists
                // before anything had been written to it, and nothing on the wire would ever
                // correct that. The loop drains the queue and answers each request with
                // makeScreenshotReply -- including a build with no graphics, which answers that it
                // cannot capture rather than leaving the editor waiting.
                const std::string path = message.payload["path"].asString();
                if (path.empty())
                {
                    outbox.push_back(
                        StudioMessage::makeReportLog("error", "screenshot without a path"));
                    break;
                }

                screenshotRequests_.push_back(ScreenshotRequest{message.requestId, path});
                break;
            }

            // Player-to-editor messages: a peer sending one of these is confused, but saying so
            // is more useful than silently ignoring it.
            case StudioMessageType::Ready:
            case StudioMessageType::ReportException:
            case StudioMessageType::ReportLog:
            case StudioMessageType::ReportFrameStats:
            case StudioMessageType::ScreenshotReady:
            case StudioMessageType::ReportInput:
            case StudioMessageType::Unknown:
                outbox.push_back(StudioMessage::makeReportLog(
                    "warn", std::string{"ignoring unexpected message '"} + toString(message.type) + "'"));
                break;
        }
    }

    void PlayerHost::setSurfaceSize(int width, int height)
    {
        surfaceWidth_ = static_cast<float>(std::max(0, width));
        surfaceHeight_ = static_cast<float>(std::max(0, height));
    }

    void PlayerHost::handleInput(const StudioMessage& message, Outbox& outbox)
    {
        const PlayerInputSnapshot sent = PlayerInputSnapshot::fromJson(message.payload);

        // Mapped here rather than at the editor, because the editor does not know how big this
        // window is -- and cannot, since the player may have been resized since it was launched.
        input_ = surfaceWidth_ > 0.0f ? sent.mapToSurface(surfaceWidth_, surfaceHeight_) : sent;

        // Answered every time rather than only on a change. The editor sends only when something
        // moved, so the traffic is already bounded, and an unanswered forward would leave the
        // editor unable to tell "the player ignored it" from "the player has not run a frame yet".
        StudioMessage reply = StudioMessage::makeReportInput(input_);
        reply.requestId = message.requestId;
        outbox.push_back(std::move(reply));
    }

    void PlayerHost::handleLoadScene(const StudioMessage& message, Outbox& outbox)
    {
        const std::string relativePath = message.payload["scenePath"].asString();
        if (relativePath.empty())
        {
            outbox.push_back(StudioMessage::makeReportLog("error", "loadScene without a scenePath"));
            return;
        }

        const std::string path = project_.resolvePath(relativePath);
        const SceneLoadResult loaded = scene_.loadFromFile(path, components_);
        if (!loaded.succeeded)
        {
            outbox.push_back(StudioMessage::makeReportLog("error", "cannot load scene: " + loaded.errorMessage));
            return;
        }

        scenePath_ = path;
        highlightedEntity_ = Uuid{};
        for (const std::string& warning : loaded.warnings)
        {
            outbox.push_back(StudioMessage::makeReportLog("warn", warning));
        }
        outbox.push_back(StudioMessage::makeReportLog(
            "info", "loaded scene '" + scene_.getName() + "' with "
                        + std::to_string(scene_.getEntityCount()) + " entities"));
    }

    void PlayerHost::handleReloadAsset(const StudioMessage& message, Outbox& outbox)
    {
        const Uuid assetId = Uuid::parse(message.payload["assetId"].asString());

        // Rescan first, then look the id up. The editor sends this because a file changed, and a
        // record still carrying the old size and timestamp would make the very next scan think the
        // asset had changed again.
        const AssetScanResult scanned = assets_.scan(project_.getAssetDirectory());
        if (!scanned.succeeded)
        {
            outbox.push_back(StudioMessage::makeReportLog(
                "error", "cannot rescan assets: " + scanned.errorMessage));
            return;
        }

        // A nil id means "everything", which is what a project-wide change is best reported as
        // rather than as one message per asset.
        if (!assetId.isValid())
        {
            outbox.push_back(StudioMessage::makeReportLog(
                "info", "rescanned " + std::to_string(assets_.getCount()) + " assets"));
            return;
        }

        const AssetRecord* record = assets_.find(assetId);
        if (record == nullptr)
        {
            // Not fatal. The editor and the player scan the same directory but not necessarily at
            // the same moment, and an asset the player has not seen yet is a timing difference,
            // not a broken session.
            outbox.push_back(StudioMessage::makeReportLog(
                "warn", "asked to reload unknown asset " + assetId.toString()));
            return;
        }

        // Recorded once however many times it changes before the graphics half drains the list:
        // dropping a cache entry twice achieves nothing, and this is what keeps the list bounded by
        // the number of distinct assets rather than by the length of the session.
        if (std::find(reloadedAssets_.begin(), reloadedAssets_.end(), assetId) == reloadedAssets_.end())
        {
            reloadedAssets_.push_back(assetId);
        }
        outbox.push_back(StudioMessage::makeReportLog("info", "reloaded '" + record->sourcePath + "'"));
    }

    void PlayerHost::handleSetProperty(const StudioMessage& message, Outbox& outbox)
    {
        const Uuid entityId = Uuid::parse(message.payload["entityId"].asString());
        const std::string componentTypeId = message.payload["component"].asString();
        const std::string propertyName = message.payload["property"].asString();

        StudioEntity* entity = scene_.findEntity(entityId);
        if (entity == nullptr)
        {
            outbox.push_back(StudioMessage::makeReportLog(
                "warn", "setProperty for unknown entity " + entityId.toString()));
            return;
        }

        StudioComponent* component = entity->findComponent(componentTypeId);
        if (component == nullptr)
        {
            outbox.push_back(StudioMessage::makeReportLog(
                "warn", "setProperty for missing component '" + componentTypeId + "' on '"
                            + entity->getName() + "'"));
            return;
        }

        // The wire carries the value's type, unlike a scene file, precisely because the player's
        // registry may not match the editor's after a plugin reload. Trusting the wire here is
        // what makes live editing survive that mismatch.
        const PropertyType type = parsePropertyType(message.payload["valueType"].asString());
        component->setProperty(propertyName, PropertyValue::fromJson(message.payload["value"], type));

        // Trace, not info, because a gizmo drag produces one of these per frame -- and the console
        // has a severity filter precisely so this can be turned off. It earns its place because
        // when a live edit does not take effect, this line is the only thing that distinguishes
        // "Studio never sent it" from "the player would not apply it", which is exactly the
        // question live editing raises.
        outbox.push_back(StudioMessage::makeReportLog(
            "trace", "set " + entity->getName() + "." + componentTypeId + "." + propertyName));
    }

    bool PlayerHost::tick()
    {
        if (playState_ == PlayState::Stopping) { return false; }

        if (playState_ == PlayState::Paused)
        {
            if (pendingSteps_ <= 0) { return false; }
            --pendingSteps_;
        }

        ++frameCount_;
        return true;
    }
}

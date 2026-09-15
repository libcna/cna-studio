// SPDX-License-Identifier: MS-PL
/**
 * @file StudioVisualQualityTests.cpp
 * @brief The properties that make the Studio shell read as a professional tool rather than a
 *        debug window.
 *
 * `plan.md` Phase 35, the CNA Studio Visual Quality 1.0 workstream.
 *
 * ### Why these are tests and not screenshots
 *
 * A screenshot catches a frame that changed and says nothing about *why* it changed or whether the
 * change was the intended one. The properties below are the ones a reviewer would otherwise have to
 * check by eye on every theme, every scale and every panel: that a layered UI actually has layers,
 * that a colour coding is consistent with the gizmo it teaches, that an icon distinguishes the
 * thing it names. Each is cheap, deterministic, and fails with a sentence rather than with a diff.
 *
 * The golden images stay, and they catch what these cannot: that the pixels are where the layout
 * says they are.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/UiCore/StudioIcons.hpp"
#include "CNA/Studio/UiCore/StudioDrawList.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief The perceived lightness of a colour, 0..255, weighted as the eye weights it. */
    float luminance(const StudioColor& colour)
    {
        return 0.2126f * static_cast<float>(colour.r)
             + 0.7152f * static_cast<float>(colour.g)
             + 0.0722f * static_cast<float>(colour.b);
    }

    /** @brief How far apart two colours are, as the largest channel difference. */
    int channelDistance(const StudioColor& a, const StudioColor& b)
    {
        return std::max({std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)),
                         std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)),
                         std::abs(static_cast<int>(a.b) - static_cast<int>(b.b))});
    }

    /**
     * @brief An entity with a transform and, optionally, one more component.
     *
     * A transform on every one, because that is what an entity in a scene has and because the
     * icon rule under test is specifically "a transform and nothing else is still an entity".
     */
    StudioEntity entityWith(std::string name, const char* componentTypeId)
    {
        StudioEntity entity{Uuid::generate(), std::move(name)};
        entity.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
        if (componentTypeId != nullptr)
        {
            entity.addComponent(StudioComponent{componentTypeId});
        }
        return entity;
    }

    /** @brief Both shipped themes, so nothing below is only true of the dark one. */
    std::vector<StudioTheme> bothThemes()
    {
        return {StudioTheme::dark(), StudioTheme::light()};
    }
}

// ------------------------------------------------------------------------------------------------
// Layering (STUDIO-35020, STUDIO-35021)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheBackgroundLayersAreActuallyDistinctFromOneAnother)
{
    // The failure this catches is the one the whole workstream started from: a dark UI whose
    // panel, tab strip, tab and chrome are all within a value or two of each other, so nothing
    // reads as sitting on anything. It is invisible in the tokens -- rgb(32,34,38) and
    // rgb(34,36,40) look like two decisions -- and obvious the moment somebody looks at it.
    for (const StudioTheme& theme : bothThemes())
    {
        const StudioColor panel = theme.color(StudioColorRole::PanelBackground);
        const StudioColor strip = theme.color(StudioColorRole::TabStripBackground);
        const StudioColor inactive = theme.color(StudioColorRole::TabInactive);
        const StudioColor active = theme.color(StudioColorRole::PanelHeaderActive);
        const StudioColor chrome = theme.color(StudioColorRole::WindowChrome);

        // A tab strip is a recess: below the panel it sits beside and below its own tabs.
        CNA_STUDIO_EXPECT(luminance(strip) < luminance(panel));
        CNA_STUDIO_EXPECT(luminance(strip) < luminance(inactive));
        // And the active tab is above the inactive ones, which is what "active" looks like.
        CNA_STUDIO_EXPECT(luminance(active) > luminance(inactive));

        // Four pairs that must be separable. Four values is about where a step stops being
        // visible on an LCD at a normal brightness; below that the layering is notional.
        CNA_STUDIO_EXPECT(channelDistance(strip, panel) >= 4);
        CNA_STUDIO_EXPECT(channelDistance(strip, inactive) >= 4);
        CNA_STUDIO_EXPECT(channelDistance(inactive, active) >= 4);
        CNA_STUDIO_EXPECT(channelDistance(chrome, panel) >= 3);
    }
}

CNA_STUDIO_TEST(APanelsOutlineIsDarkerThanEitherPanelItDivides)
{
    // Lighter would read as a highlight -- as though something were raised along that edge -- and
    // a workspace whose every seam is raised is busier than one made of seams. In the light theme
    // the same rule holds for the same reason: an outline lighter than the panels would vanish.
    for (const StudioTheme& theme : bothThemes())
    {
        const StudioColor outline = theme.color(StudioColorRole::PanelOutline);
        CNA_STUDIO_EXPECT(luminance(outline) < luminance(theme.color(StudioColorRole::PanelBackground)));
        CNA_STUDIO_EXPECT(luminance(outline) < luminance(theme.color(StudioColorRole::WindowChrome)));
        // And it has to be visible against what it divides, or it is a token nothing shows.
        CNA_STUDIO_EXPECT(
            channelDistance(outline, theme.color(StudioColorRole::PanelBackground)) >= 8);
    }
}

CNA_STUDIO_TEST(TheAlternatingRowFillIsVisibleAndIsNotAStripe)
{
    // Two failures with one test, because they are the two ends of one judgement. Too close and
    // the stripe does nothing; too far and a list looks like a 1990s table. The window is narrow
    // and stating it is what stops somebody "fixing" it in either direction.
    for (const StudioTheme& theme : bothThemes())
    {
        const int distance = channelDistance(theme.color(StudioColorRole::RowAlternate),
                                             theme.color(StudioColorRole::PanelBackground));
        CNA_STUDIO_EXPECT(distance >= 3);
        CNA_STUDIO_EXPECT(distance <= 12);

        // Hover has to beat the stripe, or half a list's rows would not respond to the pointer.
        CNA_STUDIO_EXPECT(channelDistance(theme.color(StudioColorRole::RowHover),
                                          theme.color(StudioColorRole::PanelBackground))
                          > distance);
    }
}

// ------------------------------------------------------------------------------------------------
// Axis colour coding (STUDIO-35032)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheThreeAxisColoursAreTellableApartAndAreRedGreenBlueInOrder)
{
    // Red, green, blue in axis order is the convention every 3D tool shares, and a tool that chose
    // differently would be asking its users to unlearn something true everywhere else. Asserted as
    // "each axis's own channel dominates" rather than against exact values, so the colours can be
    // retuned for contrast without the test becoming a copy of the theme.
    for (const StudioTheme& theme : bothThemes())
    {
        const StudioColor x = theme.color(StudioColorRole::AxisX);
        const StudioColor y = theme.color(StudioColorRole::AxisY);
        const StudioColor z = theme.color(StudioColorRole::AxisZ);
        const StudioColor w = theme.color(StudioColorRole::AxisW);

        CNA_STUDIO_EXPECT(x.r > x.g && x.r > x.b);
        CNA_STUDIO_EXPECT(y.g > y.r && y.g > y.b);
        CNA_STUDIO_EXPECT(z.b > z.r && z.b > z.g);

        // W is a fourth component rather than a fourth axis, and is deliberately neutral: a
        // coloured W beside a coloured Z would read as a direction it does not have.
        CNA_STUDIO_EXPECT(channelDistance(w, StudioColor{w.r, w.r, w.r, w.a}) <= 16);

        // And no two of them may be confusable, which is the whole point of colouring them.
        CNA_STUDIO_EXPECT(channelDistance(x, y) >= 48);
        CNA_STUDIO_EXPECT(channelDistance(y, z) >= 48);
        CNA_STUDIO_EXPECT(channelDistance(x, z) >= 48);
    }
}

// ------------------------------------------------------------------------------------------------
// Iconography (STUDIO-35030)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AnOutlinerRowSaysWhatItIsFromWhatItCarries)
{
    // The first thing anybody looks for in an outliner is which row is the camera. Reading that
    // off a detail column of component names is reading rather than scanning, and scanning is what
    // an outliner is for.
    SceneDocument scene;

    const Uuid empty = scene.addEntity(entityWith("Empty", nullptr));
    const Uuid camera = scene.addEntity(entityWith("Main Camera", BuiltinComponentIds::kCamera));
    const Uuid light = scene.addEntity(entityWith("Key Light", BuiltinComponentIds::kLight));
    const Uuid model = scene.addEntity(entityWith("Crate", BuiltinComponentIds::kModelRenderer));
    const Uuid sprite = scene.addEntity(entityWith("Player", BuiltinComponentIds::kSpriteRenderer));

    StudioTreeState state;
    const std::vector<StudioTreeRow> rows = studioOutlinerRows(scene, {}, state);

    const auto iconOf = [&rows](const Uuid& id) {
        for (const StudioTreeRow& row : rows)
        {
            if (row.id == id.toString()) { return row.icon; }
        }
        return StudioIcon::None;
    };

    CNA_STUDIO_EXPECT(iconOf(camera) == StudioIcon::Camera);
    CNA_STUDIO_EXPECT(iconOf(light) == StudioIcon::Light);
    CNA_STUDIO_EXPECT(iconOf(model) == StudioIcon::Mesh);
    CNA_STUDIO_EXPECT(iconOf(sprite) == StudioIcon::Sprite);

    // An entity with nothing on it is still an entity and still gets a picture. A blank where
    // every other row has one reads as a row that failed to load rather than as an empty.
    CNA_STUDIO_EXPECT(iconOf(empty) == StudioIcon::Entity);

    // And every row has one, which is the property that makes the column worth its width.
    for (const StudioTreeRow& row : rows)
    {
        CNA_STUDIO_EXPECT(row.icon != StudioIcon::None);
    }
}

CNA_STUDIO_TEST(ACameraWithALightOnItIsACamera)
{
    // Ordered by how much the answer tells a user rather than by how common the component is. An
    // entity carrying both is what somebody was looking for when they scanned for the camera.
    SceneDocument scene;
    StudioEntity entity = entityWith("Sun Camera", BuiltinComponentIds::kLight);
    entity.addComponent(StudioComponent{BuiltinComponentIds::kCamera});
    scene.addEntity(std::move(entity));

    StudioTreeState state;
    CNA_STUDIO_EXPECT(studioOutlinerRows(scene, {}, state).front().icon == StudioIcon::Camera);
}

CNA_STUDIO_TEST(EveryIconHasAUniqueNameThatRoundTripsThroughText)
{
    // The names are what a plugin manifest and a saved toolbar would name an icon by, so a
    // collision is two commands that cannot be told apart in a file. Checked over the whole set
    // rather than over the ones added today, because the set is what has to stay consistent.
    std::set<std::string> names;
    for (std::size_t i = 0; i < static_cast<std::size_t>(StudioIcon::Count); ++i)
    {
        const auto icon = static_cast<StudioIcon>(i);
        const std::string name{studioIconName(icon)};
        CNA_STUDIO_EXPECT(!name.empty());
        CNA_STUDIO_EXPECT(names.insert(name).second);

        StudioIcon parsed = StudioIcon::None;
        CNA_STUDIO_EXPECT(parseStudioIcon(name, parsed));
        CNA_STUDIO_EXPECT(parsed == icon);
    }
}

// ------------------------------------------------------------------------------------------------
// Every primitive has to reach the GPU (STUDIO-35033)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryPrimitiveTheDrawListEmitsNamesADrawableTexture)
{
    // The defect this closes: `drawLine`'s diagonal branch emitted its command against
    // `kUiTextureNone`, and both UI render backends skip a command whose texture id they cannot
    // resolve -- because a command naming a texture that was never created would otherwise sample
    // whatever happens to be bound. So every diagonal line in Studio was dropped on a real device.
    //
    // It survived because almost nothing drew one. It surfaced the day an icon set made of
    // diagonals arrived, as icons that came out as their axis-aligned parts alone: an isometric
    // cube drew as three bars.
    //
    // And the software rasterizer drew them correctly the whole time, so no headless capture
    // showed it. That is the failure mode a preview harness has -- it is a second implementation,
    // and the two agreeing is the thing being tested rather than a given.
    StudioFrame frame;
    frame.setTheme(StudioTheme::dark());

    UiInputState input;
    input.displayWidth = 320.0f;
    input.displayHeight = 240.0f;
    frame.beginFrame(input);
    frame.beginInput();
    frame.beginDraw();

    StudioDrawList& list = frame.drawList();
    const StudioColor colour = frame.theme().color(StudioColorRole::TextPrimary);

    // Standing in for the font atlas, which is what a real shell binds here: an id and the
    // coordinates of the reserved opaque white texel every untextured primitive samples. Without
    // one the default *is* kUiTextureNone -- which is correct for a draw list nobody will render,
    // and is exactly the state in which this assertion would say nothing.
    constexpr UiTextureId kAtlas = 7;
    list.setDefaultTexture(kAtlas, 0.5f, 0.5f);

    list.fillRect(UiRect{10.0f, 10.0f, 40.0f, 20.0f}, colour);
    list.drawLine(10.0f, 40.0f, 60.0f, 40.0f, colour, 2.0f);   // horizontal
    list.drawLine(10.0f, 50.0f, 10.0f, 90.0f, colour, 2.0f);   // vertical
    list.drawLine(20.0f, 100.0f, 80.0f, 160.0f, colour, 2.0f); // diagonal
    list.fillTriangle(100.0f, 10.0f, 140.0f, 10.0f, 120.0f, 50.0f, colour);
    list.strokeRect(UiRect{150.0f, 10.0f, 40.0f, 40.0f}, colour, 1.0f);

    frame.endFrame();

    const UiDrawData& data = frame.drawData();
    std::size_t commands = 0;
    for (const UiDrawList& drawList : data.lists)
    {
        for (const UiDrawCommand& command : drawList.commands)
        {
            if (command.indexCount == 0) { continue; }
            ++commands;
            // The whole assertion. A backend resolves this id or drops the command, and dropping
            // it is silent -- no warning, no missing-texture pink, just geometry that is not there.
            CNA_STUDIO_EXPECT(command.texture != kUiTextureNone);
            CNA_STUDIO_EXPECT_EQ(command.texture, kAtlas);
        }
    }
    CNA_STUDIO_EXPECT(commands > 0);
}

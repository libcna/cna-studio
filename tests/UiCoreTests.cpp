// SPDX-License-Identifier: MS-PL
/**
 * @file UiCoreTests.cpp
 * @brief Tests for the CNA Studio UI foundations: design tokens, widget identity, retained state.
 *
 * Everything here runs with no window, no GPU and no CNA. That is the property Phase 3 exists to
 * have: if the UI's identity, styling and state can only be checked by looking at a screenshot,
 * they will not be checked.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/WidgetId.hpp"
#include "CNA/Studio/UiCore/WidgetStateStore.hpp"

#include <set>
#include <string>

using namespace CNA::Studio;

// ------------------------------------------------------------------------------------------------
// Design tokens (STUDIO-03004, STUDIO-03005, STUDIO-03006)
// ------------------------------------------------------------------------------------------------

// The point of making tokens enumerable rather than struct members: a theme that forgot a role is
// caught by iteration, on the commit that added the role, instead of by someone noticing a magenta
// rectangle in a screenshot months later.
CNA_STUDIO_TEST(BothShippedThemesDefineEveryColourRole)
{
    for (const StudioTheme& theme : {StudioTheme::dark(), StudioTheme::light()})
    {
        StudioColorRole missing{};
        const bool complete = theme.isComplete(&missing);
        if (!complete)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{theme.name()} + " leaves role '"
                + std::string{studioColorRoleName(missing)} + "' at the placeholder");
        }
        CNA_STUDIO_EXPECT(complete);
    }
}

CNA_STUDIO_TEST(ADefaultConstructedThemeIsObviouslyUnconfigured)
{
    // Silence would be the wrong failure mode here: a theme nobody configured must be impossible
    // to mistake for a designed one.
    const StudioTheme theme;
    CNA_STUDIO_EXPECT(!theme.isComplete());
    CNA_STUDIO_EXPECT(theme.color(StudioColorRole::PanelBackground) == kStudioPlaceholderColor);
}

CNA_STUDIO_TEST(EveryTokenHasAStableName)
{
    // Names are written into preferences and diagnostics, so a token with no name is a token whose
    // value cannot be round-tripped.
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(StudioColorRole::Count); ++i)
    {
        CNA_STUDIO_EXPECT(!studioColorRoleName(static_cast<StudioColorRole>(i)).empty());
    }
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(StudioMetric::Count); ++i)
    {
        CNA_STUDIO_EXPECT(!studioMetricName(static_cast<StudioMetric>(i)).empty());
    }
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(StudioFontRole::Count); ++i)
    {
        CNA_STUDIO_EXPECT(!studioFontRoleName(static_cast<StudioFontRole>(i)).empty());
    }
}

CNA_STUDIO_TEST(TokenNamesAreUnique)
{
    std::set<std::string_view> seen;
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(StudioColorRole::Count); ++i)
    {
        CNA_STUDIO_EXPECT(seen.insert(studioColorRoleName(static_cast<StudioColorRole>(i))).second);
    }
}

CNA_STUDIO_TEST(EveryMetricHasANonZeroDefault)
{
    const StudioTheme theme = StudioTheme::dark();
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(StudioMetric::Count); ++i)
    {
        const auto metric = static_cast<StudioMetric>(i);
        if (theme.logicalMetric(metric) <= 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "metric '" + std::string{studioMetricName(metric)} + "' has no default");
        }
        CNA_STUDIO_EXPECT(theme.logicalMetric(metric) > 0);
    }
}

CNA_STUDIO_TEST(MetricsScaleWithDpiAndLogicalValuesDoNot)
{
    StudioTheme theme = StudioTheme::dark();
    const int logical = theme.logicalMetric(StudioMetric::ControlHeight);

    theme.setScale(2.0f);
    CNA_STUDIO_EXPECT_EQ(theme.metric(StudioMetric::ControlHeight), logical * 2);
    // The authored value is the source of truth and must not drift as the scale changes.
    CNA_STUDIO_EXPECT_EQ(theme.logicalMetric(StudioMetric::ControlHeight), logical);

    theme.setScale(1.0f);
    CNA_STUDIO_EXPECT_EQ(theme.metric(StudioMetric::ControlHeight), logical);
}

CNA_STUDIO_TEST(MetricsAreCorrectAtEveryDpiScaleStudioSupports)
{
    StudioTheme theme = StudioTheme::dark();
    const int logical = theme.logicalMetric(StudioMetric::RowHeight);

    for (const float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f})
    {
        theme.setScale(scale);
        const int expected = static_cast<int>(std::lround(static_cast<float>(logical) * scale));
        CNA_STUDIO_EXPECT_EQ(theme.metric(StudioMetric::RowHeight), expected);
    }
}

CNA_STUDIO_TEST(HairlineMetricsNeverRoundAwayToNothing)
{
    // A 1px border at 0.5 scale rounds to 0 and the control silently loses its outline. This is
    // the clamp that prevents it, and it is worth a test because the failure is invisible in code
    // review and only shows up on an unusual display.
    StudioTheme theme = StudioTheme::dark();
    theme.setScale(0.5f);

    CNA_STUDIO_EXPECT(theme.metric(StudioMetric::BorderWidth) >= 1);
    CNA_STUDIO_EXPECT(theme.metric(StudioMetric::SeparatorThickness) >= 1);
    CNA_STUDIO_EXPECT(theme.metric(StudioMetric::FocusRingWidth) >= 1);
}

CNA_STUDIO_TEST(AnAbsurdDpiScaleIsClampedRatherThanProducingABrokenWindow)
{
    StudioTheme theme = StudioTheme::dark();
    theme.setScale(0.0f);
    CNA_STUDIO_EXPECT(theme.scale() > 0.0f);
    theme.setScale(-3.0f);
    CNA_STUDIO_EXPECT(theme.scale() > 0.0f);
    theme.setScale(1000.0f);
    CNA_STUDIO_EXPECT(theme.scale() <= 4.0f);
}

CNA_STUDIO_TEST(FontSizesScaleWithDpi)
{
    StudioTheme theme = StudioTheme::dark();
    const float base = theme.font(StudioFontRole::Body).sizePx;
    theme.setScale(2.0f);
    CNA_STUDIO_EXPECT_EQ(theme.font(StudioFontRole::Body).sizePx, base * 2.0f);
}

CNA_STUDIO_TEST(EveryInteractiveStateResolvesToADistinctControlBackground)
{
    // The reason states are real tokens rather than "base colour, 8% lighter": a computed hover is
    // invisible on a dark panel and garish on an accent. If two states ever resolve to the same
    // colour the user cannot tell them apart, which is the whole failure this guards.
    const StudioTheme theme = StudioTheme::dark();
    std::set<std::uint32_t> packed;
    for (const StudioControlState state : {StudioControlState::Normal, StudioControlState::Hover,
                                           StudioControlState::Pressed, StudioControlState::Selected,
                                           StudioControlState::Disabled})
    {
        const StudioColor c = theme.controlBackground(state);
        const auto key = static_cast<std::uint32_t>((c.r << 24) | (c.g << 16) | (c.b << 8) | c.a);
        CNA_STUDIO_EXPECT(packed.insert(key).second);
    }
}

CNA_STUDIO_TEST(ADisabledControlNeverDrawsInTheAccent)
{
    // The accent means "this does something". A disabled control does not.
    const StudioTheme theme = StudioTheme::dark();
    CNA_STUDIO_EXPECT(theme.accent(StudioControlState::Disabled) != theme.color(StudioColorRole::Accent));
    CNA_STUDIO_EXPECT(theme.controlText(StudioControlState::Disabled)
                      == theme.color(StudioColorRole::TextDisabled));
}

CNA_STUDIO_TEST(FocusAndSelectionAreDistinguishable)
{
    // A focused row inside a selection must be visibly both. Making the focus ring the accent --
    // which is also the selection fill -- would make that impossible on exactly the row where it
    // matters most.
    for (const StudioTheme& theme : {StudioTheme::dark(), StudioTheme::light()})
    {
        CNA_STUDIO_EXPECT(theme.color(StudioColorRole::FocusRing)
                          != theme.color(StudioColorRole::Selection));
    }
}

CNA_STUDIO_TEST(AThemeIsAValueThatCanBeCustomisedByCopying)
{
    const StudioTheme base = StudioTheme::dark();
    StudioTheme custom = base;
    custom.setName("Custom");
    custom.setColor(StudioColorRole::Accent, StudioColor{10, 20, 30, 255});

    CNA_STUDIO_EXPECT(custom.color(StudioColorRole::Accent) != base.color(StudioColorRole::Accent));
    CNA_STUDIO_EXPECT(custom.isComplete());
    CNA_STUDIO_EXPECT_EQ(std::string{base.name()}, std::string{"CNA Studio Dark"});
}

CNA_STUDIO_TEST(TheTwoShippedThemesAreActuallyDifferent)
{
    const StudioTheme dark = StudioTheme::dark();
    const StudioTheme light = StudioTheme::light();
    CNA_STUDIO_EXPECT(dark.color(StudioColorRole::PanelBackground)
                      != light.color(StudioColorRole::PanelBackground));
    CNA_STUDIO_EXPECT(dark.color(StudioColorRole::TextPrimary)
                      != light.color(StudioColorRole::TextPrimary));
}

CNA_STUDIO_TEST(TextIsReadableAgainstItsOwnBackground)
{
    // Not a full contrast-ratio audit -- that is STUDIO-32003 -- but a floor: if body text and the
    // panel behind it ever converge, the tool is unusable and no screenshot test would phrase the
    // failure as clearly as this does.
    for (const StudioTheme& theme : {StudioTheme::dark(), StudioTheme::light()})
    {
        const StudioColor text = theme.color(StudioColorRole::TextPrimary);
        const StudioColor background = theme.color(StudioColorRole::PanelBackground);
        const int delta = std::abs(static_cast<int>(text.r) - static_cast<int>(background.r))
                        + std::abs(static_cast<int>(text.g) - static_cast<int>(background.g))
                        + std::abs(static_cast<int>(text.b) - static_cast<int>(background.b));
        CNA_STUDIO_EXPECT(delta > 250);
    }
}

// ------------------------------------------------------------------------------------------------
// Widget identity (STUDIO-03002)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheSameWidgetKeepsItsIdAcrossFrames)
{
    WidgetIdStack ids;
    ids.beginFrame();
    ids.push("Hierarchy");
    const WidgetId first = ids.make("Delete");
    ids.pop();

    ids.beginFrame();
    ids.push("Hierarchy");
    const WidgetId second = ids.make("Delete");
    ids.pop();

    CNA_STUDIO_EXPECT(first == second);
    CNA_STUDIO_EXPECT(first.isValid());
}

CNA_STUDIO_TEST(TheSameLabelInTwoScopesIsTwoWidgets)
{
    // This is what makes labels reusable: "Delete" in the Hierarchy and "Delete" in the Content
    // Browser are different buttons without either having to invent unique visible text.
    WidgetIdStack ids;
    ids.beginFrame();
    ids.push("Hierarchy");
    const WidgetId inHierarchy = ids.make("Delete");
    ids.pop();
    ids.push("ContentBrowser");
    const WidgetId inBrowser = ids.make("Delete");
    ids.pop();

    CNA_STUDIO_EXPECT(inHierarchy != inBrowser);
}

CNA_STUDIO_TEST(AWidgetIdentifiedByAStableKeySurvivesInsertionAbroveIt)
{
    // The bug this prevents: identity from iteration position. Insert a row above row three and,
    // with positional ids, row three inherits row four's scroll offset and half-typed text.
    const auto idForEntity = [](WidgetIdStack& ids, std::string_view uuid) {
        ids.push("Outliner");
        ids.push(uuid);
        const WidgetId id = ids.make("name");
        ids.pop();
        ids.pop();
        return id;
    };

    WidgetIdStack ids;
    ids.beginFrame();
    const WidgetId before = idForEntity(ids, "entity-c");

    // Next frame, two entities have been inserted ahead of it in the list.
    ids.beginFrame();
    (void) idForEntity(ids, "entity-new-1");
    (void) idForEntity(ids, "entity-new-2");
    const WidgetId after = idForEntity(ids, "entity-c");

    CNA_STUDIO_EXPECT(before == after);
}

CNA_STUDIO_TEST(TwoWidgetsSharingAnIdIsDetectedRatherThanDrawnWrong)
{
    // Two buttons both labelled "Delete" in one scope become one widget: pressing either lights
    // both and only one works. It reads as a rendering glitch, so it is detected here instead.
    WidgetIdStack ids;
    ids.beginFrame();
    (void) ids.make("Delete");
    CNA_STUDIO_EXPECT_EQ(ids.collisionCount(), std::size_t{0});

    (void) ids.make("Delete");
    CNA_STUDIO_EXPECT_EQ(ids.collisionCount(), std::size_t{1});
}

CNA_STUDIO_TEST(TheHashSuffixSeparatesWidgetsWithIdenticalVisibleText)
{
    WidgetIdStack ids;
    ids.beginFrame();
    const WidgetId a = ids.make("Delete##entity");
    const WidgetId b = ids.make("Delete##component");

    CNA_STUDIO_EXPECT(a != b);
    CNA_STUDIO_EXPECT_EQ(ids.collisionCount(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(std::string{WidgetIdStack::visibleLabel("Delete##entity")}, std::string{"Delete"});
    CNA_STUDIO_EXPECT_EQ(std::string{WidgetIdStack::visibleLabel("Delete")}, std::string{"Delete"});
}

CNA_STUDIO_TEST(CollisionsAreClearedEachFrame)
{
    WidgetIdStack ids;
    ids.beginFrame();
    (void) ids.make("Same");
    (void) ids.make("Same");
    CNA_STUDIO_EXPECT_EQ(ids.collisionCount(), std::size_t{1});

    ids.beginFrame();
    (void) ids.make("Same");
    CNA_STUDIO_EXPECT_EQ(ids.collisionCount(), std::size_t{0});
}

CNA_STUDIO_TEST(AnUnbalancedScopeDoesNotCorruptLaterFrames)
{
    // A panel that threw between push and pop has already failed once. Letting it make every
    // subsequent frame's ids wrong would turn one visible bug into an inexplicable one.
    WidgetIdStack ids;
    ids.beginFrame();
    ids.push("Leaked");
    ids.push("AlsoLeaked");
    const WidgetId leaked = ids.make("Button");

    ids.beginFrame();
    const WidgetId clean = ids.make("Button");

    CNA_STUDIO_EXPECT_EQ(ids.depth(), std::size_t{0});
    CNA_STUDIO_EXPECT(leaked != clean);
}

CNA_STUDIO_TEST(PoppingPastTheRootScopeIsHarmless)
{
    WidgetIdStack ids;
    ids.beginFrame();
    ids.pop();
    ids.pop();
    CNA_STUDIO_EXPECT_EQ(ids.depth(), std::size_t{0});
    CNA_STUDIO_EXPECT(ids.make("Button").isValid());
}

CNA_STUDIO_TEST(AWidgetIdIsNeverTheInvalidSentinel)
{
    WidgetIdStack ids;
    ids.beginFrame();
    for (int i = 0; i < 2000; ++i)
    {
        CNA_STUDIO_EXPECT(ids.makeIndex(i) != kInvalidWidgetId);
    }
}

CNA_STUDIO_TEST(DistinctKeysProduceDistinctIds)
{
    // Not a proof of no collisions -- that is not achievable with a 64-bit hash -- but a check
    // that the mixing is not degenerate over the shapes of key a real UI produces.
    WidgetIdStack ids;
    ids.beginFrame();
    std::set<std::uint64_t> seen;
    for (int scope = 0; scope < 40; ++scope)
    {
        ids.pushIndex(scope);
        for (int widget = 0; widget < 40; ++widget)
        {
            CNA_STUDIO_EXPECT(seen.insert(ids.makeIndex(widget).value()).second);
        }
        ids.pop();
    }
    CNA_STUDIO_EXPECT_EQ(seen.size(), std::size_t{1600});
    CNA_STUDIO_EXPECT_EQ(ids.collisionCount(), std::size_t{0});
}

// ------------------------------------------------------------------------------------------------
// Retained widget state (STUDIO-03003)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(WidgetStateSurvivesBetweenFrames)
{
    WidgetStateStore store;
    const WidgetId id{1234};

    store.beginFrame();
    store.get(id).expanded = true;
    store.get(id).scrollY = 42.0f;

    store.beginFrame();
    CNA_STUDIO_EXPECT(store.get(id).expanded);
    CNA_STUDIO_EXPECT_EQ(store.get(id).scrollY, 42.0f);
}

CNA_STUDIO_TEST(AWidgetSeenForTheFirstTimeGetsNeutralDefaults)
{
    WidgetStateStore store;
    const WidgetState& state = store.get(WidgetId{7});
    CNA_STUDIO_EXPECT(!state.expanded);
    CNA_STUDIO_EXPECT(!state.checked);
    CNA_STUDIO_EXPECT_EQ(state.scrollY, 0.0f);
    CNA_STUDIO_EXPECT(state.text.empty());
}

CNA_STUDIO_TEST(FindDoesNotCreateState)
{
    WidgetStateStore store;
    CNA_STUDIO_EXPECT(store.find(WidgetId{99}) == nullptr);
    CNA_STUDIO_EXPECT_EQ(store.size(), std::size_t{0});
}

CNA_STUDIO_TEST(StateForAWidgetNobodyDescribesAnyMoreIsReclaimed)
{
    // The leak this design exists to prevent: every tree node ever expanded and every folder ever
    // scrolled, retained for the life of the process. Nothing goes wrong, it just grows.
    WidgetStateStore store;
    store.setRetentionFrames(4);

    store.get(WidgetId{1}).expanded = true;
    store.get(WidgetId{2}).expanded = true;
    CNA_STUDIO_EXPECT_EQ(store.size(), std::size_t{2});

    for (int frame = 0; frame < 10; ++frame)
    {
        store.beginFrame();
        store.touch(WidgetId{1});   // widget 1 is still being described; widget 2 is not
    }
    store.sweep();

    CNA_STUDIO_EXPECT(store.find(WidgetId{1}) != nullptr);
    CNA_STUDIO_EXPECT(store.find(WidgetId{2}) == nullptr);
}

CNA_STUDIO_TEST(StateSurvivesBeingOffScreenForAWhile)
{
    // A widget in a collapsed section, on a hidden tab, or scrolled out of a virtualised list is
    // not described this frame and must not lose its state for it.
    WidgetStateStore store;
    const WidgetId id{55};
    store.get(id).scrollY = 17.0f;

    for (std::uint64_t frame = 0; frame < WidgetStateStore::kDefaultRetentionFrames / 2; ++frame)
    {
        store.beginFrame();
    }

    CNA_STUDIO_EXPECT(store.find(id) != nullptr);
    CNA_STUDIO_EXPECT_EQ(store.get(id).scrollY, 17.0f);
}

CNA_STUDIO_TEST(NothingIsReclaimedEarlyInASession)
{
    // Frame numbers start low and the retention window is large, so a cutoff computed by
    // subtracting on unsigned values would wrap and reclaim everything. This is that regression.
    WidgetStateStore store;
    store.touch(WidgetId{1});
    store.touch(WidgetId{2});

    for (int frame = 0; frame < 5; ++frame) { store.beginFrame(); }
    store.sweep();

    CNA_STUDIO_EXPECT_EQ(store.size(), std::size_t{2});
}

CNA_STUDIO_TEST(TheRetentionWindowCannotBeSetSoLowThatOffScreenStateIsLost)
{
    WidgetStateStore store;
    store.setRetentionFrames(0);
    CNA_STUDIO_EXPECT(store.retentionFrames() >= 2);
}

CNA_STUDIO_TEST(StateCanBeForgottenExplicitly)
{
    WidgetStateStore store;
    store.get(WidgetId{3}).checked = true;
    CNA_STUDIO_EXPECT(store.forget(WidgetId{3}));
    CNA_STUDIO_EXPECT(!store.forget(WidgetId{3}));
    CNA_STUDIO_EXPECT_EQ(store.size(), std::size_t{0});
}

CNA_STUDIO_TEST(TextEditingStateRoundTrips)
{
    WidgetStateStore store;
    WidgetState& state = store.get(WidgetId{8});
    state.text = "Player Speed";
    state.caret = 6;
    state.selectionAnchor = 0;

    store.beginFrame();
    const WidgetState* found = store.find(WidgetId{8});
    CNA_STUDIO_EXPECT(found != nullptr);
    CNA_STUDIO_EXPECT_EQ(found->text, std::string{"Player Speed"});
    CNA_STUDIO_EXPECT_EQ(found->caret, std::size_t{6});
}

CNA_STUDIO_TEST(WidgetStateIsKeyedByIdentitySoTwoWidgetsDoNotShareIt)
{
    WidgetIdStack ids;
    WidgetStateStore store;
    ids.beginFrame();

    ids.push("PanelA");
    const WidgetId a = ids.make("Tree");
    ids.pop();
    ids.push("PanelB");
    const WidgetId b = ids.make("Tree");
    ids.pop();

    store.get(a).scrollY = 100.0f;
    store.get(b).scrollY = 5.0f;

    CNA_STUDIO_EXPECT_EQ(store.get(a).scrollY, 100.0f);
    CNA_STUDIO_EXPECT_EQ(store.get(b).scrollY, 5.0f);
}

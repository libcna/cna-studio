// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioViewportPanel.hpp
 * @brief Navigating and selecting in the native shell's viewport.
 *
 * `plan.md` STUDIO-07009.
 *
 * ### The panel does not draw the scene
 *
 * The scene arrives as a texture the shell composites (`STUDIO-04012`); this is everything *else* a
 * viewport is — the camera the pointer moves, and what a click in it selects. Keeping the two apart
 * is what lets this half be tested with no graphics device at all: navigation and picking are
 * arithmetic over a camera and a document, and neither needs a pixel.
 *
 * ### Gestures are the ones the prototype's viewport used
 *
 * Wheel zooms about the pointer rather than about the centre, because zooming about the centre
 * makes a user chase the thing they were looking at. Middle drag pans; so does right drag, because
 * a trackpad has no middle button. Left click selects, and a click that hits nothing clears the
 * selection — which is how a user deselects without a keyboard.
 */

#pragma once

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/StudioCamera2D.hpp"
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/Scene/Tilemap.hpp"
#include "CNA/Studio/Scene/TransformGizmos.hpp"
#include "CNA/Studio/Scene/TransformGizmos3D.hpp"
#include "CNA/Studio/UiCore/StudioActionRegistry.hpp"
#include "CNA/Studio/UiCore/StudioPreferences.hpp"
#include "CNA/Studio/UiCore/StudioIcons.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include "CNA/Studio/Viewport/StudioViewport.hpp"

namespace CNA::Studio
{
    class StudioContext;

    /**
     * @brief The viewport's own retained state: which manipulator is showing, and any drag in it.
     *
     * Held by the caller rather than by the frame's widget store because a gizmo drag is *editor*
     * state, not widget state: it survives the panel being scrolled, re-docked or momentarily
     * hidden, and it is the thing an undo has to be able to reason about.
     */
    /**
     * @brief What a press in the native viewport means.
     *
     * Named apart from the prototype's `StudioTool`, which lives in the Dear ImGui panel headers
     * that `STUDIO-07030` deletes — and which shares this namespace. Two types of one name in
     * `CNA::Studio` is the ODR violation `STUDIO-02039` exists to refuse: it compiles, it links,
     * and it corrupts memory at run time hundreds of tests away from the cause.
     */
    /**
     * @brief Which projection the viewport is showing.
     *
     * Named apart from the prototype's `ViewMode` for the reason `STUDIO-02039` records: two types
     * of one name in `CNA::Studio` compile, link and corrupt memory at run time, and both UIs are
     * in this binary until `STUDIO-07031`.
     *
     * The two views share nothing below the camera. A press in 3D orbits rather than pans, picks
     * along a ray rather than against a layer order, and has no tile under it at all — so the
     * panel branches once, at the top, rather than threading a mode through six functions that
     * would each then be about two things.
     */
    enum class StudioViewportView
    {
        /** @brief The orthographic 2D scene: sprites, tilemaps, the grid. */
        TwoD,
        /** @brief The perspective or orthographic 3D scene: meshes, sprites as quads, wireframe. */
        ThreeD
    };

    /** @brief The name of @p view, for a menu row or an overlay. */
    [[nodiscard]] const char* studioViewportViewName(StudioViewportView view);

    enum class StudioViewportTool
    {
        /** @brief Pick entities and drag the gizmo. The default. */
        Select,
        /** @brief Set the tile under the cursor on the selected tilemap. */
        PaintTiles,
        /** @brief Clear the tile under the cursor. */
        EraseTiles,
        /** @brief Take the tile under the cursor as the brush, then go back to painting. */
        PickTile,
        /** @brief Fill the rectangle a drag encloses, applied on release. */
        FillTiles
    };

    /** @brief The display name of @p tool, e.g. `"Paint Tiles"`. */
    [[nodiscard]] std::string_view studioViewportToolName(StudioViewportTool tool);

    /** @brief What a viewport drag is doing right now. */
    enum class StudioViewportGesture : std::uint8_t
    {
        /** @brief Nothing: the buttons and modifiers held do not name a camera gesture. */
        None,
        /** @brief Turn the eye around the pivot. */
        Orbit,
        /** @brief Slide the eye and the pivot together. */
        Pan,
        /** @brief Move the eye towards or away from the pivot. */
        Dolly,
        /** @brief Turn the eye in place, leaving it where it is. */
        Look
    };

    /** @brief Returns a stable English name for a gesture, for diagnostics and tests. */
    [[nodiscard]] std::string_view studioViewportGestureName(StudioViewportGesture gesture);

    /** @brief The buttons and modifiers a viewport drag is being made with. */
    struct StudioViewportChord
    {
        bool left = false;
        bool middle = false;
        bool right = false;
        bool alt = false;
        bool shift = false;
        bool control = false;
    };

    /**
     * @brief Maps a button-and-modifier chord onto a camera gesture, under one navigation scheme.
     *
     * `plan.md` STUDIO-11015. Three schemes were stored, loaded, given a row in the Preferences
     * panel and **read by nothing** — the viewport's gestures were hard-coded. A preference that
     * changes nothing is worse than no preference: a user who sets it and finds the viewport
     * unchanged concludes the editor is broken, which is a fair reading.
     *
     * **A pure function**, deliberately. It is the whole of what the three schemes disagree about,
     * it takes six booleans and an enumeration and returns an enumeration, and every one of the
     * combinations that matter can therefore be stated as a test rather than performed with a
     * mouse. The viewport keeps the arithmetic; this keeps the vocabulary.
     *
     * **The schemes are what their tools do, not an interpretation of them.** Maya puts every
     * camera gesture behind Alt so an unmodified drag is always a selection; Blender puts them on
     * the middle button with Shift and Control as the modifiers, leaving left free for the same
     * reason. Studio's own is the one this editor shipped with. A user who asks for Maya and gets
     * nearly-Maya is worse served than one who was told the scheme is not implemented.
     *
     * @param style Which scheme to answer under.
     * @param chord What is held.
     * @return The gesture, or @ref StudioViewportGesture::None.
     */
    [[nodiscard]] StudioViewportGesture studioViewportGestureFor(StudioNavigationStyle style,
                                                                 const StudioViewportChord& chord);

    /** @brief Whether @p tool writes into a tilemap rather than selecting. */
    [[nodiscard]] bool studioViewportToolPaints(StudioViewportTool tool);

    struct StudioViewportState
    {
        /**
         * @brief What a press in the viewport means.
         *
         * Kept apart from @ref mode on purpose, exactly as the prototype keeps them: a gizmo mode
         * picks *which manipulator* acts on the selection, and a tool decides whether a press
         * manipulates at all. A single enum spanning both would make "paint tiles with the rotate
         * gizmo" expressible, which is not a thing.
         */
        /**
         * @brief Which projection is showing. The 2D and 3D views share only the document.
         */
        StudioViewportView view = StudioViewportView::TwoD;

        StudioViewportTool tool = StudioViewportTool::Select;

        /**
         * @brief Multiplier on pan, orbit and fly speed, from the user's preferences.
         *
         * Carried on the state rather than read from a preferences object the panel would have to
         * be handed, because the panel is CNA-free arithmetic over a camera and a document and
         * giving it a second thing to know about would be giving it a reason to need a Studio.
         * The shell copies it in every frame, for the reason the autosave interval is re-read
         * every poll: a setting applied only when the panel changes it is one that works when you
         * change it and not when you restart.
         */
        float cameraSpeed = 1.0f;

        /** @brief Whether the wheel's zoom direction is reversed. */
        bool invertZoom = false;

        /**
         * @brief Whether the 3D grid lies on the ground plane rather than the scene's own.
         *
         * `STUDIO-07056`. Copied in every frame from the preferences alongside `cameraSpeed` and
         * `invertZoom`, and for the same reason: a preference also arrives by being *assigned*
         * when the host loads it from disk, so a setting applied only when the Preferences panel
         * changes it works when you change it and not when you restart.
         *
         * It means nothing in the 2D view, which has one plane and no choice to make about it —
         * the command that sets it is disabled there rather than hidden, so a user who looked for
         * it can see it exists and see why it is greyed out.
         */
        bool gridOnGroundPlane = false;

        /** @brief Whether a 3D navigation gesture is in progress. */
        bool navigating = false;
        /** @brief Whether that gesture has moved at all, which is what makes it not a click. */
        bool navigationMoved = false;
        /** @brief Where the pointer was last frame, in panel coordinates. */
        float navigationX = 0.0f;
        float navigationY = 0.0f;

        /**
         * @brief Which navigation scheme the viewport follows.
         *
         * Read every frame from the preferences by whoever owns them, like `cameraSpeed` and
         * `invertZoom` beside it — and for the same reason: a preference also arrives by being
         * *assigned* when the host loads it from disk, so a setting applied only when the
         * Preferences panel changes it works when you change it and not when you restart.
         */
        StudioNavigationStyle navigation = StudioNavigationStyle::Studio;

        /** @brief The gesture the current drag resolved to, kept for the length of the drag. */
        StudioViewportGesture navigationGesture = StudioViewportGesture::None;

        /** @brief The tile the paint and fill tools write. */
        std::int64_t paintTile = 0;

        /**
         * @brief Distinguishes one paint stroke from the next in the undo stack's merge key.
         *
         * Without it a drag across forty tiles and the drag after it would merge into one entry,
         * and undoing would jump back past a stroke the user had finished and accepted.
         */
        std::uint64_t paintStroke = 0;

        /** @brief Whether this stroke has already pushed a command, so the next cell merges in. */
        bool paintStrokeHasEdited = false;

        /** @brief Where a fill drag began, while one is in flight. */
        std::optional<TileCoordinate> fillStart;

        /** @brief Which manipulator the selection shows. */
        GizmoMode mode = GizmoMode::Translate;

        /** @brief Whether the translate gizmo's arms follow the world axes or the entity's own. */
        GizmoSpace space = GizmoSpace::World;

        TranslateGizmoDrag translate;
        RotateGizmoDrag rotate;
        ScaleGizmoDrag scale;

        /**
         * @brief The same gesture applied to a whole selection.
         *
         * Runs *beside* the three above rather than instead of them: those compute what the
         * gesture is — how far along an axis, through what angle, by what factor — and this turns
         * that one answer into an edit per entity. Two gesture implementations would be two
         * chances for the group and the entity under the cursor to disagree.
         */
        MultiTransformDrag multi;

        /**
         * @brief The same three manipulators, over the 3D view (STUDIO-07050).
         *
         * A separate set of drag objects rather than the 2D ones reused: `TranslateGizmo3DDrag`
         * and its neighbours solve in the world against a camera ray, which is a different problem
         * from the 2D gizmos' screen-space arithmetic, and sharing one drag object between two
         * unrelated solvers would make "which math is this frame's `update()` doing" a question
         * that depends on which view happened to be open last.
         *
         * `GizmoMode` and `GizmoSpace` above are shared: which manipulator is armed and which
         * space it measures in are properties of the *selection*, not of the projection looking
         * at it, and a mode that reset itself across a view switch would be a tool that forgets
         * what it was doing every time a user pressed 2 or 3.
         */
        TranslateGizmo3DDrag translate3D;
        RotateGizmo3DDrag rotate3D;
        ScaleGizmo3DDrag scale3D;

        /** @brief The 3D form of @ref multi, for the same reason. */
        MultiTransform3D multi3D;

        /**
         * @brief Distinguishes one multi-drag from the next in the undo stack's merge key.
         *
         * Without it, two consecutive group drags would merge into one undo entry — and undoing
         * would jump back past a gesture the user had already finished and accepted.
         */
        std::uint64_t multiDragId = 0;

        /**
         * @brief Whether this drag has already pushed a command.
         *
         * The first frame of a drag opens an undo entry and every frame after it merges into that
         * one, so the whole gesture is a single Ctrl+Z rather than one per frame at sixty a second.
         */
        bool dragHasEdited = false;

        /** @brief True while any 2D manipulator is being dragged. */
        [[nodiscard]] bool dragging() const
        {
            return translate.isActive() || rotate.isActive() || scale.isActive();
        }

        /** @brief True while any 3D manipulator is being dragged. */
        [[nodiscard]] bool dragging3D() const
        {
            return translate3D.isActive() || rotate3D.isActive() || scale3D.isActive();
        }

        /** @brief Ends whatever drag is in flight, in either view. */
        void endDrag()
        {
            translate.end();
            rotate.end();
            scale.end();
            multi.end();
            translate3D.end();
            rotate3D.end();
            scale3D.end();
            multi3D.end();
            dragHasEdited = false;
        }
    };

    /** @brief What the user did in the viewport this frame. */
    /**
     * @brief Draws the armed tool's name, and its tile index where one applies.
     *
     * Over the image in the corner, which is where the prototype puts it — and it is the only
     * thing on the screen that says a press means something other than "select". A tool that
     * armed silently would be a viewport that behaves differently from yesterday with nothing
     * explaining why.
     *
     * Drawn only while a tile tool is armed: an overlay that was always there would be chrome over
     * the thing the viewport exists to show.
     *
     * @param frame The frame.
     * @param bounds The viewport's rectangle.
     * @param state The tool and its brush; the brush is written when the field commits.
     */
    void studioViewportToolOverlay(StudioFrame& frame, const UiRect& bounds,
                                   StudioViewportState& state);

    /** @brief One control on the viewport toolbar. */
    struct StudioViewportToolbarItem
    {
        /**
         * @brief The action it invokes, or empty for a group separator.
         *
         * An action id rather than a callback, so the button's enablement, its checked state, its
         * keyboard shortcut and its tooltip all come from the one place they come from everywhere
         * else in Studio. A viewport toolbar with its own copies of those would be the second
         * place "is Rotate armed" is decided, and the two would disagree the first time one of
         * them was changed.
         */
        std::string_view actionId;
        /** @brief What to draw. */
        StudioIcon icon = StudioIcon::None;

        /**
         * @brief What to draw instead while the action is checked. `None` keeps @ref icon.
         *
         * For a toggle whose two states are two *things* rather than one thing on and off. World
         * space and local space are the case: a lit button says "this is on", which is the wrong
         * sentence when the alternative is not "off" but "the other one". The prototype's button
         * was labelled with the space it was in for exactly this reason, and a toolbar that cannot
         * be read is half a control (`docs/MIGRATION-INVENTORY.md`).
         */
        StudioIcon checkedIcon = StudioIcon::None;
    };

    /** @brief The toolbar's contents, in order. Empty ids are separators. */
    [[nodiscard]] const std::vector<StudioViewportToolbarItem>& studioViewportToolbarItems();

    /**
     * @brief Draws the viewport's own toolbar over the scene, and routes its clicks.
     *
     * `plan.md` STUDIO-35050. What every professional 3D viewport has and this one did not: the
     * view, the transform mode, the transform space and snapping, where the user is already
     * looking. Before it, every one of those lived only on a menu or a window-level toolbar, which
     * means the answer to "what will a drag do" was somewhere other than the thing being dragged.
     *
     * **Over the image rather than above it.** A strip that took height from the viewport would
     * make the scene smaller, and the scene is what the panel is for. It is inset from the corner
     * and drawn on a raised surface so it reads as floating rather than as painted on.
     *
     * **Driven by the action registry**, which is what makes it free: a command that is disabled
     * greys out here, a checkable one shows its state, and a command whose shortcut is rebound
     * says so in its tooltip, with no code here for any of it.
     *
     * @param frame The frame.
     * @param bounds The viewport's rectangle.
     * @param actions The registry the buttons invoke.
     * @return The rectangle the toolbar occupied, so a caller can keep other overlays clear of it.
     */
    UiRect studioViewportToolbar(StudioFrame& frame, const UiRect& bounds,
                                 StudioActionRegistry& actions);

    struct StudioViewportResult
    {
        /** @brief The camera moved, so the scene must be re-rendered. Input pass only. */
        bool cameraChanged = false;

        /** @brief The selection changed. Input pass only. */
        bool selectionChanged = false;

        /** @brief What was picked, or the nil id when the click hit nothing. Input pass only. */
        Uuid picked;

        /** @brief The world point under the pointer, for a status bar or a ruler. */
        StudioVector2 pointerWorld;

        /** @brief Whether the pointer is over the viewport at all. */
        bool pointerInside = false;

        /** @brief A manipulator moved the selection this frame. Input pass only. */
        bool transformed = false;

        /** @brief A tile tool wrote into the tilemap this frame. Input pass only. */
        bool tilesPainted = false;

        /** @brief The tool changed itself, which the eyedropper does. Input pass only. */
        bool toolChanged = false;

        /**
         * @brief A press and release in the 3D view that turned no camera. Input pass only.
         *
         * Separate from a plain click because in 3D every button is also a navigation gesture: a
         * release after an orbit must not select whatever the camera happened to stop over, which
         * is exactly what makes a 3D viewport feel like it is fighting the user.
         */
        bool clicked3D = false;

        /**
         * @brief An asset was dropped into the view, and is to be put in the scene. Input pass only.
         *
         * `plan.md` STUDIO-09008. Reported rather than acted on here, because what an asset
         * *becomes* is one decision shared with the hierarchy (`Scene/AssetDrop.hpp`), and because
         * creating an entity is a command and this panel already reports every other one it wants.
         */
        Uuid assetDropped;

        /** @brief Where in the world it was dropped, which is where the new entity goes. */
        StudioVector3 assetDropPosition;
    };

    /**
     * @brief Drives the camera and the selection from input over @p bounds.
     *
     * @param frame The frame.
     * @param bounds The viewport panel's body, in window coordinates.
     * @param context The editor. Its scene is picked against; its selection is written.
     * @param camera The editor camera, panned and zoomed in place.
     * @param state Which manipulator is showing, and any drag in progress.
     * @param sizeProvider Resolves a sprite's texel size for picking. An empty provider makes
     *        every sprite pick at its default size, which is what a build with no device can know.
     * @return What happened.
     */
    StudioViewportResult studioViewportPanel(StudioFrame& frame, const UiRect& bounds,
                                             StudioContext& context, StudioCamera2D& camera,
                                             StudioViewportState& state,
                                             const SpriteSizeProvider& sizeProvider = {});

    /**
     * @brief Drives the 3D camera and the selection from input over @p bounds.
     *
     * The same shape as @ref studioViewportPanel and deliberately a separate function rather than a
     * branch inside it: the two views share the document and nothing else, and one function holding
     * both would be one where every reader has to work out which half they are in.
     *
     * Orbit is a left drag, pan is Shift, and the wheel dollies geometrically so that one notch
     * feels the same close up and far away. The turn rate is radians per *pixel* rather than per
     * fraction of the panel, so a narrow viewport does not turn faster than a wide one.
     *
     * @param frame The frame.
     * @param bounds The viewport panel's body, in window coordinates.
     * @param context The editor. Its scene is picked against; its selection is written.
     * @param camera The 3D editor camera, orbited, panned and dollied in place.
     * @param state The viewport's retained state.
     * @param sizeProvider Resolves a sprite's texel size, so a sprite picks at its extent.
     * @return What happened.
     */
    StudioViewportResult studioViewportPanel3D(StudioFrame& frame, const UiRect& bounds,
                                               StudioContext& context, StudioCamera3D& camera,
                                               StudioViewportState& state,
                                               const SpriteSizeProvider& sizeProvider = {});

    /**
     * @brief Moves @p camera to frame the current selection.
     *
     * An entity with no drawable geometry — a camera, an empty grouping node — still has a
     * position, and framing it centres on that rather than doing nothing: a key that appears not
     * to work is worse than one that works modestly.
     *
     * @param context The editor, for the scene and the selection.
     * @param camera The camera to move.
     * @param sizeProvider Resolves sprite sizes, so a sprite frames to its extent rather than to
     *        a point.
     * @return False when nothing is selected, or when nothing selected could be located.
     */
    bool studioFrameSelection(const StudioContext& context, StudioCamera2D& camera,
                              const SpriteSizeProvider& sizeProvider = {});

    /**
     * @brief Moves @p camera to frame the current selection, in three dimensions.
     *
     * `plan.md` STUDIO-11003. The 3D counterpart of the overload above, and needed rather than
     * optional: Focus Selected moved the *2D* camera whichever view was showing, so pressing F in
     * the 3D viewport rearranged a camera nobody was looking through and appeared to do nothing.
     *
     * Keeps the camera's orientation and moves only where it is and how far back, which is what
     * "focus" means in every editor that has it: a key that also levelled the view would take away
     * the angle the user had just set up.
     *
     * @param context The editor, for the scene and the selection.
     * @param camera The camera to move.
     * @param sizeProvider Resolves sprite sizes, so a sprite frames to its extent rather than to
     *        a point.
     * @return False when nothing is selected, or when nothing selected could be located.
     */
    bool studioFrameSelection3D(const StudioContext& context, StudioCamera3D& camera,
                                const SpriteSizeProvider& sizeProvider = {});
}

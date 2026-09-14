// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Core/EditorMatrix.hpp
 * @brief A 4x4 matrix and the transforms a 3D viewport is built from.
 *
 * Mirrors `Microsoft::Xna::Framework::Matrix` field for field, for the same reason every type in
 * `EditorMath.hpp` does (ANALYSIS.md D-03): cna-editor-core links nothing from CNA, so a camera
 * built on this is unit-testable with no window, no GPU and no CNA checkout, and the conversion in
 * cna-editor-viewport stays a field copy.
 *
 * **Conventions, all of them XNA's.** Storage is row-major (`M11`..`M44`, translation in the fourth
 * *row*), vectors are rows and multiply on the left (`v * M`), so `multiply(a, b)` means "a, then
 * b". The projections are **right-handed**, matching `Matrix::CreatePerspectiveFieldOfView` and
 * `CreateLookAt`, which is what a game passing these to `BasicEffect` will already be using: a
 * camera that agreed with itself but not with the runtime would show the scene mirrored the moment
 * anyone pressed Play.
 *
 * Depth maps to [0, 1] at the near and far planes respectively, again as XNA does, rather than
 * OpenGL's [-1, 1]. Nothing here rasterises anything, but the clip-space rule has to be stated
 * somewhere or the screen conversions below cannot be checked against anything.
 */

#include <optional>

#include "CNA/Editor/Core/EditorMath.hpp"

namespace CNA::Editor
{
    /**
     * @brief Row-major 4x4 matrix, identity by default.
     *
     * XNA's own `Matrix()` is zero-filled; this one is the identity, following `EditorQuaternion`,
     * which is likewise the identity rather than all-zeros. A default-constructed transform that
     * annihilates everything it touches is a bug waiting for someone to forget an initialiser.
     */
    struct EditorMatrix
    {
        float m11 = 1.0f, m12 = 0.0f, m13 = 0.0f, m14 = 0.0f;
        float m21 = 0.0f, m22 = 1.0f, m23 = 0.0f, m24 = 0.0f;
        float m31 = 0.0f, m32 = 0.0f, m33 = 1.0f, m34 = 0.0f;
        float m41 = 0.0f, m42 = 0.0f, m43 = 0.0f, m44 = 1.0f;

        friend bool operator==(const EditorMatrix& lhs, const EditorMatrix& rhs);
        friend bool operator!=(const EditorMatrix& lhs, const EditorMatrix& rhs) { return !(lhs == rhs); }
    };

    /** @brief Returns the transform @p a followed by the transform @p b, i.e. the product `a * b`. */
    [[nodiscard]] EditorMatrix multiply(const EditorMatrix& a, const EditorMatrix& b);

    /**
     * @brief Returns the inverse of @p matrix, or std::nullopt when it is singular.
     *
     * An optional rather than an identity fallback: "this matrix cannot be inverted" and "this
     * matrix is its own inverse" are wildly different facts, and a screen-to-world ray built on the
     * second when the first is true points somewhere plausible and wrong.
     */
    [[nodiscard]] std::optional<EditorMatrix> invert(const EditorMatrix& matrix);

    /**
     * @brief Returns @p matrix with its rows and columns exchanged.
     *
     * Here for the one thing that genuinely needs it: a normal is not transformed by the matrix
     * that transforms positions but by that matrix's inverse transpose, and under non-uniform
     * scale the difference is visible -- normals on a squashed model tilt away from the surface
     * and light it wrongly. `inverseTranspose` is the composition worth naming; this is the half
     * of it that had no name yet.
     */
    [[nodiscard]] EditorMatrix transpose(const EditorMatrix& matrix);

    /**
     * @brief Returns the matrix that transforms normals under @p matrix, or std::nullopt when
     *        @p matrix is singular.
     *
     * The inverse transpose. A singular transform -- a node scaled to zero on some axis -- has no
     * such matrix, and the optional says so rather than returning something that would silently
     * flatten every normal it touched.
     */
    [[nodiscard]] std::optional<EditorMatrix> inverseTranspose(const EditorMatrix& matrix);

    /** @brief Returns @p point transformed by @p matrix, treating it as a position (w = 1). */
    [[nodiscard]] EditorVector3 transformPosition(const EditorMatrix& matrix, const EditorVector3& point);

    /**
     * @brief Returns @p direction transformed by @p matrix's rotation and scale, ignoring translation.
     *
     * What a direction needs and a position must not have: translating a direction turns "which way
     * does this point" into "where is this", and the difference only shows up once the camera moves
     * away from the origin.
     */
    [[nodiscard]] EditorVector3 transformDirection(const EditorMatrix& matrix, const EditorVector3& direction);

    /**
     * @brief Transforms @p point by @p matrix and divides by the resulting w.
     *
     * @param outW Receives the w before the division. A caller projecting a point needs it: w <= 0
     *        means the point is at or behind the eye, where the divided result is meaningless but
     *        looks like an ordinary coordinate.
     */
    [[nodiscard]] EditorVector3 transformWithPerspective(const EditorMatrix& matrix,
                                                         const EditorVector3& point, float& outW);

    /** @brief Returns a translation by @p offset. */
    [[nodiscard]] EditorMatrix createTranslation(const EditorVector3& offset);

    /** @brief Returns a scale by @p factors, per axis. */
    [[nodiscard]] EditorMatrix createScale(const EditorVector3& factors);

    /** @brief Returns the rotation @p rotation as a matrix. */
    [[nodiscard]] EditorMatrix createFromQuaternion(const EditorQuaternion& rotation);

    /**
     * @brief Returns a right-handed view matrix looking from @p eye towards @p target.
     *
     * @param up The world's up direction. When it is parallel to the view direction -- looking
     *        straight down, the classic gimbal case -- an arbitrary perpendicular is chosen rather
     *        than a degenerate matrix produced, so a camera that orbits over the pole keeps
     *        rendering instead of blanking for one frame.
     */
    [[nodiscard]] EditorMatrix createLookAt(const EditorVector3& eye, const EditorVector3& target,
                                            const EditorVector3& up);

    /**
     * @brief Returns a right-handed perspective projection.
     *
     * @param fieldOfViewRadians Vertical field of view.
     * @param aspectRatio Width divided by height.
     * @param nearPlane Distance to the near plane; must be > 0.
     * @param farPlane Distance to the far plane; must be > nearPlane.
     */
    [[nodiscard]] EditorMatrix createPerspectiveFieldOfView(float fieldOfViewRadians, float aspectRatio,
                                                            float nearPlane, float farPlane);

    /** @brief Returns a right-handed orthographic projection of the given world-space extent. */
    [[nodiscard]] EditorMatrix createOrthographic(float width, float height, float nearPlane, float farPlane);
}

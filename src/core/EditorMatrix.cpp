// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Core/EditorMatrix.hpp"

#include <cmath>

namespace CNA::Editor
{
    bool operator==(const EditorMatrix& lhs, const EditorMatrix& rhs)
    {
        return lhs.m11 == rhs.m11 && lhs.m12 == rhs.m12 && lhs.m13 == rhs.m13 && lhs.m14 == rhs.m14
               && lhs.m21 == rhs.m21 && lhs.m22 == rhs.m22 && lhs.m23 == rhs.m23 && lhs.m24 == rhs.m24
               && lhs.m31 == rhs.m31 && lhs.m32 == rhs.m32 && lhs.m33 == rhs.m33 && lhs.m34 == rhs.m34
               && lhs.m41 == rhs.m41 && lhs.m42 == rhs.m42 && lhs.m43 == rhs.m43 && lhs.m44 == rhs.m44;
    }

    EditorMatrix transpose(const EditorMatrix& matrix)
    {
        EditorMatrix result;
        result.m11 = matrix.m11; result.m12 = matrix.m21; result.m13 = matrix.m31; result.m14 = matrix.m41;
        result.m21 = matrix.m12; result.m22 = matrix.m22; result.m23 = matrix.m32; result.m24 = matrix.m42;
        result.m31 = matrix.m13; result.m32 = matrix.m23; result.m33 = matrix.m33; result.m34 = matrix.m43;
        result.m41 = matrix.m14; result.m42 = matrix.m24; result.m43 = matrix.m34; result.m44 = matrix.m44;
        return result;
    }

    std::optional<EditorMatrix> inverseTranspose(const EditorMatrix& matrix)
    {
        const std::optional<EditorMatrix> inverted = invert(matrix);
        if (!inverted) { return std::nullopt; }
        return transpose(*inverted);
    }

    EditorMatrix multiply(const EditorMatrix& a, const EditorMatrix& b)
    {
        EditorMatrix result;
        result.m11 = a.m11 * b.m11 + a.m12 * b.m21 + a.m13 * b.m31 + a.m14 * b.m41;
        result.m12 = a.m11 * b.m12 + a.m12 * b.m22 + a.m13 * b.m32 + a.m14 * b.m42;
        result.m13 = a.m11 * b.m13 + a.m12 * b.m23 + a.m13 * b.m33 + a.m14 * b.m43;
        result.m14 = a.m11 * b.m14 + a.m12 * b.m24 + a.m13 * b.m34 + a.m14 * b.m44;

        result.m21 = a.m21 * b.m11 + a.m22 * b.m21 + a.m23 * b.m31 + a.m24 * b.m41;
        result.m22 = a.m21 * b.m12 + a.m22 * b.m22 + a.m23 * b.m32 + a.m24 * b.m42;
        result.m23 = a.m21 * b.m13 + a.m22 * b.m23 + a.m23 * b.m33 + a.m24 * b.m43;
        result.m24 = a.m21 * b.m14 + a.m22 * b.m24 + a.m23 * b.m34 + a.m24 * b.m44;

        result.m31 = a.m31 * b.m11 + a.m32 * b.m21 + a.m33 * b.m31 + a.m34 * b.m41;
        result.m32 = a.m31 * b.m12 + a.m32 * b.m22 + a.m33 * b.m32 + a.m34 * b.m42;
        result.m33 = a.m31 * b.m13 + a.m32 * b.m23 + a.m33 * b.m33 + a.m34 * b.m43;
        result.m34 = a.m31 * b.m14 + a.m32 * b.m24 + a.m33 * b.m34 + a.m34 * b.m44;

        result.m41 = a.m41 * b.m11 + a.m42 * b.m21 + a.m43 * b.m31 + a.m44 * b.m41;
        result.m42 = a.m41 * b.m12 + a.m42 * b.m22 + a.m43 * b.m32 + a.m44 * b.m42;
        result.m43 = a.m41 * b.m13 + a.m42 * b.m23 + a.m43 * b.m33 + a.m44 * b.m43;
        result.m44 = a.m41 * b.m14 + a.m42 * b.m24 + a.m43 * b.m34 + a.m44 * b.m44;
        return result;
    }

    std::optional<EditorMatrix> invert(const EditorMatrix& matrix)
    {
        // Cofactor expansion, written out. A general inverse rather than the cheaper affine one
        // because a projection matrix is not affine, and screen-to-world needs exactly that one.
        const float a = matrix.m11, b = matrix.m12, c = matrix.m13, d = matrix.m14;
        const float e = matrix.m21, f = matrix.m22, g = matrix.m23, h = matrix.m24;
        const float i = matrix.m31, j = matrix.m32, k = matrix.m33, l = matrix.m34;
        const float m = matrix.m41, n = matrix.m42, o = matrix.m43, p = matrix.m44;

        const float kp_lo = k * p - l * o;
        const float jp_ln = j * p - l * n;
        const float jo_kn = j * o - k * n;
        const float ip_lm = i * p - l * m;
        const float io_km = i * o - k * m;
        const float in_jm = i * n - j * m;

        const float a11 = f * kp_lo - g * jp_ln + h * jo_kn;
        const float a12 = -(e * kp_lo - g * ip_lm + h * io_km);
        const float a13 = e * jp_ln - f * ip_lm + h * in_jm;
        const float a14 = -(e * jo_kn - f * io_km + g * in_jm);

        const float determinant = a * a11 + b * a12 + c * a13 + d * a14;
        if (std::abs(determinant) < 1e-12f) { return std::nullopt; }

        const float inverseDeterminant = 1.0f / determinant;

        const float gp_ho = g * p - h * o;
        const float fp_hn = f * p - h * n;
        const float fo_gn = f * o - g * n;
        const float ep_hm = e * p - h * m;
        const float eo_gm = e * o - g * m;
        const float en_fm = e * n - f * m;

        const float gl_hk = g * l - h * k;
        const float fl_hj = f * l - h * j;
        const float fk_gj = f * k - g * j;
        const float el_hi = e * l - h * i;
        const float ek_gi = e * k - g * i;
        const float ej_fi = e * j - f * i;

        EditorMatrix result;
        result.m11 = a11 * inverseDeterminant;
        result.m21 = a12 * inverseDeterminant;
        result.m31 = a13 * inverseDeterminant;
        result.m41 = a14 * inverseDeterminant;

        result.m12 = -(b * kp_lo - c * jp_ln + d * jo_kn) * inverseDeterminant;
        result.m22 = (a * kp_lo - c * ip_lm + d * io_km) * inverseDeterminant;
        result.m32 = -(a * jp_ln - b * ip_lm + d * in_jm) * inverseDeterminant;
        result.m42 = (a * jo_kn - b * io_km + c * in_jm) * inverseDeterminant;

        result.m13 = (b * gp_ho - c * fp_hn + d * fo_gn) * inverseDeterminant;
        result.m23 = -(a * gp_ho - c * ep_hm + d * eo_gm) * inverseDeterminant;
        result.m33 = (a * fp_hn - b * ep_hm + d * en_fm) * inverseDeterminant;
        result.m43 = -(a * fo_gn - b * eo_gm + c * en_fm) * inverseDeterminant;

        result.m14 = -(b * gl_hk - c * fl_hj + d * fk_gj) * inverseDeterminant;
        result.m24 = (a * gl_hk - c * el_hi + d * ek_gi) * inverseDeterminant;
        result.m34 = -(a * fl_hj - b * el_hi + d * ej_fi) * inverseDeterminant;
        result.m44 = (a * fk_gj - b * ek_gi + c * ej_fi) * inverseDeterminant;
        return result;
    }

    EditorVector3 transformPosition(const EditorMatrix& matrix, const EditorVector3& point)
    {
        return EditorVector3{
            point.x * matrix.m11 + point.y * matrix.m21 + point.z * matrix.m31 + matrix.m41,
            point.x * matrix.m12 + point.y * matrix.m22 + point.z * matrix.m32 + matrix.m42,
            point.x * matrix.m13 + point.y * matrix.m23 + point.z * matrix.m33 + matrix.m43};
    }

    EditorVector3 transformDirection(const EditorMatrix& matrix, const EditorVector3& direction)
    {
        return EditorVector3{
            direction.x * matrix.m11 + direction.y * matrix.m21 + direction.z * matrix.m31,
            direction.x * matrix.m12 + direction.y * matrix.m22 + direction.z * matrix.m32,
            direction.x * matrix.m13 + direction.y * matrix.m23 + direction.z * matrix.m33};
    }

    EditorVector3 transformWithPerspective(const EditorMatrix& matrix, const EditorVector3& point, float& outW)
    {
        const EditorVector3 transformed = transformPosition(matrix, point);
        outW = point.x * matrix.m14 + point.y * matrix.m24 + point.z * matrix.m34 + matrix.m44;
        if (outW == 0.0f) { return transformed; }
        return scale(transformed, 1.0f / outW);
    }

    EditorMatrix createTranslation(const EditorVector3& offset)
    {
        EditorMatrix result;
        result.m41 = offset.x;
        result.m42 = offset.y;
        result.m43 = offset.z;
        return result;
    }

    EditorMatrix createScale(const EditorVector3& factors)
    {
        EditorMatrix result;
        result.m11 = factors.x;
        result.m22 = factors.y;
        result.m33 = factors.z;
        return result;
    }

    EditorMatrix createFromQuaternion(const EditorQuaternion& rotation)
    {
        const float xx = rotation.x * rotation.x;
        const float yy = rotation.y * rotation.y;
        const float zz = rotation.z * rotation.z;
        const float xy = rotation.x * rotation.y;
        const float zw = rotation.z * rotation.w;
        const float zx = rotation.z * rotation.x;
        const float yw = rotation.y * rotation.w;
        const float yz = rotation.y * rotation.z;
        const float xw = rotation.x * rotation.w;

        EditorMatrix result;
        result.m11 = 1.0f - 2.0f * (yy + zz);
        result.m12 = 2.0f * (xy + zw);
        result.m13 = 2.0f * (zx - yw);
        result.m21 = 2.0f * (xy - zw);
        result.m22 = 1.0f - 2.0f * (zz + xx);
        result.m23 = 2.0f * (yz + xw);
        result.m31 = 2.0f * (zx + yw);
        result.m32 = 2.0f * (yz - xw);
        result.m33 = 1.0f - 2.0f * (yy + xx);
        return result;
    }

    EditorMatrix createLookAt(const EditorVector3& eye, const EditorVector3& target, const EditorVector3& up)
    {
        // Right-handed: the camera looks down its own -Z, so the third basis vector points *back*
        // towards the eye. Getting this backwards produces a view that is correct in every respect
        // except that the scene is mirrored, which is the hardest kind of wrong to notice.
        EditorVector3 backward = normalize(subtract(eye, target));
        if (length(backward) <= 0.0f) { backward = EditorVector3{0.0f, 0.0f, 1.0f}; }

        EditorVector3 right = normalize(cross(up, backward));
        if (length(right) <= 0.0f)
        {
            // Looking along `up`. Any perpendicular will do -- the roll about the view axis is
            // unconstrained here, and refusing to produce a matrix would blank the viewport for
            // exactly as long as an orbit spent passing over the pole.
            const EditorVector3 fallback = std::abs(backward.x) < 0.9f ? EditorVector3{1.0f, 0.0f, 0.0f}
                                                                      : EditorVector3{0.0f, 1.0f, 0.0f};
            right = normalize(cross(fallback, backward));
        }
        const EditorVector3 trueUp = cross(backward, right);

        EditorMatrix result;
        result.m11 = right.x;   result.m12 = trueUp.x;  result.m13 = backward.x;
        result.m21 = right.y;   result.m22 = trueUp.y;  result.m23 = backward.y;
        result.m31 = right.z;   result.m32 = trueUp.z;  result.m33 = backward.z;
        result.m41 = -dot(right, eye);
        result.m42 = -dot(trueUp, eye);
        result.m43 = -dot(backward, eye);
        return result;
    }

    EditorMatrix createPerspectiveFieldOfView(float fieldOfViewRadians, float aspectRatio, float nearPlane,
                                              float farPlane)
    {
        const float height = 1.0f / std::tan(fieldOfViewRadians * 0.5f);
        const float width = aspectRatio > 0.0f ? height / aspectRatio : height;

        EditorMatrix result;
        result.m11 = width;
        result.m22 = height;
        result.m33 = farPlane / (nearPlane - farPlane);
        result.m34 = -1.0f;
        result.m43 = nearPlane * farPlane / (nearPlane - farPlane);
        result.m44 = 0.0f;
        return result;
    }

    EditorMatrix createOrthographic(float width, float height, float nearPlane, float farPlane)
    {
        EditorMatrix result;
        result.m11 = width > 0.0f ? 2.0f / width : 1.0f;
        result.m22 = height > 0.0f ? 2.0f / height : 1.0f;
        result.m33 = 1.0f / (nearPlane - farPlane);
        result.m43 = nearPlane / (nearPlane - farPlane);
        return result;
    }
}

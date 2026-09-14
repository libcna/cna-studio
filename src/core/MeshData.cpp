// SPDX-License-Identifier: MS-PL

#include "CNA/Editor/Core/MeshData.hpp"

#include <algorithm>
#include <limits>

namespace CNA::Editor
{
    bool MeshData::isEmpty() const
    {
        return std::ranges::all_of(parts, [](const MeshPart& part) { return part.indices.empty(); });
    }

    std::size_t MeshData::getVertexCount() const
    {
        std::size_t total = 0;
        for (const MeshPart& part : parts) { total += part.vertices.size(); }
        return total;
    }

    std::size_t MeshData::getTriangleCount() const
    {
        std::size_t total = 0;
        for (const MeshPart& part : parts) { total += part.getTriangleCount(); }
        return total;
    }

    void recomputeMeshBounds(MeshData& data)
    {
        // Seeded inverted, so a model with no vertices at all comes out with min > max on every
        // axis -- which is precisely what `WorldBounds3D::isEmpty` tests. An empty model and a
        // model that happens to sit at the origin must not produce the same bounds.
        constexpr float kInfinity = std::numeric_limits<float>::infinity();
        data.boundsMin = EditorVector3{kInfinity, kInfinity, kInfinity};
        data.boundsMax = EditorVector3{-kInfinity, -kInfinity, -kInfinity};

        for (const MeshPart& part : data.parts)
        {
            for (const MeshVertex& vertex : part.vertices)
            {
                data.boundsMin.x = std::min(data.boundsMin.x, vertex.position.x);
                data.boundsMin.y = std::min(data.boundsMin.y, vertex.position.y);
                data.boundsMin.z = std::min(data.boundsMin.z, vertex.position.z);
                data.boundsMax.x = std::max(data.boundsMax.x, vertex.position.x);
                data.boundsMax.y = std::max(data.boundsMax.y, vertex.position.y);
                data.boundsMax.z = std::max(data.boundsMax.z, vertex.position.z);
            }
        }
    }

    bool meshWindingMatchesNormals(const MeshPart& part)
    {
        for (std::size_t triangle = 0; triangle + 2 < part.indices.size(); triangle += 3)
        {
            const std::uint32_t i0 = part.indices[triangle];
            const std::uint32_t i1 = part.indices[triangle + 1];
            const std::uint32_t i2 = part.indices[triangle + 2];

            // An index past the end is a broken part rather than a wrongly wound one, but the
            // answer to "is this safe to draw" is no either way, and reading past the vector to
            // find out would be worse than both.
            if (i0 >= part.vertices.size() || i1 >= part.vertices.size()
                || i2 >= part.vertices.size())
            {
                return false;
            }

            const MeshVertex& v0 = part.vertices[i0];
            const MeshVertex& v1 = part.vertices[i1];
            const MeshVertex& v2 = part.vertices[i2];

            const EditorVector3 faceNormal = cross(subtract(v1.position, v0.position),
                                                   subtract(v2.position, v0.position));

            // Zero area: three collinear or coincident points have no winding to disagree with.
            // Skipped rather than failed, because a mesh may legitimately carry them and the
            // question being asked here is about the triangles that do face somewhere.
            constexpr float kDegenerate = 1e-12f;
            if (dot(faceNormal, faceNormal) <= kDegenerate) { continue; }

            const EditorVector3 vertexNormal = add(add(v0.normal, v1.normal), v2.normal);
            if (dot(vertexNormal, vertexNormal) <= kDegenerate) { continue; }

            // Within ninety degrees rather than equal: vertex normals on a curved surface are
            // averaged across faces and do not match any one face's normal exactly. What must
            // hold is that they point to the same *side*, and a reversed winding fails that by a
            // hundred and eighty degrees rather than by a margin.
            if (dot(faceNormal, vertexNormal) <= 0.0f) { return false; }
        }

        return true;
    }
}

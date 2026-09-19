// SPDX-License-Identifier: MS-PL
/**
 * @file ModelImportTests.cpp
 * @brief Tests for the glTF importer and the `MeshData` seam it produces (plan.md ED-405).
 *
 * The fixtures are *written by these tests*, not committed beside them, and that is deliberate.
 * A glTF checked into the repository is an opaque blob: when a case fails, nobody can see from the
 * diff what the file was supposed to contain, and nobody dares change it. Built here, the geometry
 * a case depends on is visible in the case itself -- "a triangle with a vertex one unit up" is a
 * line of C++ rather than a base64 payload -- and a new case costs a few lines instead of a new
 * binary file.
 *
 * The properties worth pinning are not "the parser parses". They are the three conversions the
 * importer performs that nothing downstream can check for itself: the Y mirror into the editor's
 * world, the winding reversal that mirror forces, and the inverse-transpose that keeps normals
 * perpendicular under a non-uniform node scale. Each of those is silent when wrong -- a model
 * that is upside down, inside-out or badly lit still loads, still counts its triangles correctly,
 * and still looks like a working importer from every angle except the one that matters.
 */

#include "TestHarness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetImporters.hpp"
#include "CNA/Studio/Assets/MeshCache.hpp"
#include "CNA/Studio/Assets/ModelImport.hpp"
#include "CNA/Studio/Core/MeshData.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Scene/SceneWireframe.hpp"

using namespace CNA::Studio;

namespace
{
    std::filesystem::path makeScratchDirectory(const std::string& name)
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path()
            / ("cna-studio-tests-" + name + "-" + Uuid::generate().toString());
        std::filesystem::create_directories(directory);
        return directory;
    }

    void writeBinaryFile(const std::filesystem::path& path, const std::string& contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    /** @brief Standard base64, so a fixture's buffer can live in the `.gltf` as a data URI. */
    std::string toBase64(const std::string& bytes)
    {
        static constexpr std::string_view kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string encoded;
        encoded.reserve((bytes.size() + 2) / 3 * 4);

        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const unsigned int b0 = static_cast<unsigned char>(bytes[i]);
            const unsigned int b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0u;
            const unsigned int b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0u;
            const unsigned int triple = (b0 << 16) | (b1 << 8) | b2;

            encoded.push_back(kAlphabet[(triple >> 18) & 0x3F]);
            encoded.push_back(kAlphabet[(triple >> 12) & 0x3F]);
            encoded.push_back(i + 1 < bytes.size() ? kAlphabet[(triple >> 6) & 0x3F] : '=');
            encoded.push_back(i + 2 < bytes.size() ? kAlphabet[triple & 0x3F] : '=');
        }
        return encoded;
    }

    /** @brief Appends @p value to @p bytes in little-endian order, which is what glTF specifies. */
    template <typename T>
    void appendLittleEndian(std::string& bytes, T value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        std::array<char, sizeof(T)> raw{};
        std::memcpy(raw.data(), &value, sizeof(T));
        bytes.append(raw.data(), raw.size());
    }

    std::string toJsonArray(const std::vector<float>& values)
    {
        std::ostringstream out;
        out << "[";
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i > 0) { out << ","; }
            out << values[i];
        }
        out << "]";
        return out.str();
    }

    /**
     * @brief The geometry and node placement one fixture describes.
     *
     * Everything is in *glTF's* frame -- Y-up, right-handed -- because that is what a real file
     * would contain and what the importer's conversion has to be tested against. A case that
     * pre-mirrored its own fixture would be asserting that the importer agrees with the test's
     * copy of the conversion rather than that either is right.
     */
    struct GltfFixture
    {
        std::vector<std::array<float, 3>> positions;

        /** @brief Empty to omit the NORMAL attribute entirely, which is legal glTF. */
        std::vector<std::array<float, 3>> normals;

        std::vector<std::array<float, 2>> texCoords;

        std::vector<std::uint16_t> indices;

        std::array<float, 3> translation{{0.0f, 0.0f, 0.0f}};
        std::array<float, 3> nodeScale{{1.0f, 1.0f, 1.0f}};

        /** @brief 4 = triangles, 5 = triangle strip, 6 = triangle fan, 1 = lines. */
        int primitiveMode = 4;

        bool withMaterial = false;

        /**
         * @brief Emit a material carrying the maps and factors `PbrEffect` reads (ED-402).
         *
         * Its own flag rather than an addition to `withMaterial`, so the older test above goes on
         * asserting what it always asserted -- that a material with *no* PBR maps still produces
         * the Blinn-Phong description a `BasicEffect` fallback needs. Folding the two together
         * would have left nothing checking the plain case.
         */
        bool withPbrMaps = false;

        /** @brief Write the occlusion map as its own image rather than packed with the ORM one. */
        bool withSeparateOcclusionMap = false;

        /**
         * @brief How many animations to declare. None of them is ever imported.
         *
         * Each is a valid one-channel animation reading its times out of the positions view, so
         * the buffer needs nothing added to it: the count is the only thing under test, and an
         * animation that failed `cgltf_validate` would fail the whole load instead.
         */
        std::size_t animationCount = 0;

        /** @brief Points the buffer at a file beside the `.gltf` instead of embedding it. */
        std::string externalBufferUri;

        std::string meshName = "Fixture";
    };

    /** @brief The buffer bytes a fixture's accessors read from: positions, normals, uvs, indices. */
    std::string buildFixtureBuffer(const GltfFixture& fixture)
    {
        std::string bytes;
        for (const auto& position : fixture.positions)
        {
            for (const float component : position) { appendLittleEndian(bytes, component); }
        }
        for (const auto& normal : fixture.normals)
        {
            for (const float component : normal) { appendLittleEndian(bytes, component); }
        }
        for (const auto& texCoord : fixture.texCoords)
        {
            for (const float component : texCoord) { appendLittleEndian(bytes, component); }
        }
        for (const std::uint16_t index : fixture.indices) { appendLittleEndian(bytes, index); }

        // glTF requires a buffer's length to be a multiple of four when a GLB carries it as a
        // binary chunk, and the unsigned-short index block can leave it odd.
        while (bytes.size() % 4 != 0) { bytes.push_back('\0'); }
        return bytes;
    }

    /**
     * @brief Builds the fixture's glTF JSON.
     *
     * @param bufferUri What the single buffer points at: a data URI, a filename, or empty for a
     *        GLB's binary chunk.
     */
    std::string buildFixtureJson(const GltfFixture& fixture, const std::string& bufferUri)
    {
        const std::size_t positionBytes = fixture.positions.size() * 3 * sizeof(float);
        const std::size_t normalBytes = fixture.normals.size() * 3 * sizeof(float);
        const std::size_t texCoordBytes = fixture.texCoords.size() * 2 * sizeof(float);
        const std::size_t indexBytes = fixture.indices.size() * sizeof(std::uint16_t);

        // POSITION's min and max are the one pair of accessor bounds glTF actually requires, and
        // cgltf_validate enforces it -- so the fixture computes them rather than guessing.
        std::vector<float> minimum{fixture.positions[0][0], fixture.positions[0][1],
                                   fixture.positions[0][2]};
        std::vector<float> maximum = minimum;
        for (const auto& position : fixture.positions)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], position[axis]);
                maximum[axis] = std::max(maximum[axis], position[axis]);
            }
        }

        std::ostringstream views;
        std::ostringstream accessors;
        std::ostringstream attributes;
        std::size_t offset = 0;
        int nextIndex = 0;

        const auto addView = [&](std::size_t length, int target) {
            if (nextIndex > 0) { views << ","; }
            views << R"({"buffer":0,"byteOffset":)" << offset << R"(,"byteLength":)" << length
                  << R"(,"target":)" << target << "}";
            offset += length;
        };

        addView(positionBytes, 34962);
        accessors << R"({"bufferView":0,"componentType":5126,"count":)" << fixture.positions.size()
                  << R"(,"type":"VEC3","min":)" << toJsonArray(minimum) << R"(,"max":)"
                  << toJsonArray(maximum) << "}";
        attributes << R"("POSITION":0)";
        ++nextIndex;

        if (!fixture.normals.empty())
        {
            addView(normalBytes, 34962);
            accessors << R"(,{"bufferView":)" << nextIndex << R"(,"componentType":5126,"count":)"
                      << fixture.normals.size() << R"(,"type":"VEC3"})";
            attributes << R"(,"NORMAL":)" << nextIndex;
            ++nextIndex;
        }

        if (!fixture.texCoords.empty())
        {
            addView(texCoordBytes, 34962);
            accessors << R"(,{"bufferView":)" << nextIndex << R"(,"componentType":5126,"count":)"
                      << fixture.texCoords.size() << R"(,"type":"VEC2"})";
            attributes << R"(,"TEXCOORD_0":)" << nextIndex;
            ++nextIndex;
        }

        const int indexAccessor = nextIndex;
        addView(indexBytes, 34963);
        accessors << R"(,{"bufferView":)" << indexAccessor
                  << R"(,"componentType":5123,"count":)" << fixture.indices.size()
                  << R"(,"type":"SCALAR"})";

        // An animation sampler's input must be SCALAR floats. The positions view already holds
        // floats, so the times are read out of it rather than added to the buffer -- the numbers
        // are meaningless and only the animation *count* is under test.
        const int animationInput = indexAccessor + 1;
        if (fixture.animationCount > 0)
        {
            accessors << R"(,{"bufferView":0,"componentType":5126,"count":)"
                      << fixture.positions.size() << R"(,"type":"SCALAR","min":[0],"max":[1]})";
        }

        std::ostringstream json;
        json << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
             << R"("nodes":[{"mesh":0,"translation":)"
             << toJsonArray({fixture.translation[0], fixture.translation[1], fixture.translation[2]})
             << R"(,"scale":)"
             << toJsonArray({fixture.nodeScale[0], fixture.nodeScale[1], fixture.nodeScale[2]})
             << R"(}],"meshes":[{"name":")" << fixture.meshName << R"(","primitives":[{"attributes":{)"
             << attributes.str() << R"(},"indices":)" << indexAccessor << R"(,"mode":)"
             << fixture.primitiveMode;

        if (fixture.withMaterial) { json << R"(,"material":0)"; }
        json << R"(}]}],)";

        if (fixture.withMaterial && fixture.withPbrMaps)
        {
            // Image 1 is the packed occlusion-roughness-metallic map and image 2 the normal map;
            // image 3 exists only when the fixture is asked for the separated-occlusion case the
            // importer warns about.
            json << R"("materials":[{"name":"Brushed","pbrMetallicRoughness":)"
                 << R"({"baseColorFactor":[1,1,1,1],"metallicFactor":0.9,"roughnessFactor":0.25,)"
                 << R"("baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1}},)"
                 << R"("normalTexture":{"index":2},"emissiveTexture":{"index":0},)"
                 << R"("occlusionTexture":{"index":)" << (fixture.withSeparateOcclusionMap ? 3 : 1)
                 << R"(},"emissiveFactor":[0,0,0]}],)"
                 << R"("textures":[{"source":0},{"source":1},{"source":2},{"source":3}],)"
                 << R"("images":[{"uri":"paint.png"},{"uri":"orm.png"},{"uri":"normal.png"},)"
                 << R"({"uri":"ao.png"}],)";
        }
        else if (fixture.withMaterial)
        {
            json << R"("materials":[{"name":"Painted","pbrMetallicRoughness":)"
                 << R"({"baseColorFactor":[0.25,0.5,0.75,0.5],"metallicFactor":0,"roughnessFactor":1,)"
                 << R"("baseColorTexture":{"index":0}},"emissiveFactor":[0.1,0.2,0.3]}],)"
                 << R"("textures":[{"source":0}],"images":[{"uri":"paint.png"}],)";
        }

        for (std::size_t animation = 0; animation < fixture.animationCount; ++animation)
        {
            json << (animation == 0 ? R"("animations":[)" : ",")
                 << R"({"samplers":[{"input":)" << animationInput
                 << R"(,"output":0,"interpolation":"LINEAR"}],)"
                 << R"("channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]})";
        }
        if (fixture.animationCount > 0) { json << R"(],)"; }

        const std::size_t bufferLength = buildFixtureBuffer(fixture).size();
        json << R"("buffers":[{"byteLength":)" << bufferLength;
        if (!bufferUri.empty()) { json << R"(,"uri":")" << bufferUri << R"(")"; }
        json << R"(}],"bufferViews":[)" << views.str() << R"(],"accessors":[)" << accessors.str()
             << "]}";
        return json.str();
    }

    /** @brief Writes @p fixture as a self-contained `.gltf` and returns its path. */
    std::filesystem::path writeGltf(const std::filesystem::path& directory, const GltfFixture& fixture)
    {
        const std::string buffer = buildFixtureBuffer(fixture);
        const std::string uri = fixture.externalBufferUri.empty()
                                    ? "data:application/octet-stream;base64," + toBase64(buffer)
                                    : fixture.externalBufferUri;

        const std::filesystem::path path = directory / "fixture.gltf";
        writeBinaryFile(path, buildFixtureJson(fixture, uri));
        return path;
    }

    /** @brief Writes @p fixture as a binary `.glb`: header, JSON chunk, BIN chunk. */
    std::filesystem::path writeGlb(const std::filesystem::path& directory, const GltfFixture& fixture)
    {
        std::string json = buildFixtureJson(fixture, std::string{});
        // Both chunks are four-byte aligned; the spec pads JSON with spaces and binary with zeros
        // so that a reader can walk them without knowing their contents.
        while (json.size() % 4 != 0) { json.push_back(' '); }

        std::string binary = buildFixtureBuffer(fixture);
        while (binary.size() % 4 != 0) { binary.push_back('\0'); }

        std::string glb;
        const std::uint32_t totalLength = static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + binary.size());

        appendLittleEndian(glb, static_cast<std::uint32_t>(0x46546C67));  // "glTF"
        appendLittleEndian(glb, static_cast<std::uint32_t>(2));
        appendLittleEndian(glb, totalLength);

        appendLittleEndian(glb, static_cast<std::uint32_t>(json.size()));
        appendLittleEndian(glb, static_cast<std::uint32_t>(0x4E4F534A));  // "JSON"
        glb += json;

        appendLittleEndian(glb, static_cast<std::uint32_t>(binary.size()));
        appendLittleEndian(glb, static_cast<std::uint32_t>(0x004E4942));  // "BIN\0"
        glb += binary;

        const std::filesystem::path path = directory / "fixture.glb";
        writeBinaryFile(path, glb);
        return path;
    }

    /**
     * @brief A triangle with one vertex a unit up glTF's +Y, with outward normals along +Z.
     *
     * The workhorse fixture. Its apex is what makes the Y mirror visible -- "up in the file" and
     * "up in the editor" are opposite signs, and a single vertex off the axis of symmetry is
     * enough to tell them apart.
     */
    GltfFixture makeTriangleFixture()
    {
        GltfFixture fixture;
        fixture.positions = {{{-1.0f, 0.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}};
        fixture.normals = {{{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f}}};
        fixture.indices = {0, 1, 2};
        return fixture;
    }

    bool nearlyEqual(float actual, float expected, float tolerance = 1e-4f)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    bool nearlyEqual(const StudioVector3& actual, const StudioVector3& expected,
                     float tolerance = 1e-4f)
    {
        return nearlyEqual(actual.x, expected.x, tolerance)
               && nearlyEqual(actual.y, expected.y, tolerance)
               && nearlyEqual(actual.z, expected.z, tolerance);
    }

    /** @brief Returns the vertex with the largest Y, which is the triangle fixture's apex. */
    const MeshVertex& highestVertex(const MeshPart& part)
    {
        const MeshVertex* best = &part.vertices[0];
        for (const MeshVertex& vertex : part.vertices)
        {
            if (vertex.position.y > best->position.y) { best = &vertex; }
        }
        return *best;
    }
}

CNA_STUDIO_TEST(AGltfTriangleImportsWithItsGeometry)
{
    const std::filesystem::path directory = makeScratchDirectory("gltf");
    const ModelImportResult imported = loadModel(writeGltf(directory, makeTriangleFixture()).string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    CNA_STUDIO_EXPECT_EQ(imported.mesh.parts.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(imported.mesh.getVertexCount(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(imported.mesh.getTriangleCount(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(imported.skippedPrimitives, std::size_t{0});
    CNA_STUDIO_EXPECT(imported.warnings.empty());
    CNA_STUDIO_EXPECT(!imported.mesh.isEmpty());
    CNA_STUDIO_EXPECT_EQ(imported.mesh.parts[0].name, std::string{"Fixture"});

    std::filesystem::remove_all(directory);
}

/**
 * The precondition plan.md calls out for day one of the model pipeline. glTF is Y-up, this
 * editor's world is Y-down, and a model that skipped the conversion would hang upside down from
 * the grid, the gizmos and every sprite around it. The fixture's apex is at glTF +1 on Y; in the
 * editor's world it must be at -1, which is up here.
 */
CNA_STUDIO_TEST(TheGltfImporterMirrorsGltfsYUpIntoTheStudiosYDownWorld)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfmirror");
    const ModelImportResult imported = loadModel(writeGltf(directory, makeTriangleFixture()).string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    const MeshVertex& apex = highestVertex(imported.mesh.parts[0]);

    // The apex is the *lowest* Y in the editor's world, so "highest" here found one of the base
    // vertices instead -- which is the whole point: after the mirror, the file's up is down.
    CNA_STUDIO_EXPECT(nearlyEqual(apex.position.y, 0.0f));

    bool foundMirroredApex = false;
    for (const MeshVertex& vertex : imported.mesh.parts[0].vertices)
    {
        if (nearlyEqual(vertex.position, StudioVector3{0.0f, -1.0f, 0.0f})) { foundMirroredApex = true; }
        // Nothing may survive at the file's own +Y: that would mean the mirror was skipped.
        CNA_STUDIO_EXPECT(vertex.position.y <= 1e-4f);
    }
    CNA_STUDIO_EXPECT(foundMirroredApex);

    std::filesystem::remove_all(directory);
}

/**
 * Mirroring one axis reverses the handedness of every triangle in the file. Left alone, the model
 * is inside-out the moment ED-402 turns backface culling on -- and looks perfectly correct until
 * then, which is what makes it worth a test now rather than a bug report later.
 */
CNA_STUDIO_TEST(TriangleWindingIsReversedToSurviveTheYMirror)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfwinding");
    const ModelImportResult imported = loadModel(writeGltf(directory, makeTriangleFixture()).string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    const MeshPart& part = imported.mesh.parts[0];

    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(part));

    // And specifically that the reversal happened, rather than the property holding by accident:
    // the file wound 0,1,2 and the importer must not have kept that order.
    CNA_STUDIO_EXPECT(part.indices == std::vector<std::uint32_t>({0, 2, 1}));

    std::filesystem::remove_all(directory);
}

/**
 * A node with a negative scale is already mirrored in the file -- authoring tools produce them
 * routinely -- so the importer's own mirror is its *second*, and the two cancel. Reversing the
 * winding unconditionally would turn exactly these models inside out.
 */
CNA_STUDIO_TEST(ANodeThatIsAlreadyMirroredIsNotMirroredTwice)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfdoublemirror");
    GltfFixture fixture = makeTriangleFixture();
    fixture.nodeScale = {{-1.0f, 1.0f, 1.0f}};

    const ModelImportResult imported = loadModel(writeGltf(directory, fixture).string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(imported.mesh.parts[0]));
    // Two mirrors cancel, so the file's own winding is the correct one here.
    CNA_STUDIO_EXPECT(imported.mesh.parts[0].indices == std::vector<std::uint32_t>({0, 1, 2}));

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(TheGltfImporterBakesNodeTransformsIntoPositions)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfnode");
    GltfFixture fixture = makeTriangleFixture();
    fixture.translation = {{10.0f, 2.0f, -3.0f}};

    const ModelImportResult imported = loadModel(writeGltf(directory, fixture).string());

    CNA_STUDIO_EXPECT(imported.succeeded);

    // The node's own translation is in glTF's frame, so it is mirrored along with the geometry:
    // the apex at glTF (0, 3, -3) becomes (0, -3, -3) here.
    bool foundApex = false;
    for (const MeshVertex& vertex : imported.mesh.parts[0].vertices)
    {
        if (nearlyEqual(vertex.position, StudioVector3{10.0f, -3.0f, -3.0f})) { foundApex = true; }
    }
    CNA_STUDIO_EXPECT(foundApex);

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(TheGltfImporterAppliesTheScaleFactorSetting)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfscale");
    const std::filesystem::path path = writeGltf(directory, makeTriangleFixture());

    ModelImportSettings settings;
    settings.scaleFactor = 100.0f;
    const ModelImportResult imported = loadModel(path.string(), settings);

    CNA_STUDIO_EXPECT(imported.succeeded);
    const StudioVector3 size = subtract(imported.mesh.boundsMax, imported.mesh.boundsMin);
    CNA_STUDIO_EXPECT(nearlyEqual(size.x, 200.0f, 1e-2f));
    CNA_STUDIO_EXPECT(nearlyEqual(size.y, 100.0f, 1e-2f));

    // A scale is not a rotation: it must not have disturbed the winding or the normals.
    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(imported.mesh.parts[0]));

    std::filesystem::remove_all(directory);
}

/**
 * Normals transform by the inverse transpose, not by the matrix that moves positions. Under a
 * non-uniform node scale the two differ, and using the wrong one tilts every normal away from the
 * surface -- a squashed model lit as though it were not squashed. The fixture squashes hard enough
 * that the naive answer is visibly wrong.
 */
CNA_STUDIO_TEST(NonUniformNodeScaleKeepsNormalsPerpendicularToTheSurface)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfnormalmatrix");

    // A triangle standing in the XY plane with its normal along +Y, then flattened on Y. The
    // naive transform would scale the normal by 0.01 and leave it pointing along +Y anyway; the
    // correct one has to keep it perpendicular to a surface that has itself been squashed.
    GltfFixture fixture;
    fixture.positions = {{{0.0f, 0.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}};
    fixture.normals = {{{0.0f, 1.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}};
    fixture.indices = {0, 2, 1};
    fixture.nodeScale = {{1.0f, 0.01f, 1.0f}};

    const ModelImportResult imported = loadModel(writeGltf(directory, fixture).string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    const MeshPart& part = imported.mesh.parts[0];

    for (const MeshVertex& vertex : part.vertices)
    {
        CNA_STUDIO_EXPECT(nearlyEqual(length(vertex.normal), 1.0f, 1e-3f));
        // Perpendicular to the flattened surface, which still lies in the XZ plane.
        CNA_STUDIO_EXPECT(nearlyEqual(std::fabs(vertex.normal.y), 1.0f, 1e-3f));
    }
    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(part));

    std::filesystem::remove_all(directory);
}

/**
 * A glTF need not carry normals, and a model with none must not arrive unlit. They are computed
 * per face and after the mirror, which makes them agree with the final winding by construction.
 * That construction is the point: it is the one path where the winding check cannot fail, so what
 * this case is actually for is the rest of it -- that normals appear at all, that they are unit
 * length, and that a shared vertex was split so each face can carry its own.
 */
CNA_STUDIO_TEST(AModelWithNoNormalsGetsFlatOnesThatAgreeWithItsWinding)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfnonormals");
    GltfFixture fixture = makeTriangleFixture();
    fixture.normals.clear();

    const ModelImportResult imported = loadModel(writeGltf(directory, fixture).string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    const MeshPart& part = imported.mesh.parts[0];

    for (const MeshVertex& vertex : part.vertices)
    {
        CNA_STUDIO_EXPECT(nearlyEqual(length(vertex.normal), 1.0f, 1e-3f));
    }
    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(part));

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(TriangleStripsAndFansBecomeTriangleLists)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfstrip");

    // Four vertices of a quad in the XY plane. As a strip that is two triangles; as a fan, two as
    // well -- but wound differently, which is the point of converting rather than reinterpreting.
    GltfFixture strip;
    strip.positions = {{{0.0f, 0.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}},
                       {{1.0f, 1.0f, 0.0f}}};
    strip.normals = {{{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f}},
                     {{0.0f, 0.0f, 1.0f}}};
    strip.indices = {0, 1, 2, 3};
    strip.primitiveMode = 5;

    const ModelImportResult stripped = loadModel(writeGltf(directory, strip).string());
    CNA_STUDIO_EXPECT(stripped.succeeded);
    CNA_STUDIO_EXPECT_EQ(stripped.mesh.getTriangleCount(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(stripped.skippedPrimitives, std::size_t{0});
    // The alternating winding a strip encodes has to be unpicked, or every other triangle faces
    // the wrong way and the quad is half invisible under culling.
    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(stripped.mesh.parts[0]));

    GltfFixture fan = strip;
    fan.primitiveMode = 6;
    fan.indices = {0, 1, 3, 2};
    const ModelImportResult fanned = loadModel(writeGltf(directory, fan).string());
    CNA_STUDIO_EXPECT(fanned.succeeded);
    CNA_STUDIO_EXPECT_EQ(fanned.mesh.getTriangleCount(), std::size_t{2});
    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(fanned.mesh.parts[0]));

    std::filesystem::remove_all(directory);
}

/**
 * A line list has no triangle in it, so there is nothing to draw and nothing to invent. It is
 * reported and left out -- the case exists because the alternative, silently importing an empty
 * model, is indistinguishable from a working importer until someone looks for the geometry.
 */
CNA_STUDIO_TEST(APrimitiveThatIsNotTrianglesIsReportedRatherThanSilentlyDropped)
{
    const std::filesystem::path directory = makeScratchDirectory("gltflines");
    GltfFixture fixture = makeTriangleFixture();
    fixture.primitiveMode = 1;

    const ModelImportResult imported = loadModel(writeGltf(directory, fixture).string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    CNA_STUDIO_EXPECT_EQ(imported.skippedPrimitives, std::size_t{1});
    CNA_STUDIO_EXPECT(imported.mesh.isEmpty());
    CNA_STUDIO_EXPECT(!imported.warnings.empty());

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(AGlbAndAGltfOfTheSameModelImportIdentically)
{
    const std::filesystem::path directory = makeScratchDirectory("glb");
    const GltfFixture fixture = makeTriangleFixture();

    const ModelImportResult fromText = loadModel(writeGltf(directory, fixture).string());
    const ModelImportResult fromBinary = loadModel(writeGlb(directory, fixture).string());

    CNA_STUDIO_EXPECT(fromText.succeeded);
    CNA_STUDIO_EXPECT(fromBinary.succeeded);
    CNA_STUDIO_EXPECT_EQ(fromBinary.mesh.getVertexCount(), fromText.mesh.getVertexCount());
    CNA_STUDIO_EXPECT_EQ(fromBinary.mesh.getTriangleCount(), fromText.mesh.getTriangleCount());
    CNA_STUDIO_EXPECT(nearlyEqual(fromBinary.mesh.boundsMin, fromText.mesh.boundsMin));
    CNA_STUDIO_EXPECT(nearlyEqual(fromBinary.mesh.boundsMax, fromText.mesh.boundsMax));

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(MaterialsComeAcrossAsMuchAsABasicEffectCanExpress)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfmaterial");
    GltfFixture fixture = makeTriangleFixture();
    fixture.withMaterial = true;
    fixture.texCoords = {{{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{0.5f, 1.0f}}};

    const std::filesystem::path path = writeGltf(directory, fixture);
    const ModelImportResult imported = loadModel(path.string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    CNA_STUDIO_EXPECT_EQ(imported.mesh.materials.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(imported.mesh.parts[0].materialIndex, 0);

    const MeshMaterial& material = imported.mesh.materials[0];
    CNA_STUDIO_EXPECT_EQ(material.name, std::string{"Painted"});
    CNA_STUDIO_EXPECT(nearlyEqual(material.diffuseColor, StudioVector3{0.25f, 0.5f, 0.75f}));
    CNA_STUDIO_EXPECT(nearlyEqual(material.emissiveColor, StudioVector3{0.1f, 0.2f, 0.3f}));
    CNA_STUDIO_EXPECT(nearlyEqual(material.alpha, 0.5f));
    // A path relative to the model file, for the caller to resolve against its asset database --
    // the importer has no database and must not pretend to.
    CNA_STUDIO_EXPECT_EQ(material.diffuseTexturePath, std::string{"paint.png"});

    // Texture coordinates come across unflipped: glTF, XNA and every CNA backend agree that the
    // origin is top-left, which makes this the one convention in the importer with nothing to do.
    bool foundApexUv = false;
    for (const MeshVertex& vertex : imported.mesh.parts[0].vertices)
    {
        if (nearlyEqual(vertex.texCoord.x, 0.5f) && nearlyEqual(vertex.texCoord.y, 1.0f))
        {
            foundApexUv = true;
        }
    }
    CNA_STUDIO_EXPECT(foundApexUv);

    ModelImportSettings withoutMaterials;
    withoutMaterials.importMaterials = false;
    const ModelImportResult bare = loadModel(path.string(), withoutMaterials);
    CNA_STUDIO_EXPECT(bare.mesh.materials.empty());
    CNA_STUDIO_EXPECT_EQ(bare.mesh.parts[0].materialIndex, -1);

    // The material above has no PBR maps, and the fields ED-402 added must still be the neutral
    // values a renderer can use rather than whatever was left in memory. Roughness 1 and metallic
    // 0 is glTF's own default: fully diffuse, non-metal.
    CNA_STUDIO_EXPECT(nearlyEqual(material.metallic, 0.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(material.roughness, 1.0f));
    CNA_STUDIO_EXPECT(material.normalTexturePath.empty());
    CNA_STUDIO_EXPECT(material.metallicRoughnessTexturePath.empty());

    std::filesystem::remove_all(directory);
}

/**
 * @brief ED-402: the metallic-roughness half of a material survives, beside the Blinn-Phong half.
 *
 * Both descriptions, because which one is drawn is a property of the *build*: `PbrEffect` is a CNA
 * extension and a backend without it falls back to `BasicEffect`. A material that carried only the
 * PBR fields would render as untextured grey on that fallback, which is a rendering bug that
 * appears on one machine and not another -- the worst kind to be told about.
 */
CNA_STUDIO_TEST(APbrMaterialCarriesItsMapsAndItsBlinnPhongApproximationTogether)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfpbr");
    GltfFixture fixture = makeTriangleFixture();
    fixture.withMaterial = true;
    fixture.withPbrMaps = true;
    fixture.texCoords = {{{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{0.5f, 1.0f}}};

    const std::filesystem::path path = writeGltf(directory, fixture);
    const ModelImportResult imported = loadModel(path.string());

    CNA_STUDIO_EXPECT(imported.succeeded);
    CNA_STUDIO_EXPECT_EQ(imported.mesh.materials.size(), std::size_t{1});

    const MeshMaterial& material = imported.mesh.materials[0];
    CNA_STUDIO_EXPECT(nearlyEqual(material.metallic, 0.9f));
    CNA_STUDIO_EXPECT(nearlyEqual(material.roughness, 0.25f));
    CNA_STUDIO_EXPECT_EQ(material.metallicRoughnessTexturePath, std::string{"orm.png"});
    CNA_STUDIO_EXPECT_EQ(material.normalTexturePath, std::string{"normal.png"});
    CNA_STUDIO_EXPECT_EQ(material.emissiveTexturePath, std::string{"paint.png"});

    // The derived half. A metal reflects its own base colour, so a metallic of 0.9 against a white
    // base must give a specular near white rather than the 0.04 a dielectric reflects -- that is
    // the one line of metallic-roughness that means the same thing in Blinn-Phong, and if the two
    // halves ever disagree it is because someone stopped deriving one from the other.
    CNA_STUDIO_EXPECT(material.specularColor.x > 0.8f);
    CNA_STUDIO_EXPECT(material.specularPower > 16.0f);

    // A packed ORM map is the ordinary case and says nothing.
    for (const ModelImportWarning& warning : imported.warnings)
    {
        CNA_STUDIO_EXPECT(warning.reason.find("occlusion") == std::string::npos);
    }

    std::filesystem::remove_all(directory);
}

/** @brief An occlusion map in its own file is reported, because only the packed form is carried. */
CNA_STUDIO_TEST(AnOcclusionMapInItsOwnFileIsReportedRatherThanHalfApplied)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfao");
    GltfFixture fixture = makeTriangleFixture();
    fixture.withMaterial = true;
    fixture.withPbrMaps = true;
    fixture.withSeparateOcclusionMap = true;
    fixture.texCoords = {{{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{0.5f, 1.0f}}};

    const std::filesystem::path path = writeGltf(directory, fixture);
    const ModelImportResult imported = loadModel(path.string());

    CNA_STUDIO_EXPECT(imported.succeeded);

    bool reported = false;
    for (const ModelImportWarning& warning : imported.warnings)
    {
        if (warning.reason.find("occlusion") != std::string::npos) { reported = true; }
    }
    CNA_STUDIO_EXPECT(reported);

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(AFileThatIsNotGltfIsReportedRatherThanGuessedAt)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfbroken");
    writeBinaryFile(directory / "notamodel.gltf", "this is not a model");

    const ModelImportResult imported = loadModel((directory / "notamodel.gltf").string());

    CNA_STUDIO_EXPECT(!imported.succeeded);
    CNA_STUDIO_EXPECT(!imported.warnings.empty());
    CNA_STUDIO_EXPECT(imported.mesh.isEmpty());

    // A missing file is the same kind of answer, not a crash.
    CNA_STUDIO_EXPECT(!loadModel((directory / "absent.gltf").string()).succeeded);
    CNA_STUDIO_EXPECT(!readModelDescription((directory / "absent.gltf").string()).has_value());

    std::filesystem::remove_all(directory);
}

/**
 * A `.gltf` whose `.bin` is missing parses perfectly and contains no vertices. Reporting that as a
 * successful import of an empty model is exactly the silent loss this importer exists to avoid.
 */
CNA_STUDIO_TEST(AGltfWhoseBufferIsMissingFailsRatherThanImportingNothing)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfnobin");
    GltfFixture fixture = makeTriangleFixture();
    fixture.externalBufferUri = "absent.bin";

    const ModelImportResult imported = loadModel(writeGltf(directory, fixture).string());

    CNA_STUDIO_EXPECT(!imported.succeeded);
    CNA_STUDIO_EXPECT(!imported.warnings.empty());

    // The same file with its buffer present must load, or the case above is testing the fixture
    // builder rather than the missing buffer.
    writeBinaryFile(directory / "absent.bin", buildFixtureBuffer(fixture));
    CNA_STUDIO_EXPECT(loadModel(writeGltf(directory, fixture).string()).succeeded);

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(ReadModelDescriptionReportsWhatTheModelContains)
{
    const std::filesystem::path directory = makeScratchDirectory("gltffacts");
    GltfFixture fixture = makeTriangleFixture();
    fixture.withMaterial = true;

    const std::optional<ModelDescription> description =
        readModelDescription(writeGltf(directory, fixture).string());

    CNA_STUDIO_EXPECT(description.has_value());
    if (description)
    {
        CNA_STUDIO_EXPECT_EQ(description->partCount, std::size_t{1});
        CNA_STUDIO_EXPECT_EQ(description->vertexCount, std::size_t{3});
        CNA_STUDIO_EXPECT_EQ(description->triangleCount, std::size_t{1});
        CNA_STUDIO_EXPECT_EQ(description->materialCount, std::size_t{1});
        CNA_STUDIO_EXPECT(nearlyEqual(description->size, StudioVector3{2.0f, 1.0f, 0.0f}));
    }

    std::filesystem::remove_all(directory);
}

/**
 * A glTF that will not load says why, in the loader's own words (`plan.md` STUDIO-10013).
 *
 * The reasons already existed -- "a .bin file beside it may be missing" is exactly what somebody
 * needs to hear -- and went into a struct that `readModelDescription` threw away, so the importer
 * above it had nothing to report but silence.
 */
CNA_STUDIO_TEST(AModelThatWillNotLoadCarriesItsReasonOutToTheImporter)
{
    const std::filesystem::path directory = makeScratchDirectory("modelwhy");
    GltfFixture fixture = makeTriangleFixture();

    // A .gltf naming an external buffer that is not beside it. This parses perfectly and has no
    // vertices in it, which is the silent loss the importer was written to avoid.
    fixture.externalBufferUri = "missing.bin";
    const std::filesystem::path file = directory / "Assets" / "prop.gltf";
    writeBinaryFile(file, buildFixtureJson(fixture, fixture.externalBufferUri));

    std::string problem;
    CNA_STUDIO_EXPECT(!readModelDescription(file.string(), {}, &problem));
    CNA_STUDIO_EXPECT(!problem.empty());
    CNA_STUDIO_EXPECT(problem.find(".bin") != std::string::npos);

    // A file that is not glTF at all still gets a reason, because a `.gltf` extension is a claim:
    // the asset is tracked as a model because somebody put it in the project as one.
    problem.clear();
    writeBinaryFile(directory / "Assets" / "notes.gltf", "this is not glTF");
    CNA_STUDIO_EXPECT(!readModelDescription((directory / "Assets" / "notes.gltf").string(), {},
                                            &problem));
    CNA_STUDIO_EXPECT(!problem.empty());

    // A model that loads but drops some primitives is a *success* with a count, not a failure: the
    // geometry that did come across is real and worth having.
    GltfFixture lines = makeTriangleFixture();
    lines.primitiveMode = 1;
    problem.clear();
    const std::optional<ModelDescription> partial =
        readModelDescription(writeGltf(directory, lines).string(), {}, &problem);
    CNA_STUDIO_EXPECT(partial.has_value());
    CNA_STUDIO_EXPECT(problem.empty());
    if (partial) { CNA_STUDIO_EXPECT_EQ(partial->skippedPrimitives, std::size_t{1}); }

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(ModelImportSettingsReadBackTheDeclaredDefaultsRatherThanZeroes)
{
    // The failure this rules out is not hypothetical: `importMaterials` reads back as *true* when
    // the sidecar does not carry it, and a reader that treated an absent field as a zero would
    // strip the materials off every model nobody had ever touched (`plan.md` STUDIO-10004).
    const ModelImportSettings untouched = ModelImportSettings::fromJson(JsonValue::makeObject());
    CNA_STUDIO_EXPECT(untouched.importMaterials);
    CNA_STUDIO_EXPECT(untouched.normals == ModelNormals::Import);
    CNA_STUDIO_EXPECT(nearlyEqual(untouched.scaleFactor, 1.0f));

    // An asset with no `importerSettings` object at all reads the same way.
    CNA_STUDIO_EXPECT(ModelImportSettings::fromJson(JsonValue{}).importMaterials);

    JsonValue chosen = JsonValue::makeObject();
    chosen.set("scaleFactor", JsonValue{0.01});
    chosen.set("importMaterials", JsonValue{false});
    chosen.set("normals", JsonValue{std::string{"Calculate"}});

    const ModelImportSettings edited = ModelImportSettings::fromJson(chosen);
    CNA_STUDIO_EXPECT(nearlyEqual(edited.scaleFactor, 0.01f));
    CNA_STUDIO_EXPECT(!edited.importMaterials);
    CNA_STUDIO_EXPECT(edited.normals == ModelNormals::Calculate);
}

/**
 * Turning materials off actually reaches the importer. This is the case the shared reader exists
 * for: both call sites used to read `scaleFactor` out of the sidecar by hand and neither read
 * `importMaterials`, so the inspector offered a checkbox that changed nothing anywhere.
 */
CNA_STUDIO_TEST(ImportMaterialsOffReachesTheImporterFromTheSidecar)
{
    const std::filesystem::path directory = makeScratchDirectory("modelnomaterials");
    GltfFixture fixture = makeTriangleFixture();
    fixture.withMaterial = true;

    const std::string gltf = buildFixtureJson(
        fixture, "data:application/octet-stream;base64," + toBase64(buildFixtureBuffer(fixture)));
    writeBinaryFile(directory / "Assets" / "prop.gltf", gltf);

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    CNA_STUDIO_EXPECT(database.scan("Assets").succeeded);

    const AssetRecord* record = database.findByPath("Assets/prop.gltf");
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record == nullptr)
    {
        std::filesystem::remove_all(directory);
        return;
    }

    const Uuid id = record->id;
    CNA_STUDIO_EXPECT(applyImporterFacts(database, id));
    CNA_STUDIO_EXPECT(nearlyEqual(
        static_cast<float>(database.find(id)->importerSettings["materialCount"].asNumber()), 1.0f));

    AssetRecord* mutableRecord = database.findMutable(id);
    CNA_STUDIO_EXPECT(mutableRecord != nullptr);
    if (mutableRecord->importerSettings.isNull())
    {
        mutableRecord->importerSettings = JsonValue::makeObject();
    }
    mutableRecord->importerSettings.set("importMaterials", JsonValue{false});

    // The facts follow the setting, because the facts describe what this asset imports as rather
    // than what the file would yield to somebody else's settings.
    CNA_STUDIO_EXPECT(applyImporterFacts(database, id));
    CNA_STUDIO_EXPECT(nearlyEqual(
        static_cast<float>(database.find(id)->importerSettings["materialCount"].asNumber()), 0.0f));

    // And the loader itself, which is where the change actually has to land.
    const ModelImportResult imported =
        loadModel(database.resolvePath(database.find(id)->sourcePath),
                  ModelImportSettings::fromJson(database.find(id)->importerSettings));
    CNA_STUDIO_EXPECT(imported.succeeded);
    CNA_STUDIO_EXPECT(imported.mesh.materials.empty());
    CNA_STUDIO_EXPECT(!imported.mesh.parts.empty());

    std::filesystem::remove_all(directory);
}

/**
 * `Normals: Calculate` throws the file's normals away rather than merely standing in for missing
 * ones. The distinction is the whole reason the setting exists: the file it is chosen for is the
 * one whose normals are *there* and wrong.
 */
CNA_STUDIO_TEST(CalculateNormalsIgnoresTheOnesTheFileCarries)
{
    const std::filesystem::path directory = makeScratchDirectory("gltfcalcnormals");

    // Normals pointing the wrong way -- into the surface rather than out of it, which is what a
    // bad export produces and what no amount of re-lighting fixes.
    GltfFixture fixture = makeTriangleFixture();
    for (auto& normal : fixture.normals) { normal = {{0.0f, 0.0f, -1.0f}}; }
    const std::filesystem::path file = writeGltf(directory, fixture);

    const ModelImportResult asImported = loadModel(file.string());
    CNA_STUDIO_EXPECT(asImported.succeeded);
    CNA_STUDIO_EXPECT(!meshWindingMatchesNormals(asImported.mesh.parts[0]));

    ModelImportSettings calculated;
    calculated.normals = ModelNormals::Calculate;
    const ModelImportResult recomputed = loadModel(file.string(), calculated);
    CNA_STUDIO_EXPECT(recomputed.succeeded);
    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(recomputed.mesh.parts[0]));

    for (const MeshVertex& vertex : recomputed.mesh.parts[0].vertices)
    {
        CNA_STUDIO_EXPECT(nearlyEqual(length(vertex.normal), 1.0f, 1e-3f));
    }

    // Flat normals cannot be shared, so the vertices are expanded to three per face. A user needs
    // to know that from the inspector, which is why the vertex count is a fact gathered with the
    // settings rather than one measured once and kept.
    const std::optional<ModelDescription> plain = readModelDescription(file.string());
    const std::optional<ModelDescription> flat = readModelDescription(file.string(), calculated);
    CNA_STUDIO_EXPECT(plain.has_value() && flat.has_value());
    if (plain && flat)
    {
        CNA_STUDIO_EXPECT_EQ(flat->vertexCount, flat->triangleCount * 3);
        CNA_STUDIO_EXPECT_EQ(plain->triangleCount, flat->triangleCount);
    }

    std::filesystem::remove_all(directory);
}

/**
 * What a model carries and loses is reported, where it used to be either invisible or a lie. The
 * animation count replaces an "Import Animations" checkbox that nothing read; the skipped-primitive
 * count has been gathered since the importer was written and went into a struct nobody looked at.
 */
CNA_STUDIO_TEST(AModelReportsTheAnimationsItCarriesAndThePrimitivesItLost)
{
    const std::filesystem::path directory = makeScratchDirectory("modelreports");

    GltfFixture animated = makeTriangleFixture();
    animated.animationCount = 3;
    const std::optional<ModelDescription> withAnimations =
        readModelDescription(writeGltf(directory, animated).string());

    CNA_STUDIO_EXPECT(withAnimations.has_value());
    if (withAnimations)
    {
        // Counted, not imported -- and saying so is the point. A file whose animations are its
        // reason for existing is one somebody needs to know Studio will not carry into a build.
        CNA_STUDIO_EXPECT_EQ(withAnimations->animationCount, std::size_t{3});
        CNA_STUDIO_EXPECT_EQ(withAnimations->triangleCount, std::size_t{1});
        CNA_STUDIO_EXPECT_EQ(withAnimations->skippedPrimitives, std::size_t{0});
    }

    GltfFixture lines = makeTriangleFixture();
    lines.primitiveMode = 1;
    const std::optional<ModelDescription> lost =
        readModelDescription(writeGltf(directory, lines).string());
    CNA_STUDIO_EXPECT(lost.has_value());
    if (lost)
    {
        CNA_STUDIO_EXPECT_EQ(lost->skippedPrimitives, std::size_t{1});
        CNA_STUDIO_EXPECT_EQ(lost->animationCount, std::size_t{0});
    }

    // And both reach the sidecar, which is where the inspector reads them from.
    writeBinaryFile(directory / "Assets" / "dancer.gltf",
                    buildFixtureJson(animated, "data:application/octet-stream;base64,"
                                                   + toBase64(buildFixtureBuffer(animated))));

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    CNA_STUDIO_EXPECT(database.scan("Assets").succeeded);
    const AssetRecord* record = database.findByPath("Assets/dancer.gltf");
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record != nullptr)
    {
        CNA_STUDIO_EXPECT(applyImporterFacts(database, record->id));
        CNA_STUDIO_EXPECT(nearlyEqual(
            static_cast<float>(database.find(record->id)->importerSettings["animationCount"].asNumber()),
            3.0f));
        CNA_STUDIO_EXPECT(
            !database.find(record->id)->importerSettings["skippedPrimitives"].isNull());
    }

    std::filesystem::remove_all(directory);
}

/**
 * The importer declares no setting it does not read. An "Import Animations" checkbox sat here for
 * a while doing nothing, documented as doing nothing -- which is not the same as a user finding
 * that out. This asserts the policy rather than the absence, so the next one is caught too.
 */
CNA_STUDIO_TEST(EverySettingTheModelImporterDeclaresIsOneTheImporterReads)
{
    ComponentRegistry importers;
    registerBuiltinImporters(importers);

    const ComponentDescriptor* model = importers.find(ImporterIds::kModel);
    CNA_STUDIO_EXPECT(model != nullptr);
    if (model == nullptr) { return; }

    // Every editable property must appear in the settings `fromJson` reads, which is checked by
    // setting it to something other than its default and requiring the struct to notice.
    for (const PropertyDescriptor& property : model->properties)
    {
        if (property.readOnly) { continue; }

        JsonValue settings = JsonValue::makeObject();
        if (property.type == PropertyType::Boolean)
        {
            settings.set(property.name, JsonValue{!property.defaultValue.get<bool>()});
        }
        else if (property.type == PropertyType::Float)
        {
            settings.set(property.name,
                         JsonValue{static_cast<double>(property.defaultValue.get<float>()) + 1.0});
        }
        else if (property.type == PropertyType::Enum)
        {
            CNA_STUDIO_EXPECT(property.enumOptions.size() > 1);
            settings.set(property.name, JsonValue{property.enumOptions.back()});
        }
        else
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         "the model importer declares a setting of a kind this "
                                         "case does not know how to change: " + property.name);
            continue;
        }

        const ModelImportSettings read = ModelImportSettings::fromJson(settings);
        const ModelImportSettings defaults;
        const bool noticed = !nearlyEqual(read.scaleFactor, defaults.scaleFactor)
                             || read.importMaterials != defaults.importMaterials
                             || read.normals != defaults.normals;
        if (!noticed)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         "the model importer declares '" + property.name
                                             + "', and changing it changes nothing the importer "
                                               "reads.");
        }
    }
}

/**
 * A model asset reports its facts into its sidecar, and reports them *once*. The second half is
 * what the case is really for: a facts pass that rewrote the sidecar on every open would turn
 * `--headless` into something that dirties a repository just by looking at it, and reading a whole
 * glTF is the most expensive way this editor has of doing that.
 */
CNA_STUDIO_TEST(AModelAssetReportsItsFactsIntoItsSidecarWithoutChurn)
{
    const std::filesystem::path directory = makeScratchDirectory("modelfacts");
    GltfFixture fixture = makeTriangleFixture();
    fixture.withMaterial = true;

    const std::string gltf = buildFixtureJson(
        fixture, "data:application/octet-stream;base64," + toBase64(buildFixtureBuffer(fixture)));
    writeBinaryFile(directory / "Assets" / "prop.gltf", gltf);

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    CNA_STUDIO_EXPECT(database.scan("Assets").succeeded);

    const AssetRecord* record = database.findByPath("Assets/prop.gltf");
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record == nullptr)
    {
        std::filesystem::remove_all(directory);
        return;
    }
    CNA_STUDIO_EXPECT(record->type == AssetType::Model);

    CNA_STUDIO_EXPECT_EQ(applyImporterFacts(database), std::size_t{1});

    record = database.findByPath("Assets/prop.gltf");
    CNA_STUDIO_EXPECT(nearlyEqual(static_cast<float>(record->importerSettings["triangleCount"].asNumber()),
                                  1.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(static_cast<float>(record->importerSettings["vertexCount"].asNumber()),
                                  3.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(static_cast<float>(record->importerSettings["materialCount"].asNumber()),
                                  1.0f));

    const StudioVector3 size =
        PropertyValue::fromJson(record->importerSettings["modelSize"], PropertyType::Vector3)
            .get<StudioVector3>();
    CNA_STUDIO_EXPECT(nearlyEqual(size, StudioVector3{2.0f, 1.0f, 0.0f}));

    // Nothing changed, so nothing is written -- the rule that keeps opening a project twice from
    // producing a diff.
    CNA_STUDIO_EXPECT_EQ(applyImporterFacts(database), std::size_t{0});

    std::filesystem::remove_all(directory);
}

/**
 * The seam, used end to end: a file on disk, through the importer, into the 3D view. This is the
 * case plan.md's ED-405 row was really asking for -- "design the seam that carries mesh data to
 * the viewport before writing the parser" -- because a seam nothing consumes is a guess. The
 * consumer here is CNA-free and runs in CI with no GPU, so the geometry is checked rather than
 * looked at, and ED-402's `VertexBuffer` pass inherits a `MeshData` that is already known to
 * survive the trip.
 */
CNA_STUDIO_TEST(AnImportedMeshIsDrawnByThe3DViewInsteadOfABadge)
{
    const std::filesystem::path directory = makeScratchDirectory("meshwireframe");
    const ModelImportResult imported = loadModel(writeGltf(directory, makeTriangleFixture()).string());
    CNA_STUDIO_EXPECT(imported.succeeded);

    const Uuid modelId = Uuid::generate();

    StudioEntity prop{Uuid::generate(), "Prop"};
    prop.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
    StudioComponent renderer{BuiltinComponentIds::kModelRenderer};
    renderer.setProperty("model", PropertyValue{PropertyValue::AssetReference{modelId}});
    prop.addComponent(std::move(renderer));

    SceneDocument scene;
    scene.addEntity(std::move(prop));

    StudioCamera3D camera;
    camera.setViewportSize(StudioVector2{640.0f, 480.0f});
    camera.frame(WorldBounds3D{StudioVector3{-2.0f, -2.0f, -2.0f}, StudioVector3{2.0f, 2.0f, 2.0f}});

    const SpriteSizeProvider sizes = [](const Uuid&) { return StudioVector2{0.0f, 0.0f}; };

    // Without a provider, the entity is a badge -- the behaviour every 3D view had before ED-405.
    WireframeOptions options;
    options.drawGrid = false;
    const WireframeResult withoutMesh = buildSceneWireframe(scene, camera, {}, sizes, options);
    CNA_STUDIO_EXPECT_EQ(withoutMesh.entitiesDrawn, std::size_t{1});

    // With one, it is the model's own edges. A triangle has three, and each is drawn once however
    // many faces claim it.
    options.meshProvider = [&](const Uuid& id) -> const MeshData* {
        return id == modelId ? &imported.mesh : nullptr;
    };
    const WireframeResult withMesh = buildSceneWireframe(scene, camera, {}, sizes, options);

    CNA_STUDIO_EXPECT_EQ(withMesh.entitiesDrawn, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(withMesh.segments.size(), std::size_t{3});
    CNA_STUDIO_EXPECT(!withMesh.truncated);

    // Turning the mesh edges off falls back to the box, which is what the shaded mode asks for
    // (`plan.md` STUDIO-11010). Before there was a shading mode the 3D view drew these edges over
    // the solid render permanently, so a scene with real geometry was always hatched.
    options.meshProvider = [&](const Uuid& id) -> const MeshData* {
        return id == modelId ? &imported.mesh : nullptr;
    };
    WireframeOptions boxesOnly = options;
    boxesOnly.drawMeshEdges = false;
    const WireframeResult shaded = buildSceneWireframe(scene, camera, {}, sizes, boxesOnly);

    CNA_STUDIO_EXPECT_EQ(shaded.entitiesDrawn, std::size_t{1});
    CNA_STUDIO_EXPECT(shaded.segments.size() != withMesh.segments.size());
    CNA_STUDIO_EXPECT_EQ(shaded.segments.size(), withoutMesh.segments.size());

    // A provider that has nothing for this id must fall back rather than draw nothing at all: an
    // asset still importing is not the same as an entity that vanished.
    options.meshProvider = [](const Uuid&) -> const MeshData* { return nullptr; };
    CNA_STUDIO_EXPECT_EQ(buildSceneWireframe(scene, camera, {}, sizes, options).entitiesDrawn,
                         std::size_t{1});

    std::filesystem::remove_all(directory);
}

/**
 * A dense model must not be able to spend the whole frame's segment budget, and what it draws when
 * it cannot fit must still look like the model. Sampling at a stride shows the whole shape sparsely;
 * stopping at the budget would show one corner of it completely, which reads as broken geometry
 * rather than as a full view.
 */
CNA_STUDIO_TEST(ADenseMeshIsDrawnSparselyRatherThanPartially)
{
    MeshData mesh;
    mesh.parts.emplace_back();
    MeshPart& part = mesh.parts[0];

    // A fan of a thousand triangles spread along X, so "the whole shape" and "one corner of it"
    // are visibly different answers.
    constexpr int kTriangles = 1000;
    for (int i = 0; i < kTriangles; ++i)
    {
        const float x = static_cast<float>(i);
        const std::uint32_t base = static_cast<std::uint32_t>(part.vertices.size());
        part.vertices.push_back(MeshVertex{StudioVector3{x, 0.0f, 0.0f}, StudioVector3{0, 0, 1}, {}});
        part.vertices.push_back(MeshVertex{StudioVector3{x + 1.0f, 0.0f, 0.0f}, StudioVector3{0, 0, 1}, {}});
        part.vertices.push_back(MeshVertex{StudioVector3{x, 1.0f, 0.0f}, StudioVector3{0, 0, 1}, {}});
        part.indices.insert(part.indices.end(), {base, base + 1, base + 2});
    }
    recomputeMeshBounds(mesh);

    StudioCamera3D camera;
    camera.setViewportSize(StudioVector2{640.0f, 480.0f});
    camera.frame(WorldBounds3D{mesh.boundsMin, mesh.boundsMax});

    std::vector<WireSegment> segments;
    bool truncated = false;
    constexpr std::size_t kBudget = 300;
    const std::size_t drawn = appendMeshEdges(segments, camera, mesh, StudioMatrix{},
                                              WireColors::kEntity, 1.0f, kBudget, truncated);

    CNA_STUDIO_EXPECT(drawn <= kBudget);
    CNA_STUDIO_EXPECT(drawn > 0);
    CNA_STUDIO_EXPECT(truncated);

    // The sampled triangles must span the model rather than cluster at its start: something drawn
    // beyond the far end of the first tenth is what tells a stride apart from a cut-off.
    float furthest = 0.0f;
    for (const WireSegment& segment : segments)
    {
        furthest = std::max(furthest, std::max(segment.from.x, segment.to.x));
    }
    float centre = 0.0f;
    const std::optional<StudioVector2> middleOfModel =
        camera.worldToScreen(StudioVector3{static_cast<float>(kTriangles) * 0.5f, 0.0f, 0.0f});
    CNA_STUDIO_EXPECT(middleOfModel.has_value());
    if (middleOfModel) { centre = middleOfModel->x; }
    CNA_STUDIO_EXPECT(furthest > centre);
}

/**
 * The cache exists so that a frame does not re-parse a glTF once per entity. Both halves matter:
 * that a second ask is free, and that a *failed* ask is remembered too -- an absent key would mean
 * "not tried yet" and send the importer back at a broken file sixty times a second.
 */
CNA_STUDIO_TEST(TheMeshCacheImportsOnceAndRemembersFailuresToo)
{
    const std::filesystem::path directory = makeScratchDirectory("meshcache");
    const GltfFixture fixture = makeTriangleFixture();
    writeBinaryFile(directory / "Assets" / "prop.gltf",
                    buildFixtureJson(fixture, "data:application/octet-stream;base64,"
                                                  + toBase64(buildFixtureBuffer(fixture))));
    writeBinaryFile(directory / "Assets" / "broken.gltf", "not a model at all");
    writeBinaryFile(directory / "Assets" / "art.png", "pixels");

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    CNA_STUDIO_EXPECT(database.scan("Assets").succeeded);

    const AssetRecord* prop = database.findByPath("Assets/prop.gltf");
    const AssetRecord* broken = database.findByPath("Assets/broken.gltf");
    const AssetRecord* texture = database.findByPath("Assets/art.png");
    CNA_STUDIO_EXPECT(prop != nullptr && broken != nullptr && texture != nullptr);
    if (prop == nullptr || broken == nullptr || texture == nullptr)
    {
        std::filesystem::remove_all(directory);
        return;
    }

    MeshCache cache;
    const MeshData* first = cache.get(database, prop->id);
    CNA_STUDIO_EXPECT(first != nullptr);

    // The same pointer, so the second ask neither re-read the file nor moved what the first one
    // handed out -- the 3D view holds these across a frame.
    CNA_STUDIO_EXPECT_EQ(cache.get(database, prop->id), first);
    CNA_STUDIO_EXPECT_EQ(cache.getVertexCount(), std::size_t{3});

    // A broken model, a non-model asset and an id that is nothing at all are all "nothing to
    // draw". The first two are *remembered* as such, so the broken file is not re-parsed every
    // frame; the third is not, because an id the database has not heard of may simply not have
    // been scanned yet, and a remembered "no" there would outlive the reason for it.
    CNA_STUDIO_EXPECT(cache.get(database, broken->id) == nullptr);
    CNA_STUDIO_EXPECT(cache.get(database, texture->id) == nullptr);
    CNA_STUDIO_EXPECT(cache.get(database, Uuid::generate()) == nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getEntryCount(), std::size_t{3});

    // Invalidation is what the asset watcher will call when a file changes on disk.
    cache.invalidate(prop->id);
    CNA_STUDIO_EXPECT_EQ(cache.getEntryCount(), std::size_t{2});
    CNA_STUDIO_EXPECT(cache.get(database, prop->id) != nullptr);

    cache.clear();
    CNA_STUDIO_EXPECT_EQ(cache.getEntryCount(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(cache.getVertexCount(), std::size_t{0});

    std::filesystem::remove_all(directory);
}

/**
 * The winding checker is an assertion the other cases lean on, so it needs one of its own: a
 * checker that returned true unconditionally would make half this file pass for nothing.
 */
CNA_STUDIO_TEST(TheWindingCheckActuallyCatchesAReversedTriangle)
{
    MeshPart part;
    part.vertices = {
        MeshVertex{StudioVector3{0.0f, 0.0f, 0.0f}, StudioVector3{0.0f, 0.0f, 1.0f}, StudioVector2{}},
        MeshVertex{StudioVector3{1.0f, 0.0f, 0.0f}, StudioVector3{0.0f, 0.0f, 1.0f}, StudioVector2{}},
        MeshVertex{StudioVector3{0.0f, 1.0f, 0.0f}, StudioVector3{0.0f, 0.0f, 1.0f}, StudioVector2{}}};

    part.indices = {0, 1, 2};
    CNA_STUDIO_EXPECT(meshWindingMatchesNormals(part));

    part.indices = {0, 2, 1};
    CNA_STUDIO_EXPECT(!meshWindingMatchesNormals(part));

    // An index past the end is not safe to draw either, and saying "well wound" about it would be
    // a read past the vector to find out.
    part.indices = {0, 1, 99};
    CNA_STUDIO_EXPECT(!meshWindingMatchesNormals(part));
}

/**
 * An empty model and a model that happens to sit at the origin must not report the same bounds --
 * the 3D view frames what it is given, and framing a point at the origin because a model failed
 * to load would be a camera flying somewhere for no reason.
 */
CNA_STUDIO_TEST(AnEmptyMeshReportsEmptyBoundsRatherThanAPointAtTheOrigin)
{
    MeshData empty;
    recomputeMeshBounds(empty);

    CNA_STUDIO_EXPECT(empty.isEmpty());
    CNA_STUDIO_EXPECT(empty.boundsMin.x > empty.boundsMax.x);
    CNA_STUDIO_EXPECT(empty.boundsMin.y > empty.boundsMax.y);
    CNA_STUDIO_EXPECT(empty.boundsMin.z > empty.boundsMax.z);

    MeshData atOrigin;
    atOrigin.parts.emplace_back();
    atOrigin.parts[0].vertices.emplace_back();
    recomputeMeshBounds(atOrigin);

    CNA_STUDIO_EXPECT(nearlyEqual(atOrigin.boundsMin, StudioVector3{}));
    CNA_STUDIO_EXPECT(nearlyEqual(atOrigin.boundsMax, StudioVector3{}));
}

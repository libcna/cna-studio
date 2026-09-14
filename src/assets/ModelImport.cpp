// SPDX-License-Identifier: MS-PL

#include "CNA/Editor/Assets/ModelImport.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <unordered_set>

#include "CNA/Editor/Core/EditorMatrix.hpp"

#include "cgltf_prefixed.h"

namespace CNA::Editor
{
    namespace
    {
        /**
         * @brief Owns a `cgltf_data*` so that every early return frees it.
         *
         * cgltf's contract is that a successful parse must be matched by `cgltf_free`, and the
         * loading path below has a dozen places it can give up -- an unloadable buffer, a failed
         * validation, a file with no scenes. One `goto`-free way to get that right is to make the
         * pointer own itself.
         */
        class GltfHandle
        {
        public:
            GltfHandle() = default;
            ~GltfHandle() { if (data != nullptr) { cgltf_free(data); } }

            GltfHandle(const GltfHandle&) = delete;
            GltfHandle& operator=(const GltfHandle&) = delete;
            GltfHandle(GltfHandle&&) = delete;
            GltfHandle& operator=(GltfHandle&&) = delete;

            cgltf_data* data = nullptr;
        };

        /**
         * @brief Converts a glTF node matrix into an `EditorMatrix`.
         *
         * A straight element-for-element copy, which is worth a sentence because it looks like it
         * should need a transpose and does not. glTF stores matrices column-major with the
         * translation at indices 12..14; `EditorMatrix` is XNA's row-vector layout with the
         * translation in `m41`..`m43`, which is field index 12..14 in declaration order. The two
         * conventions are transposes of each other *and* the two storage orders are transposes of
         * each other, so the pair cancels exactly.
         */
        EditorMatrix toEditorMatrix(const cgltf_float source[16])
        {
            EditorMatrix result;
            result.m11 = source[0];  result.m12 = source[1];  result.m13 = source[2];  result.m14 = source[3];
            result.m21 = source[4];  result.m22 = source[5];  result.m23 = source[6];  result.m24 = source[7];
            result.m31 = source[8];  result.m32 = source[9];  result.m33 = source[10]; result.m34 = source[11];
            result.m41 = source[12]; result.m42 = source[13]; result.m43 = source[14]; result.m44 = source[15];
            return result;
        }

        /** @brief Returns the determinant of @p matrix's upper-left 3x3, which is its handedness. */
        float determinant3x3(const EditorMatrix& matrix)
        {
            return matrix.m11 * (matrix.m22 * matrix.m33 - matrix.m23 * matrix.m32)
                   - matrix.m12 * (matrix.m21 * matrix.m33 - matrix.m23 * matrix.m31)
                   + matrix.m13 * (matrix.m21 * matrix.m32 - matrix.m22 * matrix.m31);
        }

        /**
         * @brief glTF's Y-up frame to this editor's Y-down world.
         *
         * The whole of the convention conversion, in one line, applied to positions and normals
         * alike. plan.md's ED-400 fixed the editor's world as Y-down so that the 2D and 3D views
         * agree about which way is down; glTF is Y-up; a model that skipped this would hang upside
         * down from the grid it is supposed to stand on.
         */
        EditorVector3 mirrorY(const EditorVector3& vector)
        {
            return EditorVector3{vector.x, -vector.y, vector.z};
        }

        /** @brief Reads @p count floats per element from @p accessor into @p out. */
        template <std::size_t kComponents>
        bool readAccessor(const cgltf_accessor* accessor, std::vector<float>& out)
        {
            if (accessor == nullptr) { return false; }

            out.assign(accessor->count * kComponents, 0.0f);
            for (cgltf_size element = 0; element < accessor->count; ++element)
            {
                if (cgltf_accessor_read_float(accessor, element, out.data() + element * kComponents,
                                              kComponents)
                    == 0)
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief Returns @p primitive's indices as a triangle list, or nothing when it is not one.
         *
         * Strips and fans are converted rather than refused: they are the same triangles in a
         * denser encoding, the conversion is four lines, and a model that used them would
         * otherwise arrive with holes in it. Lines and points are refused, because there is no
         * triangle in them to draw and inventing one would be worse than saying so.
         */
        std::optional<std::vector<std::uint32_t>> readTriangleIndices(const cgltf_primitive& primitive,
                                                                      std::size_t vertexCount)
        {
            // An index-free primitive draws its vertices in order, which glTF permits.
            std::vector<std::uint32_t> source;
            if (primitive.indices != nullptr)
            {
                source.reserve(primitive.indices->count);
                for (cgltf_size i = 0; i < primitive.indices->count; ++i)
                {
                    const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
                    if (index >= vertexCount) { return std::nullopt; }
                    source.push_back(static_cast<std::uint32_t>(index));
                }
            }
            else
            {
                source.resize(vertexCount);
                for (std::size_t i = 0; i < vertexCount; ++i)
                {
                    source[i] = static_cast<std::uint32_t>(i);
                }
            }

            std::vector<std::uint32_t> triangles;
            switch (primitive.type)
            {
                case cgltf_primitive_type_triangles:
                    if (source.size() % 3 != 0) { return std::nullopt; }
                    return source;

                case cgltf_primitive_type_triangle_strip:
                    if (source.size() < 3) { return std::vector<std::uint32_t>{}; }
                    for (std::size_t i = 0; i + 2 < source.size(); ++i)
                    {
                        // Every other triangle in a strip is wound the other way, and the encoding
                        // relies on the consumer swapping it back. Emitting them all in source
                        // order would produce a mesh that is inside-out in alternating stripes.
                        if (i % 2 == 0)
                        {
                            triangles.insert(triangles.end(), {source[i], source[i + 1], source[i + 2]});
                        }
                        else
                        {
                            triangles.insert(triangles.end(), {source[i + 1], source[i], source[i + 2]});
                        }
                    }
                    return triangles;

                case cgltf_primitive_type_triangle_fan:
                    if (source.size() < 3) { return std::vector<std::uint32_t>{}; }
                    for (std::size_t i = 1; i + 1 < source.size(); ++i)
                    {
                        triangles.insert(triangles.end(), {source[0], source[i], source[i + 1]});
                    }
                    return triangles;

                default:
                    return std::nullopt;
            }
        }

        /**
         * @brief Fills in flat per-face normals for a part whose glTF carried none.
         *
         * Computed after the mirror and the winding fix rather than before, which makes them right
         * by construction: the face normal of the final triangle is, by definition, the normal
         * that agrees with the final winding. Vertices are duplicated per face first, because a
         * shared vertex cannot carry two different flat normals -- a cube with eight vertices
         * would otherwise get one averaged normal per corner and light like a sphere.
         */
        void computeFlatNormals(MeshPart& part)
        {
            std::vector<MeshVertex> expanded;
            expanded.reserve(part.indices.size());
            std::vector<std::uint32_t> indices;
            indices.reserve(part.indices.size());

            for (std::size_t triangle = 0; triangle + 2 < part.indices.size(); triangle += 3)
            {
                const MeshVertex& v0 = part.vertices[part.indices[triangle]];
                const MeshVertex& v1 = part.vertices[part.indices[triangle + 1]];
                const MeshVertex& v2 = part.vertices[part.indices[triangle + 2]];

                EditorVector3 normal = cross(subtract(v1.position, v0.position),
                                             subtract(v2.position, v0.position));
                // A degenerate triangle has no normal to compute. `MeshVertex`'s default is used
                // rather than a zero, for the reason recorded on that field: zero lights as black
                // and reads as a renderer bug.
                normal = dot(normal, normal) > 0.0f ? normalize(normal) : EditorVector3{0.0f, 0.0f, 1.0f};

                for (const MeshVertex* source : {&v0, &v1, &v2})
                {
                    MeshVertex vertex = *source;
                    vertex.normal = normal;
                    indices.push_back(static_cast<std::uint32_t>(expanded.size()));
                    expanded.push_back(vertex);
                }
            }

            part.vertices = std::move(expanded);
            part.indices = std::move(indices);
        }

        /**
         * @brief Returns @p view's image URI, or empty when there is no file to point at.
         *
         * A data URI is the texture *itself* rather than a path to one, and reporting it as a path
         * would have a caller trying to resolve a hundred kilobytes of base64 against the project
         * directory. Empty is the honest answer in that case, and in the `.glb`-embedded case, for
         * the same reason: there is no file.
         */
        std::string textureUri(const cgltf_texture_view& view)
        {
            if (view.texture == nullptr || view.texture->image == nullptr
                || view.texture->image->uri == nullptr)
            {
                return {};
            }

            const std::string_view uri{view.texture->image->uri};
            if (uri.starts_with("data:")) { return {}; }
            return std::string{uri};
        }

        /**
         * @brief Converts one glTF material into both descriptions of itself.
         *
         * Both, because which one gets drawn is not this function's decision: ED-402 draws through
         * CNA's `PbrEffect` where the build has one and falls back to `BasicEffect` where it does
         * not, so the metallic-roughness fields and the Blinn-Phong ones are filled in together.
         * The Blinn-Phong pair is *derived* from the PBR pair right here, which is what stops the
         * two from ever describing different materials.
         */
        MeshMaterial convertMaterial(const cgltf_material& source,
                                     std::vector<ModelImportWarning>& warnings)
        {
            MeshMaterial material;
            material.name = source.name != nullptr ? source.name : std::string{};

            if (source.has_pbr_metallic_roughness != 0)
            {
                const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
                material.diffuseColor = EditorVector3{pbr.base_color_factor[0],
                                                      pbr.base_color_factor[1],
                                                      pbr.base_color_factor[2]};
                material.alpha = pbr.base_color_factor[3];

                // A metal reflects its own base colour and a dielectric reflects white. That is the
                // one line of metallic-roughness that survives the trip to Blinn-Phong meaning what
                // it meant, so it is the one that is carried across.
                const float metallic = pbr.metallic_factor;
                material.metallic = std::clamp(metallic, 0.0f, 1.0f);
                material.specularColor = EditorVector3{
                    metallic * material.diffuseColor.x + (1.0f - metallic) * 0.04f,
                    metallic * material.diffuseColor.y + (1.0f - metallic) * 0.04f,
                    metallic * material.diffuseColor.z + (1.0f - metallic) * 0.04f};

                // Roughness 0 is a mirror and roughness 1 is matte; a Blinn-Phong exponent runs the
                // other way and over a much larger range. The mapping is the conventional one and
                // is an approximation, not a conversion -- there is no exact answer here to get
                // wrong, which is exactly why it is written down.
                const float roughness = std::clamp(pbr.roughness_factor, 0.03f, 1.0f);
                material.specularPower = std::clamp(2.0f / (roughness * roughness) - 2.0f, 1.0f, 1024.0f);

                // The *unclamped* roughness, unlike the exponent above. The clamp exists because
                // roughness 0 sends that division to infinity; a PBR renderer wants the number the
                // file actually says, and 0 is a legitimate one there.
                material.roughness = std::clamp(pbr.roughness_factor, 0.0f, 1.0f);

                material.diffuseTexturePath = textureUri(pbr.base_color_texture);
                material.metallicRoughnessTexturePath = textureUri(pbr.metallic_roughness_texture);
            }

            material.emissiveColor = EditorVector3{source.emissive_factor[0],
                                                   source.emissive_factor[1],
                                                   source.emissive_factor[2]};
            material.normalTexturePath = textureUri(source.normal_texture);
            material.emissiveTexturePath = textureUri(source.emissive_texture);

            // glTF lets the occlusion map be its own image, and `PbrEffect` takes one. Where it is
            // the *same* image as the metallic-roughness map -- the packed ORM layout, which is
            // what nearly every exporter writes -- there is nothing to report, because the caller
            // already has that texture. Where it is a different file, say so rather than apply
            // half a material and leave the model looking subtly flat with no explanation.
            const std::string occlusionUri = textureUri(source.occlusion_texture);
            if (!occlusionUri.empty() && occlusionUri != material.metallicRoughnessTexturePath)
            {
                warnings.push_back(ModelImportWarning{
                    material.name.empty() ? occlusionUri : material.name,
                    "the occlusion map is a separate image from the metallic-roughness map, and "
                    "only the packed occlusion-roughness-metallic form is carried"});
            }

            return material;
        }

        /** @brief The state one `loadModel` call threads through the node walk. */
        struct Loader
        {
            const cgltf_data* data = nullptr;
            ModelImportSettings settings;
            ModelImportResult result;

            /** @brief Guards against a glTF whose node graph contains a cycle. */
            std::unordered_set<const cgltf_node*> visited;

            void addWarning(std::string subject, std::string reason)
            {
                // Bounded, because a file with a thousand broken primitives should produce a
                // report a person will read rather than one they will scroll past.
                constexpr std::size_t kMaximumWarnings = 32;
                if (result.warnings.size() >= kMaximumWarnings) { return; }
                result.warnings.push_back(ModelImportWarning{std::move(subject), std::move(reason)});
            }

            void addNode(const cgltf_node& node);
            void addPrimitive(const cgltf_primitive& primitive, const std::string& name,
                              const EditorMatrix& world);
        };

        void Loader::addPrimitive(const cgltf_primitive& primitive, const std::string& name,
                                  const EditorMatrix& world)
        {
            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals = nullptr;
            const cgltf_accessor* texCoords = nullptr;

            for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
            {
                const cgltf_attribute& attribute = primitive.attributes[i];
                switch (attribute.type)
                {
                    case cgltf_attribute_type_position: positions = attribute.data; break;
                    case cgltf_attribute_type_normal: normals = attribute.data; break;
                    // TEXCOORD_0 only. A second UV set has nothing in `MeshVertex` to go into and
                    // nothing in `BasicEffect` to sample with, so taking it would be storing data
                    // no one can use.
                    case cgltf_attribute_type_texcoord:
                        if (attribute.index == 0) { texCoords = attribute.data; }
                        break;
                    default: break;
                }
            }

            if (positions == nullptr)
            {
                ++result.skippedPrimitives;
                addWarning(name, "the primitive declares no POSITION attribute, so it has no geometry");
                return;
            }

            std::vector<float> positionData;
            if (!readAccessor<3>(positions, positionData))
            {
                ++result.skippedPrimitives;
                addWarning(name, "its vertex positions could not be read; a buffer may be missing");
                return;
            }
            const std::size_t vertexCount = positions->count;

            std::vector<float> normalData;
            const bool hasNormals = normals != nullptr && normals->count == positions->count
                                    && readAccessor<3>(normals, normalData);

            std::vector<float> texCoordData;
            const bool hasTexCoords = texCoords != nullptr && texCoords->count == positions->count
                                      && readAccessor<2>(texCoords, texCoordData);

            const std::optional<std::vector<std::uint32_t>> indices =
                readTriangleIndices(primitive, vertexCount);
            if (!indices)
            {
                ++result.skippedPrimitives;
                addWarning(name, "it is drawn as lines or points rather than triangles, which this "
                                 "editor has nothing to draw it with");
                return;
            }
            if (indices->empty()) { return; }

            // Normals do not transform by the matrix that moves positions. Under non-uniform scale
            // the inverse transpose is what keeps them perpendicular to the surface; a node scaled
            // to nothing on some axis has no such matrix, and the file's normals are used unchanged
            // rather than dropped -- a flattened model with its original normals is a better
            // failure than one lit as if it had none.
            const std::optional<EditorMatrix> normalMatrix = inverseTranspose(world);

            MeshPart part;
            part.name = name;
            part.materialIndex = -1;
            if (settings.importMaterials && primitive.material != nullptr)
            {
                part.materialIndex =
                    static_cast<int>(cgltf_material_index(data, primitive.material));
            }

            part.vertices.resize(vertexCount);
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                MeshVertex& vertex = part.vertices[i];

                const EditorVector3 local{positionData[i * 3], positionData[i * 3 + 1],
                                          positionData[i * 3 + 2]};
                vertex.position = scale(mirrorY(transformPosition(world, local)), settings.scaleFactor);

                if (hasNormals)
                {
                    const EditorVector3 sourceNormal{normalData[i * 3], normalData[i * 3 + 1],
                                                     normalData[i * 3 + 2]};
                    const EditorVector3 transformed =
                        normalMatrix ? transformDirection(*normalMatrix, sourceNormal) : sourceNormal;
                    const EditorVector3 mirrored = mirrorY(transformed);
                    vertex.normal = dot(mirrored, mirrored) > 0.0f ? normalize(mirrored)
                                                                   : EditorVector3{0.0f, 0.0f, 1.0f};
                }

                if (hasTexCoords)
                {
                    // No V flip. glTF puts its texture origin at the top left, and so do XNA and
                    // every backend CNA has; the one convention in this file that already agrees.
                    vertex.texCoord = EditorVector2{texCoordData[i * 2], texCoordData[i * 2 + 1]};
                }
            }

            part.indices = *indices;

            // Mirroring one axis reverses the handedness of every triangle in the file, so a
            // winding that was counter-clockwise-from-outside is now clockwise and the model is
            // inside-out under backface culling. A node that was *already* mirrored -- a negative
            // scale on one axis, which authoring tools produce routinely -- has flipped once
            // already, and the two flips cancel. So the test is on the sign of the total
            // transform, not on the mirror alone.
            if (determinant3x3(world) * -1.0f < 0.0f)
            {
                for (std::size_t triangle = 0; triangle + 2 < part.indices.size(); triangle += 3)
                {
                    std::swap(part.indices[triangle + 1], part.indices[triangle + 2]);
                }
            }

            if (!hasNormals) { computeFlatNormals(part); }

            result.mesh.parts.push_back(std::move(part));
        }

        void Loader::addNode(const cgltf_node& node)
        {
            // glTF's node graph is meant to be a tree, and a file whose parent pointers form a
            // cycle would send `cgltf_node_transform_world` and this walk round it forever.
            // Validation catches most such files; this catches the rest, cheaply.
            if (!visited.insert(&node).second) { return; }

            if (node.mesh != nullptr)
            {
                cgltf_float worldValues[16];
                cgltf_node_transform_world(&node, worldValues);
                const EditorMatrix world = toEditorMatrix(worldValues);

                const std::string meshName = node.mesh->name != nullptr ? node.mesh->name
                                             : node.name != nullptr     ? node.name
                                                                        : std::string{"mesh"};

                for (cgltf_size i = 0; i < node.mesh->primitives_count; ++i)
                {
                    // Suffixed only when there is more than one, so the common case -- a mesh with
                    // a single primitive -- keeps the name the author gave it.
                    const std::string partName =
                        node.mesh->primitives_count > 1
                            ? meshName + "." + std::to_string(static_cast<unsigned long long>(i))
                            : meshName;
                    addPrimitive(node.mesh->primitives[i], partName, world);
                }
            }

            for (cgltf_size i = 0; i < node.children_count; ++i)
            {
                if (node.children[i] != nullptr) { addNode(*node.children[i]); }
            }
        }
    }

    ModelImportResult loadModel(const std::string& absolutePath, const ModelImportSettings& settings)
    {
        ModelImportResult failure;
        // Bounds that mean "empty" rather than "a point at the origin", matching what
        // `recomputeMeshBounds` produces for a model with no vertices.
        recomputeMeshBounds(failure.mesh);

        cgltf_options options{};
        GltfHandle handle;

        if (cgltf_parse_file(&options, absolutePath.c_str(), &handle.data) != cgltf_result_success)
        {
            failure.warnings.push_back(
                ModelImportWarning{absolutePath, "the file could not be read as glTF or GLB"});
            return failure;
        }

        // Separate from the parse, and separately fatal: a `.gltf` whose `.bin` is missing parses
        // perfectly and has no vertices in it. Reporting that as a successful import of an empty
        // model is exactly the silent loss this importer is written to avoid.
        if (cgltf_load_buffers(&options, handle.data, absolutePath.c_str()) != cgltf_result_success)
        {
            failure.warnings.push_back(ModelImportWarning{
                absolutePath, "its buffers could not be loaded; a .bin file beside it may be missing"});
            return failure;
        }

        if (cgltf_validate(handle.data) != cgltf_result_success)
        {
            failure.warnings.push_back(
                ModelImportWarning{absolutePath, "the file is glTF but does not satisfy the spec"});
            return failure;
        }

        Loader loader;
        loader.data = handle.data;
        loader.settings = settings;

        // The file's own default scene, when it names one. A glTF may carry several scenes and a
        // default; walking every node instead would import a variant set as though it were one
        // model, with every variant occupying the same space.
        if (handle.data->scene != nullptr)
        {
            for (cgltf_size i = 0; i < handle.data->scene->nodes_count; ++i)
            {
                loader.addNode(*handle.data->scene->nodes[i]);
            }
        }
        else if (handle.data->scenes_count > 0)
        {
            for (cgltf_size i = 0; i < handle.data->scenes[0].nodes_count; ++i)
            {
                loader.addNode(*handle.data->scenes[0].nodes[i]);
            }
        }
        else
        {
            // No scene at all is legal glTF -- the file is then a library of meshes rather than an
            // arrangement of them. Every node is the only available reading of it.
            for (cgltf_size i = 0; i < handle.data->nodes_count; ++i)
            {
                loader.addNode(handle.data->nodes[i]);
            }
        }

        if (settings.importMaterials)
        {
            loader.result.mesh.materials.reserve(handle.data->materials_count);
            for (cgltf_size i = 0; i < handle.data->materials_count; ++i)
            {
                loader.result.mesh.materials.push_back(
                    convertMaterial(handle.data->materials[i], loader.result.warnings));
            }
        }

        if (loader.result.skippedPrimitives > 0)
        {
            loader.addWarning(absolutePath,
                              std::to_string(loader.result.skippedPrimitives)
                                  + " primitive(s) were left out of this model");
        }

        recomputeMeshBounds(loader.result.mesh);
        loader.result.succeeded = true;
        return loader.result;
    }

    std::optional<ModelDescription> readModelDescription(const std::string& absolutePath,
                                                         const ModelImportSettings& settings)
    {
        const ModelImportResult imported = loadModel(absolutePath, settings);
        if (!imported.succeeded) { return std::nullopt; }

        ModelDescription description;
        description.partCount = imported.mesh.parts.size();
        description.vertexCount = imported.mesh.getVertexCount();
        description.triangleCount = imported.mesh.getTriangleCount();
        description.materialCount = imported.mesh.materials.size();
        if (!imported.mesh.isEmpty())
        {
            description.size = subtract(imported.mesh.boundsMax, imported.mesh.boundsMin);
        }
        return description;
    }
}

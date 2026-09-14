// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file third_party/cgltf/cgltf_prefixed.h
 * @brief Includes cgltf with its public symbols renamed, so ours cannot clash with CNA's.
 *
 * Not upstream, and not optional. CNA vendors the *same* cgltf, compiles its implementation into
 * `CNA::Internal::GltfImport`, and cgltf declares its whole API inside `extern "C"` -- so in a
 * build that links both (`-DCNA_EDITOR_WITH_CNA=ON`) the two implementations define the same
 * unmangled symbols and the link fails outright with "multiple definition of cgltf_parse". A
 * namespace does not help: `extern "C"` is precisely the instruction to ignore one.
 *
 * The failure is worth describing because of where it does *not* appear. The default build has no
 * CNA in it, so the standalone configuration and CI both link cleanly with the clash present; only
 * the CNA configuration shows it, and only at the final executable. This header is what keeps the
 * two copies apart, and `ModelImport.cpp` and `cgltf_impl.cpp` must both include it rather than
 * `cgltf.h` directly -- one of them including the raw header would put the renamed declarations
 * and the unrenamed definitions in one program, which links and then calls the wrong one.
 *
 * The alternative considered and rejected was to drop this copy in the CNA build and use CNA's.
 * It fails on the module boundary: `cna-editor-assets` is CNA-free by D-03 and is linked into
 * `cna-player`, which is also CNA-free -- that binary would be left with cgltf symbols nothing
 * defines.
 */

#define cgltf_accessor_index cna_editor_cgltf_accessor_index
#define cgltf_accessor_read_float cna_editor_cgltf_accessor_read_float
#define cgltf_accessor_read_index cna_editor_cgltf_accessor_read_index
#define cgltf_accessor_read_uint cna_editor_cgltf_accessor_read_uint
#define cgltf_accessor_unpack_floats cna_editor_cgltf_accessor_unpack_floats
#define cgltf_accessor_unpack_indices cna_editor_cgltf_accessor_unpack_indices
#define cgltf_animation_channel_index cna_editor_cgltf_animation_channel_index
#define cgltf_animation_index cna_editor_cgltf_animation_index
#define cgltf_animation_sampler_index cna_editor_cgltf_animation_sampler_index
#define cgltf_buffer_index cna_editor_cgltf_buffer_index
#define cgltf_buffer_view_data cna_editor_cgltf_buffer_view_data
#define cgltf_buffer_view_index cna_editor_cgltf_buffer_view_index
#define cgltf_calc_size cna_editor_cgltf_calc_size
#define cgltf_camera_index cna_editor_cgltf_camera_index
#define cgltf_component_size cna_editor_cgltf_component_size
#define cgltf_copy_extras_json cna_editor_cgltf_copy_extras_json
#define cgltf_decode_string cna_editor_cgltf_decode_string
#define cgltf_decode_uri cna_editor_cgltf_decode_uri
#define cgltf_find_accessor cna_editor_cgltf_find_accessor
#define cgltf_free cna_editor_cgltf_free
#define cgltf_image_index cna_editor_cgltf_image_index
#define cgltf_light_index cna_editor_cgltf_light_index
#define cgltf_load_buffer_base64 cna_editor_cgltf_load_buffer_base64
#define cgltf_load_buffers cna_editor_cgltf_load_buffers
#define cgltf_material_index cna_editor_cgltf_material_index
#define cgltf_mesh_index cna_editor_cgltf_mesh_index
#define cgltf_node_index cna_editor_cgltf_node_index
#define cgltf_node_transform_local cna_editor_cgltf_node_transform_local
#define cgltf_node_transform_world cna_editor_cgltf_node_transform_world
#define cgltf_num_components cna_editor_cgltf_num_components
#define cgltf_parse cna_editor_cgltf_parse
#define cgltf_parse_file cna_editor_cgltf_parse_file
#define cgltf_sampler_index cna_editor_cgltf_sampler_index
#define cgltf_scene_index cna_editor_cgltf_scene_index
#define cgltf_skin_index cna_editor_cgltf_skin_index
#define cgltf_texture_index cna_editor_cgltf_texture_index
#define cgltf_validate cna_editor_cgltf_validate

#include "cgltf.h"

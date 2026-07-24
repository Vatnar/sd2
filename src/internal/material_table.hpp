#pragma once

#include "descriptor_writer.hpp"

namespace sd {

struct TextureId { U32 index; };

struct MaterialGpuData {
  S32 albedo_index = -1;
  S32 normal_index = -1;
  S32 orm_index = -1;
  S32 emissive_index = -1;
};

struct MaterialHandle { U32 index; };

// Material table: owns the shared material descriptor set,
// texture array, and material data SSBO.
struct MaterialTable {
  vk::Device device;
  desc::DescriptorSystem* descs;

  // Shared material set (set 1) for all materials
  desc::DescriptorSetHandle material_set;

  // Texture array (bound at binding 0)
  vk::ImageView texture_views[desc::MAX_TEXTURES];
  U32 texture_count{};
  vk::Sampler sampler;

  // Material data SSBO (bound at binding 1)
  vk::Buffer material_buffer;
  void* material_mapped;
  U32 material_count{};
  vk::DeviceSize buffer_capacity{};
};

struct MaterialTableConfig {
  vk::Device device;
  vk::PhysicalDevice phys_dev;
  VKArena* arena;
  VKGpuArena* host_arena;
  desc::DescriptorSystem* descs;
  vk::Sampler sampler;
  U32 max_textures = desc::MAX_TEXTURES;
  U32 max_materials = desc::MAX_MATERIALS;
};

internal MaterialTable create_material_table(MaterialTableConfig const& cfg) {
  MaterialTable mt{};
  mt.device = cfg.device;
  mt.descs = cfg.descs;
  mt.sampler = cfg.sampler;
  mt.texture_count = 0;

  // Allocate the shared material descriptor set
  desc::LayoutHandle mat_layout = get_layout(cfg.descs->layouts, desc::Set::Material);
  mt.material_set = alloc_set(cfg.device, &cfg.descs->persistent_pool, mat_layout);

  // Create material data SSBO
  vk::DeviceSize buf_size = sizeof(MaterialGpuData) * cfg.max_materials;
  auto [buf, alloc] = vk_create_buffer(
      cfg.phys_dev, cfg.device, buf_size,
      vk::BufferUsageFlagBits::eStorageBuffer,
      cfg.host_arena, cfg.arena);
  mt.material_buffer = buf;
  mt.material_mapped = alloc.mapped;
  mt.buffer_capacity = buf_size;

  return mt;
}

// Register a texture view into the table. Returns the index.
internal TextureId register_texture(MaterialTable* mt, vk::ImageView view) {
  U32 idx = mt->texture_count++;
  ASSERT_ALWAYS(idx < desc::MAX_TEXTURES);
  mt->texture_views[idx] = view;
  return {idx};
}

// Sync: write texture array + material buffer to the shared descriptor set.
// Call after all textures and materials are registered (or updated).
internal void material_table_sync(MaterialTable* mt) {
  // Write texture array (binding 0)
  // If no textures registered, write a dummy
  U32 tex_count = mt->texture_count;
  if (tex_count == 0) {
    // No textures - nothing to write
    return;
  }

  // Write all texture array elements
  for (U32 i = 0; i < tex_count; ++i) {
    sd::desc::DescriptorWriter w{mt->material_set};
    w.image(0, i,
            vk::DescriptorType::eCombinedImageSampler,
            mt->sampler,
            mt->texture_views[i],
            vk::ImageLayout::eShaderReadOnlyOptimal)
     .commit();
  }

  // Write material data SSBO (binding 1)
  if (mt->material_count > 0) {
    sd::desc::DescriptorWriter w{mt->material_set};
    w.buffer(1, 0,
             vk::DescriptorType::eStorageBuffer,
             mt->material_buffer,
             0,
             mt->material_count * sizeof(MaterialGpuData))
     .commit();
  }
}

// Create a material record. Call before finalize.
internal MaterialHandle create_material(
    MaterialTable* mt,
    MaterialGpuData const& gpu)
{
  U32 idx = mt->material_count++;
  ASSERT_ALWAYS(idx < desc::MAX_MATERIALS);

  // Write GPU data into the SSBO
  auto* data = static_cast<MaterialGpuData*>(mt->material_mapped) + idx;
  *data = gpu;
  return {idx};
}

} // namespace sd

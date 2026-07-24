#pragma once

#include "base.hpp"
#include "base_array.hpp"
#include "descriptor_schema.hpp"

namespace sd::desc {

//--- Pool size computation from schemas ---
struct PoolSizes {
  vk::DescriptorPoolSize sizes[8];
  U32 count{};
};

internal PoolSizes compute_pool_sizes(U32 const* set_counts) {
  PoolSizes result{};
  for (U32 s = 0; s < 3; ++s) {
    if (set_counts[s] == 0) continue;
    auto const& schema = schemas[s];
    for (U64 b = 0; b < schema.bindings.length; ++b) {
      auto const& binding = schema.bindings[b];
      U32 total = binding.count * set_counts[s];
      bool found = false;
      for (U32 i = 0; i < result.count; ++i) {
        if (result.sizes[i].type == binding.type) {
          result.sizes[i].descriptorCount += total;
          found = true;
          break;
        }
      }
      if (!found) {
        result.sizes[result.count++] = {binding.type, total};
      }
    }
  }
  return result;
}

//--- DescriptorArena wraps a vk::DescriptorPool ---
struct DescriptorArena {
  vk::DescriptorPool pool;
};

struct ArenaConfig {
  U32 max_sets;
  U32 set_counts[U32(Set::COUNT_)];
  vk::DescriptorPoolCreateFlags flags{};
};

internal DescriptorArena create_arena(
    vk::Device device,
    VKArena* arena,
    ArenaConfig const& config)
{
  PoolSizes ps = compute_pool_sizes(config.set_counts);

  vk::DescriptorPoolCreateInfo ci{
    .flags = config.flags,
    .maxSets = config.max_sets,
    .poolSizeCount = ps.count,
    .pPoolSizes = ps.sizes,
  };
  vk::DescriptorPool pool = arena->ds.push(
      vk_abort_if_error(device.createDescriptorPoolUnique(ci)));
  return {pool};
}

//--- DescriptorSetHandle carries layout + device for writer construction ---
struct DescriptorSetHandle {
  vk::DescriptorSet set;
  LayoutHandle layout;
  vk::Device device;
};

internal void reset_arena(vk::Device device, DescriptorArena* da) {
  device.resetDescriptorPool(da->pool, {});
}

internal DescriptorSetHandle alloc_set(
    vk::Device device,
    DescriptorArena* pool,
    LayoutHandle layout)
{
  vk::DescriptorSetLayout layout_handle = layout.handle;
  vk::DescriptorSetAllocateInfo ai{
    .descriptorPool = pool->pool,
    .descriptorSetCount = 1,
    .pSetLayouts = &layout_handle,
  };
  vk::DescriptorSet ds{};
  vk_abort_if_error(device.allocateDescriptorSets(&ai, &ds));
  return {ds, layout, device};
}

//--- Per-frame resources ---
struct FrameResources {
  vk::Buffer buffer{};
  void* mapped{};
};

//--- DescriptorSystem: owns all descriptor state ---
struct DescriptorSystem {
  vk::Device device;
  vk::PhysicalDevice phys_dev;
  DescriptorLayoutCache layouts;
  DescriptorArena persistent_pool;
  DescriptorArena frame_pools[MAX_FRAMES_IN_FLIGHT];
  U32 frames_in_flight;
  FrameResources frame_resources[MAX_FRAMES_IN_FLIGHT];
  vk::DeviceSize globals_size;
};

struct SystemConfig {
  vk::PhysicalDevice phys_dev;
  vk::Device device;
  VKArena* arena;
  VKGpuArena* host_arena;
  U32 frames_in_flight;
  vk::DeviceSize globals_size;
  ArenaConfig persistent_config;
  ArenaConfig frame_config;
};

internal DescriptorSystem create_system(SystemConfig const& cfg) {
  DescriptorSystem sys{};
  sys.device = cfg.device;
  sys.phys_dev = cfg.phys_dev;
  sys.frames_in_flight = cfg.frames_in_flight;
  sys.globals_size = cfg.globals_size;
  sys.layouts = create_layouts(cfg.device, cfg.arena);
  sys.persistent_pool = create_arena(cfg.device, cfg.arena, cfg.persistent_config);
  for (U32 i = 0; i < cfg.frames_in_flight; ++i) {
    sys.frame_pools[i] = create_arena(cfg.device, cfg.arena, cfg.frame_config);
  }

  for (U32 i = 0; i < cfg.frames_in_flight; ++i) {
    auto [buf, alloc] = vk_create_buffer(
        cfg.phys_dev,
        cfg.device,
        cfg.globals_size,
        vk::BufferUsageFlagBits::eUniformBuffer,
        cfg.host_arena,
        cfg.arena);
    sys.frame_resources[i] = {buf, alloc.mapped};
  }
  return sys;
}

//--- FrameContext returned by begin_frame ---
struct FrameContext {
  DescriptorSetHandle frame_set;
  vk::Buffer globals_buffer;
  void* globals_mapped;
  DescriptorArena* transient_pool;
  LayoutHandle draw_layout;
  LayoutHandle frame_layout;
  vk::Device device;
  vk::DeviceSize globals_size;
};

internal FrameContext begin_frame(DescriptorSystem* sys, U32 frame_index) {
  DescriptorArena* pool = &sys->frame_pools[frame_index];
  reset_arena(sys->device, pool);

  LayoutHandle fl = get_layout(sys->layouts, Set::Frame);
  DescriptorSetHandle frame_set = alloc_set(sys->device, pool, fl);
  LayoutHandle dl = get_layout(sys->layouts, Set::Draw);

  return {
    .frame_set = frame_set,
    .globals_buffer = sys->frame_resources[frame_index].buffer,
    .globals_mapped = sys->frame_resources[frame_index].mapped,
    .transient_pool = pool,
    .draw_layout = dl,
    .frame_layout = fl,
    .device = sys->device,
    .globals_size = sys->globals_size,
  };
}

//--- Convenience: allocate a transient draw set from frame context ---
internal DescriptorSetHandle alloc_draw_set(FrameContext& fc) {
  return alloc_set(fc.device, fc.transient_pool, fc.draw_layout);
}

} // namespace sd::desc

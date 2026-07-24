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

//--- Per-frame upload arena for CPU→GPU data ---
enum class UploadUsage : U8 { Vertex, Uniform, Storage };

struct UploadSlice {
  vk::Buffer buffer;
  U8* base;
  U64 offset;
  U64 size;
};

struct FrameUpload {
  vk::Buffer buffer;
  U8* mapped;
  U64 capacity;
  U64 head;
  U64 uniform_align;
  U64 storage_align;
  U64 peak_this_frame{};
  U64 peak_ever{};

  UploadSlice alloc(U64 size, U64 alignment) {
    U64 off = (head + alignment - 1) & ~(alignment - 1);
    ASSERT_ALWAYS(off + size <= capacity && "FrameUpload overflow");
    head = off + size;
    peak_this_frame = max(peak_this_frame, head);
    peak_ever = max(peak_ever, peak_this_frame);

    return {buffer, mapped, off, size};
  }
};

//--- DescriptorSystem: owns all descriptor state ---
struct DescriptorSystem {
  vk::Device device;
  vk::PhysicalDevice phys_dev;
  DescriptorLayoutCache layouts;
  DescriptorArena persistent_pool;
  DescriptorArena frame_pools[MAX_FRAMES_IN_FLIGHT];
  U32 frames_in_flight;
  FrameUpload frame_uploads[MAX_FRAMES_IN_FLIGHT];
  vk::DeviceSize upload_capacity;
};

struct SystemConfig {
  vk::PhysicalDevice phys_dev;
  vk::Device device;
  VKArena* arena;
  VKGpuArena* host_arena;
  U32 frames_in_flight;
  vk::DeviceSize upload_capacity;
  vk::DeviceSize uniform_align;
  vk::DeviceSize storage_align;
  ArenaConfig persistent_config;
  ArenaConfig frame_config;
};

internal DescriptorSystem create_system(SystemConfig const& cfg) {
  DescriptorSystem sys{};
  sys.device = cfg.device;
  sys.phys_dev = cfg.phys_dev;
  sys.frames_in_flight = cfg.frames_in_flight;
  sys.upload_capacity = cfg.upload_capacity;
  sys.layouts = create_layouts(cfg.device, cfg.arena);
  sys.persistent_pool = create_arena(cfg.device, cfg.arena, cfg.persistent_config);
  for (U32 i = 0; i < cfg.frames_in_flight; ++i) {
    sys.frame_pools[i] = create_arena(cfg.device, cfg.arena, cfg.frame_config);
  }

  vk::BufferUsageFlags upload_usage =
      vk::BufferUsageFlagBits::eUniformBuffer |
      vk::BufferUsageFlagBits::eStorageBuffer |
      vk::BufferUsageFlagBits::eVertexBuffer;
  for (U32 i = 0; i < cfg.frames_in_flight; ++i) {
    auto [buf, alloc] = vk_create_buffer(
        cfg.phys_dev, cfg.device, cfg.upload_capacity,
        upload_usage, cfg.host_arena, cfg.arena);
    sys.frame_uploads[i] = {
        .buffer = buf,
        .mapped = static_cast<U8*>(alloc.mapped),
        .capacity = cfg.upload_capacity,
        .head = 0,
        .uniform_align = cfg.uniform_align,
        .storage_align = cfg.storage_align,
    };
  }
  return sys;
}

//--- FrameContext returned by begin_frame ---
struct FrameContext {
  DescriptorSetHandle frame_set;
  FrameUpload* upload;
  DescriptorArena* transient_pool;
  LayoutHandle draw_layout;
  LayoutHandle frame_layout;
  vk::Device device;
};

internal FrameContext begin_frame(DescriptorSystem* sys, U32 frame_index) {
  DescriptorArena* pool = &sys->frame_pools[frame_index];
  reset_arena(sys->device, pool);

  FrameUpload* fu = &sys->frame_uploads[frame_index];
  fu->head = 0;
  fu->peak_this_frame = 0;

  LayoutHandle fl = get_layout(sys->layouts, Set::Frame);
  DescriptorSetHandle frame_set = alloc_set(sys->device, pool, fl);
  LayoutHandle dl = get_layout(sys->layouts, Set::Draw);

  return {
    .frame_set = frame_set,
    .upload = fu,
    .transient_pool = pool,
    .draw_layout = dl,
    .frame_layout = fl,
    .device = sys->device,
  };
}

//--- Convenience: allocate a transient draw set from frame context ---
internal DescriptorSetHandle alloc_draw_set(FrameContext& fc) {
  return alloc_set(fc.device, fc.transient_pool, fc.draw_layout);
}

//--- Upload helpers ---
internal U64 upload_alignment(FrameUpload* fu, UploadUsage usage) {
  switch (usage) {
    case UploadUsage::Uniform: return fu->uniform_align;
    case UploadUsage::Storage: return fu->storage_align;
    case UploadUsage::Vertex:  return 16;
  }
  return 16;
}

internal UploadSlice upload_alloc(FrameUpload* fu, U64 size, U64 alignment) {
  return fu->alloc(size, alignment);
}

internal UploadSlice upload_alloc_usage(FrameUpload* fu, U64 size, UploadUsage usage) {
  return fu->alloc(size, upload_alignment(fu, usage));
}

internal U64 upload_allocs_this_frame(FrameUpload* fu) {
  return fu->peak_this_frame;
}

template<typename T>
internal UploadSlice upload_struct(FrameUpload* fu, T const& v, UploadUsage usage) {
  U64 align = max(alignof(T), upload_alignment(fu, usage));
  auto s = fu->alloc(sizeof(T), align);
  MemoryCopy(s.base + s.offset, &v, sizeof(T));
  return s;
}

template<typename T>
internal UploadSlice upload_span(FrameUpload* fu, ArraySlice<T> xs, UploadUsage usage) {
  U64 bytes = xs.length * sizeof(T);
  U64 align = upload_alignment(fu, usage);
  auto s = fu->alloc(bytes, align);
  MemoryCopy(s.base + s.offset, xs.data, bytes);
  return s;
}

} // namespace sd::desc

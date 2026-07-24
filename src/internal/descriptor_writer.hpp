#pragma once

#include "descriptor_arena.hpp"

namespace sd::desc {

struct DescriptorWriter {
  DescriptorSetHandle dst;

  vk::WriteDescriptorSet writes[16];
  vk::DescriptorBufferInfo buffer_infos[16];
  vk::DescriptorImageInfo image_infos[16];
  U32 write_count{};
  U32 buffer_count{};
  U32 image_count{};

  explicit DescriptorWriter(DescriptorSetHandle dst_) : dst(dst_) {}

#ifdef SD2_DEBUG
  bool error{};
#endif

  DescriptorWriter& buffer(
      U32 binding,
      U32 array_element,
      vk::DescriptorType type,
      vk::Buffer buffer,
      vk::DeviceSize offset,
      vk::DeviceSize range)
  {
    SetSchema const& schema = *dst.layout.schema;

#ifdef SD2_DEBUG
    bool found = false;
    for (U64 i = 0; i < schema.bindings.length; ++i) {
      auto const& b = schema.bindings[i];
      if (b.binding == binding) {
        ASSERT_ALWAYS(b.type == type && "Descriptor type mismatch");
        ASSERT_ALWAYS(array_element < b.count && "Array element out of range");
        found = true;
        break;
      }
    }
    ASSERT_ALWAYS(found && "Binding not found in schema");
#endif

    buffer_infos[buffer_count].buffer = buffer;
    buffer_infos[buffer_count].offset = offset;
    buffer_infos[buffer_count].range = range;
    writes[write_count].dstSet = dst.set;
    writes[write_count].dstBinding = binding;
    writes[write_count].dstArrayElement = array_element;
    writes[write_count].descriptorCount = 1;
    writes[write_count].descriptorType = type;
    writes[write_count].pBufferInfo = &buffer_infos[buffer_count];
    write_count++;
    buffer_count++;
    return *this;
  }

  DescriptorWriter& image(
      U32 binding,
      U32 array_element,
      vk::DescriptorType type,
      vk::Sampler sampler,
      vk::ImageView view,
      vk::ImageLayout layout)
  {
    SetSchema const& schema = *dst.layout.schema;

#ifdef SD2_DEBUG
    bool found = false;
    for (U64 i = 0; i < schema.bindings.length; ++i) {
      auto const& b = schema.bindings[i];
      if (b.binding == binding) {
        ASSERT_ALWAYS(b.type == type && "Descriptor type mismatch");
        ASSERT_ALWAYS(array_element < b.count && "Array element out of range");
        found = true;
        break;
      }
    }
    ASSERT_ALWAYS(found && "Binding not found in schema");
#endif

    image_infos[image_count].sampler = sampler;
    image_infos[image_count].imageView = view;
    image_infos[image_count].imageLayout = layout;
    writes[write_count].dstSet = dst.set;
    writes[write_count].dstBinding = binding;
    writes[write_count].dstArrayElement = array_element;
    writes[write_count].descriptorCount = 1;
    writes[write_count].descriptorType = type;
    writes[write_count].pImageInfo = &image_infos[image_count];
    write_count++;
    image_count++;
    return *this;
  }

  void commit() {
    if (write_count == 0) return;
    dst.device.updateDescriptorSets(
        static_cast<U32>(write_count), writes, 0, nullptr);
  }

  //--- Typed binding overloads ---
  DescriptorWriter& buffer(FrameBinding b, vk::Buffer buf, vk::DeviceSize offset, vk::DeviceSize range) {
    return buffer(U32(b), 0, vk::DescriptorType::eUniformBuffer, buf, offset, range);
  }
  DescriptorWriter& buffer(DrawBinding b, vk::Buffer buf, vk::DeviceSize offset, vk::DeviceSize range) {
    return buffer(U32(b), 0, vk::DescriptorType::eStorageBuffer, buf, offset, range);
  }
};

//--- Convenience: upload globals data + write the descriptor ---
internal void upload_globals(FrameContext& fc, void const* data, U64 size) {
  MemoryCopy(fc.globals_mapped, data, size);
  DescriptorWriter{fc.frame_set}
    .buffer(FrameBinding::Globals, fc.globals_buffer, 0, size)
    .commit();
}

template<typename T>
internal void upload_globals(FrameContext& fc, T const& data) {
  ASSERT_ALWAYS(sizeof(T) <= fc.globals_size && "Globals type exceeds globals buffer size");
  upload_globals(fc, &data, sizeof(T));
}

//--- Write helper that deduces device from the set handle ---
internal DescriptorWriter begin_write(DescriptorSetHandle dst) {
  return DescriptorWriter{dst};
}

} // namespace sd::desc

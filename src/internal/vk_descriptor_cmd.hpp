#pragma once

#include "descriptor_schema.hpp"

namespace sd::desc {

#ifdef SD2_DEBUG
internal bool pipeline_has_set(PipelineLayout const& layout, Set set) {
  return layout.slot[U32(set)] != 0xFF;
}
#endif

} // namespace sd::desc

namespace sd::cmd {

#ifdef SD2_DEBUG
internal void validate_bind_set(
    desc::PipelineLayout const& layout,
    desc::DescriptorSetHandle set,
    desc::Set set_enum)
{
  U32 idx = U32(set_enum);
  ASSERT_ALWAYS(layout.slot[idx] != 0xFF && "Set not in pipeline layout");
  ASSERT_ALWAYS(layout.expected[idx] != VK_NULL_HANDLE && "Expected layout is null");
  ASSERT_ALWAYS(set.layout.handle == layout.expected[idx] && "Descriptor set layout mismatch");
}
#endif

internal void bind_frame_set(
    vk::CommandBuffer cmd,
    desc::PipelineLayout const& layout,
    desc::DescriptorSetHandle set)
{
#ifdef SD2_DEBUG
  validate_bind_set(layout, set, desc::Set::Frame);
#endif
  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      layout.handle,
      layout.slot[U32(desc::Set::Frame)],
      set.set,
      nullptr);
}

internal void bind_material_set(
    vk::CommandBuffer cmd,
    desc::PipelineLayout const& layout,
    desc::DescriptorSetHandle set)
{
#ifdef SD2_DEBUG
  validate_bind_set(layout, set, desc::Set::Material);
#endif
  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      layout.handle,
      layout.slot[U32(desc::Set::Material)],
      set.set,
      nullptr);
}

internal void bind_draw_set(
    vk::CommandBuffer cmd,
    desc::PipelineLayout const& layout,
    desc::DescriptorSetHandle set)
{
#ifdef SD2_DEBUG
  validate_bind_set(layout, set, desc::Set::Draw);
#endif
  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      layout.handle,
      layout.slot[U32(desc::Set::Draw)],
      set.set,
      nullptr);
}

} // namespace sd::cmd

#pragma once

#include "base.hpp"
#include "base_array.hpp"
#include "base_string.hpp"
#include "vk_arena.hpp"
#include "vk_helpers.hpp"

namespace sd::desc {

constexpr U32 MAX_TEXTURES = 256;
constexpr U32 MAX_MATERIALS = 1024;

enum class Set : U32 {
  Frame    = 0,
  Material = 1,
  Draw     = 2,
  COUNT_
};

enum class FrameBinding : U32    { Globals = 0 };
enum class MaterialBinding : U32 { Textures = 0, MaterialBuffer = 1 };
enum class DrawBinding : U32     { Payload = 0 };

struct BindingSpec {
  U32 binding;
  vk::DescriptorType type;
  U32 count;
  vk::ShaderStageFlags stages;
  vk::DescriptorBindingFlags flags{};
};

struct SetSchema {
  String8 name;
  ArraySlice<BindingSpec> bindings;
};

//--- Frame set (set 0) ---
inline BindingSpec frame_bindings[] = {
  {0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex},
};

//--- Material set (set 1) ---
inline BindingSpec material_bindings[] = {
  {0, vk::DescriptorType::eCombinedImageSampler, MAX_TEXTURES,
   vk::ShaderStageFlagBits::eFragment,
   vk::DescriptorBindingFlagBits::ePartiallyBound |
   vk::DescriptorBindingFlagBits::eUpdateAfterBind},
  {1, vk::DescriptorType::eStorageBuffer, 1,
   vk::ShaderStageFlagBits::eFragment},
};

//--- Draw set (set 2) ---
inline BindingSpec draw_bindings[] = {
  {0, vk::DescriptorType::eStorageBuffer, 1,
   vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
};

inline SetSchema schemas[] = {
  {str8_lit("frame"),    {frame_bindings,    1}},
  {str8_lit("material"), {material_bindings, 2}},
  {str8_lit("draw"),     {draw_bindings,     1}},
};

internal SetSchema const& get_schema(Set set) {
  return schemas[U32(set)];
}

//--- LayoutHandle ---
struct LayoutHandle {
  vk::DescriptorSetLayout handle;
  SetSchema const* schema;
};

//--- Layout cache (creates all 3 layouts from schemas) ---
struct DescriptorLayoutCache {
  LayoutHandle layouts[3];
};

internal bool layout_has_update_after_bind(SetSchema const& s) {
  for (U64 i = 0; i < s.bindings.length; ++i) {
    if (static_cast<bool>(s.bindings[i].flags & vk::DescriptorBindingFlagBits::eUpdateAfterBind))
      return true;
  }
  return false;
}

internal DescriptorLayoutCache create_layouts(
    vk::Device device,
    VKArena* arena)
{
  DescriptorLayoutCache cache{};
  for (U32 i = 0; i < 3; ++i) {
    auto const& s = schemas[i];

    TempScope scratch = scratch_begin_scoped(0, 0);
    Arena* sa = scratch;

    auto* vk_bindings = sa->push_array<vk::DescriptorSetLayoutBinding>(s.bindings.length);
    auto* vk_flags = sa->push_array<vk::DescriptorBindingFlags>(s.bindings.length);
    bool has_flags = false;
    for (U64 j = 0; j < s.bindings.length; ++j) {
      auto const& b = s.bindings[j];
      vk_bindings[j] = {
        .binding = b.binding,
        .descriptorType = b.type,
        .descriptorCount = b.count,
        .stageFlags = b.stages,
      };
      vk_flags[j] = b.flags;
      if (static_cast<bool>(b.flags)) has_flags = true;
    }

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flags_ci{
      .bindingCount = static_cast<U32>(s.bindings.length),
      .pBindingFlags = vk_flags,
    };

    vk::DescriptorSetLayoutCreateFlags layout_flags{};
    if (has_flags && layout_has_update_after_bind(s)) {
      layout_flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
    }

    vk::DescriptorSetLayoutCreateInfo ci{
      .flags = layout_flags,
      .bindingCount = static_cast<U32>(s.bindings.length),
      .pBindings = vk_bindings,
    };
    if (has_flags) ci.pNext = &flags_ci;

    vk::DescriptorSetLayout layout = arena->ds.push(
        vk_abort_if_error(device.createDescriptorSetLayoutUnique(ci)));
    cache.layouts[i] = {layout, &schemas[i]};
  }
  return cache;
}

internal LayoutHandle get_layout(DescriptorLayoutCache const& cache, Set set) {
  return cache.layouts[U32(set)];
}

//--- PipelineLayout: wraps VkPipelineLayout with set→slot mapping ---
struct PipelineLayout {
  vk::PipelineLayout handle;
  Array<U8, U32(Set::COUNT_)> slot;
  vk::DescriptorSetLayout expected[U32(Set::COUNT_)];
};

internal PipelineLayout create_pipeline_layout(
    vk::Device device,
    VKArena* arena,
    DescriptorLayoutCache const& layouts,
    ArraySlice<Set> sets,
    ArraySlice<vk::PushConstantRange> push_constants = {})
{
  PipelineLayout result{};
  for (U32 i = 0; i < U32(Set::COUNT_); ++i) {
    result.slot[i] = 0xFF;
    result.expected[i] = VK_NULL_HANDLE;
  }

  TempScope scratch = scratch_begin_scoped(0, 0);
  Arena* sa = scratch;
  auto* set_layouts = sa->push_array<vk::DescriptorSetLayout>(sets.length);
  for (U64 i = 0; i < sets.length; ++i) {
    U32 set_idx = U32(sets[i]);
    ASSERT_ALWAYS(result.slot[set_idx] == 0xFF && "Duplicate set in pipeline layout");
    result.slot[set_idx] = static_cast<U8>(i);
    LayoutHandle lh = get_layout(layouts, sets[i]);
    result.expected[set_idx] = lh.handle;
    set_layouts[i] = lh.handle;
  }

  vk::PipelineLayoutCreateInfo ci{
    .setLayoutCount = static_cast<U32>(sets.length),
    .pSetLayouts = set_layouts,
    .pushConstantRangeCount = static_cast<U32>(push_constants.length),
    .pPushConstantRanges = push_constants,
  };
  result.handle = arena->ds.push(vk_abort_if_error(device.createPipelineLayoutUnique(ci)));
  return result;
}

} // namespace sd::desc

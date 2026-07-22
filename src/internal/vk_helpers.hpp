#pragma once

#include "base.hpp"
#include "base_array.hpp"
#include "vk_arena.hpp"
#include "vk_destructor_stack.hpp"

constexpr U32 MAX_FRAMES_IN_FLIGHT = 2;

struct VKInstanceResult {
  vk::Instance instance;
  vk::SurfaceKHR surface;
  PFN_vkGetInstanceProcAddr get_instance_proc_addr;
#ifdef SD2_DEBUG
  VkDebugUtilsMessengerEXT debug_messenger{};
#endif
};

internal VKInstanceResult vk_create_vulkan_instance(GLFWwindow *glfw_window, Arena *arena);

struct VKGpuArena {
  static constexpr vk::DeviceSize DEFAULT_CAPACITY = mb(2);
  vk::DeviceMemory memory{};
  void *mapped_ptr{};
  vk::DeviceSize offset{};
  vk::DeviceSize capacity{};
  U32 memory_type_index{};
};

struct VKGpuArenaAlloc {
  vk::DeviceMemory memory;
  vk::DeviceSize offset;
  void *mapped;
};

struct VKTextureImage {
  U32 mip_levels{};
  vk::Image image{};
  vk::Format format;
  vk::ImageView image_view{};
  VKGpuArenaAlloc image_alloc{};
};

struct VKQueueFamilyIndices {
  U32 graphics_index{0xFFFFFFFF};
  U32 present_index{0xFFFFFFFF};

  [[nodiscard]] bool is_complete() const { return graphics_index != 0xFFFFFFFF && present_index != 0xFFFFFFFF; }
};

constexpr U32 SC_MAX_GENERATIONS = 10;
constexpr U32 SC_MAX_IMAGES = 8;

struct SwapchainResources {
  vk::Image images[SC_MAX_IMAGES];
  vk::ImageView image_views[SC_MAX_IMAGES];
  vk::Image msaa_images[SC_MAX_IMAGES];
  vk::ImageView msaa_image_views[SC_MAX_IMAGES];
  bool image_initialized[SC_MAX_IMAGES];
  vk::Fence images_in_flight[SC_MAX_IMAGES];
  vk::Semaphore render_finished_sems[SC_MAX_IMAGES];
  U32 image_count{};

  VKArena sc_res_arena{};
};

struct VKSwapchainConfig {
  vk::Device dev{};
  vk::PhysicalDevice phys_dev{};
  vk::SurfaceKHR surface{};

  AppWindow *window{};
  VKQueueFamilyIndices queue_indices{};

  vk::Format image_format{};
  vk::ColorSpaceKHR color_space{};
  vk::PresentModeKHR present_mode{};

  vk::SampleCountFlagBits msaa_samples{};

  Array<SwapchainResources, SC_MAX_GENERATIONS> swapchain_pool{};
  Array<vk::Fence, MAX_FRAMES_IN_FLIGHT> in_flight_fences{};
  Array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> acquire_sems{};

  VKGpuArena *device_arena{};
  VKGpuArena sc_arena{};
};

struct VKSwapchainState {
  vk::Extent2D extent{};
  VKTextureImage depth{};
  vk::SwapchainKHR swapchain{};

  U32 image_index{};
  SwapchainResources *sc_res{};
  U32 pool_gen{};
  bool needs_rebuild{};
  Array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> command_buffers{};
};

internal void vk_destroy_texture(vk::Device device, VKTextureImage *tex);

internal vk::CommandBuffer
vk_begin_single_time_command(vk::Device vk_device, vk::CommandPool command_pool, char const *name = nullptr);

internal void vk_end_single_time_command(vk::Device vk_device,
                                         vk::Queue queue,
                                         vk::CommandPool command_pool,
                                         vk::CommandBuffer command_buffer);

internal void vk_abort_if_error(vk::Result res, std::string_view message = {});

template<typename T> concept VKResultValue = requires { typename vk::ResultValueType<T>::type; };

template<typename T>
  requires VKResultValue<T>
internal T vk_abort_if_error(vk::ResultValue<T> ret, std::string_view message = {}) {
  vk_abort_if_error(ret.result, message);
  return std::move(ret.value);
}


internal VKQueueFamilyIndices vk_find_queue_families(vk::PhysicalDevice device, vk::SurfaceKHR surface);

internal bool vk_check_device_extension_support(vk::PhysicalDevice device, char const *extension_name);

struct VKSwapchainSupport {
  vk::SurfaceCapabilitiesKHR capabilities;
  U32 format_count;
  vk::SurfaceFormatKHR *formats;
  U32 mode_count;
  vk::PresentModeKHR *modes;
};

internal VKSwapchainSupport vk_query_swapchain_support(vk::PhysicalDevice device, vk::SurfaceKHR surface, Arena *arena);

struct VKRatedDevice {
  vk::PhysicalDevice device;
  U32 score;
  VKQueueFamilyIndices queue_family_indices{};
};

internal VKRatedDevice vk_pick_best_physical_device(vk::Instance instance, vk::SurfaceKHR surface);


internal bool vk_check_validation_layer_support();


internal U32 vk_find_memory_type(vk::PhysicalDevice vk_phys_dev, U32 type_filter, vk::MemoryPropertyFlags properties);

internal std::pair<vk::Buffer, vk::DeviceMemory> vk_create_buffer(vk::PhysicalDevice vk_phys_dev,
                                                                  vk::Device vk_device,
                                                                  vk::DeviceSize size,
                                                                  vk::BufferUsageFlags usage,
                                                                  vk::MemoryPropertyFlags properties);

internal void vk_copy_buffer(vk::Device vk_device,
                             vk::CommandPool command_pool,
                             vk::Queue queue,
                             vk::Buffer src_buffer,
                             vk::Buffer dst_buffer,
                             vk::DeviceSize size);

internal VKGpuArena vk_create_gpu_arena(vk::PhysicalDevice phys_dev,
                                        vk::Device device,
                                        U32 memory_type_index,
                                        vk::DeviceSize capacity = VKGpuArena::DEFAULT_CAPACITY);
internal void vk_destroy_gpu_arena(vk::Device device, VKGpuArena *arena);


internal void vk_create_depth_resources(VKSwapchainConfig *config,
                                        VKSwapchainState *state,
                                        VKArena *arena);

internal std::pair<vk::Buffer, VKGpuArenaAlloc>
vk_create_buffer(vk::PhysicalDevice, vk::Device, vk::DeviceSize size, vk::BufferUsageFlags usage, VKGpuArena *arena);
internal std::pair<vk::Image, VKGpuArenaAlloc> vk_create_image(vk::PhysicalDevice vk_phys_dev,
                                                               vk::Device vk_device,
                                                               U32 width,
                                                               U32 height,
                                                               U32 mip_levels,
                                                               vk::Format format,
                                                               vk::ImageTiling tiling,
                                                               vk::ImageUsageFlags usage,
                                                               VKGpuArena *gpu_arena,
                                                               VKArena *arena,
                                                               vk::SampleCountFlagBits num_samples =
                                                                   vk::SampleCountFlagBits::e1);

#ifdef SD2_DEBUG
internal VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                          VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                          const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
                                                          void *p_user_data) {
  if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    std::fprintf(stderr, "[VK] %s\n", p_callback_data->pMessage);
  return VK_FALSE;
}
#endif

internal void vk_transition_image_layout(vk::CommandBuffer command_buffer,
                                         vk::Image image,
                                         vk::ImageLayout old_layout,
                                         vk::ImageLayout new_layout,
                                         vk::AccessFlags2 src_access_mask,
                                         vk::AccessFlags2 dst_access_mask,
                                         vk::PipelineStageFlags2 src_stage_mask,
                                         vk::PipelineStageFlags2 dst_stage_mask,
                                         vk::ImageAspectFlags aspect_flags,
                                         U32 mip_levels,
                                         U32 base_mip_level = 0);

internal void
vk_copy_buffer_to_image(vk::CommandBuffer command_buffer, vk::Buffer buffer, vk::Image image, U32 width, U32 height);

internal vk::ImageView vk_create_image_view(vk::Device device,
                                            vk::Image image,
                                            vk::Format format,
                                            vk::ImageAspectFlags aspect_mask,
                                            U32 mip_levels,
                                            VKArena *arena);

using PhysicalDeviceFeatures = vk::StructureChain<vk::PhysicalDeviceFeatures2,
                                                  vk::PhysicalDeviceVulkan11Features,
                                                  vk::PhysicalDeviceVulkan13Features,
                                                  vk::PhysicalDeviceVulkan14Features,
                                                  vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                                                  vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT>;
internal PhysicalDeviceFeatures enable_phys_dev_features(vk::PhysicalDevice vk_phys_dev);

internal vk::Format find_supported_format(vk::Format *candidates,
                                          U32 candidate_count,
                                          vk::ImageTiling tiling,
                                          vk::FormatFeatureFlags features,
                                          vk::PhysicalDevice phys_dev);


internal void vk_generate_mip_maps(vk::PhysicalDevice phys_dev,
                                   vk::CommandBuffer command_buffer,
                                   vk::Image image,
                                   vk::Format image_format,
                                   U32 width,
                                   U32 height,
                                   U32 mip_levels);
internal vk::SampleCountFlagBits vk_get_max_usable_sample_count(vk::PhysicalDevice device);


internal std::tuple<vk::Device, vk::Queue, vk::Queue>
vk_create_logical_device(vk::PhysicalDevice phys_dev, VKQueueFamilyIndices queue_indices);

internal VKSwapchainConfig
vk_create_swapchain_config(vk::Device device,
                           vk::PhysicalDevice phys_dev,
                           vk::SurfaceKHR surface,
                           VKQueueFamilyIndices queue_indices,
                           VKGpuArena *device_arena,
                           Arena *arena);

internal void vk_create_swapchain(VKSwapchainConfig *config, VKSwapchainState *state, vk::SwapchainKHR old_swapchain);

internal vk::CommandPool
vk_create_swapchain_resources(VKSwapchainConfig *config,
                              VKSwapchainState *state);

internal VKTextureImage
vk_load_texture(vk::PhysicalDevice vk_phys_dev,
                vk::Device vk_device,
                VKGpuArena *host_arena,
                VKGpuArena *device_arena,
                vk::CommandPool vk_command_pool,
                vk::Queue graphics_queue,
                char const *texture_path);

// GPL subset descriptors
struct VertexInputLibDesc {
  ArraySlice<vk::VertexInputBindingDescription> bindings{};
  ArraySlice<vk::VertexInputAttributeDescription> attributes{};
  vk::PrimitiveTopology topology{vk::PrimitiveTopology::eTriangleList};
  bool primitive_restart{false};
};

struct PreRasterLibDesc {
  ArraySlice<vk::PipelineShaderStageCreateInfo> shader_stages{};
  bool tessellation{false};
  U32 tess_patch_control_points{0};
  U32 viewport_count{1};
  U32 scissor_count{1};
  bool depth_clamp{false};
  bool rasterizer_discard{false};
  vk::PolygonMode polygon_mode{vk::PolygonMode::eFill};
  vk::CullModeFlags cull_mode{vk::CullModeFlagBits::eBack};
  vk::FrontFace front_face{vk::FrontFace::eCounterClockwise};
  bool depth_bias{false};
  F32 line_width{1.0f};
  vk::DynamicState const *dynamic_states{nullptr};
  U32 dynamic_state_count{0};
};

struct FragmentLibDesc {
  vk::PipelineShaderStageCreateInfo *fragment_shader{};
  bool depth_test{true};
  bool depth_write{true};
  vk::CompareOp depth_compare{vk::CompareOp::eLess};
  bool depth_bounds_test{false};
  bool stencil_test{false};
  vk::SampleCountFlagBits msaa_samples{vk::SampleCountFlagBits::e1};
  VkBool32 sample_shading_enable{vk::False};
  float min_sample_shading{1.0f};
};

struct FragmentOutputLibDesc {
  vk::SampleCountFlagBits msaa_samples{vk::SampleCountFlagBits::e1};
  bool blend_enable{false};
  vk::Format color_format{};
  vk::Format depth_format{};
};

internal vk::Pipeline create_gpl(vk::Device vk_device,
                                 VKArena *arena,
                                 vk::PipelineCache cache,
                                 vk::GraphicsPipelineLibraryFlagsEXT subset,
                                 vk::GraphicsPipelineCreateInfo ci);

internal vk::DescriptorSetLayout create_descriptor_set_layout(
    vk::Device device,
    U32 binding_count,
    vk::DescriptorSetLayoutBinding const *bindings);

internal vk::PipelineLayout create_pipeline_layout(
    vk::Device device,
    vk::DescriptorSetLayout set_layout);

internal vk::Pipeline create_vertex_input_library(
    vk::Device device,
    VertexInputLibDesc const &desc);

internal vk::Pipeline create_pre_raster_library(
    vk::Device device,
    VKArena *arena,
    vk::PipelineLayout layout,
    PreRasterLibDesc const &desc);

internal vk::Pipeline create_fragment_library(
    vk::Device device,
    vk::PipelineLayout layout,
    FragmentLibDesc const &desc
    );

internal vk::Pipeline create_fragment_output_library(
    vk::Device device,
    FragmentOutputLibDesc const &desc);

internal vk::Pipeline create_linked_pipeline(
    vk::Device device,
    vk::PipelineLayout layout,
    U32 library_count,
    vk::Pipeline const *libraries,
    vk::Format color_format,
    vk::Format depth_format);


//~ Transient buffers
struct TransientAlloc {
  vk::Buffer buffer{};
  void *mapped{};
  U64 offset{};
  U64 size{};
};

struct TransientBufferFrame {
  vk::Buffer buffer{};
  void *mapped{};
  U64 used{};
};

struct TransientBuffer {
  VKGpuArena *arena{};
  Array<TransientBufferFrame, MAX_FRAMES_IN_FLIGHT> frames{};
  U64 size_per_frame{};

  void reset(U32 frame_index) {
    frames[frame_index].used = 0;
  }

  TransientAlloc alloc(U32 frame_index, U64 size, U64 alignment) {
    auto &f = frames[frame_index];
    U64 offset = (f.used + alignment - 1) & ~(alignment - 1);
    ASSERT_ALWAYS(offset + size <= size_per_frame);
    f.used = offset + size;
    return {
        .buffer = f.buffer,
        .mapped = static_cast<U8 *>(f.mapped) + offset,
        .offset = offset,
        .size = size
    };
  }
};

internal TransientBuffer vk_create_transient_buffer(vk::PhysicalDevice phys_dev,
                                                    vk::Device device,
                                                    VKGpuArena *gpu_arena,
                                                    U64 size_per_frame,
                                                    vk::BufferUsageFlags usage);
internal void vk_destroy_transient_buffer(vk::Device device, TransientBuffer *tb);

internal void vk_present(VKSwapchainConfig *config,
                         VKSwapchainState *state,
                         vk::Queue present_queue,
                         VKArena *arena);
internal bool vk_recreate_swapchain_if_needed(VkResult acquire_res,
                                              VKSwapchainConfig *config,
                                              VKSwapchainState *state,
                                              VKArena *arena);
vk::Extent2D vk_get_extent(VKSwapchainConfig *config, U32 width, U32 height);

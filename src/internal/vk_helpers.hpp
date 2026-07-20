#pragma once

#include "base.hpp"
#include "base_array.hpp"

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
  vk::ImageView image_view{};
  VKGpuArenaAlloc image_alloc{};
};

struct VKDepthResources {
  VKTextureImage tex;
  vk::Format format;
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
internal T vk_abort_if_error(vk::ResultValue<T> const &ret, std::string_view message = {}) {
  vk_abort_if_error(ret.result, message);
  return ret.value;
}

struct VKQueueFamilyIndices {
  U32 graphics_index{0xFFFFFFFF};
  U32 present_index{0xFFFFFFFF};

  [[nodiscard]] bool is_complete() const { return graphics_index != 0xFFFFFFFF && present_index != 0xFFFFFFFF; }
};

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
};

internal VKRatedDevice vk_pick_best_physical_device(vk::Instance instance, vk::SurfaceKHR surface);

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
};

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


internal VKDepthResources vk_create_depth_resources(vk::PhysicalDevice vk_phys_dev,
                                                    vk::Device vk_device,
                                                    VKGpuArena *device_arena,
                                                    vk::Extent2D extent,
                                                    vk::SampleCountFlagBits msaa_samples
    );

internal std::pair<vk::Buffer, VKGpuArenaAlloc>
vk_create_buffer(vk::PhysicalDevice, vk::Device, vk::DeviceSize size, vk::BufferUsageFlags usage, VKGpuArena *arena);
internal std::pair<vk::Image, VKGpuArenaAlloc> vk_create_image(vk::PhysicalDevice,
                                                               vk::Device,
                                                               U32 width,
                                                               U32 height,
                                                               U32 mip_levels,
                                                               vk::Format format,
                                                               vk::ImageTiling tiling,
                                                               vk::ImageUsageFlags usage,
                                                               VKGpuArena *arena,
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
                                            U32 mip_levels);

using PhysicalDeviceFeatures = vk::StructureChain<vk::PhysicalDeviceFeatures2,
                                                  vk::PhysicalDeviceVulkan11Features,
                                                  vk::PhysicalDeviceVulkan13Features,
                                                  vk::PhysicalDeviceVulkan14Features,
                                                  vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>;
internal PhysicalDeviceFeatures enable_phys_dev_features(vk::PhysicalDevice vk_phys_dev);

internal vk::Format find_supported_format(vk::Format *candidates,
                                          U32 candidate_count,
                                          vk::ImageTiling tiling,
                                          vk::FormatFeatureFlags features,
                                          vk::PhysicalDevice phys_dev);

internal void vk_recreate_swapchain(GLFWwindow *glfw_window,
                                    vk::Device vk_device,
                                    SwapchainResources *sc,
                                    vk::SwapchainKHR &vk_swapchain,
                                    vk::PhysicalDevice vk_phys_dev,
                                    vk::SurfaceKHR vk_surface,
                                    vk::Extent2D &swapchain_extent,
                                    vk::Format swapchain_image_format,
                                    vk::ColorSpaceKHR swapchain_color_space,
                                    vk::PresentModeKHR present_mode,
                                    vk::Image *depth_image,
                                    vk::ImageView *depth_image_view,
                                    vk::SampleCountFlagBits msaa_samples,
                                    VKGpuArena *device_arena);
internal void vk_generate_mip_maps(vk::PhysicalDevice phys_dev,
                                   vk::CommandBuffer command_buffer,
                                   vk::Image image,
                                   vk::Format image_format,
                                   U32 width,
                                   U32 height,
                                   U32 mip_levels);
internal vk::SampleCountFlagBits vk_get_max_usable_sample_count(vk::PhysicalDevice device);

constexpr U32 MAX_FRAMES_IN_FLIGHT = 2;

internal std::tuple<vk::Device, vk::Queue, vk::Queue>
vk_create_logical_device(vk::PhysicalDevice phys_dev, VKQueueFamilyIndices queue_indices);

internal std::tuple<vk::Format, vk::ColorSpaceKHR, vk::PresentModeKHR, vk::Extent2D>
vk_create_swapchain_config(vk::PhysicalDevice dev, vk::SurfaceKHR surface, U32 width, U32 height, Arena *arena);

internal vk::SwapchainKHR vk_create_swapchain(vk::PhysicalDevice vk_phys_dev,
                                              vk::Device vk_device,
                                              vk::SurfaceKHR vk_surface,
                                              vk::Format chosen_format,
                                              vk::ColorSpaceKHR chosen_color_space,
                                              vk::Extent2D swapchain_extent,
                                              vk::PresentModeKHR chosen_present_mode,
                                              VKQueueFamilyIndices queue_indices);

internal std::tuple<SwapchainResources *, vk::CommandPool>
vk_create_swapchain_resources(vk::Device vk_device,
                              vk::PhysicalDevice vk_phys_dev,
                              vk::SwapchainKHR swapchain,
                              vk::Extent2D swapchain_extent,
                              vk::Format chosen_format,
                              vk::SampleCountFlagBits msaa_samples,
                              VKQueueFamilyIndices queue_indices,
                              VKGpuArena *device_arena,
                              Array<SwapchainResources, SC_MAX_GENERATIONS> *swapchain_pool,
                              Array<vk::Fence, MAX_FRAMES_IN_FLIGHT> *in_flight_fences,
                              Array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> *acquire_sems,
                              Array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> *command_buffers);

internal VKTextureImage
vk_load_texture(vk::PhysicalDevice vk_phys_dev,
                vk::Device vk_device,
                VKGpuArena *host_arena,
                VKGpuArena *device_arena,
                vk::CommandPool vk_command_pool,
                vk::Queue graphics_queue,
                char const *texture_path);

struct VKShaderStageDesc {
  vk::ShaderStageFlagBits stage{};
  String8 entrypoint_name{};
  String8 file_name{};
};

struct VKLoadedShaderModule {
  String8 file_name;
  vk::ShaderModule module;
};

struct VKBuiltShaderStages {
  DynArray<vk::PipelineShaderStageCreateInfo> stages;
  DynArray<VKLoadedShaderModule> modules;
};

internal VKBuiltShaderStages build_shader_stages(Arena *arena,
                                                 vk::Device vk_device,
                                                 DynArray<VKShaderStageDesc> *shader_stage_descriptions);

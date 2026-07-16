#pragma once

internal vk::CommandBuffer
         begin_single_time_command(vk::Device vk_device, vk::CommandPool command_pool, char const *name = nullptr);

internal void end_single_time_command(vk::Device        vk_device,
                                      vk::Queue         queue,
                                      vk::CommandPool   command_pool,
                                      vk::CommandBuffer command_buffer);

internal void abort_if_vk_error(vk::Result res, std::string_view message = {});

template<typename T> concept VkResultValue = requires { typename vk::ResultValueType<T>::type; };
template<typename T>
  requires VkResultValue<T>
internal T abort_if_vk_error(vk::ResultValue<T> const &ret, std::string_view message = {}) {
  abort_if_vk_error(ret.result, message);
  return ret.value;
}

struct QueueFamilyIndices {
  U32 graphics_index{0xFFFFFFFF};
  U32 present_index{0xFFFFFFFF};

  [[nodiscard]] bool is_complete() const { return graphics_index != 0xFFFFFFFF && present_index != 0xFFFFFFFF; }
};

internal QueueFamilyIndices find_queue_families(vk::PhysicalDevice device, vk::SurfaceKHR surface);

internal bool check_device_extension_support(vk::PhysicalDevice device, char const *extension_name);

struct SwapchainSupport {
  vk::SurfaceCapabilitiesKHR capabilities;
  U32                        format_count;
  vk::SurfaceFormatKHR      *formats;
  U32                        mode_count;
  vk::PresentModeKHR        *modes;
};

internal SwapchainSupport query_swapchain_support(vk::PhysicalDevice device, vk::SurfaceKHR surface, Arena *arena);

struct RatedDevice {
  vk::PhysicalDevice device;
  U32                score;
};

internal RatedDevice pick_best_physical_device(vk::Instance instance, vk::SurfaceKHR surface);

constexpr U32 SC_MAX_GENERATIONS = 10;
constexpr U32 SC_MAX_IMAGES      = 8;

struct SwapchainResources {
  vk::Image     images[SC_MAX_IMAGES];
  vk::ImageView image_views[SC_MAX_IMAGES];
  bool          image_initialized[SC_MAX_IMAGES];
  vk::Fence     images_in_flight[SC_MAX_IMAGES];
  vk::Semaphore render_finished_sems[SC_MAX_IMAGES];
  U32           image_count{};
};

internal bool check_validation_layer_support();

constexpr U32 MAX_INSTANCE_EXTENSIONS = 16;

internal U32 get_required_extensions(char const **out_extensions, U32 max_extensions);

internal U32 find_memory_type(vk::PhysicalDevice vk_phys_dev, U32 type_filter, vk::MemoryPropertyFlags properties);

internal std::pair<vk::Buffer, vk::DeviceMemory> create_buffer(vk::PhysicalDevice      vk_phys_dev,
                                                               vk::Device              vk_device,
                                                               vk::DeviceSize          size,
                                                               vk::BufferUsageFlags    usage,
                                                               vk::MemoryPropertyFlags properties);

internal void copy_buffer(vk::Device      vk_device,
                          vk::CommandPool command_pool,
                          vk::Queue       queue,
                          vk::Buffer      src_buffer,
                          vk::Buffer      dst_buffer,
                          vk::DeviceSize  size);

internal std::pair<vk::Image, vk::DeviceMemory> create_image(vk::PhysicalDevice      vk_phys_dev,
                                                             vk::Device              vk_device,
                                                             U32                     width,
                                                             U32                     height,
                                                             vk::Format              format,
                                                             vk::ImageTiling         tiling,
                                                             vk::ImageUsageFlags     usage,
                                                             vk::MemoryPropertyFlags properties);

struct GpuArena {
  static constexpr vk::DeviceSize DEFAULT_CAPACITY = mb(2);
  vk::DeviceMemory                memory{};
  void                           *mapped_ptr{};
  vk::DeviceSize                  offset{};
  vk::DeviceSize                  capacity{};
  U32                             memory_type_index{};
};

internal GpuArena create_gpu_arena(vk::PhysicalDevice phys_dev,
                                   vk::Device         device,
                                   U32                memory_type_index,
                                   vk::DeviceSize     capacity = GpuArena::DEFAULT_CAPACITY);
internal void     destroy_gpu_arena(vk::Device device, GpuArena &arena);

struct GpuArenaAlloc {
  vk::DeviceMemory memory;
  vk::DeviceSize   offset;
  void            *mapped;
};

struct DepthResources {
  vk::Image     image;
  vk::ImageView image_view;
  vk::Format    format;
};

internal DepthResources create_depth_resources(vk::PhysicalDevice vk_phys_dev,
                                                vk::Device         vk_device,
                                                GpuArena          &device_arena,
                                                vk::Extent2D       extent);

internal std::pair<vk::Buffer, GpuArenaAlloc>
create_buffer(vk::PhysicalDevice, vk::Device, vk::DeviceSize size, vk::BufferUsageFlags usage, GpuArena &arena);
internal std::pair<vk::Image, GpuArenaAlloc> create_image(vk::PhysicalDevice,
                                                          vk::Device,
                                                          U32                 width,
                                                          U32                 height,
                                                          vk::Format          format,
                                                          vk::ImageTiling     tiling,
                                                          vk::ImageUsageFlags usage,
                                                          GpuArena           &arena);

#ifdef SD2_DEBUG
internal VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
                                                       VkDebugUtilsMessageTypeFlagsEXT             message_type,
                                                       const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
                                                       void                                       *p_user_data) {
  if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    std::fprintf(stderr, "[VK] %s\n", p_callback_data->pMessage);
  return VK_FALSE;
}
#endif

internal vk::CommandBuffer
         begin_single_time_command(vk::Device vk_device, vk::CommandPool command_pool, char const *name);

internal void end_single_time_command(vk::Device        vk_device,
                                      vk::Queue         queue,
                                      vk::CommandPool   command_pool,
                                      vk::CommandBuffer command_buffer);

internal void transition_image_layout(vk::CommandBuffer       command_buffer,
                                      vk::Image               image,
                                      vk::ImageLayout         old_layout,
                                      vk::ImageLayout         new_layout,
                                      vk::AccessFlags2        src_access_mask,
                                      vk::AccessFlags2        dst_access_mask,
                                      vk::PipelineStageFlags2 src_stage_mask,
                                      vk::PipelineStageFlags2 dst_stage_mask,
                                      vk::ImageAspectFlags    aspect_flags);

internal void
copy_buffer_to_image(vk::CommandBuffer command_buffer, vk::Buffer buffer, vk::Image image, U32 width, U32 height);

internal vk::ImageView
         create_image_view(vk::Device device, vk::Image image, vk::Format format, vk::ImageAspectFlags aspect_mask);
using PhysicalDeviceFeatures = vk::StructureChain<vk::PhysicalDeviceFeatures2,
                                                  vk::PhysicalDeviceVulkan11Features,
                                                  vk::PhysicalDeviceVulkan13Features,
                                                  vk::PhysicalDeviceVulkan14Features,
                                                  vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>;
internal PhysicalDeviceFeatures enable_phys_dev_features(vk::PhysicalDevice vk_phys_dev);

internal vk::Format find_supported_format(vk::Format            *candidates,
                                          U32                    candidate_count,
                                          vk::ImageTiling        tiling,
                                          vk::FormatFeatureFlags features,
                                          vk::PhysicalDevice     phys_dev);

internal void recreate_swapchain(GLFWwindow         *glfw_window,
                                 vk::Device          vk_device,
                                 SwapchainResources *sc,
                                 vk::SwapchainKHR   &vk_swapchain,
                                 vk::PhysicalDevice  vk_phys_dev,
                                 vk::SurfaceKHR      vk_surface,
                                 vk::Extent2D       &swapchain_extent,
                                 vk::Format          swapchain_image_format,
                                 vk::ColorSpaceKHR   swapchain_color_space,
                                 vk::PresentModeKHR  present_mode,
                                 vk::Image          *depth_image,
                                 vk::ImageView      *depth_image_view,
                                 GpuArena           &device_arena);

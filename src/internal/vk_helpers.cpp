#include <string_view>

#define SD2_TRACY_ENABLE
#include "../sd2_inc.hpp"

internal VKInstanceResult vk_create_vulkan_instance(GLFWwindow *glfw_window, Arena *arena) {
  VKInstanceResult res{};

  res.get_instance_proc_addr =
      g_vulkan_dynamic_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(res.get_instance_proc_addr);

  vk::ApplicationInfo vk_app_info{.pApplicationName = "sd2",
                                  .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                  .pEngineName = "NoEngine",
                                  .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                  .apiVersion = VK_API_VERSION_1_4};

#ifdef SD2_DEBUG
  if (!vk_check_validation_layer_support()) {
    std::fprintf(stderr, "VK_LAYER_KHRONOS_validation not available\n");
    TRAP();
  }

  VkDebugUtilsMessengerCreateInfoEXT vk_debug_create_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .pNext = nullptr,
      .flags = 0,
      .messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = vk_debug_callback,
      .pUserData = nullptr,
  };
#endif

  const char **extensions = arena->push_array<const char *>(MAX_INSTANCE_EXTENSIONS);
  U32 ext_count = glfw_get_required_extensions(extensions, MAX_INSTANCE_EXTENSIONS);
  Array vk_validation_layers = {"VK_LAYER_KHRONOS_validation"};

  vk::InstanceCreateInfo vk_inst_info{
#ifdef SD2_DEBUG
      .pApplicationInfo = &vk_app_info,
      .enabledLayerCount = 1,
      .ppEnabledLayerNames = vk_validation_layers,
      .enabledExtensionCount = ext_count,
      .ppEnabledExtensionNames = extensions,
#else
      .enabledExtensionCount = ext_count,
      .ppEnabledExtensionNames = extensions,
#endif
  };

  res.instance = vk_abort_if_error(vk::createInstance(vk_inst_info), "Failed to create vulkan instance");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(res.instance);

#ifdef SD2_DEBUG
  {
    PFN_vkCreateDebugUtilsMessengerEXT func =
        (PFN_vkCreateDebugUtilsMessengerEXT)res.get_instance_proc_addr(static_cast<VkInstance>(res.instance),
                                                                       "vkCreateDebugUtilsMessengerEXT");
    if (func)
      func(static_cast<VkInstance>(res.instance), &vk_debug_create_info, nullptr, &res.debug_messenger);
  }
#endif

  VkSurfaceKHR raw_surface{};
  if (glfwCreateWindowSurface(res.instance, glfw_window, nullptr, &raw_surface) != VK_SUCCESS) {
    TRAP();
  }
  res.surface = raw_surface;

  return res;
}


internal vk::CommandBuffer
vk_begin_single_time_command(vk::Device vk_device, vk::CommandPool command_pool, char const *name) {
  vk::CommandBuffer command_buffer = nullptr;
  vk::CommandBufferAllocateInfo alloc_info{.commandPool = command_pool,
                                           .level = vk::CommandBufferLevel::ePrimary,
                                           .commandBufferCount = 1};

  vk_abort_if_error(vk_device.allocateCommandBuffers(&alloc_info, &command_buffer));
  vk::CommandBufferBeginInfo begin_info{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
  vk_abort_if_error(command_buffer.begin(begin_info));
  if (name) {
    vk_abort_if_error(vk_device.setDebugUtilsObjectNameEXT({
        .objectType = vk::ObjectType::eCommandBuffer,
        .objectHandle = (uint64_t)(VkCommandBuffer)command_buffer,
        .pObjectName = name,
    }));
  }
  return command_buffer;
}

internal void vk_end_single_time_command(vk::Device vk_device,
                                         vk::Queue queue,
                                         vk::CommandPool command_pool,
                                         vk::CommandBuffer command_buffer) {
  vk_abort_if_error(command_buffer.end());
  vk::SubmitInfo submit_info{
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
  };
  vk_abort_if_error(queue.submit(submit_info, nullptr));
  vk_abort_if_error(queue.waitIdle());
  vk_device.freeCommandBuffers(command_pool, 1, &command_buffer);
}

internal void vk_abort_if_error(vk::Result res, std::string_view message) {
  if (res != vk::Result::eSuccess) {
    std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(message.size()), message.data(), vk::to_string(res).c_str());
    std::fflush(stderr);
    TRAP();
  }
}


internal VKQueueFamilyIndices vk_find_queue_families(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  Temp scratch = scratch_begin(0, 0);
  VKQueueFamilyIndices indices{};

  U32 queue_family_count = 0;
  device.getQueueFamilyProperties(&queue_family_count, nullptr);


  auto *queue_family_properties = scratch.arena->push_array<vk::QueueFamilyProperties>(queue_family_count);
  device.getQueueFamilyProperties(&queue_family_count, queue_family_properties);
  for (U32 i = 0; i < queue_family_count; ++i) {
    vk::QueueFamilyProperties queue_family = queue_family_properties[i];

    if (queue_family.queueFlags & vk::QueueFlagBits::eGraphics) {
      indices.graphics_index = i;
    }

    vk::Bool32 present_support = VK_FALSE;
    auto res = device.getSurfaceSupportKHR(i, surface, &present_support);
    if (res == vk::Result::eSuccess && present_support) {
      indices.present_index = i;
    }

    if (indices.is_complete()) {
      break;
    }
  }
  scratch.end();
  return indices;
}

internal bool vk_check_device_extension_support(vk::PhysicalDevice device, char const *extension_name) {
  Temp scratch = scratch_begin(0, 0);
  U32 ext_count; // real ext count is 279..
  bool result = false;
  vk_abort_if_error(device.enumerateDeviceExtensionProperties(nullptr, &ext_count, nullptr));

  auto *available = scratch.arena->push_array<vk::ExtensionProperties>(ext_count);
  vk_abort_if_error(device.enumerateDeviceExtensionProperties(nullptr, &ext_count, available));

  for (U32 i = 0; i < ext_count; ++i) {
    if (std::strcmp(available[i].extensionName, extension_name) == 0)
      result = true;
  }
  scratch.end();
  return result;
}


internal VKSwapchainSupport
vk_query_swapchain_support(vk::PhysicalDevice device, vk::SurfaceKHR surface, Arena *arena) {
  VKSwapchainSupport support{};
  support.capabilities = vk_abort_if_error(device.getSurfaceCapabilitiesKHR(surface));

  vk_abort_if_error(device.getSurfaceFormatsKHR(surface, &support.format_count, nullptr));
  support.formats = arena->push_array<vk::SurfaceFormatKHR>(support.format_count);

  vk_abort_if_error(device.getSurfaceFormatsKHR(surface, &support.format_count, support.formats));

  vk_abort_if_error(device.getSurfacePresentModesKHR(surface, &support.mode_count, nullptr));
  support.modes = arena->push_array<vk::PresentModeKHR>(support.mode_count);
  vk_abort_if_error(device.getSurfacePresentModesKHR(surface, &support.mode_count, support.modes));

  return support;
}


internal VKRatedDevice vk_pick_best_physical_device(vk::Instance instance, vk::SurfaceKHR surface) {
  Temp scratch = scratch_begin(0, 0);
  U32 device_count = 0;
  vk_abort_if_error(instance.enumeratePhysicalDevices(&device_count, nullptr));

  auto *devices = scratch.arena->push_array<vk::PhysicalDevice>(device_count);
  vk_abort_if_error(instance.enumeratePhysicalDevices(&device_count, devices));

  vk::PhysicalDevice best_device = nullptr;
  U32 max_score = 0;

  for (U32 i = 0; i < device_count; i++) {
    vk::PhysicalDevice &device = devices[i];

    VKQueueFamilyIndices indices = vk_find_queue_families(device, surface);
    if (!indices.is_complete())
      continue;

    if (!vk_check_device_extension_support(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
      continue;

    VKSwapchainSupport swapchain_support = vk_query_swapchain_support(device, surface, scratch);
    if (swapchain_support.format_count == 0 || swapchain_support.mode_count == 0)
      continue;

    vk::PhysicalDeviceProperties props = device.getProperties();

    U32 score = 0;
    if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
      score += 80000;
    else if (props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
      score += 5000;

    score += props.limits.maxImageDimension2D;

    vk::PhysicalDeviceMemoryProperties mem_props = device.getMemoryProperties();
    for (U32 j = 0; j < mem_props.memoryHeapCount; ++j) {
      if (mem_props.memoryHeaps[j].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
        score += static_cast<U32>(mem_props.memoryHeaps[j].size / (1024 * 1024));
    }

    if (score > max_score) {
      max_score = score;
      best_device = device;
    }
  }
  scratch.end();
  return {.device = best_device, .score = max_score};
}

internal bool vk_check_validation_layer_support() {
  Temp scratch = scratch_begin(0, 0);
  U32 layer_count = 0;
  bool result = false;

  // TODO: consider not requerying this if this is called a lot of times. fine for one offs though
  vk_abort_if_error(vk::enumerateInstanceLayerProperties(&layer_count, nullptr));
  auto *available_layers = scratch.arena->push_array<vk::LayerProperties>(layer_count);
  vk_abort_if_error(vk::enumerateInstanceLayerProperties(&layer_count, available_layers));

  for (U32 i = 0; i < layer_count; ++i) {
    if (std::strcmp(available_layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
      result = true;
      break;
    }
  }
  scratch.end();
  return result;
}


internal U32 vk_find_memory_type(vk::PhysicalDevice vk_phys_dev, U32 type_filter, vk::MemoryPropertyFlags properties) {
  vk::PhysicalDeviceMemoryProperties mem_properties = vk_phys_dev.getMemoryProperties();
  for (U32 i = 0; i < mem_properties.memoryTypeCount; i++) {
    if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  NOT_IMPLEMENTED;
  return 0;
}


[[maybe_unused]] internal std::pair<vk::Buffer, vk::DeviceMemory> vk_create_buffer(vk::PhysicalDevice vk_phys_dev,
  vk::Device vk_device,
  vk::DeviceSize size,
  vk::BufferUsageFlags usage,
  vk::MemoryPropertyFlags properties) {
  vk::BufferCreateInfo buffer_info{.size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive};
  vk::Buffer buffer = vk_abort_if_error(vk_device.createBuffer(buffer_info));
  vk::MemoryRequirements memory_requirements = vk_device.getBufferMemoryRequirements(buffer);
  vk::MemoryAllocateInfo alloc_info = {
      .allocationSize = memory_requirements.size,
      .memoryTypeIndex = vk_find_memory_type(vk_phys_dev, memory_requirements.memoryTypeBits, properties)};
  vk::DeviceMemory buffer_memory = vk_abort_if_error(vk_device.allocateMemory(alloc_info));
  vk_abort_if_error(vk_device.bindBufferMemory(buffer, buffer_memory, 0));
  return {buffer, buffer_memory};
}

internal void vk_copy_buffer(vk::Device vk_device,
                             vk::CommandPool command_pool,
                             vk::Queue queue,
                             vk::Buffer src_buffer,
                             vk::Buffer dst_buffer,
                             vk::DeviceSize size) {
  vk::CommandBuffer command_copy_buffer = vk_begin_single_time_command(vk_device, command_pool, "copy buffer");
  command_copy_buffer.copyBuffer(src_buffer, dst_buffer, vk::BufferCopy(0, 0, size));
  vk_end_single_time_command(vk_device, queue, command_pool, command_copy_buffer);
}

internal vk::DeviceSize vk_gpu_arena_alloc(VKGpuArena *arena, vk::DeviceSize size, vk::DeviceSize alignment) {
  vk::DeviceSize aligned_offset = (arena->offset + alignment - 1) & ~(alignment - 1);
  ASSERT_ALWAYS(aligned_offset + size <= arena->capacity);
  arena->offset = aligned_offset + size;
  return aligned_offset;
}

internal VKGpuArena vk_create_gpu_arena(vk::PhysicalDevice phys_dev,
                                        vk::Device device,
                                        U32 memory_type_index,
                                        vk::DeviceSize capacity) {
  vk::MemoryAllocateInfo alloc_info{.allocationSize = capacity, .memoryTypeIndex = memory_type_index};
  vk::DeviceMemory memory = vk_abort_if_error(device.allocateMemory(alloc_info));

  vk::PhysicalDeviceMemoryProperties mem_props = phys_dev.getMemoryProperties();
  void *mapped_ptr = nullptr;
  if (memory_type_index < mem_props.memoryTypeCount &&
      (mem_props.memoryTypes[memory_type_index].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {
    mapped_ptr = vk_abort_if_error(device.mapMemory(memory, 0, capacity));
  }
  return {.memory = memory,
          .mapped_ptr = mapped_ptr,
          .offset = 0,
          .capacity = capacity,
          .memory_type_index = memory_type_index};
}

internal void vk_destroy_gpu_arena(vk::Device device, VKGpuArena *arena) {
  if (arena->mapped_ptr)
    device.unmapMemory(arena->memory);
  if (arena->memory)
    device.freeMemory(arena->memory);
  arena = {};
}

internal VKDepthResources vk_create_depth_resources(vk::PhysicalDevice vk_phys_dev,
                                                    vk::Device vk_device,
                                                    VKGpuArena *device_arena,
                                                    vk::Extent2D extent,
                                                    vk::SampleCountFlagBits msaa_samples) {
  Array format_candidates = {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint};
  vk::Format format = find_supported_format(format_candidates,
                                            format_candidates.size(),
                                            vk::ImageTiling::eOptimal,
                                            vk::FormatFeatureFlagBits::eDepthStencilAttachment,
                                            vk_phys_dev);
  VKDepthResources depth{};
  depth.tex.mip_levels = 1;
  std::tie(depth.tex.image, depth.tex.image_alloc) = vk_create_image(vk_phys_dev,
                                                                     vk_device,
                                                                     extent.width,
                                                                     extent.height,
                                                                     1,
                                                                     format,
                                                                     vk::ImageTiling::eOptimal,
                                                                     vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                                     device_arena,
                                                                     msaa_samples);
  depth.tex.image_view = vk_create_image_view(vk_device, depth.tex.image, format, vk::ImageAspectFlagBits::eDepth, 1);
  depth.format = format;
  return depth;
}

internal std::pair<vk::Buffer, VKGpuArenaAlloc> vk_create_buffer(vk::PhysicalDevice vk_phys_dev,
                                                                 vk::Device vk_device,
                                                                 vk::DeviceSize size,
                                                                 vk::BufferUsageFlags usage,
                                                                 VKGpuArena *arena) {
  (void)vk_phys_dev;

  vk::BufferCreateInfo buffer_info{.size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive};
  vk::Buffer buffer = vk_abort_if_error(vk_device.createBuffer(buffer_info));
  vk::MemoryRequirements mem_reqs = vk_device.getBufferMemoryRequirements(buffer);
  ASSERT_ALWAYS(mem_reqs.memoryTypeBits & (1u << arena->memory_type_index));
  vk::DeviceSize offset = vk_gpu_arena_alloc(arena, mem_reqs.size, mem_reqs.alignment);
  vk_abort_if_error(vk_device.bindBufferMemory(buffer, arena->memory, offset));
  void *mapped = arena->mapped_ptr ? static_cast<char *>(arena->mapped_ptr) + offset : nullptr;
  return {
      buffer,
      {.memory = arena->memory, .offset = offset, .mapped = mapped}
  };
}

internal std::pair<vk::Image, VKGpuArenaAlloc> vk_create_image(vk::PhysicalDevice vk_phys_dev,
                                                               vk::Device vk_device,
                                                               U32 width,
                                                               U32 height,
                                                               U32 mip_levels,
                                                               vk::Format format,
                                                               vk::ImageTiling tiling,
                                                               vk::ImageUsageFlags usage,
                                                               VKGpuArena *arena,
                                                               vk::SampleCountFlagBits num_samples) {
  (void)vk_phys_dev;

  vk::ImageCreateInfo image_info{
      .imageType = vk::ImageType::e2D,
      .format = format,
      .extent = {width, height, 1},
      .mipLevels = mip_levels,
      .arrayLayers = 1,
      .samples = num_samples,
      .tiling = tiling,
      .usage = usage,
      .sharingMode = vk::SharingMode::eExclusive,
      .initialLayout = vk::ImageLayout::eUndefined,
  };
  vk::Image image = vk_abort_if_error(vk_device.createImage(image_info));
  vk::MemoryRequirements mem_reqs = vk_device.getImageMemoryRequirements(image);
  ASSERT_ALWAYS(mem_reqs.memoryTypeBits & (1u << arena->memory_type_index));
  vk::DeviceSize offset = vk_gpu_arena_alloc(arena, mem_reqs.size, mem_reqs.alignment);
  vk_abort_if_error(vk_device.bindImageMemory(image, arena->memory, offset));
  void *mapped = arena->mapped_ptr ? static_cast<char *>(arena->mapped_ptr) + offset : nullptr;
  return {
      image,
      {.memory = arena->memory, .offset = offset, .mapped = mapped}
  };
}


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
                                         U32 base_mip_level) {
  vk::ImageMemoryBarrier2 barrier{
      .srcStageMask = src_stage_mask,
      .srcAccessMask = src_access_mask,
      .dstStageMask = dst_stage_mask,
      .dstAccessMask = dst_access_mask,
      .oldLayout = old_layout,
      .newLayout = new_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {.aspectMask = aspect_flags,
                           .baseMipLevel = base_mip_level,
                           .levelCount = mip_levels,
                           .layerCount = 1},
  };
  vk::DependencyInfo dep_info{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
  command_buffer.pipelineBarrier2(dep_info);
}

internal void
vk_copy_buffer_to_image(vk::CommandBuffer command_buffer, vk::Buffer buffer, vk::Image image, U32 width, U32 height) {
  vk::BufferImageCopy region{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                           .mipLevel = 0,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
      .imageOffset = {.x = 0, .y = 0, .z = 0},
      .imageExtent = {.width = width, .height = height, .depth = 1}
  };
  command_buffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
}

internal vk::ImageView vk_create_image_view(vk::Device device,
                                            vk::Image image,
                                            vk::Format format,
                                            vk::ImageAspectFlags aspect_mask,
                                            U32 mip_levels) {
  vk::ImageViewCreateInfo view_info{
      .image = image,
      .viewType = vk::ImageViewType::e2D,
      .format = format,
      .subresourceRange = {.aspectMask = aspect_mask, .levelCount = mip_levels, .layerCount = 1}
  };
  return vk_abort_if_error(device.createImageView(view_info));
}


internal PhysicalDeviceFeatures enable_phys_dev_features(vk::PhysicalDevice vk_phys_dev) {
  PhysicalDeviceFeatures s_features{};
  vk_phys_dev.getFeatures2(&s_features.get<vk::PhysicalDeviceFeatures2>());

  ASSERT_ALWAYS(s_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2);
  ASSERT_ALWAYS(s_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering);
  ASSERT_ALWAYS(s_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters);
  ASSERT_ALWAYS(s_features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy);
  ASSERT_ALWAYS(s_features.get<vk::PhysicalDeviceFeatures2>().features.wideLines);
  ASSERT_ALWAYS(s_features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState);

  PhysicalDeviceFeatures e_features{};
  e_features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState = vk::True;
  e_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = vk::True;
  e_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = vk::True;
  e_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = vk::True;
  e_features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy = vk::True;
  e_features.get<vk::PhysicalDeviceFeatures2>().features.sampleRateShading = vk::True;
  e_features.get<vk::PhysicalDeviceFeatures2>().features.wideLines = vk::True;
  e_features.get<vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT>().graphicsPipelineLibrary = vk::True;
  // TODO: figure out how to do fast linking


  return e_features;
}

vk::Format find_supported_format(vk::Format *candidates,
                                 U32 candidate_count,
                                 vk::ImageTiling tiling,
                                 vk::FormatFeatureFlags features,
                                 vk::PhysicalDevice phys_dev) {
  for (U32 i = 0; i < candidate_count; i++) {
    vk::FormatProperties props = phys_dev.getFormatProperties(candidates[i]);
    if (((tiling == vk::ImageTiling::eLinear) && ((props.linearTilingFeatures & features) == features)) ||
        ((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features))) {
      return candidates[i];
    }
  }
  INVALID_PATH;
}

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
                                    VKGpuArena *device_arena) {
  S32 width = 0, height = 0;
  glfwGetFramebufferSize(glfw_window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(glfw_window, &width, &height);
    glfwWaitEvents();
  }

  vk_abort_if_error(vk_device.waitIdle());

  for (U32 i = 0; i < sc->image_count; ++i) {
    vk_device.destroyImageView(sc->msaa_image_views[i]);
    vk_device.destroyImage(sc->msaa_images[i]);
    vk_device.destroySemaphore(sc->render_finished_sems[i]);
    vk_device.destroyImageView(sc->image_views[i], nullptr);
    sc->image_initialized[i] = false;
  }

  vk::SwapchainKHR old_swapchain = vk_swapchain;

  vk::SurfaceCapabilitiesKHR capabilities = vk_abort_if_error(vk_phys_dev.getSurfaceCapabilitiesKHR(vk_surface));
  vk::Extent2D new_extent = capabilities.currentExtent;

  // TODO: drivers rarely return 0xFFFFFFFF for currentextent, instead they return exact changing
  // physical pixel sizes. Which makes it take arbitrary dimensions the surface reported mid frame.
  if (new_extent.width == std::numeric_limits<U32>::max()) {
    new_extent.width =
        std::clamp(static_cast<U32>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    new_extent.height =
        std::clamp(static_cast<U32>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
  }


  swapchain_extent = new_extent;

  VKQueueFamilyIndices indices = vk_find_queue_families(vk_phys_dev, vk_surface);
  U32 queue_family_indices[] = {indices.graphics_index, indices.present_index};

  U32 desired_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && desired_count > capabilities.maxImageCount) {
    desired_count = capabilities.maxImageCount;
  }

  vk::SwapchainCreateInfoKHR swapchain_info{
      .surface = vk_surface,
      .minImageCount = desired_count,
      .imageFormat = swapchain_image_format,
      .imageColorSpace = swapchain_color_space,
      .imageExtent = swapchain_extent,
      .imageArrayLayers = 1,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode = present_mode,
      .clipped = true,
      .oldSwapchain = old_swapchain,
  };

  if (indices.graphics_index != indices.present_index) {
    swapchain_info.imageSharingMode = vk::SharingMode::eConcurrent;
    swapchain_info.queueFamilyIndexCount = 2;
    swapchain_info.pQueueFamilyIndices = queue_family_indices;
  } else {
    swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
  }

  vk_swapchain = vk_abort_if_error(vk_device.createSwapchainKHR(swapchain_info));
  if (old_swapchain) {
    vk_device.destroySwapchainKHR(old_swapchain, nullptr);
  }

  vk_abort_if_error(vk_device.getSwapchainImagesKHR(vk_swapchain, &sc->image_count, nullptr));
  vk_abort_if_error(vk_device.getSwapchainImagesKHR(vk_swapchain, &sc->image_count, sc->images));

  for (U64 i = 0; i < sc->image_count; ++i) {
    sc->image_views[i] =
        vk_create_image_view(vk_device, sc->images[i], swapchain_image_format, vk::ImageAspectFlagBits::eColor, 1);
    auto [msaa_img, msaa_alloc] = vk_create_image(vk_phys_dev,
                                                  vk_device,
                                                  swapchain_extent.width,
                                                  swapchain_extent.height,
                                                  1,
                                                  swapchain_image_format,
                                                  vk::ImageTiling::eOptimal,
                                                  vk::ImageUsageFlagBits::eColorAttachment,
                                                  device_arena,
                                                  msaa_samples);
    sc->msaa_images[i] = msaa_img;
    sc->msaa_image_views[i] =
        vk_create_image_view(vk_device, msaa_img, swapchain_image_format, vk::ImageAspectFlagBits::eColor, 1);
    sc->render_finished_sems[i] = vk_abort_if_error(vk_device.createSemaphore({}));
    sc->images_in_flight[i] = vk::Fence{};
    sc->image_initialized[i] = false;
  }

  if (depth_image)
    vk_device.destroyImage(*depth_image);
  if (depth_image_view)
    vk_device.destroyImageView(*depth_image_view);

  VKDepthResources depth = vk_create_depth_resources(vk_phys_dev,
                                                     vk_device,
                                                     device_arena,
                                                     swapchain_extent,
                                                     msaa_samples);
  *depth_image = depth.tex.image;
  *depth_image_view = depth.tex.image_view;
}

void vk_generate_mip_maps(vk::PhysicalDevice phys_dev,
                          vk::CommandBuffer command_buffer,
                          vk::Image image,
                          vk::Format image_format,
                          U32 width,
                          U32 height,
                          U32 mip_levels) {
  vk::FormatProperties format_properties = phys_dev.getFormatProperties(image_format);
  if (!(format_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
    NOT_IMPLEMENTED;
  }

  S32 mip_width = static_cast<S32>(width);
  S32 mip_height = static_cast<S32>(height);
  for (U32 i = 1; i < mip_levels; i++) {
    vk_transition_image_layout(command_buffer,
                               image,
                               vk::ImageLayout::eTransferDstOptimal,
                               vk::ImageLayout::eTransferSrcOptimal,
                               vk::AccessFlagBits2::eTransferWrite,
                               vk::AccessFlagBits2::eTransferRead,
                               vk::PipelineStageFlagBits2::eTransfer,
                               vk::PipelineStageFlagBits2::eTransfer,
                               vk::ImageAspectFlagBits::eColor,
                               1,
                               i - 1);
    vk::ImageBlit blit{
        .srcSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = i - 1, .layerCount = 1},
        .srcOffsets = std::array<vk::Offset3D, 2>({{}, {mip_width, mip_height, 1}}
            ),
        .dstSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = i, .layerCount = 1},
        .dstOffsets = std::array<vk::Offset3D, 2>(
            {{}, {1 < mip_width ? mip_width / 2 : 1, 1 < mip_height ? mip_height / 2 : 1, 1}}
            ),
    };
    command_buffer.blitImage(image,
                             vk::ImageLayout::eTransferSrcOptimal,
                             image,
                             vk::ImageLayout::eTransferDstOptimal,
                             blit,
                             vk::Filter::eLinear);
    vk_transition_image_layout(command_buffer,
                               image,
                               vk::ImageLayout::eTransferSrcOptimal,
                               vk::ImageLayout::eShaderReadOnlyOptimal,
                               vk::AccessFlagBits2::eTransferRead,
                               vk::AccessFlagBits2::eShaderRead,
                               vk::PipelineStageFlagBits2::eTransfer,
                               vk::PipelineStageFlagBits2::eFragmentShader,
                               vk::ImageAspectFlagBits::eColor,
                               1,
                               i - 1);
    if (1 < mip_width) {
      mip_width /= 2;
    }
    if (1 < mip_height) {
      mip_height /= 2;
    }
  }
  vk_transition_image_layout(command_buffer,
                             image,
                             vk::ImageLayout::eTransferDstOptimal,
                             vk::ImageLayout::eShaderReadOnlyOptimal,
                             vk::AccessFlagBits2::eTransferWrite,
                             vk::AccessFlagBits2::eShaderRead,
                             vk::PipelineStageFlagBits2::eTransfer,
                             vk::PipelineStageFlagBits2::eFragmentShader,
                             vk::ImageAspectFlagBits::eColor,
                             1,
                             mip_levels - 1);
}

internal vk::SampleCountFlagBits vk_get_max_usable_sample_count(vk::PhysicalDevice device) {
  vk::PhysicalDeviceProperties physical_device_properties = device.getProperties();
  vk::SampleCountFlags counts = physical_device_properties.limits.framebufferColorSampleCounts &
                                physical_device_properties.limits.framebufferDepthSampleCounts;
  if (counts & vk::SampleCountFlagBits::e64)
    return vk::SampleCountFlagBits::e64;
  if (counts & vk::SampleCountFlagBits::e32)
    return vk::SampleCountFlagBits::e32;
  if (counts & vk::SampleCountFlagBits::e16)
    return vk::SampleCountFlagBits::e16;
  if (counts & vk::SampleCountFlagBits::e8)
    return vk::SampleCountFlagBits::e8;
  if (counts & vk::SampleCountFlagBits::e4)
    return vk::SampleCountFlagBits::e4;
  if (counts & vk::SampleCountFlagBits::e2)
    return vk::SampleCountFlagBits::e2;

  return vk::SampleCountFlagBits::e1;
}

internal std::tuple<vk::Device, vk::Queue, vk::Queue>
vk_create_logical_device(vk::PhysicalDevice phys_dev, VKQueueFamilyIndices queue_indices) {
  float queue_priority = 1.0f;
  Array<vk::DeviceQueueCreateInfo, 2> queue_infos{};
  U32 queue_info_count = 0;
  queue_infos[queue_info_count++] = vk::DeviceQueueCreateInfo{
      .queueFamilyIndex = queue_indices.graphics_index, .queueCount = 1, .pQueuePriorities = &queue_priority,
  };
  if (queue_indices.graphics_index != queue_indices.present_index) {
    queue_infos[queue_info_count++] = vk::DeviceQueueCreateInfo{
        .queueFamilyIndex = queue_indices.present_index, .queueCount = 1, .pQueuePriorities = &queue_priority,
    };
  }
  Array device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME};
  PhysicalDeviceFeatures features = enable_phys_dev_features(phys_dev);
  vk::DeviceCreateInfo dev_info{
      .pNext = &features.get<vk::PhysicalDeviceFeatures2>(),
      .queueCreateInfoCount = queue_info_count,
      .pQueueCreateInfos = queue_infos,
      .enabledExtensionCount = device_extensions.size(),
      .ppEnabledExtensionNames = device_extensions,
  };
  vk::Device vk_device = vk_abort_if_error(phys_dev.createDevice(dev_info));
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_device);
  vk::Queue graphics_queue{}, present_queue{};
  vk_device.getQueue(queue_indices.graphics_index, 0, &graphics_queue);
  vk_device.getQueue(queue_indices.present_index, 0, &present_queue);
  return {vk_device, graphics_queue, present_queue};
}

internal std::tuple<vk::Format, vk::ColorSpaceKHR, vk::PresentModeKHR, vk::Extent2D>
vk_create_swapchain_config(vk::PhysicalDevice dev, vk::SurfaceKHR surface, U32 width, U32 height, Arena *arena) {
  vk::SurfaceCapabilitiesKHR capabilities{};
  vk_abort_if_error(dev.getSurfaceCapabilitiesKHR(surface, &capabilities));
  U32 format_count = 0;
  vk_abort_if_error(dev.getSurfaceFormatsKHR(surface, &format_count, nullptr));
  auto *formats = arena->push_array<vk::SurfaceFormatKHR>(format_count);
  vk_abort_if_error(dev.getSurfaceFormatsKHR(surface, &format_count, formats));
  U32 mode_count = 0;
  vk_abort_if_error(dev.getSurfacePresentModesKHR(surface, &mode_count, nullptr));
  auto *present_modes = arena->push_array<vk::PresentModeKHR>(mode_count);
  vk_abort_if_error(dev.getSurfacePresentModesKHR(surface, &mode_count, present_modes));
  vk::Format chosen_format = vk::Format::eB8G8R8A8Srgb;
  vk::ColorSpaceKHR chosen_color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
  bool format_found = false;
  for (U32 i = 0; i < format_count; ++i) {
    if (formats[i].format == chosen_format && formats[i].colorSpace == chosen_color_space) {
      format_found = true;
      break;
    }
  }
  if (!format_found && format_count > 0) {
    chosen_format = formats[0].format;
    chosen_color_space = formats[0].colorSpace;
  }
  vk::PresentModeKHR chosen_present_mode = vk::PresentModeKHR::eMailbox;
  bool mode_found = false;
  for (U32 i = 0; i < mode_count; ++i) {
    if (present_modes[i] == chosen_present_mode) {
      mode_found = true;
      break;
    }
  }
  if (!mode_found) {
    chosen_present_mode = vk::PresentModeKHR::eFifo;
  }
  vk::Extent2D swapchain_extent = capabilities.currentExtent;
  if (swapchain_extent.width == 0xFFFFFFFF) {
    swapchain_extent.width = width;
    swapchain_extent.height = height;
  }
  return {chosen_format, chosen_color_space, chosen_present_mode, swapchain_extent};
}

internal vk::SwapchainKHR vk_create_swapchain(vk::PhysicalDevice vk_phys_dev,
                                              vk::Device vk_device,
                                              vk::SurfaceKHR vk_surface,
                                              vk::Format chosen_format,
                                              vk::ColorSpaceKHR chosen_color_space,
                                              vk::Extent2D swapchain_extent,
                                              vk::PresentModeKHR chosen_present_mode,
                                              VKQueueFamilyIndices queue_indices) {
  vk::SurfaceCapabilitiesKHR capabilities{};
  vk_abort_if_error(vk_phys_dev.getSurfaceCapabilitiesKHR(vk_surface, &capabilities));
  U32 desired_image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && desired_image_count > capabilities.maxImageCount) {
    desired_image_count = capabilities.maxImageCount;
  }
  Array queue_indices_array = {queue_indices.graphics_index, queue_indices.present_index};
  vk::SharingMode sharing_mode = vk::SharingMode::eExclusive;
  U32 queue_family_index_count = 0;
  U32 *p_queue_family_indices = nullptr;
  if (queue_indices.graphics_index != queue_indices.present_index) {
    sharing_mode = vk::SharingMode::eConcurrent;
    queue_family_index_count = 2;
    p_queue_family_indices = queue_indices_array;
  }
  vk::SwapchainCreateInfoKHR swapchain_info{
      .surface = vk_surface,
      .minImageCount = desired_image_count,
      .imageFormat = chosen_format,
      .imageColorSpace = chosen_color_space,
      .imageExtent = swapchain_extent,
      .imageArrayLayers = 1,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
      .imageSharingMode = sharing_mode,
      .queueFamilyIndexCount = queue_family_index_count,
      .pQueueFamilyIndices = p_queue_family_indices,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode = chosen_present_mode,
      .clipped = true,
      .oldSwapchain = nullptr,
  };
  return vk_abort_if_error(vk_device.createSwapchainKHR(swapchain_info));
}

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
                              Array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> *command_buffers) {
  SwapchainResources *sc = &*swapchain_pool[0];
  vk_abort_if_error(vk_device.getSwapchainImagesKHR(swapchain, &sc->image_count, nullptr));
  vk_abort_if_error(vk_device.getSwapchainImagesKHR(swapchain, &sc->image_count, sc->images));
  for (U32 i = 0; i < sc->image_count; ++i) {
    vk::ImageViewCreateInfo view_info{
        .image = sc->images[i],
        .viewType = vk::ImageViewType::e2D,
        .format = chosen_format,
        .components = vk::ComponentMapping{
            .r = vk::ComponentSwizzle::eIdentity,
            .g = vk::ComponentSwizzle::eIdentity,
            .b = vk::ComponentSwizzle::eIdentity,
            .a = vk::ComponentSwizzle::eIdentity,
        },
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    sc->image_views[i] = vk_abort_if_error(vk_device.createImageView(view_info));
    auto [msaa_img, msaa_alloc] = vk_create_image(vk_phys_dev,
                                                  vk_device,
                                                  swapchain_extent.width,
                                                  swapchain_extent.height,
                                                  1,
                                                  chosen_format,
                                                  vk::ImageTiling::eOptimal,
                                                  vk::ImageUsageFlagBits::eColorAttachment,
                                                  device_arena,
                                                  msaa_samples);
    sc->msaa_images[i] = msaa_img;
    sc->msaa_image_views[i] =
        vk_create_image_view(vk_device, msaa_img, chosen_format, vk::ImageAspectFlagBits::eColor, 1);
  }
  vk::FenceCreateInfo fence_info{.flags = vk::FenceCreateFlagBits::eSignaled};
  for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    (*in_flight_fences)[i] = vk_abort_if_error(vk_device.createFence(fence_info));
    (*acquire_sems)[i] = vk_abort_if_error(vk_device.createSemaphore({}));
  }
  for (U32 i = 0; i < sc->image_count; ++i) {
    sc->render_finished_sems[i] = vk_abort_if_error(vk_device.createSemaphore({}));
  }
  vk::CommandPoolCreateInfo pool_info{
      .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      .queueFamilyIndex = queue_indices.graphics_index,
  };
  vk::CommandPool command_pool = vk_abort_if_error(vk_device.createCommandPool(pool_info));
  vk::CommandBufferAllocateInfo alloc_info{
      .commandPool = command_pool,
      .level = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
  };
  vk_abort_if_error(vk_device.allocateCommandBuffers(&alloc_info, *command_buffers));
  return {sc, command_pool};
}

internal VKTextureImage
vk_load_texture(vk::PhysicalDevice vk_phys_dev,
                vk::Device vk_device,
                VKGpuArena *host_arena,
                VKGpuArena *device_arena,
                vk::CommandPool vk_command_pool,
                vk::Queue graphics_queue,
                char const *texture_path) {
  VKTextureImage tex{};
  U32 tex_width{}, tex_height{}, tex_channels{};
  stbi_uc *pixels = stbi_load(texture_path,
                              reinterpret_cast<int *>(&tex_width),
                              reinterpret_cast<int *>(&tex_height),
                              reinterpret_cast<int *>(&tex_channels),
                              STBI_rgb_alpha);
  vk::DeviceSize image_size = tex_width * tex_height * 4;
  tex.mip_levels = static_cast<U32>(std::floor(std::log2(std::max(tex_width, tex_height)))) + 1;
  ASSERT_ALWAYS(pixels);
  auto [staging_buffer, staging_alloc] =
      vk_create_buffer(vk_phys_dev, vk_device, image_size, vk::BufferUsageFlagBits::eTransferSrc, host_arena);
  MemoryCopy(staging_alloc.mapped, pixels, image_size);
  stbi_image_free(pixels);
  std::tie(tex.image, tex.image_alloc) = vk_create_image(
      vk_phys_dev,
      vk_device,
      tex_width,
      tex_height,
      tex.mip_levels,
      vk::Format::eR8G8B8A8Srgb,
      vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
      device_arena);
  vk_abort_if_error(vk_device.setDebugUtilsObjectNameEXT({
      .objectType = vk::ObjectType::eImage,
      .objectHandle = reinterpret_cast<uint64_t>(static_cast<VkImage>(tex.image)),
      .pObjectName = "texture image",
  }));
  vk::CommandBuffer command_buffer = vk_begin_single_time_command(vk_device, vk_command_pool, "texture upload");
  vk_transition_image_layout(command_buffer,
                             tex.image,
                             vk::ImageLayout::eUndefined,
                             vk::ImageLayout::eTransferDstOptimal,
                             {},
                             vk::AccessFlagBits2::eTransferWrite,
                             vk::PipelineStageFlagBits2::eTopOfPipe,
                             vk::PipelineStageFlagBits2::eTransfer,
                             vk::ImageAspectFlagBits::eColor,
                             tex.mip_levels);
  vk_copy_buffer_to_image(command_buffer, staging_buffer, tex.image, tex_width, tex_height);
  vk_generate_mip_maps(vk_phys_dev,
                       command_buffer,
                       tex.image,
                       vk::Format::eR8G8B8A8Srgb,
                       tex_width,
                       tex_height,
                       tex.mip_levels);
  vk_end_single_time_command(vk_device, graphics_queue, vk_command_pool, command_buffer);
  vk_device.destroyBuffer(staging_buffer);
  tex.image_view = vk_create_image_view(vk_device,
                                        tex.image,
                                        vk::Format::eR8G8B8A8Srgb,
                                        vk::ImageAspectFlagBits::eColor,
                                        tex.mip_levels);
  return tex;
}

internal void vk_destroy_texture(vk::Device device, VKTextureImage *tex) {
  device.destroyImageView(tex->image_view);
  device.destroyImage(tex->image);
}

internal VKBuiltShaderStages build_shader_stages(Arena *arena,
                                                 vk::Device vk_device,
                                                 DynArray<VKShaderStageDesc> *shader_stage_descriptions) {
  VKBuiltShaderStages out{};
  out.stages = DynArray<vk::PipelineShaderStageCreateInfo>::with_capacity(arena, shader_stage_descriptions->size);
  // will create some extra, but its fine. this object is freed after pipeline creation anyways
  out.modules = DynArray<VKLoadedShaderModule>::with_capacity(arena, shader_stage_descriptions->size);

  // compile modules
  for (U64 i = 0; i < shader_stage_descriptions->size; i++) {
    String8 file_name = (*shader_stage_descriptions)[i].file_name;
    bool already_compiled = false;
    for (U64 j = 0; j < out.modules.size; j++) {
      if (out.modules[j].file_name == file_name) {
        already_compiled = true;
        break;
      }
    }
    if (already_compiled)
      continue;

    auto *dst = &out.modules[out.modules.size++];
    dst->file_name = file_name;

    TempScope scratch = scratch_begin_scoped(&arena, 1);

    SPIRVBlob spirv = read_spirv_blob(scratch, file_name);
    dst->module = vk_abort_if_error(
        vk_device.createShaderModule({.codeSize = spirv.byte_count, .pCode = spirv.words}));
  }

  for (U64 i = 0; i < shader_stage_descriptions->size; i++) {
    VKShaderStageDesc stage = (*shader_stage_descriptions)[i];

    auto *dst = &out.stages[out.stages.size++];

    dst->stage = (*shader_stage_descriptions)[i].stage;
    dst->sType = vk::StructureType::ePipelineShaderStageCreateInfo;
    dst->pName = stage.entrypoint_name.c_str(arena);

    bool found_module = false;
    vk::ShaderModule *shader_module{};
    for (U64 j = 0; j < out.modules.size; j++) {
      if ((*shader_stage_descriptions)[i].file_name == out.modules[j].file_name) {
        found_module = true;
        shader_module = &out.modules[j].module;
        break;
      }
    }
    if (!found_module) {
      ASSERT(!"Didnt find shader module");
    }
    dst->module = *shader_module;
  }
  return out;
}

internal vk::Pipeline create_gpl(vk::Device vk_device,
                                 vk::PipelineCache cache,
                                 vk::GraphicsPipelineLibraryFlagsEXT subset,
                                 vk::GraphicsPipelineCreateInfo ci) {
  vk::GraphicsPipelineLibraryCreateInfoEXT gpl_ci{
      .flags = subset
  };

  ci.flags |= vk::PipelineCreateFlagBits::eLibraryKHR;
  ci.flags |= vk::PipelineCreateFlagBits::eRetainLinkTimeOptimizationInfoEXT;
  gpl_ci.pNext = ci.pNext;
  ci.pNext = &gpl_ci;

  vk::Pipeline pipe = vk_abort_if_error(vk_device.createGraphicsPipeline(cache, ci));
  return pipe;
}

internal vk::DescriptorSetLayout create_descriptor_set_layout(
    vk::Device device, U32 binding_count, vk::DescriptorSetLayoutBinding const* bindings)
{
  vk::DescriptorSetLayoutCreateInfo ci{.bindingCount = binding_count, .pBindings = bindings};
  return vk_abort_if_error(device.createDescriptorSetLayout(ci));
}

internal vk::PipelineLayout create_pipeline_layout(
    vk::Device device, vk::DescriptorSetLayout set_layout)
{
  vk::PipelineLayoutCreateInfo ci{.setLayoutCount = 1, .pSetLayouts = &set_layout};
  return vk_abort_if_error(device.createPipelineLayout(ci));
}

internal vk::Pipeline create_vertex_input_library(
    vk::Device device, VertexInputLibDesc const& desc)
{
  vk::PipelineVertexInputStateCreateInfo vertex_input{
      .vertexBindingDescriptionCount = static_cast<U32>(desc.bindings.size),
      .pVertexBindingDescriptions = desc.bindings,
      .vertexAttributeDescriptionCount = static_cast<U32>(desc.attributes.size),
      .pVertexAttributeDescriptions = desc.attributes,
  };
  vk::PipelineInputAssemblyStateCreateInfo input_assembly{
      .topology = desc.topology,
      .primitiveRestartEnable = desc.primitive_restart,
  };
  vk::GraphicsPipelineCreateInfo ci{
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
  };
  return create_gpl(device, nullptr,
      vk::GraphicsPipelineLibraryFlagBitsEXT::eVertexInputInterface, ci);
}

internal vk::Pipeline create_pre_raster_library(
    vk::Device device, vk::PipelineLayout layout,
    PreRasterLibDesc const& desc,
    vk::PipelineShaderStageCreateInfo const* stages, U32 stage_count)
{
  vk::PipelineViewportStateCreateInfo viewport_state{
      .viewportCount = desc.viewport_count,
      .scissorCount = desc.scissor_count,
  };
  vk::PipelineRasterizationStateCreateInfo rasterizer{
      .depthClampEnable = desc.depth_clamp,
      .rasterizerDiscardEnable = desc.rasterizer_discard,
      .polygonMode = desc.polygon_mode,
      .cullMode = desc.cull_mode,
      .frontFace = desc.front_face,
      .depthBiasEnable = desc.depth_bias,
      .lineWidth = desc.line_width,
  };
  vk::DynamicState default_states[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
  auto* states = desc.dynamic_states ? desc.dynamic_states : default_states;
  U32 state_count = desc.dynamic_states ? desc.dynamic_state_count : 2;
  vk::PipelineDynamicStateCreateInfo dynamic_state{
      .dynamicStateCount = state_count,
      .pDynamicStates = states,
  };

  vk::GraphicsPipelineCreateInfo ci{
      .stageCount = stage_count,
      .pStages = stages,
      .pViewportState = &viewport_state,
      .pRasterizationState = &rasterizer,
      .pDynamicState = &dynamic_state,
      .layout = layout,
  };

  vk::PipelineTessellationStateCreateInfo tess_state{};
  if (desc.tessellation) {
    tess_state.patchControlPoints = desc.tess_patch_control_points;
    ci.pTessellationState = &tess_state;
  }

  return create_gpl(device, nullptr,
      vk::GraphicsPipelineLibraryFlagBitsEXT::ePreRasterizationShaders, ci);
}

internal vk::Pipeline create_fragment_library(
    vk::Device device, vk::PipelineLayout layout,
    FragmentLibDesc const& desc,
    vk::PipelineShaderStageCreateInfo const* stages, U32 stage_count)
{
  vk::PipelineDepthStencilStateCreateInfo depth_stencil{
      .depthTestEnable = desc.depth_test,
      .depthWriteEnable = desc.depth_write,
      .depthCompareOp = desc.depth_compare,
      .depthBoundsTestEnable = desc.depth_bounds_test,
      .stencilTestEnable = desc.stencil_test,
  };
  vk::PipelineMultisampleStateCreateInfo multisampling{};
  if (desc.sample_shading_enable) {
    multisampling = vk::PipelineMultisampleStateCreateInfo{
        .rasterizationSamples = desc.msaa_samples,
        .sampleShadingEnable = desc.sample_shading_enable,
        .minSampleShading = desc.min_sample_shading,
    };
  }

  vk::GraphicsPipelineCreateInfo ci{
      .stageCount = stage_count,
      .pStages = stages,
      .pMultisampleState = desc.sample_shading_enable ? &multisampling : nullptr,
      .pDepthStencilState = &depth_stencil,
      .layout = layout,
  };
  return create_gpl(device, nullptr,
      vk::GraphicsPipelineLibraryFlagBitsEXT::eFragmentShader, ci);
}

internal vk::Pipeline create_fragment_output_library(
    vk::Device device, FragmentOutputLibDesc const& desc)
{
  vk::PipelineColorBlendAttachmentState color_blend_attachment{
      .blendEnable = desc.blend_enable,
      .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
  };
  vk::PipelineColorBlendStateCreateInfo color_blending{
      .logicOpEnable = vk::False,
      .logicOp = vk::LogicOp::eCopy,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment,
  };
  vk::PipelineMultisampleStateCreateInfo multisampling{
      .rasterizationSamples = desc.msaa_samples,
      .sampleShadingEnable = vk::True,
      .minSampleShading = 0.2f,
  };
  vk::PipelineRenderingCreateInfo prci{
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &desc.color_format,
      .depthAttachmentFormat = desc.depth_format,
  };
  vk::GraphicsPipelineCreateInfo ci{
      .pNext = &prci,
      .pMultisampleState = &multisampling,
      .pColorBlendState = &color_blending,
  };
  return create_gpl(device, nullptr,
      vk::GraphicsPipelineLibraryFlagBitsEXT::eFragmentOutputInterface, ci);
}

internal vk::Pipeline create_linked_pipeline(
    vk::Device device, vk::PipelineLayout layout,
    U32 library_count, vk::Pipeline const* libraries,
    vk::Format color_format, vk::Format depth_format)
{
  vk::PipelineRenderingCreateInfo prci{
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &color_format,
      .depthAttachmentFormat = depth_format,
  };
  vk::PipelineLibraryCreateInfoKHR link_info{
      .libraryCount = library_count,
      .pLibraries = libraries,
  };
  prci.pNext = &link_info;
  vk::GraphicsPipelineCreateInfo ci{
      .pNext = &prci,
      .layout = layout,
  };
  return vk_abort_if_error(device.createGraphicsPipeline(nullptr, ci));
}

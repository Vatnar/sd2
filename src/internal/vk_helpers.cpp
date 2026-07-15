#include <string_view>

#include "../sd2_inc.hpp"


internal void abort_if_vk_error(vk::Result res, std::string_view message) {
  if (res != vk::Result::eSuccess) {
    std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(message.size()), message.data(), vk::to_string(res).c_str());
    std::fflush(stderr);
    TRAP();
  }
}


internal QueueFamilyIndices find_queue_families(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  Temp               scratch = scratch_begin(0, 0);
  QueueFamilyIndices indices{};

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
    auto       res             = device.getSurfaceSupportKHR(i, surface, &present_support);
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

internal bool check_device_extension_support(vk::PhysicalDevice device, char const *extension_name) {
  Temp scratch = scratch_begin(0, 0);
  U32  ext_count; // real ext count is 279..
  bool result = false;
  abort_if_vk_error(device.enumerateDeviceExtensionProperties(nullptr, &ext_count, nullptr));

  auto *available = scratch.arena->push_array<vk::ExtensionProperties>(ext_count);
  abort_if_vk_error(device.enumerateDeviceExtensionProperties(nullptr, &ext_count, available));

  for (U32 i = 0; i < ext_count; ++i) {
    if (std::strcmp(available[i].extensionName, extension_name) == 0)
      result = true;
  }
  scratch.end();
  return result;
}


internal SwapchainSupport query_swapchain_support(vk::PhysicalDevice device, vk::SurfaceKHR surface, Arena *arena) {
  SwapchainSupport support{};
  support.capabilities = abort_if_vk_error(device.getSurfaceCapabilitiesKHR(surface));

  abort_if_vk_error(device.getSurfaceFormatsKHR(surface, &support.format_count, nullptr));
  support.formats = arena->push_array<vk::SurfaceFormatKHR>(support.format_count);

  abort_if_vk_error(device.getSurfaceFormatsKHR(surface, &support.format_count, support.formats));

  abort_if_vk_error(device.getSurfacePresentModesKHR(surface, &support.mode_count, nullptr));
  support.modes = arena->push_array<vk::PresentModeKHR>(support.mode_count);
  abort_if_vk_error(device.getSurfacePresentModesKHR(surface, &support.mode_count, support.modes));

  return support;
}


internal RatedDevice pick_best_physical_device(vk::Instance instance, vk::SurfaceKHR surface) {
  Temp scratch      = scratch_begin(0, 0);
  U32  device_count = 0;
  abort_if_vk_error(instance.enumeratePhysicalDevices(&device_count, nullptr));

  auto *devices = scratch.arena->push_array<vk::PhysicalDevice>(device_count);
  abort_if_vk_error(instance.enumeratePhysicalDevices(&device_count, devices));

  vk::PhysicalDevice best_device = nullptr;
  U32                max_score   = 0;

  for (U32 i = 0; i < device_count; i++) {
    vk::PhysicalDevice &device = devices[i];

    QueueFamilyIndices indices = find_queue_families(device, surface);
    if (!indices.is_complete())
      continue;

    if (!check_device_extension_support(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
      continue;

    SwapchainSupport swapchain_support = query_swapchain_support(device, surface, scratch.arena);
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
      max_score   = score;
      best_device = device;
    }
  }
  scratch.end();
  return {best_device, max_score};
}


internal void recreate_swapchain(GLFWwindow         *glfw_window,
                                 vk::Device          vk_device,
                                 SwapchainResources *sc,
                                 vk::SwapchainKHR   &vk_swapchain,
                                 vk::PhysicalDevice  vk_phys_dev,
                                 vk::SurfaceKHR      vk_surface,
                                 vk::Extent2D       &swapchain_extent,
                                 vk::Format          swapchain_image_format,
                                 vk::ColorSpaceKHR   swapchain_color_space,
                                 vk::PresentModeKHR  present_mode) {
  S32 width = 0, height = 0;
  glfwGetFramebufferSize(glfw_window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(glfw_window, &width, &height);
    glfwWaitEvents();
  }

  abort_if_vk_error(vk_device.waitIdle());

  for (U32 i = 0; i < sc->image_count; ++i) {
    vk_device.destroySemaphore(sc->render_finished_sems[i]);
    vk_device.destroyImageView(sc->image_views[i], nullptr);
    sc->image_initialized[i] = false;
  }

  vk::SwapchainKHR old_swapchain = vk_swapchain;

  vk::SurfaceCapabilitiesKHR capabilities = abort_if_vk_error(vk_phys_dev.getSurfaceCapabilitiesKHR(vk_surface));
  vk::Extent2D               new_extent   = capabilities.currentExtent;

  // TODO: drivers rarely return 0xFFFFFFFF for currentextent, instead they return exact changing
  // physical pixel sizes. Which makes it take arbitrary dimensions the surface reported mid frame.
  if (new_extent.width == std::numeric_limits<U32>::max()) {
    new_extent.width =
        std::clamp(static_cast<U32>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    new_extent.height =
        std::clamp(static_cast<U32>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
  }

  swapchain_extent = new_extent;

  QueueFamilyIndices indices                = find_queue_families(vk_phys_dev, vk_surface);
  U32                queue_family_indices[] = {indices.graphics_index, indices.present_index};

  U32 desired_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && desired_count > capabilities.maxImageCount) {
    desired_count = capabilities.maxImageCount;
  }

  vk::SwapchainCreateInfoKHR swapchain_info{
      .surface          = vk_surface,
      .minImageCount    = desired_count,
      .imageFormat      = swapchain_image_format,
      .imageColorSpace  = swapchain_color_space,
      .imageExtent      = swapchain_extent,
      .imageArrayLayers = 1,
      .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
      .preTransform     = capabilities.currentTransform,
      .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode      = present_mode,
      .clipped          = true,
      .oldSwapchain     = old_swapchain,
  };

  if (indices.graphics_index != indices.present_index) {
    swapchain_info.imageSharingMode      = vk::SharingMode::eConcurrent;
    swapchain_info.queueFamilyIndexCount = 2;
    swapchain_info.pQueueFamilyIndices   = queue_family_indices;
  } else {
    swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
  }

  vk_swapchain = abort_if_vk_error(vk_device.createSwapchainKHR(swapchain_info));
  if (old_swapchain) {
    vk_device.destroySwapchainKHR(old_swapchain, nullptr);
  }

  abort_if_vk_error(vk_device.getSwapchainImagesKHR(vk_swapchain, &sc->image_count, nullptr));
  abort_if_vk_error(vk_device.getSwapchainImagesKHR(vk_swapchain, &sc->image_count, sc->images));

  for (U64 i = 0; i < sc->image_count; ++i) {
    vk::ImageViewCreateInfo view_info{
        .image            = sc->images[i],
        .viewType         = vk::ImageViewType::e2D,
        .format           = swapchain_image_format,
        .subresourceRange = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1}
    };
    sc->image_views[i]          = abort_if_vk_error(vk_device.createImageView(view_info));
    sc->render_finished_sems[i] = abort_if_vk_error(vk_device.createSemaphore({}));
    sc->images_in_flight[i]     = vk::Fence{};
    sc->image_initialized[i]    = false;
  }
}

internal bool check_validation_layer_support() {
  Temp scratch     = scratch_begin(0, 0);
  U32  layer_count = 0;
  bool result      = false;

  // TODO: consider not requerying this if this is called a lot of times. fine for one offs though
  abort_if_vk_error(vk::enumerateInstanceLayerProperties(&layer_count, nullptr));
  auto *available_layers = scratch.arena->push_array<vk::LayerProperties>(layer_count);
  abort_if_vk_error(vk::enumerateInstanceLayerProperties(&layer_count, available_layers));

  for (U32 i = 0; i < layer_count; ++i) {
    if (std::strcmp(available_layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
      result = true;
      break;
    }
  }
  scratch.end();
  return result;
}

// TODO: should this maxed, or should we just scratch alloc or alloc into arena

internal U32 get_required_extensions(char const **out_extensions, U32 max_extensions) {
  U32          glfw_count;
  char const **glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_count);

  U32 count = 0;
  for (U32 i = 0; i < glfw_count && count < max_extensions; ++i)
    out_extensions[count++] = glfw_exts[i];

#ifdef SD2_DEBUG
  if (count < max_extensions)
    out_extensions[count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif
  return count;
}

internal vk::ImageMemoryBarrier2 make_image_barrier(U32                     image_index,
                                                    SwapchainResources     *sc,
                                                    vk::ImageLayout         old_layout,
                                                    vk::ImageLayout         new_layout,
                                                    vk::AccessFlags2        src_access_mask,
                                                    vk::AccessFlags2        dst_access_mask,
                                                    vk::PipelineStageFlags2 src_stage_mask,
                                                    vk::PipelineStageFlags2 dst_stage_mask) {
  return {
      .srcStageMask        = src_stage_mask,
      .srcAccessMask       = src_access_mask,
      .dstStageMask        = dst_stage_mask,
      .dstAccessMask       = dst_access_mask,
      .oldLayout           = old_layout,
      .newLayout           = new_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image               = sc->images[image_index],
      .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                              .baseMipLevel   = 0,
                              .levelCount     = 1,
                              .baseArrayLayer = 0,
                              .layerCount     = 1},
  };
}

internal vk::DependencyInfo dep_info(vk::ImageMemoryBarrier2 *barrier) {
  return {.dependencyFlags = {}, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = barrier};
}

internal U32 find_memory_type(vk::PhysicalDevice vk_phys_dev, U32 type_filter, vk::MemoryPropertyFlags properties) {
  vk::PhysicalDeviceMemoryProperties mem_properties = vk_phys_dev.getMemoryProperties();
  for (U32 i = 0; i < mem_properties.memoryTypeCount; i++) {
    if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  NOT_IMPLEMENTED;
  return 0;
}
internal std::pair<vk::Buffer, vk::DeviceMemory> create_buffer(vk::PhysicalDevice      vk_phys_dev,
                                                               vk::Device              vk_device,
                                                               vk::DeviceSize          size,
                                                               vk::BufferUsageFlags    usage,
                                                               vk::MemoryPropertyFlags properties) {
  vk::BufferCreateInfo   buffer_info{.size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive};
  vk::Buffer             buffer              = abort_if_vk_error(vk_device.createBuffer(buffer_info));
  vk::MemoryRequirements memory_requirements = vk_device.getBufferMemoryRequirements(buffer);
  vk::MemoryAllocateInfo alloc_info          = {
               .allocationSize  = memory_requirements.size,
               .memoryTypeIndex = find_memory_type(vk_phys_dev, memory_requirements.memoryTypeBits, properties)};
  vk::DeviceMemory buffer_memory = abort_if_vk_error(vk_device.allocateMemory(alloc_info));
  abort_if_vk_error(vk_device.bindBufferMemory(buffer, buffer_memory, 0));
  return {buffer, buffer_memory};
}
internal void copy_buffer(vk::Device      vk_device,
                          vk::CommandPool command_pool,
                          vk::Queue       queue,
                          vk::Buffer      src_buffer,
                          vk::Buffer      dst_buffer,
                          vk::DeviceSize  size) {
  vk::CommandBuffer command_copy_buffer = begin_single_time_command(vk_device, command_pool, "copy buffer");
  command_copy_buffer.copyBuffer(src_buffer, dst_buffer, vk::BufferCopy(0, 0, size));
  end_single_time_command(vk_device, queue, command_pool, command_copy_buffer);
}
internal std::pair<vk::Image, vk::DeviceMemory> create_image(vk::PhysicalDevice      vk_phys_dev,
                                                             vk::Device              vk_device,
                                                             U32                     width,
                                                             U32                     height,
                                                             vk::Format              format,
                                                             vk::ImageTiling         tiling,
                                                             vk::ImageUsageFlags     usage,
                                                             vk::MemoryPropertyFlags properties) {
  vk::Image           image     = nullptr;
  vk::DeviceMemory    image_mem = nullptr;
  vk::ImageCreateInfo image_info{
      .imageType     = vk::ImageType::e2D,
      .format        = format,
      .extent        = {width, height, 1},
      .mipLevels     = 1,
      .arrayLayers   = 1,
      .samples       = vk::SampleCountFlagBits::e1,
      .tiling        = tiling,
      .usage         = usage,
      .sharingMode   = vk::SharingMode::eExclusive,
      .initialLayout = vk::ImageLayout::eUndefined,
  };
  image                           = abort_if_vk_error(vk_device.createImage(image_info));
  vk::MemoryRequirements mem_reqs = vk_device.getImageMemoryRequirements(image);
  vk::MemoryAllocateInfo alloc_info{
      .allocationSize  = mem_reqs.size,
      .memoryTypeIndex = find_memory_type(vk_phys_dev, mem_reqs.memoryTypeBits, properties)};
  image_mem = abort_if_vk_error(vk_device.allocateMemory(alloc_info));
  abort_if_vk_error(vk_device.bindImageMemory(image, image_mem, 0));
  return {image, image_mem};
}


internal vk::CommandBuffer
         begin_single_time_command(vk::Device vk_device, vk::CommandPool command_pool, char const *name) {
  vk::CommandBuffer             command_buffer = nullptr;
  vk::CommandBufferAllocateInfo alloc_info{.commandPool        = command_pool,
                                                    .level              = vk::CommandBufferLevel::ePrimary,
                                                    .commandBufferCount = 1};

  abort_if_vk_error(vk_device.allocateCommandBuffers(&alloc_info, &command_buffer));
  vk::CommandBufferBeginInfo begin_info{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
  abort_if_vk_error(command_buffer.begin(begin_info));
  if (name) {
    abort_if_vk_error(vk_device.setDebugUtilsObjectNameEXT({
                 .objectType   = vk::ObjectType::eCommandBuffer,
                 .objectHandle = (uint64_t)(VkCommandBuffer)command_buffer,
                 .pObjectName  = name,
    }));
  }
  return command_buffer;
}
internal void end_single_time_command(vk::Device        vk_device,
                                      vk::Queue         queue,
                                      vk::CommandPool   command_pool,
                                      vk::CommandBuffer command_buffer) {
  abort_if_vk_error(command_buffer.end());
  vk::SubmitInfo submit_info{
      .commandBufferCount = 1,
      .pCommandBuffers    = &command_buffer,
  };
  abort_if_vk_error(queue.submit(submit_info, nullptr));
  abort_if_vk_error(queue.waitIdle());
  vk_device.freeCommandBuffers(command_pool, 1, &command_buffer);
}

internal void transition_image_layout(vk::CommandBuffer command_buffer,
                                      vk::Image         image,
                                      vk::ImageLayout   old_layout,
                                      vk::ImageLayout   new_layout) {
  vk::PipelineStageFlags source_stage      = {};
  vk::PipelineStageFlags destination_stage = {};
  vk::AccessFlags        src_access_mask   = {};
  vk::AccessFlags        dst_access_mask   = {};
  {
    if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferDstOptimal) {
      dst_access_mask = vk::AccessFlagBits::eTransferWrite;

      source_stage      = vk::PipelineStageFlagBits::eTopOfPipe;
      destination_stage = vk::PipelineStageFlagBits::eTransfer;
    } else if (old_layout == vk::ImageLayout::eTransferDstOptimal &&
               new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
      src_access_mask = vk::AccessFlagBits::eTransferWrite;
      dst_access_mask = vk::AccessFlagBits::eShaderRead;

      source_stage      = vk::PipelineStageFlagBits::eTransfer;
      destination_stage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
      INVALID_PATH;
    }
  }

  vk::ImageMemoryBarrier barrier{
      .srcAccessMask       = src_access_mask,
      .dstAccessMask       = dst_access_mask,
      .oldLayout           = old_layout,
      .newLayout           = new_layout,
      .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
      .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
      .image               = image,
      .subresourceRange    = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1}
  };

  command_buffer.pipelineBarrier(source_stage, destination_stage, {}, {}, nullptr, barrier);
}

internal void
copy_buffer_to_image(vk::CommandBuffer command_buffer, vk::Buffer buffer, vk::Image image, U32 width, U32 height) {
  vk::BufferImageCopy region{
      .bufferOffset      = 0,
      .bufferRowLength   = 0,
      .bufferImageHeight = 0,
      .imageSubresource  = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                            .mipLevel       = 0,
                            .baseArrayLayer = 0,
                            .layerCount     = 1},
      .imageOffset       = {.x = 0, .y = 0, .z = 0},
      .imageExtent       = {.width = width, .height = height, .depth = 1}
  };
  command_buffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
}

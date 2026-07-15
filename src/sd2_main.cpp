#include <cstdio>
#include <ctime>
#include <fstream>
#include <thread>

#include <sys/ioctl.h>
#define SD2_DEBUG 1
#define STB_IMAGE_IMPLEMENTATION
#include "sd2_inc.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
static vk::detail::DynamicLoader g_vulkan_dynamic_loader{};

//~ cpp

#include "internal/arena.cpp"
#include "internal/vk_helpers.cpp"


struct AppParams {
  String8 name{};
  S32     width{};
  S32     height{};
};

void glfw_error_callback(int ec, char const *desc) {
  printf("%d: %s\n", ec, desc);
}

struct Vertex {
  glm::vec2 pos;
  glm::vec3 color;

  static constexpr vk::VertexInputBindingDescription get_binding_description() {
    return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
  }

  static constexpr Array<vk::VertexInputAttributeDescription, 2> get_attribute_descriptions() {
    return {
        {
         vk::VertexInputAttributeDescription{.location = 0,
                                                .binding  = 0,
                                                .format   = vk::Format::eR32G32Sfloat,
                                                .offset   = offsetof(Vertex, pos)},
         vk::VertexInputAttributeDescription{.location = 1,
                                                .binding  = 0,
                                                .format   = vk::Format::eR32G32B32Sfloat,
                                                .offset   = offsetof(Vertex, color)},
         }
    };
  }
};


struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};


int main() {
  thread_ctx_init();
  Arena    *app_arena   = arena_alloc({.name = "App arena"});
  Arena    *frame_arena = arena_alloc({.flags = ArenaFlags::NO_CHAIN, .name = "Frame arena"});
  Arena    *init_arena  = arena_alloc({.name = "init"});
  AppParams params{
      .name   = str8_lit("sd2"),
      .width  = 800,
      .height = 600,
  };

  //~ Scope 1: Window + GLFW
  Window window{};
  F64    target_frame_ms{};
  {
    glfwSetErrorCallback(&glfw_error_callback);
    if (glfwInit() == false) {
      return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window.glfw_window      = glfwCreateWindow(params.width, params.height, "sd2", nullptr, nullptr);
    GLFWwindow *glfw_window = window.glfw_window;

    glfwSetWindowUserPointer(glfw_window, &window);
    glfwSetFramebufferSizeCallback(glfw_window, Window::dispatch_resize);
    glfwSetKeyCallback(glfw_window, Window::dispatch_key);
    glfwSetScrollCallback(glfw_window, Window::dispatch_scroll);
    glfwSetCursorPosCallback(glfw_window, Window::dispatch_cursor);
    glfwSetMouseButtonCallback(glfw_window, Window::dispatch_mouse_button);
    glfwSetWindowCloseCallback(glfw_window, Window::dispatch_close);
    glfwSetWindowRefreshCallback(glfw_window, Window::dispatch_refresh);
    glfwSetCharCallback(glfw_window, Window::dispatch_char);

    GLFWmonitor       *primary_monitor = glfwGetPrimaryMonitor();
    GLFWvidmode const *video_mode      = glfwGetVideoMode(primary_monitor);
    U32                monitor_hz      = video_mode->refreshRate > 0 ? video_mode->refreshRate : 60;
    target_frame_ms                    = 1000.0 / monitor_hz;
    printf("Monitor: %u Hz (target: %.3f ms/frame)\n", monitor_hz, target_frame_ms);
  }

  //~ Scope 2: Vulkan Instance + Debug Messenger + Surface
  vk::Instance              vk_instance{};
  vk::SurfaceKHR            vk_surface{};
  PFN_vkGetInstanceProcAddr vk_get_instance_proc_addr{};
#ifdef SD2_DEBUG
  VkDebugUtilsMessengerEXT debug_messenger{};
#endif
  {
    vk_get_instance_proc_addr =
        g_vulkan_dynamic_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_get_instance_proc_addr);

    vk::ApplicationInfo app_info{.pApplicationName   = "sd2",
                                 .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                 .pEngineName        = "NoEngine",
                                 .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
                                 .apiVersion         = VK_API_VERSION_1_4};

#ifdef SD2_DEBUG
    if (!check_validation_layer_support()) {
      std::fprintf(stderr, "VK_LAYER_KHRONOS_validation not available\n");
      TRAP();
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData       = nullptr,
    };
#endif

    const char **extensions        = init_arena->push_array<const char *>(MAX_INSTANCE_EXTENSIONS);
    U32          ext_count         = get_required_extensions(extensions, MAX_INSTANCE_EXTENSIONS);
    Array        validation_layers = {"VK_LAYER_KHRONOS_validation"};

    vk::InstanceCreateInfo inst_info{
#ifdef SD2_DEBUG
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = 1,
        .ppEnabledLayerNames     = validation_layers,
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = extensions,
#else
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = extensions,
#endif
    };

    vk_instance = abort_if_vk_error(vk::createInstance(inst_info), "Failed to create vulkan instance");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_instance);

#ifdef SD2_DEBUG
    {
      PFN_vkCreateDebugUtilsMessengerEXT func =
          (PFN_vkCreateDebugUtilsMessengerEXT)vk_get_instance_proc_addr(static_cast<VkInstance>(vk_instance),
                                                                        "vkCreateDebugUtilsMessengerEXT");
      if (func)
        func(static_cast<VkInstance>(vk_instance), &debug_create_info, nullptr, &debug_messenger);
    }
#endif

    VkSurfaceKHR raw_surface{};
    if (glfwCreateWindowSurface(vk_instance, window.glfw_window, nullptr, &raw_surface) != VK_SUCCESS) {
      TRAP();
    }
    vk_surface = raw_surface;
  }

  //~ Scope 3: Physical Device + Queue Families
  vk::PhysicalDevice vk_phys_dev{};
  QueueFamilyIndices queue_indices{};
  {
    RatedDevice rated_device = pick_best_physical_device(vk_instance, vk_surface);
    if (rated_device.score == 0) {
      TRAP();
    }
    vk_phys_dev   = rated_device.device;
    queue_indices = find_queue_families(vk_phys_dev, vk_surface);
  }

  //~ Scope 4: Logical Device + Queues
  vk::Device vk_device{};
  vk::Queue  graphics_queue{};
  vk::Queue  present_queue{};
  {
    float queue_priority = 1.0f;

    Array<vk::DeviceQueueCreateInfo, 2> queue_infos{};
    U32                                 queue_info_count = 0;

    queue_infos[queue_info_count++] = vk::DeviceQueueCreateInfo{
        .queueFamilyIndex = queue_indices.graphics_index,
        .queueCount       = 1,
        .pQueuePriorities = &queue_priority,
    };
    if (queue_indices.graphics_index != queue_indices.present_index) {
      queue_infos[queue_info_count++] = vk::DeviceQueueCreateInfo{
          .queueFamilyIndex = queue_indices.present_index,
          .queueCount       = 1,
          .pQueuePriorities = &queue_priority,
      };
    }

    Array device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    vk::PhysicalDeviceFeatures         device_features{};
    vk::PhysicalDeviceVulkan11Features device_features11{.shaderDrawParameters = true};
    vk::PhysicalDeviceVulkan13Features device_features13{.pNext            = &device_features11,
                                                         .synchronization2 = true,
                                                         .dynamicRendering = true};

    vk::DeviceCreateInfo dev_info{.pNext                   = &device_features13,
                                  .queueCreateInfoCount    = queue_info_count,
                                  .pQueueCreateInfos       = queue_infos,
                                  .enabledExtensionCount   = 1,
                                  .ppEnabledExtensionNames = device_extensions,
                                  .pEnabledFeatures        = &device_features};

    vk_device = abort_if_vk_error(vk_phys_dev.createDevice(dev_info));
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_device);

    vk_device.getQueue(queue_indices.graphics_index, 0, &graphics_queue);
    vk_device.getQueue(queue_indices.present_index, 0, &present_queue);
  }

  //~ Scope 5: Swapchain Format + Present Mode Selection
  vk::Format         chosen_format{};
  vk::ColorSpaceKHR  chosen_color_space{};
  vk::PresentModeKHR chosen_present_mode{};
  vk::Extent2D       swapchain_extent{};
  {
    vk::SurfaceCapabilitiesKHR capabilities{};
    abort_if_vk_error(vk_phys_dev.getSurfaceCapabilitiesKHR(vk_surface, &capabilities));

    U32 format_count = 0;
    abort_if_vk_error(vk_phys_dev.getSurfaceFormatsKHR(vk_surface, &format_count, nullptr));
    auto *formats = init_arena->push_array<vk::SurfaceFormatKHR>(format_count);
    abort_if_vk_error(vk_phys_dev.getSurfaceFormatsKHR(vk_surface, &format_count, formats));

    U32 mode_count = 0;
    abort_if_vk_error(vk_phys_dev.getSurfacePresentModesKHR(vk_surface, &mode_count, nullptr));
    auto *present_modes = init_arena->push_array<vk::PresentModeKHR>(mode_count);
    abort_if_vk_error(vk_phys_dev.getSurfacePresentModesKHR(vk_surface, &mode_count, present_modes));

    chosen_format      = vk::Format::eB8G8R8A8Srgb;
    chosen_color_space = vk::ColorSpaceKHR::eSrgbNonlinear;

    bool format_found = false;
    for (U32 i = 0; i < format_count; ++i) {
      if (formats[i].format == chosen_format && formats[i].colorSpace == chosen_color_space) {
        format_found = true;
        break;
      }
    }
    if (!format_found && format_count > 0) {
      chosen_format      = formats[0].format;
      chosen_color_space = formats[0].colorSpace;
    }

    chosen_present_mode = vk::PresentModeKHR::eImmediate;
    bool mode_found     = false;
    for (U32 i = 0; i < mode_count; ++i) {
      if (present_modes[i] == chosen_present_mode) {
        mode_found = true;
        break;
      }
    }
    if (!mode_found) {
      chosen_present_mode = vk::PresentModeKHR::eFifo;
    }

    swapchain_extent = capabilities.currentExtent;
    if (swapchain_extent.width == 0xFFFFFFFF) {
      swapchain_extent.width  = params.width;
      swapchain_extent.height = params.height;
    }
  }

  //~ Scope 6: Swapchain Creation
  vk::SwapchainKHR vk_swapchain{};
  {
    vk::SurfaceCapabilitiesKHR capabilities{};
    abort_if_vk_error(vk_phys_dev.getSurfaceCapabilitiesKHR(vk_surface, &capabilities));

    U32 desired_image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && desired_image_count > capabilities.maxImageCount) {
      desired_image_count = capabilities.maxImageCount;
    }

    Array           queue_indices_array      = {queue_indices.graphics_index, queue_indices.present_index};
    vk::SharingMode sharing_mode             = vk::SharingMode::eExclusive;
    U32             queue_family_index_count = 0;
    U32            *p_queue_family_indices   = nullptr;
    if (queue_indices.graphics_index != queue_indices.present_index) {
      sharing_mode             = vk::SharingMode::eConcurrent;
      queue_family_index_count = 2;
      p_queue_family_indices   = queue_indices_array;
    }

    vk::SwapchainCreateInfoKHR swapchain_info{
        .surface               = vk_surface,
        .minImageCount         = desired_image_count,
        .imageFormat           = chosen_format,
        .imageColorSpace       = chosen_color_space,
        .imageExtent           = swapchain_extent,
        .imageArrayLayers      = 1,
        .imageUsage            = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode      = sharing_mode,
        .queueFamilyIndexCount = queue_family_index_count,
        .pQueueFamilyIndices   = p_queue_family_indices,
        .preTransform          = capabilities.currentTransform,
        .compositeAlpha        = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode           = chosen_present_mode,
        .clipped               = true,
        .oldSwapchain          = nullptr,
    };
    vk_swapchain = abort_if_vk_error(vk_device.createSwapchainKHR(swapchain_info));
  }

  //~ Scope 7: Swapchain Image Views + Sync Primitives + Command Pool
  Array<SwapchainResources, SC_MAX_GENERATIONS>  swapchain_pool{};
  U32                                            pool_gen{};
  SwapchainResources                            *sc{};
  constexpr U32                                  MAX_FRAMES_IN_FLIGHT = 2;
  Array<vk::Fence, MAX_FRAMES_IN_FLIGHT>         in_flight_fences{};
  Array<vk::Fence, MAX_FRAMES_IN_FLIGHT>         acquire_fences{};
  Array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> command_buffers{};
  vk::CommandPool                                command_pool{};
  {
    sc = &swapchain_pool[0];

    abort_if_vk_error(vk_device.getSwapchainImagesKHR(vk_swapchain, &sc->image_count, nullptr));
    abort_if_vk_error(vk_device.getSwapchainImagesKHR(vk_swapchain, &sc->image_count, sc->images));

    for (U32 i = 0; i < sc->image_count; ++i) {
      vk::ImageViewCreateInfo view_info{
          .image            = sc->images[i],
          .viewType         = vk::ImageViewType::e2D,
          .format           = chosen_format,
          .components       = vk::ComponentMapping{.r = vk::ComponentSwizzle::eIdentity,
                                                   .g = vk::ComponentSwizzle::eIdentity,
                                                   .b = vk::ComponentSwizzle::eIdentity,
                                                   .a = vk::ComponentSwizzle::eIdentity},
          .subresourceRange = vk::ImageSubresourceRange{.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                                   .baseMipLevel   = 0,
                                                   .levelCount     = 1,
                                                   .baseArrayLayer = 0,
                                                   .layerCount     = 1}
      };
      sc->image_views[i] = abort_if_vk_error(vk_device.createImageView(view_info));
    }

    vk::FenceCreateInfo fence_info{.flags = vk::FenceCreateFlagBits::eSignaled};
    vk::FenceCreateInfo acquire_fence_info{};

    for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      in_flight_fences[i] = abort_if_vk_error(vk_device.createFence(fence_info));
      acquire_fences[i]   = abort_if_vk_error(vk_device.createFence(acquire_fence_info));
    }

    for (U32 i = 0; i < sc->image_count; ++i) {
      sc->render_finished_sems[i] = abort_if_vk_error(vk_device.createSemaphore({}));
    }

    vk::CommandPoolCreateInfo pool_info{
        .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queue_indices.graphics_index,
    };
    command_pool = abort_if_vk_error(vk_device.createCommandPool(pool_info));
    vk::CommandBufferAllocateInfo alloc_info{.commandPool        = command_pool,
                                             .level              = vk::CommandBufferLevel::ePrimary,
                                             .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
    abort_if_vk_error(vk_device.allocateCommandBuffers(&alloc_info, command_buffers));
    init_arena = arena_release(init_arena);
  }

  //~ Texture images
  vk::Image        tex_image        = nullptr;
  vk::DeviceMemory tex_image_memory = nullptr;
  {
    U32            tex_width, tex_height, tex_channels;
    stbi_uc       *pixels     = stbi_load("assets/textures/texture.jpg",
                                reinterpret_cast<int *>(&tex_width),
                                reinterpret_cast<int *>(&tex_height),
                                reinterpret_cast<int *>(&tex_channels),
                                STBI_rgb_alpha);
    vk::DeviceSize image_size = tex_width * tex_height * 4;
    ASSERT_ALWAYS(pixels);
    auto [staging_buffer, staging_buffer_mem] =
        create_buffer(vk_phys_dev,
                      vk_device,
                      image_size,
                      vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void *data = abort_if_vk_error(vk_device.mapMemory(staging_buffer_mem, 0, image_size));
    MemoryCopy(data, pixels, image_size);
    vk_device.unmapMemory(staging_buffer_mem);
    stbi_image_free(pixels);
    std::tie(tex_image, tex_image_memory) =
        create_image(vk_phys_dev,
                     vk_device,
                     tex_width,
                     tex_height,
                     chosen_format,
                     vk::ImageTiling::eOptimal,
                     vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                     vk::MemoryPropertyFlagBits::eDeviceLocal);
    abort_if_vk_error(vk_device.setDebugUtilsObjectNameEXT({
        .objectType   = vk::ObjectType::eImage,
        .objectHandle = (uint64_t)(VkImage)tex_image,
        .pObjectName  = "texture image",
    }));

    vk::CommandBuffer command_buffer = begin_single_time_command(vk_device, command_pool, "texture upload");
    transition_image_layout(command_buffer,
                            tex_image,
                            vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eTransferDstOptimal);
    copy_buffer_to_image(command_buffer, staging_buffer, tex_image, tex_width, tex_height);
    transition_image_layout(command_buffer,
                            tex_image,
                            vk::ImageLayout::eTransferDstOptimal,
                            vk::ImageLayout::eShaderReadOnlyOptimal);
    end_single_time_command(vk_device, graphics_queue, command_pool, command_buffer);
    vk_device.destroyBuffer(staging_buffer);
    vk_device.freeMemory(staging_buffer_mem);
  }

  //~ Scope 9: Shader + Pipeline + Descriptor Set Layout
  constexpr Array VERTICES{
      Vertex{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
      Vertex{ {0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
      Vertex{  {0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
      Vertex{ {-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
  };
  constexpr Array<U16, 6> INDICES = {0, 1, 2, 2, 3, 0};

  vk::ShaderModule   shader_module{};
  vk::PipelineLayout pipeline_layout{};
  vk::Pipeline       graphics_pipeline{};
  {
    String8                    shader_code = read_file(app_arena, str8_lit("assets/shaders/shader.spv"));
    vk::ShaderModuleCreateInfo module_info{.codeSize = shader_code.size * sizeof(U8),
                                           .pCode    = reinterpret_cast<U32 *>(shader_code.str)};
    shader_module = abort_if_vk_error(vk_device.createShaderModule(module_info));

    vk::PipelineShaderStageCreateInfo vert_shader_stage_info{.stage  = vk::ShaderStageFlagBits::eVertex,
                                                             .module = shader_module,
                                                             .pName  = "vert_main"};
    vk::PipelineShaderStageCreateInfo frag_shader_stage_info{.stage  = vk::ShaderStageFlagBits::eFragment,
                                                             .module = shader_module,
                                                             .pName  = "frag_main"};

    Array                              shader_stages  = {vert_shader_stage_info, frag_shader_stage_info};
    Array                              dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic_state{.dynamicStateCount = 2, .pDynamicStates = dynamic_states};

    vk::VertexInputBindingDescription             binding_descriptions   = Vertex::get_binding_description();
    Array<vk::VertexInputAttributeDescription, 2> attribute_descriptions = Vertex::get_attribute_descriptions();

    vk::PipelineVertexInputStateCreateInfo   vertex_input_info{.vertexBindingDescriptionCount   = 1,
                                                               .pVertexBindingDescriptions      = &binding_descriptions,
                                                               .vertexAttributeDescriptionCount = 2,
                                                               .pVertexAttributeDescriptions = attribute_descriptions};
    vk::PipelineInputAssemblyStateCreateInfo input_assembly{.topology = vk::PrimitiveTopology::eTriangleList};
    vk::PipelineViewportStateCreateInfo      viewport_state{.viewportCount = 1, .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{.depthClampEnable        = vk::False,
                                                        .rasterizerDiscardEnable = vk::False,
                                                        .polygonMode             = vk::PolygonMode::eFill,
                                                        .cullMode                = vk::CullModeFlagBits::eBack,
                                                        .frontFace               = vk::FrontFace::eCounterClockwise,
                                                        .depthBiasEnable         = vk::False,
                                                        .lineWidth               = 1.0f};
    vk::PipelineMultisampleStateCreateInfo   multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                                                           .sampleShadingEnable  = vk::False};

    vk::PipelineColorBlendAttachmentState color_blend_attachment{
        .blendEnable    = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    vk::PipelineColorBlendStateCreateInfo color_blending{.logicOpEnable   = vk::False,
                                                         .logicOp         = vk::LogicOp::eCopy,
                                                         .attachmentCount = 1,
                                                         .pAttachments    = &color_blend_attachment};

    vk::DescriptorSetLayoutBinding    ubo_layout_binding{.binding         = 0,
                                                         .descriptorType  = vk::DescriptorType::eUniformBuffer,
                                                         .descriptorCount = 1,
                                                         .stageFlags      = vk::ShaderStageFlagBits::eVertex};
    vk::DescriptorSetLayoutCreateInfo dslci{.bindingCount = 1, .pBindings = &ubo_layout_binding};
    vk::DescriptorSetLayout descriptor_set_layout = abort_if_vk_error(vk_device.createDescriptorSetLayout(dslci));

    PROFILE_START("pipeline_creation");
    vk::PipelineLayoutCreateInfo pipeline_layout_info{.setLayoutCount         = 1,
                                                      .pSetLayouts            = &descriptor_set_layout,
                                                      .pushConstantRangeCount = 0};
    pipeline_layout = abort_if_vk_error(vk_device.createPipelineLayout(pipeline_layout_info));

    vk::GraphicsPipelineCreateInfo gpci{
        .stageCount          = 2,
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisampling,
        .pColorBlendState    = &color_blending,
        .pDynamicState       = &dynamic_state,
        .layout              = pipeline_layout,
        .renderPass          = nullptr,
    };
    vk::PipelineRenderingCreateInfo prci{.colorAttachmentCount = 1, .pColorAttachmentFormats = &chosen_format};
    vk::StructureChain              pipeline_create_info_chain{gpci, prci};

    graphics_pipeline = abort_if_vk_error(
        vk_device.createGraphicsPipeline(nullptr, pipeline_create_info_chain.get<vk::GraphicsPipelineCreateInfo>()));
    U64 pipe_cycles = PROFILE_END();
    printf("Pipeline creation: %lu cycles\n", pipe_cycles);

    vk_device.destroyDescriptorSetLayout(descriptor_set_layout);
  }

  //~ Scope 10: Vertex + Index Buffers
  vk::Buffer       vertex_buffer{};
  vk::DeviceMemory vertex_buffer_memory{};
  vk::Buffer       index_buffer{};
  vk::DeviceMemory index_buffer_memory{};
  {
    {
      vk::DeviceSize buffer_size = VERTICES.array_size();

      auto [staging_buffer, staging_buffer_memory] =
          create_buffer(vk_phys_dev,
                        vk_device,
                        buffer_size,
                        vk::BufferUsageFlagBits::eTransferSrc,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

      void *staging_data = abort_if_vk_error(vk_device.mapMemory(staging_buffer_memory, 0, buffer_size));
      MemoryCopy(staging_data, VERTICES, buffer_size);
      vk_device.unmapMemory(staging_buffer_memory);

      std::tie(vertex_buffer, vertex_buffer_memory) =
          create_buffer(vk_phys_dev,
                        vk_device,
                        buffer_size,
                        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                        vk::MemoryPropertyFlagBits::eDeviceLocal);
      copy_buffer(vk_device, command_pool, graphics_queue, staging_buffer, vertex_buffer, buffer_size);
      vk_device.destroyBuffer(staging_buffer);
      vk_device.freeMemory(staging_buffer_memory);
    }
    {
      vk::DeviceSize buffer_size = INDICES.array_size();

      auto [staging_buffer, staging_buffer_memory] =
          create_buffer(vk_phys_dev,
                        vk_device,
                        buffer_size,
                        vk::BufferUsageFlagBits::eTransferSrc,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

      void *staging_data = abort_if_vk_error(vk_device.mapMemory(staging_buffer_memory, 0, buffer_size));
      MemoryCopy(staging_data, INDICES, buffer_size);
      vk_device.unmapMemory(staging_buffer_memory);

      std::tie(index_buffer, index_buffer_memory) =
          create_buffer(vk_phys_dev,
                        vk_device,
                        buffer_size,
                        vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                        vk::MemoryPropertyFlagBits::eDeviceLocal);
      copy_buffer(vk_device, command_pool, graphics_queue, staging_buffer, index_buffer, buffer_size);
      vk_device.destroyBuffer(staging_buffer);
      vk_device.freeMemory(staging_buffer_memory);
    }
  }

  //~ Scope 11: Uniform Buffers + Descriptors
  Array<vk::Buffer, MAX_FRAMES_IN_FLIGHT>        uniform_buffers{};
  Array<vk::DeviceMemory, MAX_FRAMES_IN_FLIGHT>  uniform_buffers_memory{};
  Array<void *, MAX_FRAMES_IN_FLIGHT>            uniform_buffers_mapped{};
  vk::DescriptorPool                             descriptor_pool{};
  Array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptor_sets{};
  {
    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vk::DeviceSize buffer_size = sizeof(UniformBufferObject);
      std::tie(uniform_buffers[i], uniform_buffers_memory[i]) =
          create_buffer(vk_phys_dev,
                        vk_device,
                        buffer_size,
                        vk::BufferUsageFlagBits::eUniformBuffer,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
      uniform_buffers_mapped[i] = abort_if_vk_error(vk_device.mapMemory(uniform_buffers_memory[i], 0, buffer_size));
    }

    vk::DescriptorPoolSize       pool_size{.type            = vk::DescriptorType::eUniformBuffer,
                                           .descriptorCount = MAX_FRAMES_IN_FLIGHT};
    vk::DescriptorPoolCreateInfo dpci{.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets       = MAX_FRAMES_IN_FLIGHT,
                                      .poolSizeCount = 1,
                                      .pPoolSizes    = &pool_size};
    descriptor_pool = abort_if_vk_error(vk_device.createDescriptorPool(dpci));

    vk::DescriptorSetLayoutBinding    ubo_layout_binding{.binding         = 0,
                                                         .descriptorType  = vk::DescriptorType::eUniformBuffer,
                                                         .descriptorCount = 1,
                                                         .stageFlags      = vk::ShaderStageFlagBits::eVertex};
    vk::DescriptorSetLayoutCreateInfo dslci{.bindingCount = 1, .pBindings = &ubo_layout_binding};
    vk::DescriptorSetLayout           temp_layout = abort_if_vk_error(vk_device.createDescriptorSetLayout(dslci));

    Array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{};
    fill_array(layouts, temp_layout);

    vk::DescriptorSetAllocateInfo alloc_info{.descriptorPool     = descriptor_pool,
                                             .descriptorSetCount = layouts.size(),
                                             .pSetLayouts        = layouts};
    abort_if_vk_error(vk_device.allocateDescriptorSets(&alloc_info, descriptor_sets));

    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vk::DescriptorBufferInfo buffer_info{.buffer = uniform_buffers[i],
                                           .offset = 0,
                                           .range  = sizeof(UniformBufferObject)};
      vk::WriteDescriptorSet   descriptor_write{.dstSet          = descriptor_sets[i],
                                                .dstBinding      = 0,
                                                .dstArrayElement = 0,
                                                .descriptorCount = 1,
                                                .descriptorType  = vk::DescriptorType::eUniformBuffer,
                                                .pBufferInfo     = &buffer_info};
      vk_device.updateDescriptorSets(descriptor_write, {});
    }

    vk_device.destroyDescriptorSetLayout(temp_layout);
  }

  //~ Scope 12: Main Loop
  {
    U64 absolute_frame_index = 0;
    (void)absolute_frame_index;
    U32                 current_frame = 0;
    vk::ClearColorValue clear_color{};
    FrameClock          clock{.target_ms = target_frame_ms};
    while (!glfwWindowShouldClose(window.glfw_window)) {
      clock.start();
      frame_arena->clear();
      glfwPollEvents();
      WindowEvent event{};
      while (window.events.pop(event)) {
        switch (event.type) {
          using enum WindowEventType;
          case NONE: {
            break;
          }
          case RESIZE: {
            window.framebuffer_resized = true;
            break;
          }
          case KEY: {
            WKeyEvent &key = event.key;
            printf("%d, %d, %d, %d\n", key.key, key.action, key.mods, key.scancode);
            if (key.action == GLFW_PRESS) {
              if (key.key == GLFW_KEY_R)
                clear_color = {{{0.15f, 0.05f, 0.05f, 1.0f}}};
              if (key.key == GLFW_KEY_G)
                clear_color = {{{0.05f, 0.15f, 0.05f, 1.0f}}};
              if (key.key == GLFW_KEY_B)
                clear_color = {{{0.05f, 0.05f, 0.15f, 1.0f}}};
            }
            break;
          }
          case SCROLL: {
            break;
          }
          case CURSOR: {
            break;
          }
          case MBUTTON: {
            WMButtonEvent &mbutton = event.mbutton;
            char const    *action  = mbutton.action == GLFW_PRESS ? "Pressed" : "Released";
            switch (mbutton.button) {
              default: {
                printf("other mouse button: %d\n", mbutton.button);
                break;
              }
              case 0: {
                printf("left click %s\n", action);
                break;
              }
              case 1: {
                printf("right click %s\n", action);
                break;
              }
              case 2: {
                printf("middle click %s\n", action);
                break;
              }
            }
            break;
          }
          case CLOSE: {
            break;
          }
          case REFRESH: {
            break;
          }
          case TEXT: {
            break;
          }
        }
      }

      abort_if_vk_error(vk_device.waitForFences(1, &in_flight_fences[current_frame], VK_TRUE, UINT64_MAX));

      bool swapchain_needs_rebuild = false;
      U32  image_index             = 0;

      VkResult acquire_res = vkAcquireNextImageKHR(vk_device,
                                                   vk_swapchain,
                                                   UINT64_MAX,
                                                   VK_NULL_HANDLE,
                                                   acquire_fences[current_frame],
                                                   &image_index);

      if (acquire_res == VK_ERROR_OUT_OF_DATE_KHR) {
        pool_gen = (pool_gen + 1) % SC_MAX_GENERATIONS;
        sc       = &swapchain_pool[pool_gen];
        recreate_swapchain(window.glfw_window,
                           vk_device,
                           sc,
                           vk_swapchain,
                           vk_phys_dev,
                           vk_surface,
                           swapchain_extent,
                           chosen_format,
                           chosen_color_space,
                           chosen_present_mode);
        continue;
      }
      if (acquire_res == VK_SUBOPTIMAL_KHR) {
        swapchain_needs_rebuild = true;
      } else if (acquire_res != VK_SUCCESS) {
        std::fprintf(stderr, "vkAcquireNextImageKHR failed: %d\n", acquire_res);
        TRAP();
      }
      {
        abort_if_vk_error(vk_device.waitForFences(1, &acquire_fences[current_frame], VK_TRUE, UINT64_MAX));
        if (sc->images_in_flight[image_index]) {
          abort_if_vk_error(vk_device.waitForFences(1, &sc->images_in_flight[image_index], VK_TRUE, UINT64_MAX));
        }
        abort_if_vk_error(vk_device.resetFences(1, &acquire_fences[current_frame]));
      }
      sc->images_in_flight[image_index] = in_flight_fences[current_frame];
      abort_if_vk_error(vk_device.resetFences(1, &in_flight_fences[current_frame]));

      vk::CommandBuffer cmd = command_buffers[current_frame];
      abort_if_vk_error(cmd.reset());
      abort_if_vk_error(cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));

      vk::ImageMemoryBarrier2 to_render_barrier =
          make_image_barrier(image_index,
                             sc,
                             vk::ImageLayout::eUndefined,
                             vk::ImageLayout::eColorAttachmentOptimal,
                             {},
                             vk::AccessFlagBits2::eColorAttachmentWrite,
                             vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                             vk::PipelineStageFlagBits2::eColorAttachmentOutput);
      cmd.pipelineBarrier2(dep_info(&to_render_barrier));

      vk::RenderingAttachmentInfo color_attachment{.imageView   = sc->image_views[image_index],
                                                   .imageLayout = vk::ImageLayout::eAttachmentOptimal,
                                                   .loadOp      = vk::AttachmentLoadOp::eClear,
                                                   .storeOp     = vk::AttachmentStoreOp::eStore,
                                                   .clearValue  = {clear_color}};
      vk::RenderingInfo           rendering_info{
                    .renderArea           = vk::Rect2D{{0, 0}, swapchain_extent},
                    .layerCount           = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments    = &color_attachment
      };
      cmd.beginRendering(rendering_info);

      cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline);
      cmd.setViewport(0,
                      vk::Viewport(0.0f,
                                   0.0f,
                                   static_cast<float>(swapchain_extent.width),
                                   static_cast<float>(swapchain_extent.height),
                                   0.0f,
                                   1.0f));
      cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapchain_extent));
      cmd.bindVertexBuffers(0, vertex_buffer, {0});
      cmd.bindIndexBuffer(index_buffer, 0, vk::IndexType::eUint16);
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             pipeline_layout,
                             0,
                             descriptor_sets[current_frame],
                             nullptr);
      cmd.drawIndexed(INDICES.size(), 1, 0, 0, 0);
      cmd.endRendering();

      vk::ImageMemoryBarrier2 to_present_barrier =
          make_image_barrier(image_index,
                             sc,
                             vk::ImageLayout::eColorAttachmentOptimal,
                             vk::ImageLayout::ePresentSrcKHR,
                             vk::AccessFlagBits2::eColorAttachmentWrite,
                             {},
                             vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                             vk::PipelineStageFlagBits2::eBottomOfPipe);
      cmd.pipelineBarrier2(dep_info(&to_present_barrier));
      sc->image_initialized[image_index] = true;
      abort_if_vk_error(cmd.end());

      static auto         start_time   = std::chrono::high_resolution_clock::now();
      auto                current_time = std::chrono::high_resolution_clock::now();
      float               time         = std::chrono::duration<float>(current_time - start_time).count();
      UniformBufferObject ubo{
          .model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
          .view  = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
          .proj =
              glm::perspective(glm::radians(45.0f),
                               static_cast<float>(swapchain_extent.width) / static_cast<float>(swapchain_extent.height),
                               0.1f,
                               10.0f),
      };
      ubo.proj[1][1] *= -1;
      MemoryCopy(uniform_buffers_mapped[current_frame], &ubo, sizeof(ubo));

      vk::SubmitInfo submit_info{.commandBufferCount   = 1,
                                 .pCommandBuffers      = &cmd,
                                 .signalSemaphoreCount = 1,
                                 .pSignalSemaphores    = &sc->render_finished_sems[image_index]};
      abort_if_vk_error(graphics_queue.submit(1, &submit_info, in_flight_fences[current_frame]));

      VkPresentInfoKHR present_info{
          .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
          .pNext              = nullptr,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores    = reinterpret_cast<VkSemaphore const *>(&sc->render_finished_sems[image_index]),
          .swapchainCount     = 1,
          .pSwapchains        = reinterpret_cast<VkSwapchainKHR const *>(&vk_swapchain),
          .pImageIndices      = &image_index,
          .pResults           = nullptr};
      VkResult present_res = vkQueuePresentKHR(present_queue, &present_info);

      if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR || window.framebuffer_resized ||
          swapchain_needs_rebuild) {
        pool_gen = (pool_gen + 1) % SC_MAX_GENERATIONS;
        sc       = &swapchain_pool[pool_gen];
        recreate_swapchain(window.glfw_window,
                           vk_device,
                           sc,
                           vk_swapchain,
                           vk_phys_dev,
                           vk_surface,
                           swapchain_extent,
                           chosen_format,
                           chosen_color_space,
                           chosen_present_mode);
        window.framebuffer_resized = false;
      } else if (present_res != VK_SUCCESS) {
        std::fprintf(stderr, "vkQueuePresentKHR failed: %d\n", present_res);
        TRAP();
      }
      clock.mark_work_done();
      clock.wait_for_target();

      absolute_frame_index++;
      current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

      F64 total_ms = clock.total_ms();
      F64 work_ms  = clock.work_ms();

      local_persist F64 s_debug_accum_ms = 0;
      local_persist U32 s_debug_frames   = 0;
      local_persist F64 s_debug_work_sum = 0;

      s_debug_accum_ms += total_ms;
      s_debug_frames++;
      s_debug_work_sum += work_ms;

      if (s_debug_accum_ms >= 1000.0) {
        F64 fps = s_debug_frames / (s_debug_accum_ms / 1000.0);
        printf("Frames: %-4u | work: %6.3f ms | total: %6.3f ms | wait: %6.3f ms | FPS: %6.1f\n",
               s_debug_frames,
               s_debug_work_sum / s_debug_frames,
               s_debug_accum_ms / s_debug_frames,
               s_debug_accum_ms / s_debug_frames - s_debug_work_sum / s_debug_frames,
               fps);
        s_debug_accum_ms = 0;
        s_debug_frames   = 0;
        s_debug_work_sum = 0;
      }
    }
  }

  //~ Cleanup
  abort_if_vk_error(vk_device.waitIdle());

  for (U32 i = 0; i < SC_MAX_GENERATIONS; ++i) {
    SwapchainResources *gen = &swapchain_pool[i];
    for (U32 j = 0; j < gen->image_count; ++j) {
      if (gen->image_views[j])
        vk_device.destroyImageView(gen->image_views[j]);
      if (gen->render_finished_sems[j])
        vk_device.destroySemaphore(gen->render_finished_sems[j]);
    }
  }
  for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    vk_device.destroyFence(in_flight_fences[i]);
    vk_device.destroyFence(acquire_fences[i]);
  }
  vk_device.destroyPipelineLayout(pipeline_layout);
  vk_device.destroyPipeline(graphics_pipeline);
  abort_if_vk_error(vk_device.freeDescriptorSets(descriptor_pool, descriptor_sets.size(), descriptor_sets));
  vk_device.destroyDescriptorPool(descriptor_pool);
  vk_device.destroyShaderModule(shader_module);
  vk_device.destroyImage(tex_image);
  vk_device.freeMemory(tex_image_memory);
  vk_device.destroyCommandPool(command_pool);
  vk_device.destroyBuffer(vertex_buffer);
  vk_device.freeMemory(vertex_buffer_memory);
  vk_device.destroyBuffer(index_buffer);
  vk_device.freeMemory(index_buffer_memory);
  for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    vk_device.destroyBuffer(uniform_buffers[i]);
    vk_device.freeMemory(uniform_buffers_memory[i]);
  }
  vk_device.destroySwapchainKHR(vk_swapchain);
  vk_device.destroy();

#ifdef SD2_DEBUG
  if (debug_messenger) {
    PFN_vkDestroyDebugUtilsMessengerEXT func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vk_get_instance_proc_addr(static_cast<VkInstance>(vk_instance), "vkDestroyDebugUtilsMessengerEXT"));
    if (func)
      func(static_cast<VkInstance>(vk_instance), debug_messenger, nullptr);
  }
#endif
  vk_instance.destroySurfaceKHR(vk_surface);
  vk_instance.destroy();
  glfwDestroyWindow(window.glfw_window);
  glfwTerminate();
  arena_release(frame_arena);
  arena_release(app_arena);
  thread_ctx_release();
  return 0;
}

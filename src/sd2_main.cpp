#include <cstdio>
#include <ctime>
#include <fstream>
#include <thread>

#include <sys/ioctl.h>
#define SD2_DEBUG 1
#define TINYOBJLOADER_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include <filesystem>

#include "sd2_inc.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static vk::detail::DynamicLoader g_vulkan_dynamic_loader{};

struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 tex_coord;

  static constexpr vk::VertexInputBindingDescription get_binding_description() {
    return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
  }

  static constexpr Array<vk::VertexInputAttributeDescription, 3> get_attribute_descriptions() {
    return {
        {
            vk::VertexInputAttributeDescription{.location = 0,
                                                .binding = 0,
                                                .format = vk::Format::eR32G32B32Sfloat,
                                                .offset = offsetof(Vertex, pos)},
            vk::VertexInputAttributeDescription{.location = 1,
                                                .binding = 0,
                                                .format = vk::Format::eR32G32B32Sfloat,
                                                .offset = offsetof(Vertex, color)},
            vk::VertexInputAttributeDescription{.location = 2,
                                                .binding = 0,
                                                .format = vk::Format::eR32G32Sfloat,
                                                .offset = offsetof(Vertex, tex_coord)},
        }
    };
  }

  bool operator==(Vertex const &other) const {
    return pos == other.pos && color == other.color && tex_coord == other.tex_coord;
  }
};

template<>
struct std::hash<Vertex> {
  size_t operator()(Vertex const &vertex) const noexcept {
    return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
           (hash<glm::vec2>()(vertex.tex_coord) << 1);
  }
};


struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr char const *MODEL_PATH = "assets/models/viking_room.obj";
constexpr char const *TEXTURE_PATH = "assets/textures/viking_room.png";

internal std::tuple<DynArray<Vertex>, DynArray<U32>> load_obj(Arena *arena, const char *obj_path);

//~ cpp
#include "internal/arena.cpp"
#include "internal/vk_helpers.cpp"
#include "internal/helpers.cpp"
#include "internal/imgui_helpers.cpp"
#include "internal/debug_ui.cpp"


int main() {
  thread_ctx_init();
  Arena *app_arena = arena_alloc({.name = "App arena"});
  Arena *frame_arena = arena_alloc({.flags = ArenaFlags::NO_CHAIN, .name = "Frame arena"});
  Arena *init_arena = arena_alloc({.name = "init"});
  AppParams app_params{
      .name = str8_lit("sd2"),
      .width = WIDTH,
      .height = HEIGHT,
  };

  F64 target_frame_ms = 1000.0 / 60;
  bool monitor_detected = false;
  bool paused = false;
  PaletteState palette_state{};

  //~ Window + GLFW
  AppWindow window = glfw_init_window(&app_params);
  //~ Vulkan Instance + Debug Messenger + Surface
  VKInstanceResult vk_instance_result = vk_create_vulkan_instance(window.glfw_window, init_arena);
  vk::Instance vk_instance = vk_instance_result.instance;
  vk::SurfaceKHR vk_surface = vk_instance_result.surface;
  PFN_vkGetInstanceProcAddr vk_get_instance_proc_addr = vk_instance_result.get_instance_proc_addr;
#ifdef SD2_DEBUG
  VkDebugUtilsMessengerEXT vk_debug_messenger = vk_instance_result.debug_messenger;
#endif

  //~ Physical Device + Queue Families
  vk::PhysicalDevice vk_phys_dev{};
  VKQueueFamilyIndices vk_queue_indices{};
  {
    VKRatedDevice rated_device = vk_pick_best_physical_device(vk_instance, vk_surface);
    if (rated_device.score == 0) {
      TRAP();
    }
    vk_phys_dev = rated_device.device;
    vk_queue_indices = vk_find_queue_families(vk_phys_dev, vk_surface);
  }

  //~ Logical Device + Queues
  auto [vk_device, vk_graphics_queue, vk_present_queue] = vk_create_logical_device(vk_phys_dev, vk_queue_indices);

  //~ GPU memory arenas
  U32 vk_host_mem_type =
      vk_find_memory_type(vk_phys_dev,
                          UINT32_MAX,
                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  U32 vk_device_mem_type = vk_find_memory_type(vk_phys_dev, UINT32_MAX, vk::MemoryPropertyFlagBits::eDeviceLocal);

  VKGpuArena vk_host_arena = vk_create_gpu_arena(vk_phys_dev, vk_device, vk_host_mem_type, mb(8));
  VKGpuArena vk_device_arena = vk_create_gpu_arena(vk_phys_dev, vk_device, vk_device_mem_type, gb(2));

  vk::SampleCountFlagBits vk_msaa_samples = vk_get_max_usable_sample_count(vk_phys_dev);

  //~ Swapchain Format + Present Mode Selection
  auto [vk_chosen_format, vk_chosen_color_space, vk_chosen_present_mode, vk_swapchain_extent] =
      vk_create_swapchain_config(vk_phys_dev, vk_surface, app_params.width, app_params.height, init_arena);

  //~ Swapchain Creation
  vk::SwapchainKHR vk_swapchain = vk_create_swapchain(vk_phys_dev,
                                                      vk_device,
                                                      vk_surface,
                                                      vk_chosen_format,
                                                      vk_chosen_color_space,
                                                      vk_swapchain_extent,
                                                      vk_chosen_present_mode,
                                                      vk_queue_indices);

  //~ Swapchain Image Views + Sync Primitives + Command Pool
  Array<SwapchainResources, SC_MAX_GENERATIONS> vk_swapchain_pool{};
  Array<vk::Fence, MAX_FRAMES_IN_FLIGHT> vk_in_flight_fences{};
  Array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> vk_acquire_sems{};
  Array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> vk_command_buffers{};

  auto [vk_sc, vk_command_pool] = vk_create_swapchain_resources(vk_device,
                                                                vk_phys_dev,
                                                                vk_swapchain,
                                                                vk_swapchain_extent,
                                                                vk_chosen_format,
                                                                vk_msaa_samples,
                                                                vk_queue_indices,
                                                                &vk_device_arena,
                                                                &vk_swapchain_pool,
                                                                &vk_in_flight_fences,
                                                                &vk_acquire_sems,
                                                                &vk_command_buffers);


#ifdef TRACY_ENABLE
  TracyVkCtx g_tracy_vk_ctx = nullptr;
  {
    auto vk_tracy_cmds = vk_abort_if_error(vk_device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{.commandPool = vk_command_pool, .level = vk::CommandBufferLevel::ePrimary,
                                      .commandBufferCount = 1}));
    vk::CommandBuffer vk_tracy_cmd = vk_tracy_cmds[0];
    g_tracy_vk_ctx = TracyVkContext(
        static_cast<VkPhysicalDevice>(vk_phys_dev),
        static_cast<VkDevice>(vk_device),
        static_cast<VkQueue>(vk_graphics_queue),
        static_cast<VkCommandBuffer>(vk_tracy_cmd));
    vk_device.freeCommandBuffers(vk_command_pool, 1, &vk_tracy_cmd);
  }
#else
  void *g_tracy_vk_ctx = nullptr;
  (void)g_tracy_vk_ctx;
#endif

  //~ Sampler
  vk::Sampler vk_sampler{};
  {
    vk::PhysicalDeviceProperties properties = vk_phys_dev.getProperties();
    vk::SamplerCreateInfo sampler_info{.magFilter = vk::Filter::eLinear,
                                       .minFilter = vk::Filter::eLinear,
                                       .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                       .addressModeU = vk::SamplerAddressMode::eRepeat,
                                       .addressModeV = vk::SamplerAddressMode::eRepeat,
                                       .addressModeW = vk::SamplerAddressMode::eRepeat,
                                       .mipLodBias = 0.0f,
                                       .anisotropyEnable = vk::True,
                                       .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
                                       .compareEnable = vk::False,
                                       .compareOp = vk::CompareOp::eAlways,
                                       .minLod = 0.0f,
                                       .maxLod = vk::LodClampNone,
                                       .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                       .unnormalizedCoordinates = vk::False};
    vk_sampler = vk_abort_if_error(vk_device.createSampler(sampler_info));
  }

  imgui_init({
      .instance = vk_instance,
      .phys_dev = vk_phys_dev,
      .device = vk_device,
      .color_format = static_cast<VkFormat>(vk_chosen_format),
      .graphics_queue = vk_graphics_queue,
      .glfw_window = window.glfw_window,
      .graphics_queue_family = vk_queue_indices.graphics_index,
      .image_count = vk_sc->image_count,
      .min_image_count = MAX_FRAMES_IN_FLIGHT,
  });

  imgui_setup_theme();

  struct PalCtx {
    bool *paused;
    GLFWwindow *glfw_window;
  } pal_ctx{&paused, window.glfw_window};

  DynArray<PaletteAction> pa{};
  pa.size = pa.capacity = 11;
  pa.data = app_arena->push_array<PaletteAction>(11);
  pa.data[0] = {"Open File", [](void *) { puts("[palette] Open File"); }, nullptr};
  pa.data[1] = {"Save", [](void *) { puts("[palette] Save"); }, nullptr};
  pa.data[2] = {"Save All", [](void *) { puts("[palette] Save All"); }, nullptr};
  pa.data[3] = {"Run", [](void *) { puts("[palette] Run"); }, nullptr};
  pa.data[4] = {"Debug", [](void *) { puts("[palette] Debug"); }, nullptr};
  pa.data[5] = {"Build", [](void *) { puts("[palette] Build"); }, nullptr};
  pa.data[6] = {"Toggle Fullscreen", [](void *c) {
                  auto *p = (PalCtx *)c;
                  auto *w = (AppWindow *)glfwGetWindowUserPointer(p->glfw_window);
                  if (!w->fullscreen) {
                    glfwGetWindowPos(p->glfw_window, &w->windowed_x, &w->windowed_y);
                    glfwGetWindowSize(p->glfw_window, &w->windowed_w, &w->windowed_h);
                    int cx = w->windowed_x + w->windowed_w / 2;
                    int cy = w->windowed_y + w->windowed_h / 2;
                    int count;
                    GLFWmonitor **monitors = glfwGetMonitors(&count);
                    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
                    for (int i = 0; i < count; i++) {
                      int mx, my;
                      glfwGetMonitorPos(monitors[i], &mx, &my);
                      GLFWvidmode const *mode = glfwGetVideoMode(monitors[i]);
                      if (cx >= mx && cx < mx + (int)mode->width &&
                          cy >= my && cy < my + (int)mode->height) {
                        monitor = monitors[i];
                        break;
                      }
                    }
                    int mx, my;
                    glfwGetMonitorPos(monitor, &mx, &my);
                    GLFWvidmode const *mode = glfwGetVideoMode(monitor);
                    glfwSetWindowAttrib(p->glfw_window, GLFW_DECORATED, GLFW_FALSE);
                    glfwSetWindowPos(p->glfw_window, mx, my);
                    glfwSetWindowSize(p->glfw_window, mode->width, mode->height);
                  } else {
                    glfwSetWindowAttrib(p->glfw_window, GLFW_DECORATED, GLFW_TRUE);
                    glfwSetWindowPos(p->glfw_window, w->windowed_x, w->windowed_y);
                    glfwSetWindowSize(p->glfw_window, w->windowed_w, w->windowed_h);
                  }
                  w->fullscreen = !w->fullscreen;
                },
                &pal_ctx};
  pa.data[7] = {"Toggle Pause", [](void *c) {
                  auto *p = (PalCtx *)c;
                  *p->paused = !*p->paused;
                },
                &pal_ctx};
  pa.data[8] = {"Show FPS", [](void *) { puts("[palette] Show FPS"); }, nullptr};
  pa.data[9] = {"Quit", [](void *c) {
                  auto *p = (PalCtx *)c;
                  glfwSetWindowShouldClose(p->glfw_window, true);
                },
                &pal_ctx};
  pa.data[10] = {"Exit", [](void *c) {
                   auto *p = (PalCtx *)c;
                   glfwSetWindowShouldClose(p->glfw_window, true);
                 },
                 &pal_ctx};
  debug_ui_palette_init(&palette_state, app_arena, pa);

  // TODO: extract to create_pipeline(device, format, render_format, extent, arena) -> {pipeline, layout, module,
  // ds_layout}
  //~ Shader + Pipeline + Descriptor Set Layout

  vk::ShaderModule vk_shader_module{};
  vk::PipelineLayout vk_pipeline_layout{};
  vk::Pipeline vk_graphics_pipeline{};
  vk::DescriptorSetLayout vk_descriptor_set_layout{};
  vk::Image vk_depth_image{};
  vk::ImageView vk_depth_image_view{};
  {
    // TODO: extract to create_shader_module(device, arena, path) -> ShaderModule
    //~ Shader Module
    String8 shader_code = read_file(app_arena, str8_lit("assets/shaders/shader.spv"));
    vk::ShaderModuleCreateInfo module_info{.codeSize = shader_code.size * sizeof(U8),
                                           .pCode = reinterpret_cast<U32 *>(shader_code.str)};
    vk_shader_module = vk_abort_if_error(vk_device.createShaderModule(module_info));

    //~ Pipeline state infos
    vk::PipelineShaderStageCreateInfo vert_shader_stage_info{.stage = vk::ShaderStageFlagBits::eVertex,
                                                             .module = vk_shader_module,
                                                             .pName = "vert_main"};
    vk::PipelineShaderStageCreateInfo frag_shader_stage_info{.stage = vk::ShaderStageFlagBits::eFragment,
                                                             .module = vk_shader_module,
                                                             .pName = "frag_main"};

    Array shader_stages = {vert_shader_stage_info, frag_shader_stage_info};
    Array dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic_state{.dynamicStateCount = 2, .pDynamicStates = dynamic_states};

    auto binding_descriptions = Vertex::get_binding_description();
    auto attribute_descriptions = Vertex::get_attribute_descriptions();

    vk::PipelineVertexInputStateCreateInfo vertex_input_info{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_descriptions,
        .vertexAttributeDescriptionCount = attribute_descriptions.size(),
        .pVertexAttributeDescriptions = attribute_descriptions};
    vk::PipelineInputAssemblyStateCreateInfo input_assembly{.topology = vk::PrimitiveTopology::eTriangleList};
    vk::PipelineViewportStateCreateInfo viewport_state{.viewportCount = 1, .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{.depthClampEnable = vk::False,
                                                        .rasterizerDiscardEnable = vk::False,
                                                        .polygonMode = vk::PolygonMode::eFill,
                                                        .cullMode = vk::CullModeFlagBits::eBack,
                                                        .frontFace = vk::FrontFace::eCounterClockwise,
                                                        .depthBiasEnable = vk::False,
                                                        .lineWidth = 1.0f};
    vk::PipelineColorBlendAttachmentState color_blend_attachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    vk::PipelineColorBlendStateCreateInfo color_blending{.logicOpEnable = vk::False,
                                                         .logicOp = vk::LogicOp::eCopy,
                                                         .attachmentCount = 1,
                                                         .pAttachments = &color_blend_attachment};

    vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk_msaa_samples,
                                                         .sampleShadingEnable = vk::True,
                                                         .minSampleShading = 0.2f};
    //~ Depth Resources
    VKDepthResources depth = vk_create_depth_resources(vk_phys_dev,
                                                       vk_device,
                                                       &vk_device_arena,
                                                       vk_swapchain_extent,
                                                       vk_msaa_samples);
    vk_depth_image = depth.tex.image;
    vk_depth_image_view = depth.tex.image_view;
    vk::Format depth_format = depth.format;
    vk::PipelineDepthStencilStateCreateInfo depth_stencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };
    // TODO: extract to create_descriptor_set_layout(device, bindings) -> DescriptorSetLayout
    //~ Descriptor set bindings
    Array bindings{
        vk::DescriptorSetLayoutBinding{.binding = 0,
                                       .descriptorType = vk::DescriptorType::eUniformBuffer,
                                       .descriptorCount = 1,
                                       .stageFlags = vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{.binding = 1,
                                       .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                       .descriptorCount = 1,
                                       .stageFlags = vk::ShaderStageFlagBits::eFragment}
    };

    vk::DescriptorSetLayoutCreateInfo dslci{.bindingCount = bindings.size(), .pBindings = bindings};
    vk_descriptor_set_layout = vk_abort_if_error(vk_device.createDescriptorSetLayout(dslci));

    // TODO: extract to create_graphics_pipeline(...) -> {Pipeline, PipelineLayout}
    //~ Pipeline Creation
    PROFILE_START("pipeline_creation");
    vk::PipelineLayoutCreateInfo pipeline_layout_info{.setLayoutCount = 1,
                                                      .pSetLayouts = &vk_descriptor_set_layout,
                                                      .pushConstantRangeCount = 0};
    vk_pipeline_layout = vk_abort_if_error(vk_device.createPipelineLayout(pipeline_layout_info));

    vk::GraphicsPipelineCreateInfo gpci{
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = vk_pipeline_layout,
        .renderPass = nullptr,
    };
    vk::PipelineRenderingCreateInfo prci{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &vk_chosen_format,
        .depthAttachmentFormat = depth_format,
    };
    vk::StructureChain pipeline_create_info_chain{gpci, prci};

    vk_graphics_pipeline = vk_abort_if_error(
        vk_device.createGraphicsPipeline(nullptr, pipeline_create_info_chain.get<vk::GraphicsPipelineCreateInfo>()));
    U64 pipe_cycles = PROFILE_END();
    printf("Pipeline creation: %lu cycles\n", pipe_cycles);
  }

  //~ Load model
  auto [vertices, indices] = load_obj(app_arena, MODEL_PATH);
  //~ Load texture
  VKTextureImage vk_tex = vk_load_texture(vk_phys_dev,
                                          vk_device,
                                          &vk_host_arena,
                                          &vk_device_arena,
                                          vk_command_pool,
                                          vk_graphics_queue,
                                          TEXTURE_PATH);

  auto vk_create_geometry_buffers = [](vk::PhysicalDevice vk_phys_dev,
                                       vk::Device vk_device,
                                       VKGpuArena *host_arena,
                                       VKGpuArena *device_arena,
                                       vk::CommandPool command_pool,
                                       vk::Queue graphics_queue,
                                       DynArray<Vertex> vertices,
                                       DynArray<U32> indices) -> std::tuple<vk::Buffer, vk::Buffer> {
    vk::Buffer vertex_buffer{}, index_buffer{};
    {
      vk::DeviceSize buffer_size = vertices.array_size();

      auto [staging_buffer, staging_alloc] =
          vk_create_buffer(vk_phys_dev, vk_device, buffer_size, vk::BufferUsageFlagBits::eTransferSrc, host_arena);

      MemoryCopy(staging_alloc.mapped, vertices.data, buffer_size);

      auto [vb, vb_alloc] =
          vk_create_buffer(vk_phys_dev,
                           vk_device,
                           buffer_size,
                           vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                           device_arena);
      vertex_buffer = vb;
      (void)vb_alloc;
      vk_copy_buffer(vk_device, command_pool, graphics_queue, staging_buffer, vertex_buffer, buffer_size);
      vk_device.destroyBuffer(staging_buffer);
    }
    {
      vk::DeviceSize buffer_size = indices.array_size();

      auto [staging_buffer, staging_alloc] =
          vk_create_buffer(vk_phys_dev, vk_device, buffer_size, vk::BufferUsageFlagBits::eTransferSrc, host_arena);

      MemoryCopy(staging_alloc.mapped, indices.data, buffer_size);

      auto [ib, ib_alloc] = vk_create_buffer(vk_phys_dev,
                                             vk_device,
                                             buffer_size,
                                             vk::BufferUsageFlagBits::eIndexBuffer |
                                             vk::BufferUsageFlagBits::eTransferDst,
                                             device_arena);
      index_buffer = ib;
      (void)ib_alloc;
      vk_copy_buffer(vk_device, command_pool, graphics_queue, staging_buffer, index_buffer, buffer_size);
      vk_device.destroyBuffer(staging_buffer);
    }
    return {vertex_buffer, index_buffer};
  };
  //~ Vertex + Index Buffers
  auto [vk_vertex_buffer, vk_index_buffer] = vk_create_geometry_buffers(vk_phys_dev,
                                                                        vk_device,
                                                                        &vk_host_arena,
                                                                        &vk_device_arena,
                                                                        vk_command_pool,
                                                                        vk_graphics_queue,
                                                                        vertices,
                                                                        indices);
  vk::DeviceSize vk_swapchain_arena_checkpoint = vk_device_arena.offset;
  // TODO: extract to create_uniform_descriptors(device, phys_dev, host_arena, tex_sampler, tex_image, ...) ->
  // {uniform_buffers, descriptor_pool, descriptor_sets, uniform_buffers_mapped}
  //~ Uniform Buffers + Descriptors
  Array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> vk_uniform_buffers{};
  Array<void *, MAX_FRAMES_IN_FLIGHT> vk_uniform_buffers_mapped{};
  vk::DescriptorPool vk_descriptor_pool{};
  Array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> vk_descriptor_sets{};
  {
    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vk::DeviceSize buffer_size = sizeof(UniformBufferObject);
      auto [ub, ub_alloc] =
          vk_create_buffer(vk_phys_dev,
                           vk_device,
                           buffer_size,
                           vk::BufferUsageFlagBits::eUniformBuffer,
                           &vk_host_arena);
      vk_uniform_buffers[i] = ub;
      vk_uniform_buffers_mapped[i] = ub_alloc.mapped;
    }

    Array pool_size{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler,
                               .descriptorCount = MAX_FRAMES_IN_FLIGHT}
    };
    vk::DescriptorPoolCreateInfo dpci{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets = MAX_FRAMES_IN_FLIGHT,
                                      .poolSizeCount = pool_size.size(),
                                      .pPoolSizes = pool_size};
    vk_descriptor_pool = vk_abort_if_error(vk_device.createDescriptorPool(dpci));

    Array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{};
    fill_array(layouts, vk_descriptor_set_layout);

    vk::DescriptorSetAllocateInfo alloc_info{.descriptorPool = vk_descriptor_pool,
                                             .descriptorSetCount = layouts.size(),
                                             .pSetLayouts = layouts};
    vk_abort_if_error(vk_device.allocateDescriptorSets(&alloc_info, vk_descriptor_sets));

    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vk::DescriptorBufferInfo buffer_info{.buffer = vk_uniform_buffers[i],
                                           .offset = 0,
                                           .range = sizeof(UniformBufferObject)};
      vk::DescriptorImageInfo image_info{.sampler = vk_sampler,
                                         .imageView = vk_tex.image_view,
                                         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      Array descriptor_writes{
          vk::WriteDescriptorSet{.dstSet = vk_descriptor_sets[i],
                                 .dstBinding = 0,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType = vk::DescriptorType::eUniformBuffer,
                                 .pBufferInfo = &buffer_info},
          vk::WriteDescriptorSet{.dstSet = vk_descriptor_sets[i],
                                 .dstBinding = 1,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &image_info}
      };
      vk_device.updateDescriptorSets(descriptor_writes.data, {});
    }
  }
  init_arena = arena_release(init_arena);

  //~ Main Loop
  {
    U32 vk_pool_gen{};
    U64 absolute_frame_index = 0;
    (void)absolute_frame_index;
    U32 current_frame = 0;
    vk::ClearValue vk_clear_color{};
    float rgba[4] = {0.0f, 0.00f, 0.0f, 1.0f};
    MemoryCopy(&vk_clear_color, rgba, sizeof(rgba));
    vk::ClearValue vk_clear_depth = {
        .depthStencil = {.depth = 1.0, .stencil = 0}
    };

    FrameClock clock{.target_ms = target_frame_ms};
    while (!glfwWindowShouldClose(window.glfw_window)) {
      ZoneScopedN("Frame");
      clock.start();
      frame_arena->clear();
      {
        ZoneScopedN("Events");
        glfwPollEvents();

        if (!monitor_detected) {
          int wx, wy, ww, wh;
          glfwGetWindowPos(window.glfw_window, &wx, &wy);
          glfwGetWindowSize(window.glfw_window, &ww, &wh);
          int win_cx = wx + ww / 2;
          int win_cy = wy + wh / 2;

          U32 monitor_hz = 60;
          int count;
          GLFWmonitor **monitors = glfwGetMonitors(&count);
          for (int i = 0; i < count; i++) {
            int mx, my;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            GLFWvidmode const *mode = glfwGetVideoMode(monitors[i]);
            if (win_cx >= mx && win_cx < mx + static_cast<int>(mode->width) && win_cy >= my &&
                win_cy < my + static_cast<int>(mode->height)) {
              monitor_hz = mode->refreshRate > 0 ? mode->refreshRate : 60;
              break;
            }
          }
          target_frame_ms = 1000.0 / monitor_hz;
          clock.target_ms = target_frame_ms;
          printf("Monitor: %u Hz (target: %.3f ms/frame)\n", monitor_hz, target_frame_ms);
          monitor_detected = true;
        }

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
              if (key.key == GLFW_KEY_P && key.action == GLFW_PRESS && (key.mods & GLFW_MOD_CONTROL))
                debug_ui_palette_toggle(&palette_state);
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
              switch (mbutton.button) {
                default: {
                  break;
                }
                case 0: {
                  break;
                }
                case 1: {
                  break;
                }
                case 2: {
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
      }

      {
        ZoneScopedN("ImGui");
        imgui_new_frame();
        debug_ui_palette_render(&palette_state);
      }

      // TODO: extract to render_frame(vk_device, ...) -> bool (swapchain_needs_rebuild)
      ZoneNamedN(___tracy_gpu_sync, "GpuSync", true);
      vk_abort_if_error(vk_device.waitForFences(1, &vk_in_flight_fences[current_frame], VK_TRUE, UINT64_MAX));

      bool swapchain_needs_rebuild = false;
      U32 vk_image_index = 0;

      VkResult vk_acquire_res = vkAcquireNextImageKHR(vk_device,
                                                      vk_swapchain,
                                                      UINT64_MAX,
                                                      vk_acquire_sems[current_frame],
                                                      VK_NULL_HANDLE,
                                                      &vk_image_index);

      if (vk_acquire_res == VK_ERROR_OUT_OF_DATE_KHR) {
        vk_pool_gen = (vk_pool_gen + 1) % SC_MAX_GENERATIONS;
        vk_sc = &vk_swapchain_pool[vk_pool_gen];
        vk_device_arena.offset = vk_swapchain_arena_checkpoint;
        vk_recreate_swapchain(window.glfw_window,
                              vk_device,
                              vk_sc,
                              vk_swapchain,
                              vk_phys_dev,
                              vk_surface,
                              vk_swapchain_extent,
                              vk_chosen_format,
                              vk_chosen_color_space,
                              vk_chosen_present_mode,
                              &vk_depth_image,
                              &vk_depth_image_view,
                              vk_msaa_samples,
                              &vk_device_arena);
        imgui_set_min_image_count(vk_sc->image_count);
        continue;
      }
      if (vk_acquire_res == VK_SUBOPTIMAL_KHR) {
        swapchain_needs_rebuild = true;
      } else if (vk_acquire_res != VK_SUCCESS) {
        std::fprintf(stderr, "vkAcquireNextImageKHR failed: %d\n", vk_acquire_res);
        TRAP();
      }
      {
        if (vk_sc->images_in_flight[vk_image_index]) {
          vk_abort_if_error(vk_device.waitForFences(1, &vk_sc->images_in_flight[vk_image_index], VK_TRUE, UINT64_MAX));
        }
      }
      vk_sc->images_in_flight[vk_image_index] = vk_in_flight_fences[current_frame];
      vk_abort_if_error(vk_device.resetFences(1, &vk_in_flight_fences[current_frame]));

      vk::CommandBuffer vk_cmd = vk_command_buffers[current_frame];
      vk_abort_if_error(vk_cmd.reset());
      vk_abort_if_error(vk_cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));

      {
        TracyVkZone(g_tracy_vk_ctx, static_cast<VkCommandBuffer>(vk_cmd), "Render Scene");
        vk_transition_image_layout(vk_cmd,
                                   vk_sc->msaa_images[vk_image_index],
                                   vk::ImageLayout::eUndefined,
                                   vk::ImageLayout::eColorAttachmentOptimal,
                                   {},
                                   vk::AccessFlagBits2::eColorAttachmentWrite,
                                   vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                   vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                   vk::ImageAspectFlagBits::eColor,
                                   1);
        vk_transition_image_layout(vk_cmd,
                                   vk_sc->images[vk_image_index],
                                   vk::ImageLayout::eUndefined,
                                   vk::ImageLayout::eColorAttachmentOptimal,
                                   {},
                                   vk::AccessFlagBits2::eColorAttachmentWrite,
                                   vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                   vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                   vk::ImageAspectFlagBits::eColor,
                                   1);
        vk_transition_image_layout(
            vk_cmd,
            vk_depth_image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth,
            1);

        vk::RenderingAttachmentInfo vk_color_attachment{.imageView = vk_sc->msaa_image_views[vk_image_index],
                                                        .imageLayout = vk::ImageLayout::eAttachmentOptimal,
                                                        .resolveMode = vk::ResolveModeFlagBits::eAverage,
                                                        .resolveImageView = vk_sc->image_views[vk_image_index],
                                                        .resolveImageLayout = vk::ImageLayout::eAttachmentOptimal,
                                                        .loadOp = vk::AttachmentLoadOp::eClear,
                                                        .storeOp = vk::AttachmentStoreOp::eDontCare,
                                                        .clearValue = {vk_clear_color}};
        vk::RenderingAttachmentInfo vk_depth_attachment = {.imageView = vk_depth_image_view,
                                                           .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                                                           .loadOp = vk::AttachmentLoadOp::eClear,
                                                           .storeOp = vk::AttachmentStoreOp::eDontCare,
                                                           .clearValue = vk_clear_depth};
        vk::RenderingInfo vk_rendering_info{
            .renderArea = vk::Rect2D{{0, 0}, vk_swapchain_extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &vk_color_attachment,
            .pDepthAttachment = &vk_depth_attachment
        };
        vk_cmd.beginRendering(vk_rendering_info);

        vk_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, vk_graphics_pipeline);
        vk_cmd.setViewport(0,
                           vk::Viewport(0.0f,
                                        0.0f,
                                        static_cast<float>(vk_swapchain_extent.width),
                                        static_cast<float>(vk_swapchain_extent.height),
                                        0.0f,
                                        1.0f));
        vk_cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), vk_swapchain_extent));
        vk_cmd.bindVertexBuffers(0, vk_vertex_buffer, {0});
        vk_cmd.bindIndexBuffer(vk_index_buffer, 0, vk::IndexTypeValue<decltype(indices)::value_type>::value);
        vk_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                  vk_pipeline_layout,
                                  0,
                                  vk_descriptor_sets[current_frame],
                                  nullptr);
        vk_cmd.drawIndexed(indices.size, 1, 0, 0, 0);
        vk_cmd.endRendering();
      } // TracyVkZone("Render Scene")

      {
        TracyVkZone(g_tracy_vk_ctx, static_cast<VkCommandBuffer>(vk_cmd), "ImGui Pass");
        imgui_render(vk_cmd, vk_sc->image_views[vk_image_index], vk_swapchain_extent);
      } // TracyVkZone("ImGui Pass")

      TracyVkCollect(g_tracy_vk_ctx, static_cast<VkCommandBuffer>(vk_cmd));

      vk_transition_image_layout(vk_cmd,
                                 vk_sc->images[vk_image_index],
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::ePresentSrcKHR,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 {},
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::PipelineStageFlagBits2::eBottomOfPipe,
                                 vk::ImageAspectFlagBits::eColor,
                                 1);
      vk_sc->image_initialized[vk_image_index] = true;
      vk_abort_if_error(vk_cmd.end());

      {
        ZoneScopedN("UboUpdate");
        static float s_accumulated_time = 0.0f;
        static auto s_prev_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(current_time - s_prev_time).count();
        s_prev_time = current_time;
        if (!paused)
          s_accumulated_time += dt;
        UniformBufferObject ubo{
            .model = glm::rotate(glm::mat4(1.0f),
                                 s_accumulated_time * glm::radians(90.0f),
                                 glm::vec3(0.0f, 0.0f, 1.0f)),
            .view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            .proj =
            glm::perspective(glm::radians(45.0f),
                             static_cast<float>(vk_swapchain_extent.width) / static_cast<float>(
                               vk_swapchain_extent.height),
                             0.1f,
                             10.0f),
        };
        ubo.proj[1][1] *= -1;
        MemoryCopy(vk_uniform_buffers_mapped[current_frame], &ubo, sizeof(ubo));
      }

      vk::PipelineStageFlags vk_wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
      vk::SubmitInfo vk_submit_info{.waitSemaphoreCount = 1,
                                    .pWaitSemaphores = &vk_acquire_sems[current_frame],
                                    .pWaitDstStageMask = &vk_wait_stage,
                                    .commandBufferCount = 1,
                                    .pCommandBuffers = &vk_cmd,
                                    .signalSemaphoreCount = 1,
                                    .pSignalSemaphores = &vk_sc->render_finished_sems[vk_image_index]};
      {
        ZoneScopedN("Submit");
        vk_abort_if_error(vk_graphics_queue.submit(1, &vk_submit_info, vk_in_flight_fences[current_frame]));

        VkPresentInfoKHR vk_present_info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = reinterpret_cast<VkSemaphore const *>(&vk_sc->render_finished_sems[vk_image_index]),
            .swapchainCount = 1,
            .pSwapchains = reinterpret_cast<VkSwapchainKHR const *>(&vk_swapchain),
            .pImageIndices = &vk_image_index,
            .pResults = nullptr};
        VkResult vk_present_res = vkQueuePresentKHR(vk_present_queue, &vk_present_info);

        if (vk_present_res == VK_ERROR_OUT_OF_DATE_KHR || vk_present_res == VK_SUBOPTIMAL_KHR || window.
            framebuffer_resized ||
            swapchain_needs_rebuild) {
          vk_pool_gen = (vk_pool_gen + 1) % SC_MAX_GENERATIONS;
          vk_sc = &vk_swapchain_pool[vk_pool_gen];
          vk_device_arena.offset = vk_swapchain_arena_checkpoint;
          vk_recreate_swapchain(window.glfw_window,
                                vk_device,
                                vk_sc,
                                vk_swapchain,
                                vk_phys_dev,
                                vk_surface,
                                vk_swapchain_extent,
                                vk_chosen_format,
                                vk_chosen_color_space,
                                vk_chosen_present_mode,
                                &vk_depth_image,
                                &vk_depth_image_view,
                                vk_msaa_samples,
                                &vk_device_arena);
          imgui_set_min_image_count(vk_sc->image_count);
          window.framebuffer_resized = false;
        } else if (vk_present_res != VK_SUCCESS) {
          std::fprintf(stderr, "vkQueuePresentKHR failed: %d\n", vk_present_res);
          TRAP();
        }
      }
      clock.mark_work_done();
      {
        ZoneScopedN("FrameLimit");
        clock.wait_for_target();
      }
      FrameMark;

      absolute_frame_index++;
      current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
  }

  // TODO: extract to destroy_vulkan_resources(...) -> void
  //~ Cleanup (reverse of creation order)
  {
    vk_abort_if_error(vk_device.waitIdle());
    imgui_shutdown();
#ifdef TRACY_ENABLE
    TracyVkDestroy(g_tracy_vk_ctx);
#endif

    vk_abort_if_error(vk_device.freeDescriptorSets(vk_descriptor_pool, vk_descriptor_sets.size(), vk_descriptor_sets));
    vk_device.destroyDescriptorPool(vk_descriptor_pool);
    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
      vk_device.destroyBuffer(vk_uniform_buffers[i]);
    vk_device.destroyBuffer(vk_index_buffer);
    vk_device.destroyBuffer(vk_vertex_buffer);
    vk_device.destroyPipeline(vk_graphics_pipeline);
    vk_device.destroyPipelineLayout(vk_pipeline_layout);
    vk_device.destroyDescriptorSetLayout(vk_descriptor_set_layout);
    vk_device.destroyImageView(vk_depth_image_view);
    vk_device.destroyImage(vk_depth_image);
    vk_device.destroyShaderModule(vk_shader_module);
    vk_device.destroySampler(vk_sampler);
    vk_destroy_texture(vk_device, &vk_tex);
    vk_device.destroyCommandPool(vk_command_pool);
    for (U32 i = 0; i < SC_MAX_GENERATIONS; ++i) {
      SwapchainResources *gen = &vk_swapchain_pool[i];
      for (U32 j = 0; j < gen->image_count; ++j) {
        if (gen->render_finished_sems[j])
          vk_device.destroySemaphore(gen->render_finished_sems[j]);
        if (gen->image_views[j])
          vk_device.destroyImageView(gen->image_views[j]);
        if (gen->msaa_image_views[j])
          vk_device.destroyImageView(gen->msaa_image_views[j]);
        if (gen->msaa_images[j])
          vk_device.destroyImage(gen->msaa_images[j]);
      }
    }
    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
      vk_device.destroySemaphore(vk_acquire_sems[i]);
    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
      vk_device.destroyFence(vk_in_flight_fences[i]);
    vk_device.destroySwapchainKHR(vk_swapchain);
    vk_destroy_gpu_arena(vk_device, &vk_device_arena);
    vk_destroy_gpu_arena(vk_device, &vk_host_arena);
    vk_device.destroy();
#ifdef SD2_DEBUG
    if (vk_debug_messenger) {
      PFN_vkDestroyDebugUtilsMessengerEXT func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vk_get_instance_proc_addr(static_cast<VkInstance>(vk_instance), "vkDestroyDebugUtilsMessengerEXT"));
      if (func)
        func(static_cast<VkInstance>(vk_instance), vk_debug_messenger, nullptr);
    }
#endif
    vk_instance.destroySurfaceKHR(vk_surface);
    vk_instance.destroy();
    // glfw
    glfwDestroyWindow(window.glfw_window);
    glfwTerminate();
    arena_release(frame_arena);
    arena_release(app_arena);
    thread_ctx_release();
  }
  return 0;
}

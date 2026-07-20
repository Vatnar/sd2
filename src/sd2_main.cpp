#include <cstdio>
#include <ctime>
#include <fstream>
#include <thread>

#include <sys/ioctl.h>
#include <filesystem>

#define SD2_DEBUG 0
#define TINYOBJLOADER_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#include "sd2_inc.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static vk::detail::DynamicLoader g_vulkan_dynamic_loader{};


struct DebugCtx {
  bool *paused{};
  AppWindow *window{};
  FrameClock *clock{};
  bool *debug_show_window{};
  const char *last_command{};
  bool *debug_show_last_command{};
  bool *debug_show_cursor_info{};
  bool *debug_show_timings{};
  bool *debug_show_camera_info{};
  Camera *camera;
  bool *debug_show_scroll_info;
};

static DebugCtx g_dbg_ctx{};

struct LineVertex {
  glm::vec3 pos;
  glm::vec4 color;

  static constexpr vk::VertexInputBindingDescription get_binding_description() {
    return {.binding = 0, .stride = sizeof(LineVertex), .inputRate = vk::VertexInputRate::eVertex};
  }

  static constexpr Array<vk::VertexInputAttributeDescription, 2> get_attribute_descriptions() {
    return {
        {
            vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,
                                                .offset = offsetof(LineVertex, pos)},
            vk::VertexInputAttributeDescription{.location = 1, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat,
                                                .offset = offsetof(LineVertex, color)}
        }
    };
  }
};

struct TextureVertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 tex_coord;

  static constexpr Array<vk::VertexInputBindingDescription, 1> get_binding_descriptions() {
    return {
        vk::VertexInputBindingDescription{.binding = 0, .stride = sizeof(TextureVertex),
                                          .inputRate = vk::VertexInputRate::eVertex}};
  }

  static constexpr Array<vk::VertexInputAttributeDescription, 3> get_attribute_descriptions() {
    return {
        {
            vk::VertexInputAttributeDescription{.location = 0,
                                                .binding = 0,
                                                .format = vk::Format::eR32G32B32Sfloat,
                                                .offset = offsetof(TextureVertex, pos)},
            vk::VertexInputAttributeDescription{.location = 1,
                                                .binding = 0,
                                                .format = vk::Format::eR32G32B32Sfloat,
                                                .offset = offsetof(TextureVertex, color)},
            vk::VertexInputAttributeDescription{.location = 2,
                                                .binding = 0,
                                                .format = vk::Format::eR32G32Sfloat,
                                                .offset = offsetof(TextureVertex, tex_coord)},
        }
    };
  }

  bool operator==(TextureVertex const &other) const {
    return pos == other.pos && color == other.color && tex_coord == other.tex_coord;
  }
};

template<>
struct std::hash<TextureVertex> {
  size_t operator()(TextureVertex const &vertex) const noexcept {
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

internal std::tuple<DynArray<TextureVertex>, DynArray<U32>> load_obj(Arena *arena, const char *obj_path);

//~ cpp
#include "internal/base_arena.cpp"
#include "internal/vk_helpers.cpp"
#include "internal/helpers.cpp"
#include "internal/imgui_helpers.cpp"
#include "internal/debug_ui.cpp"


FORCE_INLINE internal void toggle_cursor() {
  switch (glfwGetInputMode(g_dbg_ctx.window->glfw_window, GLFW_CURSOR)) {
    case GLFW_CURSOR_DISABLED: {
      glfwSetInputMode(g_dbg_ctx.window->glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      break;
    }
    case GLFW_CURSOR_NORMAL: {
      glfwSetInputMode(g_dbg_ctx.window->glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      break;
    }
  }
}

FORCE_INLINE internal void show_cursor() {
  glfwSetInputMode(g_dbg_ctx.window->glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

FORCE_INLINE internal void hide_cursor() {
  glfwSetInputMode(g_dbg_ctx.window->glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

FORCE_INLINE internal void toggle_fullscreen() {
  auto *w = g_dbg_ctx.window;
  GLFWwindow *glfw_window = g_dbg_ctx.window->glfw_window;
  if (!w->fullscreen) {
    glfwGetWindowPos(glfw_window, &w->windowed_x, &w->windowed_y);
    glfwGetWindowSize(glfw_window, &w->windowed_w, &w->windowed_h);
    int cx = w->windowed_x + w->windowed_w / 2;
    int cy = w->windowed_y + w->windowed_h / 2;
    int count;
    GLFWmonitor **monitors = glfwGetMonitors(&count);
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    for (int i = 0; i < count; i++) {
      int mx, my;
      glfwGetMonitorPos(monitors[i], &mx, &my);
      GLFWvidmode const *mode = glfwGetVideoMode(monitors[i]);
      if (cx >= mx && cx < mx + static_cast<int>(mode->width) &&
          cy >= my && cy < my + static_cast<int>(mode->height)) {
        monitor = monitors[i];
        break;
      }
    }
    int mx, my;
    glfwGetMonitorPos(monitor, &mx, &my);
    GLFWvidmode const *mode = glfwGetVideoMode(monitor);
    glfwSetWindowAttrib(glfw_window, GLFW_DECORATED, GLFW_FALSE);
    glfwSetWindowPos(glfw_window, mx, my);
    glfwSetWindowSize(glfw_window, mode->width, mode->height);
  } else {
    glfwSetWindowAttrib(glfw_window, GLFW_DECORATED, GLFW_TRUE);
    glfwSetWindowPos(glfw_window, w->windowed_x, w->windowed_y);
    glfwSetWindowSize(glfw_window, w->windowed_w, w->windowed_h);
  }
  w->fullscreen = !w->fullscreen;
}

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

  load_debug_ui_state();

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


  g_dbg_ctx.paused = &paused;
  g_dbg_ctx.window = &window;

  //~ Palette
  auto pa = DynArray<PaletteAction>::from_init(app_arena,
                                               {
                                                   PaletteAction::call("fullscreen", toggle_fullscreen),
                                                   PaletteAction::call("pause",
                                                                       [] {
                                                                         *g_dbg_ctx.paused = !*g_dbg_ctx.paused;
                                                                         if (*g_dbg_ctx.paused)
                                                                           show_cursor();
                                                                         else {
                                                                           hide_cursor();
                                                                         }
                                                                       }
                                                       ),
                                                   PaletteAction::call("exit/quit",
                                                                       [] {
                                                                         glfwSetWindowShouldClose(
                                                                             g_dbg_ctx.window->glfw_window,
                                                                             true);
                                                                       }
                                                       ),
                                                   PaletteAction::call("toggle cursor",
                                                                       [] {
                                                                         toggle_cursor();
                                                                       }
                                                       ),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_cursor_info>(g_dbg_ctx),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_window>(g_dbg_ctx),
                                                   PaletteAction::toggle<^^
                                                     DebugCtx::debug_show_last_command>(g_dbg_ctx),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_timings>(g_dbg_ctx),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_camera_info>(g_dbg_ctx),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_scroll_info>(g_dbg_ctx),
                                               });
  debug_ui_palette_init(&palette_state,
                        pa
      );

  //~ Graphics Pipeline Libraries (GPL)
  vk::PipelineLayout vk_pipeline_layout{};
  vk::DescriptorSetLayout vk_descriptor_set_layout{};
  vk::Image vk_depth_image{};
  vk::ImageView vk_depth_image_view{};
  vk::Pipeline vk_output_lib{};
  vk::Pipeline vk_model_a{}, vk_model_b{}, vk_model_c{};
  vk::Pipeline vk_line_a{}, vk_line_b{}, vk_line_c{};
  vk::Pipeline vk_graphics_pipeline{};
  vk::Pipeline vk_line_pipeline{};
  vk::Buffer vk_line_vertex_buffer{};

  {
    TempScope scratch = scratch_begin_scoped(0, 0);

    //--- Shared: shader stages (compiled once, sliced for B and C) ---
    VKShaderStageDesc vert_desc{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .entrypoint_name = str8_lit("vert_main"),
        .file_name = str8_lit("assets/shaders/shader.spv"),
    };
    VKShaderStageDesc frag_desc{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .entrypoint_name = str8_lit("frag_main"),
        .file_name = str8_lit("assets/shaders/shader.spv"),
    };
    VKShaderStageDesc line_vert_desc{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .entrypoint_name = str8_lit("vert_main"),
        .file_name = str8_lit("assets/shaders/line.spv"),
    };
    VKShaderStageDesc line_frag_desc{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .entrypoint_name = str8_lit("frag_main"),
        .file_name = str8_lit("assets/shaders/line.spv"),
    };
    DynArray<VKShaderStageDesc> shader_descs =
        DynArray<VKShaderStageDesc>::from_init(scratch, {vert_desc, frag_desc, line_vert_desc, line_frag_desc});
    auto shader_stages = build_shader_stages(scratch, vk_device, &shader_descs);

    //--- Shared: depth resources (swapchain lifetime, not pipeline lifetime) ---
    VKDepthResources depth = vk_create_depth_resources(
        vk_phys_dev,
        vk_device,
        &vk_device_arena,
        vk_swapchain_extent,
        vk_msaa_samples);
    vk_depth_image = depth.tex.image;
    vk_depth_image_view = depth.tex.image_view;
    vk::Format depth_format = depth.format;

    //--- Shared: descriptor set layout + pipeline layout ---
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
    vk_descriptor_set_layout = create_descriptor_set_layout(vk_device, bindings.size(), bindings);
    vk_pipeline_layout = create_pipeline_layout(vk_device, vk_descriptor_set_layout);

    //--- Shared D: Fragment Output Interface ---
    FragmentOutputLibDesc desc_d{
        .msaa_samples = vk_msaa_samples,
        .color_format = vk_chosen_format,
        .depth_format = depth_format,
    };
    vk_output_lib = create_fragment_output_library(vk_device, desc_d);

    //--- Model pipeline: A (Vertex Input Interface) ---
    VertexInputLibDesc desc_a{
        .bindings = TextureVertex::get_binding_descriptions().to_dyn(scratch),
        .attributes = TextureVertex::get_attribute_descriptions().to_dyn(scratch),
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    vk_model_a = create_vertex_input_library(vk_device, desc_a);

    //--- Model pipeline: B (Pre-Rasterization Shaders) ---
    PreRasterLibDesc desc_b{};
    vk_model_b = create_pre_raster_library(
        vk_device,
        vk_pipeline_layout,
        desc_b,
        shader_stages.stages,
        1);

    //--- Model pipeline: C (Fragment Shader) ---
    FragmentLibDesc desc_c{
        .msaa_samples = vk_msaa_samples,
        .sample_shading_enable = vk::True,
        .min_sample_shading = 0.2f,
    };
    vk_model_c = create_fragment_library(
        vk_device,
        vk_pipeline_layout,
        desc_c,
        &shader_stages.stages[1],
        1);

    //--- Model pipeline: Link ---
    PROFILE_START("pipeline_creation");
    vk::Pipeline model_libs[] = {vk_model_a, vk_model_b, vk_model_c, vk_output_lib};
    vk_graphics_pipeline = create_linked_pipeline(
        vk_device,
        vk_pipeline_layout,
        4,
        model_libs,
        vk_chosen_format,
        depth_format);
    U64 pipe_cycles = PROFILE_END();
    printf("Model pipeline creation: %lu cycles\n", pipe_cycles);

    //--- Line pipeline: A (LineVertex, line list) ---
    VertexInputLibDesc line_desc_a{
        .bindings = DynArray<vk::VertexInputBindingDescription>::from_init(scratch,
                                                                           {LineVertex::get_binding_description()}),
        .attributes = LineVertex::get_attribute_descriptions().to_dyn(scratch),
        .topology = vk::PrimitiveTopology::eLineList,
    };
    vk_line_a = create_vertex_input_library(vk_device, line_desc_a);

    //--- Line pipeline: B (same raster defaults, dynamic line width) ---
    vk::DynamicState line_states[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth};
    vk_line_b = create_pre_raster_library(
        vk_device,
        vk_pipeline_layout,
        PreRasterLibDesc{.line_width = 5.0f, .dynamic_states = line_states, .dynamic_state_count = 3},
        &shader_stages.stages[2],
        1);

    //--- Line pipeline: C (matching msaa for GPL compatibility) ---
    FragmentLibDesc line_desc_c{
        .msaa_samples = vk_msaa_samples,
        .sample_shading_enable = vk::True,
        .min_sample_shading = 0.2f,
    };
    vk_line_c = create_fragment_library(
        vk_device,
        vk_pipeline_layout,
        line_desc_c,
        &shader_stages.stages[3],
        1);

    //--- Line pipeline: Link ---
    vk::Pipeline line_libs[] = {vk_line_a, vk_line_b, vk_line_c, vk_output_lib};
    vk_line_pipeline = create_linked_pipeline(
        vk_device,
        vk_pipeline_layout,
        4,
        line_libs,
        vk_chosen_format,
        depth_format);

    //--- Cleanup (shader modules only; library pipelines persist for linked pipeline) ---
    for (U64 i = 0; i < shader_stages.modules.size; i++) {
      vk_device.destroyShaderModule(shader_stages.modules[i].module);
    }
    scratch.end();
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
                                       DynArray<TextureVertex> vertices,
                                       DynArray<U32> indices) -> std::tuple<vk::Buffer, vk::Buffer> {
    vk::Buffer vertex_buffer{}, index_buffer{};
    {
      vk::DeviceSize buffer_size = vertices.byte_size();

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
      vk::DeviceSize buffer_size = indices.byte_size();

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
  {
    LineVertex line_vertices[] = {
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    vk::DeviceSize buffer_size = sizeof(line_vertices);
    auto [staging, staging_alloc] = vk_create_buffer(vk_phys_dev,
                                                     vk_device,
                                                     buffer_size,
                                                     vk::BufferUsageFlagBits::eTransferSrc,
                                                     &vk_host_arena);
    MemoryCopy(staging_alloc.mapped, line_vertices, buffer_size);
    auto [vb, vb_alloc] = vk_create_buffer(vk_phys_dev,
                                           vk_device,
                                           buffer_size,
                                           vk::BufferUsageFlagBits::eVertexBuffer |
                                           vk::BufferUsageFlagBits::eTransferDst,
                                           &vk_device_arena);
    vk_line_vertex_buffer = vb;
    (void)vb_alloc;
    vk_copy_buffer(vk_device, vk_command_pool, vk_graphics_queue, staging, vk_line_vertex_buffer, buffer_size);
    vk_device.destroyBuffer(staging);
  }
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
    glfwSetInputMode(window.glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);


    FrameClock clock{.target_ms = target_frame_ms};
    g_dbg_ctx.clock = &clock;
    while (!glfwWindowShouldClose(window.glfw_window)) {
      Vec3<F32> move{};
      local_persist F32 key_speed = 0.1f;
      clock.start();
      frame_arena->clear();
      glfwPollEvents();

      //~ Input
      {
        handle_key_input(&window.key);
        handle_mouse_input(&window.mouse);
      }
      if (window.key.held[GLFW_KEY_LEFT_CONTROL] && window.key.pressed[
            GLFW_KEY_P]) {
        debug_ui_palette_toggle(&palette_state);
      }


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

      // TODO: For input events like movement we rather wanna query the input instead of handling the event callback. Callback should be more for one off things really.

      // TODO: trigger palette with ctrl p by defaulrt
      // TODO: m,ovement

      //~ ImGui
      {
        imgui_new_frame();
        debug_ui_palette_render(&palette_state);
        debug_ui_debug_ui(&clock.report);
      }

      // TODO: extract to render_frame(vk_device, ...) -> bool (swapchain_needs_rebuild)
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
          vk_abort_if_error(
              vk_device.waitForFences(1, &vk_sc->images_in_flight[vk_image_index], VK_TRUE, UINT64_MAX));
        }
      }
      vk_sc->images_in_flight[vk_image_index] = vk_in_flight_fences[current_frame];
      vk_abort_if_error(vk_device.resetFences(1, &vk_in_flight_fences[current_frame]));

      vk::CommandBuffer vk_cmd = vk_command_buffers[current_frame];
      vk_abort_if_error(vk_cmd.reset());
      vk_abort_if_error(vk_cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));

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

      //~ Render
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
      vk_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, vk_line_pipeline);
      vk_cmd.setLineWidth(5.0f);
      vk_cmd.bindVertexBuffers(0, vk_line_vertex_buffer, {0});
      vk_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                vk_pipeline_layout,
                                0,
                                vk_descriptor_sets[current_frame],
                                nullptr);
      vk_cmd.draw(6, 1, 0, 0);
      vk_cmd.endRendering();

      imgui_render(vk_cmd, vk_sc->image_views[vk_image_index], vk_swapchain_extent);


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

      //~ State update
      {
        if (!palette_state.open) {
          if (window.key.held[GLFW_KEY_W])
            move.x += 1.0f * key_speed;
          if (window.key.held[GLFW_KEY_S])
            move.x -= 1.0f * key_speed;

          if (window.key.held[GLFW_KEY_D])
            move.y += 1.0f * key_speed;
          if (window.key.held[GLFW_KEY_A])
            move.y -= 1.0f * key_speed;

          if (window.key.held[GLFW_KEY_SPACE])
            move.z += 1.0f * key_speed;
          if (window.key.held[GLFW_KEY_LEFT_SHIFT])
            move.z -= 1.0f * key_speed;

          key_speed += 0.05f * static_cast<F32>(window.mouse.delta_scroll.y);
          key_speed = clamp_bot(0.0005f, key_speed);
        }
        static float s_accumulated_time = 0.0f;
        static auto s_prev_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(current_time - s_prev_time).count();
        s_prev_time = current_time;


        //~ Camera
        constexpr F32 MOUSE_SENSITIVITY = 6.0f;
        local_persist Camera camera = Camera::from_pos(glm::vec3(2.0f));
        g_dbg_ctx.camera = &camera;
        if (!paused) {
          s_accumulated_time += dt;
          Vec3 rot{
              window.mouse.delta_pos.x * MOUSE_SENSITIVITY * dt,
              window.mouse.delta_pos.y * MOUSE_SENSITIVITY * dt,
              0.0f // roll
          };
          camera.transform(rot, move);
        }
        UniformBufferObject ubo{
            .model = glm::rotate(glm::mat4(1.0f),
                                 s_accumulated_time * glm::radians(90.0f),
                                 glm::vec3(0.0f, 0.0f, 1.0f)),
            .view = camera.view(),
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
      //~ vk submit

      vk::PipelineStageFlags vk_wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
      vk::SubmitInfo vk_submit_info{.waitSemaphoreCount = 1,
                                    .pWaitSemaphores = &vk_acquire_sems[current_frame],
                                    .pWaitDstStageMask = &vk_wait_stage,
                                    .commandBufferCount = 1,
                                    .pCommandBuffers = &vk_cmd,
                                    .signalSemaphoreCount = 1,
                                    .pSignalSemaphores = &vk_sc->render_finished_sems[vk_image_index]};
      {
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
        clock.wait_for_target();
      }

      absolute_frame_index++;
      current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
  }


  // TODO: extract to destroy_vulkan_resources(...) -> void
  //~ Cleanup (reverse of creation order)
  {
    vk_abort_if_error(vk_device.waitIdle());
    imgui_shutdown();

    vk_abort_if_error(
        vk_device.freeDescriptorSets(vk_descriptor_pool, vk_descriptor_sets.size(), vk_descriptor_sets));
    vk_device.destroyDescriptorPool(vk_descriptor_pool);
    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
      vk_device.destroyBuffer(vk_uniform_buffers[i]);
    vk_device.destroyBuffer(vk_index_buffer);
    vk_device.destroyBuffer(vk_vertex_buffer);
    vk_device.destroyBuffer(vk_line_vertex_buffer);
    vk_device.destroyPipeline(vk_graphics_pipeline);
    vk_device.destroyPipeline(vk_line_pipeline);
    vk_device.destroyPipeline(vk_output_lib);
    vk_device.destroyPipeline(vk_line_c);
    vk_device.destroyPipeline(vk_line_b);
    vk_device.destroyPipeline(vk_line_a);
    vk_device.destroyPipeline(vk_model_c);
    vk_device.destroyPipeline(vk_model_b);
    vk_device.destroyPipeline(vk_model_a);
    vk_device.destroyPipelineLayout(vk_pipeline_layout);
    vk_device.destroyDescriptorSetLayout(vk_descriptor_set_layout);
    vk_device.destroyImageView(vk_depth_image_view);
    vk_device.destroyImage(vk_depth_image);
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
  save_debug_ui_state();
  return
      0;
}

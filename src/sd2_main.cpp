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
  bool *debug_show_line_width{};
  float line_width{5.0f};
};

static DebugCtx g_dbg_ctx{};

struct LineVertex {
  glm::vec3 pos;
  glm::vec4 color;
};

Array<vk::VertexInputBindingDescription, 1> line_vertex_binding_descriptions{{
    {.binding = 0, .stride = sizeof(LineVertex), .inputRate = vk::VertexInputRate::eVertex},
}};

Array<vk::VertexInputAttributeDescription, 2> line_vertex_attribute_descriptions{{
    vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,
                                        .offset = offsetof(LineVertex, pos)},
    vk::VertexInputAttributeDescription{.location = 1, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat,
                                        .offset = offsetof(LineVertex, color)},
}};

enum class ArrowFlags : U32 {
  NONE = 0 << 0,
  CAMERA_FACING = (1 << 0),
};

struct Arrow {
  glm::vec3 origin;
  glm::vec3 direction;
  F32 magnitude;
  F32 width;
  F32 roundness;
  glm::vec4 color;
  U32 flags;

  static constexpr U64 vertex_count_per_arrow() {
    return 6; // draw quad which we fill in fragment
  }

  static constexpr Array<vk::VertexInputBindingDescription, 1> get_binding_descriptions() {
    return {
        vk::VertexInputBindingDescription{.binding = 0, .stride = sizeof(Arrow),
                                          .inputRate = vk::VertexInputRate::eInstance}
    };
  }

  static constexpr Array<vk::VertexInputAttributeDescription, 7> get_attribute_descriptions() {
    return {
        vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,
                                            .offset = offsetof(Arrow, origin)},
        vk::VertexInputAttributeDescription{.location = 1, .binding = 0, .format = vk::Format::eR32Sfloat,
                                            .offset = offsetof(Arrow, magnitude)},
        vk::VertexInputAttributeDescription{.location = 2, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,
                                            .offset = offsetof(Arrow, direction)},
        vk::VertexInputAttributeDescription{.location = 3, .binding = 0, .format = vk::Format::eR32Sfloat,
                                            .offset = offsetof(Arrow, width)},
        vk::VertexInputAttributeDescription{.location = 4, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat,
                                            .offset = offsetof(Arrow, color)},
        vk::VertexInputAttributeDescription{.location = 5, .binding = 0, .format = vk::Format::eR32Sfloat,
                                            .offset = offsetof(Arrow, roundness)},
        vk::VertexInputAttributeDescription{.location = 6, .binding = 0, .format = vk::Format::eR32Uint,
                                            .offset = offsetof(Arrow, flags)},
    };
  }
};

struct TextureVertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 tex_coord;

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


Array<vk::VertexInputBindingDescription, 1> texture_vertex_binding_descriptions{{
    vk::VertexInputBindingDescription{.binding = 0, .stride = sizeof(TextureVertex),
                                      .inputRate = vk::VertexInputRate::eVertex},
}};

Array<vk::VertexInputAttributeDescription, 3> texture_vertex_attribute_descriptions{{
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
}};

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
#include "internal/window.cpp"
#include "internal/vk_helpers.cpp"
#include "internal/helpers.cpp"
#include "internal/imgui_helpers.cpp"
#include "internal/debug_ui.cpp"


int main() {
  thread_ctx_init();
  VKArena app_arena{arena_alloc({.name = "App arena"})};

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
  VKInstanceResult vk_instance_result = vk_create_vulkan_instance(window.glfw_window, &app_arena);
  vk::Instance vk_instance = vk_instance_result.instance;
  vk::SurfaceKHR vk_surface = vk_instance_result.surface;

  //~ Physical Device + Queue Families
  vk::PhysicalDevice vk_phys_dev{};
  VKQueueFamilyIndices vk_queue_indices{};
  {
    VKRatedDevice rated_device = vk_pick_best_physical_device(vk_instance, vk_surface);
    if (rated_device.score == 0) {
      TRAP();
    }
    vk_phys_dev = rated_device.device;
    vk_queue_indices = rated_device.queue_family_indices;
  }

  //~ Logical Device + Queues
  auto [vk_device, vk_graphics_queue, vk_present_queue] = vk_create_logical_device(
      vk_phys_dev,
      vk_surface);

  //~ GPU memory arenas
  U32 vk_host_mem_type =
      vk_find_memory_type(vk_phys_dev,
                          UINT32_MAX,
                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  U32 vk_device_mem_type = vk_find_memory_type(vk_phys_dev, UINT32_MAX, vk::MemoryPropertyFlagBits::eDeviceLocal);

  VKGpuArena vk_host_arena = vk_create_gpu_arena(vk_phys_dev, vk_device, vk_host_mem_type, mb(8));
  VKGpuArena vk_device_arena = vk_create_gpu_arena(vk_phys_dev, vk_device, vk_device_mem_type, gb(2));

  //~ Swapchain Creation

  VKSwapchainConfig vk_sc_config =
      vk_create_swapchain_config(vk_device,
                                 vk_phys_dev,
                                 vk_surface,
                                 vk_queue_indices,
                                 &vk_device_arena,
                                 init_arena);
  vk_sc_config.window = &window;

  U32 vk_graphics_index = vk_sc_config.queue_indices.graphics_index;
  U32 vk_present_index = vk_sc_config.queue_indices.present_index;

  VKSwapchainState vk_sc_state{
      .extent = vk_get_extent(&vk_sc_config, app_params.width, app_params.height),
  };

  vk_create_swapchain(
      &vk_sc_config,
      &vk_sc_state,
      nullptr);


  //~ Swapchain Image Views + Sync Primitives + Command Pool

  vk::CommandPool vk_command_pool = vk_create_swapchain_resources(&vk_sc_config,
                                                                  &vk_sc_state);


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
    vk_sampler = app_arena.ds.push(vk_abort_if_error(vk_device.createSamplerUnique(sampler_info)));
  }

  imgui_init({
      .instance = vk_instance,
      .phys_dev = vk_phys_dev,
      .device = vk_device,
      .color_format = static_cast<VkFormat>(vk_sc_config.image_format),
      .graphics_queue = vk_graphics_queue,
      .glfw_window = window.glfw_window,
      .graphics_queue_family = vk_graphics_index,
      .image_count = vk_sc_state.sc_res->image_count,
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
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_cursor_info>(
                                                       g_dbg_ctx),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_window>(g_dbg_ctx),
                                                   PaletteAction::toggle<^^
                                                     DebugCtx::debug_show_last_command>(g_dbg_ctx),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_timings>(g_dbg_ctx),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_camera_info>(
                                                       g_dbg_ctx),
                                                   PaletteAction::toggle<^^DebugCtx::debug_show_scroll_info>(
                                                       g_dbg_ctx),
                                                   PaletteAction::toggle<^^
                                                     DebugCtx::debug_show_line_width>(g_dbg_ctx),
                                               });
  debug_ui_palette_init(&palette_state, pa);

  vk_create_depth_resources(
      &vk_sc_config,
      &vk_sc_state,
      &vk_sc_config.swapchain_pool[0].sc_res_arena);

  Array ubo_and_sampler_bindings{

      vk::DescriptorSetLayoutBinding{.binding = 0,
                                     .descriptorType = vk::DescriptorType::eUniformBuffer,
                                     .descriptorCount = 1,
                                     .stageFlags = vk::ShaderStageFlagBits::eVertex},
      vk::DescriptorSetLayoutBinding{.binding = 1,
                                     .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                     .descriptorCount = 1,
                                     .stageFlags = vk::ShaderStageFlagBits::eFragment}
  };
  // Array arrow_bindings{
  // vk::DescriptorSetLayoutBinding{.binding = 0, .descriptorType = vk::DescriptorType::eStorageBuffer,
  // .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex},
  // vk::DescriptorSetLayoutBinding{.binding = 1,
  // .descriptorType = vk::DescriptorType::eUniformBuffer,
  // .descriptorCount = 1,
  // .stageFlags = vk::ShaderStageFlagBits::eVertex},
  // };

  vk::DescriptorSetLayout vk_ubo_and_sampler = create_descriptor_set_layout(
      vk_device,
      &app_arena,
      ubo_and_sampler_bindings.to_slice());

  vk::PipelineLayout vk_ubo_and_sampler_layout = create_pipeline_layout(
      vk_device,
      &app_arena,
      make_slice(vk_ubo_and_sampler),
      {});
  // vk::DescriptorSetLayout arrow_dsl = create_descriptor_set_layout(
  // vk_device,
  // &app_arena,
  // arrow_bindings.to_slice());

  // vk::PipelineLayout arrow_layout = create_pipeline_layout(vk_device,
  // &app_arena,
  // make_slice(arrow_dsl),
  // {});


  //~ Graphics Pipeline Libraries (GPL)
  vk::Pipeline vk_output_lib{};
  vk::Pipeline vk_model_a{}, vk_model_b{}, vk_model_c{};
  vk::Pipeline vk_line_a{}, vk_line_b{}, vk_line_c{};
  vk::Pipeline vk_line_pipeline{};
  // vk::Pipeline vk_arrow_a{}, vk_arrow_b{}, vk_arrow_c{};
  // vk::Pipeline vk_arrow_pipeline{};
  vk::Pipeline vk_graphics_pipeline{};

  {
    TempScope scratch = scratch_begin_scoped(0, 0);
    VKArenaScoped vk_scratch = vk_scratch_begin_scoped(0, 0);

    vk::ShaderModule mesh_mod = load_shader_module(vk_device, vk_scratch, str8_lit("assets/shaders/shader.spv"));

    vk::PipelineShaderStageCreateInfo mesh_vert{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = mesh_mod,
        .pName = "vert_main",
    };
    vk::PipelineShaderStageCreateInfo mesh_frag{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = mesh_mod,
        .pName = "frag_main",
    };
    vk::ShaderModule line_mod = load_shader_module(vk_device, vk_scratch, str8_lit("assets/shaders/line.spv"));
    vk::PipelineShaderStageCreateInfo line_vert{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = line_mod,
        .pName = "vert_main",
    };
    vk::PipelineShaderStageCreateInfo line_frag{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = line_mod,
        .pName = "frag_main",
    };
    // vk::ShaderModule arrow_mod = load_shader_module(vk_device, vk_scratch, str8_lit("assets/shaders/proc_arrow.spv"));
    // vk_device,
    // str8_lit("assets/shaders/proc_arrow.spv"),
    // str8_lit("vert_main"),
    // str8_lit("frag_main"));

    FragmentOutputLibDesc desc_d{
        .msaa_samples = vk_sc_config.msaa_samples,
        .color_format = vk_sc_config.image_format,
        .depth_format = vk_sc_state.depth.format,
    };
    vk_output_lib = create_fragment_output_library(vk_device,
                                                   &app_arena,
                                                   desc_d);

    VertexInputLibDesc desc_a{
        .bindings = texture_vertex_binding_descriptions.to_slice(),
        .attributes = texture_vertex_attribute_descriptions.to_slice(),
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    vk_model_a = create_vertex_input_library(vk_device,
                                             &app_arena,
                                             desc_a);

    //--- Model pipeline: B (Pre-Rasterization Shaders) ---
    vk_model_b = create_pre_raster_library(vk_device,
                                           &app_arena,
                                           vk_ubo_and_sampler_layout,
                                           PreRasterLibDesc{.shader_stages = make_slice(mesh_vert)});

    //--- Model pipeline: C (Fragment Shader) ---
    vk_model_c = create_fragment_library(vk_device,
                                         &app_arena,
                                         vk_ubo_and_sampler_layout,
                                         FragmentLibDesc{.fragment_shader = &mesh_frag,
                                                         .msaa_samples = vk_sc_config.msaa_samples,
                                                         .sample_shading_enable = vk::True,
                                                         .min_sample_shading = 0.2f});

    //--- Model pipeline: Link ---
    PROFILE_START("pipeline_creation");
    Array model_libs = {vk_model_a, vk_model_b, vk_model_c, vk_output_lib};
    vk_graphics_pipeline = create_linked_pipeline(
        vk_device,
        &app_arena,
        vk_ubo_and_sampler_layout,
        model_libs.to_slice(),
        vk_sc_config.image_format,
        vk_sc_state.depth.format);
    U64 pipe_cycles = PROFILE_END();
    printf("Model pipeline creation: %lu cycles\n", pipe_cycles);

    //--- Line pipeline: A (LineVertex, line list) ---
    VertexInputLibDesc line_desc_a{
        .bindings = line_vertex_binding_descriptions.to_slice(),
        .attributes = line_vertex_attribute_descriptions.to_slice(),
        .topology = vk::PrimitiveTopology::eLineList,
    };
    vk_line_a = create_vertex_input_library(vk_device,
                                            &app_arena,
                                            line_desc_a);

    //--- Line pipeline: B (Pre-Rasterization Shaders) ---
    vk::DynamicState line_states[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor,
                                      vk::DynamicState::eLineWidth};
    vk_line_b = create_pre_raster_library(vk_device,

                                          &app_arena,
                                          vk_ubo_and_sampler_layout,
                                          PreRasterLibDesc{.shader_stages = make_slice(line_vert),
                                                           .line_width = 5.0f,
                                                           .dynamic_states = line_states, .dynamic_state_count = 3});

    //--- Line pipeline: C (Fragment Shader) ---
    vk_line_c = create_fragment_library(vk_device,
                                        &app_arena,
                                        vk_ubo_and_sampler_layout,
                                        FragmentLibDesc{.fragment_shader = &line_frag,
                                                        .msaa_samples = vk_sc_config.msaa_samples,
                                                        .sample_shading_enable = vk::True,
                                                        .min_sample_shading = 0.2f});

    //--- Line pipeline: Link ---
    Array line_libs = {vk_line_a, vk_line_b, vk_line_c, vk_output_lib};
    vk_line_pipeline = create_linked_pipeline(
        vk_device,
        &app_arena,
        vk_ubo_and_sampler_layout,
        line_libs.to_slice(),
        vk_sc_config.image_format,
        vk_sc_state.depth.format);

    //--- Arrow pipeline: A (Arrow vertex input) ---
    // VertexInputLibDesc arrow_desc_a{
    // .bindings = Arrow::get_binding_descriptions().to_slice(),
    // .attributes = Arrow::get_attribute_descriptions().to_slice(),
  };
  // vk_arrow_a = create_vertex_input_library(vk_device,
  // &app_arena,
  // arrow_desc_a);

  //--- Arrow pipeline: B (Pre-Rasterization Shaders) ---
  // vk_arrow_b = create_pre_raster_library(vk_device,
  // &app_arena,
  // arrow_layout,
  // PreRasterLibDesc{.shader_stages = make_slice(arrow_vert)});

  //--- Arrow pipeline: C (Fragment Shader) ---
  // vk_arrow_c = create_fragment_library(vk_device,
  // &app_arena,
  // arrow_layout,
  // FragmentLibDesc{.fragment_shader = &arrow_frag,
  // .msaa_samples = vk_sc_config.msaa_samples,
  // .sample_shading_enable = true,
  // .min_sample_shading = 0.2f});

  //--- Arrow pipeline: Link ---
  // vk::Pipeline arrow_libs[] = {vk_arrow_a, vk_arrow_b, vk_arrow_c, vk_output_lib};
  // vk_arrow_pipeline = create_linked_pipeline(
  // vk_device,
  // &app_arena,
  // arrow_layout,
  // 4,
  // arrow_libs,
  // vk_sc_config.image_format,
  // vk_sc_state.depth.format);
  // }

  //~ Load model
  auto [vertices, indices] = load_obj(app_arena, MODEL_PATH);
  //~ Load texture
  VKTextureImage vk_tex = vk_load_texture(vk_phys_dev,
                                          vk_device,
                                          &vk_host_arena,
                                          &vk_device_arena,
                                          &app_arena,
                                          vk_command_pool,
                                          vk_graphics_queue,
                                          TEXTURE_PATH);

  auto vk_create_geometry_buffers = [](vk::PhysicalDevice vk_phys_dev,
                                       vk::Device vk_device,
                                       VKGpuArena *host_arena,
                                       VKGpuArena *device_arena,
                                       VKArena *arena,
                                       vk::CommandPool command_pool,
                                       vk::Queue graphics_queue,
                                       DynArray<TextureVertex> vertices,
                                       DynArray<U32> indices) -> std::tuple<vk::Buffer, vk::Buffer> {
    vk::Buffer vertex_buffer{}, index_buffer{};
    {
      Arena *scratch_conflicts[] = {arena->arena};
      VKArenaScoped scratch = vk_scratch_begin_scoped(scratch_conflicts, 1);
      vk::DeviceSize buffer_size = vertices.byte_size();

      auto [staging_buffer, staging_alloc] =
          vk_create_buffer(vk_phys_dev,
                           vk_device,
                           buffer_size,
                           vk::BufferUsageFlagBits::eTransferSrc,
                           host_arena,
                           scratch);

      MemoryCopy(staging_alloc.mapped, vertices.data, buffer_size);

      auto [vb, vb_alloc] =
          vk_create_buffer(vk_phys_dev,
                           vk_device,
                           buffer_size,
                           vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                           device_arena,
                           arena);
      vertex_buffer = vb;
      (void)vb_alloc;
      vk_copy_buffer(vk_device, command_pool, graphics_queue, staging_buffer, vertex_buffer, buffer_size);
    }
    {
      Arena *scratch_conflicts[] = {arena->arena};
      VKArenaScoped scratch = vk_scratch_begin_scoped(scratch_conflicts, 1);
      vk::DeviceSize buffer_size = indices.byte_size();

      auto [staging_buffer, staging_alloc] =
          vk_create_buffer(vk_phys_dev,
                           vk_device,
                           buffer_size,
                           vk::BufferUsageFlagBits::eTransferSrc,
                           host_arena,
                           scratch);

      MemoryCopy(staging_alloc.mapped, indices.data, buffer_size);

      auto [ib, ib_alloc] = vk_create_buffer(vk_phys_dev,
                                             vk_device,
                                             buffer_size,
                                             vk::BufferUsageFlagBits::eIndexBuffer |
                                             vk::BufferUsageFlagBits::eTransferDst,
                                             device_arena,
                                             arena);
      index_buffer = ib;
      (void)ib_alloc;
      vk_copy_buffer(vk_device, command_pool, graphics_queue, staging_buffer, index_buffer, buffer_size);
    }
    return {vertex_buffer, index_buffer};
  };
  //~ Vertex + Index Buffers
  auto [vk_vertex_buffer, vk_index_buffer] = vk_create_geometry_buffers(vk_phys_dev,
                                                                        vk_device,
                                                                        &vk_host_arena,
                                                                        &vk_device_arena,
                                                                        &app_arena,
                                                                        vk_command_pool,
                                                                        vk_graphics_queue,
                                                                        vertices,
                                                                        indices);
  TransientBuffer debug_line_stream = vk_create_transient_buffer(vk_phys_dev,
                                                                 vk_device,
                                                                 &vk_host_arena,
                                                                 &app_arena,
                                                                 kb(64),
                                                                 vk::BufferUsageFlagBits::eVertexBuffer);

  // TransientBuffer arrows = vk_create_transient_buffer(vk_phys_dev,
  // vk_device,
  // &vk_host_arena,
  // &app_arena,
  // kb(64),
  // vk::BufferUsageFlagBits::eVertexBuffer |
  // vk::BufferUsageFlagBits::eStorageBuffer);

  // TODO: extract to create_uniform_descriptors(device, phys_dev, host_arena, tex_sampler, tex_image, ...) ->
  // {uniform_buffers, descriptor_pool, descriptor_sets, uniform_buffers_mapped}
  //~ Uniform Buffers + Descriptors
  Array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> vk_uniform_buffers{};
  Array<void *, MAX_FRAMES_IN_FLIGHT> vk_uniform_buffers_mapped{};
  vk::DescriptorPool vk_descriptor_pool{};

  Array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> vk_descriptor_sets{};

  // Array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> arrow_descriptor_sets{};
  {
    for (U64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vk::DeviceSize buffer_size = sizeof(UniformBufferObject);
      auto [ub, ub_alloc] =
          vk_create_buffer(vk_phys_dev,
                           vk_device,
                           buffer_size,
                           vk::BufferUsageFlagBits::eUniformBuffer,
                           &vk_host_arena,
                           &app_arena);
      vk_uniform_buffers[i] = ub;
      vk_uniform_buffers_mapped[i] = ub_alloc.mapped;
    }

    Array pool_size{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler,
                               .descriptorCount = MAX_FRAMES_IN_FLIGHT},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT}
    };
    vk::DescriptorPoolCreateInfo dpci{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                      .maxSets = 2 * MAX_FRAMES_IN_FLIGHT,
                                      .poolSizeCount = pool_size.size(),
                                      .pPoolSizes = pool_size};
    vk_descriptor_pool = app_arena.ds.push(vk_abort_if_error(vk_device.createDescriptorPoolUnique(dpci)));

    Array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> ubo_and_sample_layouts{};
    fill_array(ubo_and_sample_layouts, vk_ubo_and_sampler);

    vk::DescriptorSetAllocateInfo alloc_info{.descriptorPool = vk_descriptor_pool,
                                             .descriptorSetCount = ubo_and_sample_layouts.size(),
                                             .pSetLayouts = ubo_and_sample_layouts};
    vk_abort_if_error(vk_device.allocateDescriptorSets(&alloc_info, vk_descriptor_sets));

    // Array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> arrow_layouts{};
    // fill_array(arrow_layouts, arrow_dsl);
    // vk::DescriptorSetAllocateInfo arrow_alloc_info{
    // .descriptorPool = vk_descriptor_pool,
    // .descriptorSetCount = arrow_layouts.size(),
    // .pSetLayouts = arrow_layouts
    // };
    // vk_abort_if_error(vk_device.allocateDescriptorSets(&arrow_alloc_info, arrow_descriptor_sets));

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

  // clear vlaues
  vk::ClearValue vk_clear_color{};
  float rgba[4] = {0.0f, 0.00f, 0.0f, 1.0f};
  MemoryCopy(&vk_clear_color, rgba, sizeof(rgba));

  vk::ClearValue vk_clear_depth = {
      .depthStencil = {.depth = 1.0, .stencil = 0}
  };

  glfwSetInputMode(window
                   .
                   glfw_window,
                   GLFW_CURSOR,
                   GLFW_CURSOR_DISABLED
      );

  //~ Main Loop Setup
  {
    vk_sc_state.pool_gen = 0;
    vk_sc_state.needs_rebuild = false;
    vk_sc_state.image_index = 0;

    U64 absolute_frame_index = 0;
    U32 current_frame = 0;

    FrameClock clock{.target_ms = target_frame_ms};
    g_dbg_ctx.clock = &clock;

    using Clock = std::chrono::steady_clock;
    constexpr F32 TICK_RATE = 120.0f;
    constexpr F32 FIXED_DT = 1.0f / TICK_RATE;
    constexpr F32 MAX_FRAME_TIME = 0.25f; //  basically time until it will stop hitch
    constexpr U32 MAX_STEPS_PER_FRAME = static_cast<U32>(MAX_FRAME_TIME / FIXED_DT);
    auto prev_tick = Clock::now();
    F32 accumulator = 0.0f;

    //~ Main Loop
    while (!glfwWindowShouldClose(window.glfw_window)) {
      if (!monitor_detected) [[unlikely]] {
        find_target_ms(&target_frame_ms, window.glfw_window, &clock);
        monitor_detected = true;
      }
      clock.start();

      auto now = Clock::now();
      F32 frame_dt = std::chrono::duration<F32>(now - prev_tick).count();
      prev_tick = now;

      frame_dt = std::min(frame_dt, MAX_FRAME_TIME);
      accumulator += frame_dt;

      frame_arena->clear();
      glfwPollEvents();
      //~ Input
      window.handle_input();
      // FrameInput input = window.get_frame_input();


      //~ variable dt
      //~ Update state
      //~ fixed steps
      static F32 accumulated_time = 0.0f;
      local_persist Camera camera = Camera::from_pos(glm::vec3(2.0f));
      static F32 key_speed = 1.0f;

      {
        timespec sim_start{}, sim_end{};
        clock_gettime(CLOCK_MONOTONIC, &sim_start);
        U32 fixed_steps = 0;

        Input frame_input = window.get_frame_input(); // wont mutate outer input
        while (accumulator >= FIXED_DT && fixed_steps < MAX_STEPS_PER_FRAME) {
          {
            // fixed update stuff goes in this scope
            {
              Vec3<F32> move{};


              if (!palette_state.open) {
                if (frame_input.key.held[GLFW_KEY_W])
                  move.x += 1.0f;
                if (frame_input.key.held[GLFW_KEY_S])
                  move.x -= 1.0f;

                if (frame_input.key.held[GLFW_KEY_D])
                  move.y += 1.0f;
                if (frame_input.key.held[GLFW_KEY_A])
                  move.y -= 1.0f;

                if (frame_input.key.held[GLFW_KEY_SPACE])
                  move.z += 1.0f;
                if (frame_input.key.held[GLFW_KEY_LEFT_SHIFT])
                  move.z -= 1.0f;
              }


              //~ Camera
              constexpr F32 MOUSE_SENSITIVITY = 0.05f;
              g_dbg_ctx.camera = &camera;

              if (!paused) {
                accumulated_time += FIXED_DT;
                Vec3 rot{
                    frame_input.mouse.pos_delta.x * MOUSE_SENSITIVITY,
                    frame_input.mouse.pos_delta.y * MOUSE_SENSITIVITY,
                    0.0f // roll
                };
                frame_input.mouse.pos_delta = {0.0f, 0.0f};
                camera.transform(rot, move * (key_speed / 20.0f));
                key_speed += 0.05f * frame_input.mouse.scroll_delta.y;
                key_speed = clamp_bot(0.0005f, key_speed);
                frame_input.mouse.scroll_delta.y = 0.0f;
              }
            }
            accumulator -= FIXED_DT;
            ++fixed_steps;
          }
        }
        clock_gettime(CLOCK_MONOTONIC, &sim_end);
        bool hit_max_steps = (fixed_steps == MAX_STEPS_PER_FRAME) && (accumulator >= FIXED_DT);
        clock.submit_sim_sample(fixed_steps,
                                frame_dt,
                                FIXED_DT,
                                accumulator,
                                static_cast<F32>(diff_ms(sim_start, sim_end)),
                                hit_max_steps);
      }
      //~ Wait for fence and acquire
      vk_abort_if_error(vk_device.waitForFences(1, &vk_sc_config.in_flight_fences[current_frame], VK_TRUE, UINT64_MAX));


      VkResult vk_acquire_res = vkAcquireNextImageKHR(vk_device,
                                                      vk_sc_state.swapchain,
                                                      UINT64_MAX,
                                                      vk_sc_config.acquire_sems[current_frame],
                                                      VK_NULL_HANDLE,
                                                      &vk_sc_state.image_index);
      if (vk_recreate_swapchain_if_needed(vk_acquire_res, &vk_sc_config, &vk_sc_state, &app_arena)) {
        continue;
      }


      //~ Image-in-flight wait + fence reset
      {
        if (vk_sc_state.sc_res->images_in_flight[vk_sc_state.image_index]) {
          vk_abort_if_error(
              vk_device.waitForFences(1,
                                      &vk_sc_state.sc_res->images_in_flight[vk_sc_state.image_index],
                                      VK_TRUE,
                                      UINT64_MAX));
        }
      }
      vk_sc_state.sc_res->images_in_flight[vk_sc_state.image_index] = vk_sc_config.in_flight_fences[current_frame];
      vk_abort_if_error(vk_device.resetFences(1, &vk_sc_config.in_flight_fences[current_frame]));

      // Per frame
      {
        if (window.raw_key_input.held[GLFW_KEY_LEFT_CONTROL] && window.raw_key_input.pressed[
              GLFW_KEY_P]) {
          debug_ui_palette_toggle(&palette_state);
        }
      }

      //~ Render state stuff
      debug_line_stream.reset(current_frame);
      Array line_vertices = {
          LineVertex{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
          LineVertex{{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
          LineVertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
          LineVertex{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
          LineVertex{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
          LineVertex{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
      };
      TransientAlloc line_alloc = debug_line_stream.alloc(
          current_frame,
          line_vertices.byte_count(),
          alignof(LineVertex));

      MemoryCopy(line_alloc.mapped, line_vertices, sizeof(line_vertices));

      // procedural arrows :)
      // arrows.reset(current_frame);
      // Array current_arrows = {
      // Arrow{.origin = {1.0f, 1.0f, 1.0f}, .direction = {1.0f, 0.0f, 0.0f}, .magnitude = 5.0f, .width = 5.0f,
      // .roundness = 5.0f, .color = {1.0f, 0.0f, 0.0f, 1.0f}, .flags = 0}
      // };
      // TransientAlloc arrow_alloc = arrows.alloc(
      // current_frame,
      // current_arrows.byte_count(),
      // alignof(Arrow));
      // vk::DescriptorBufferInfo arrow_storage_info{
      // .buffer = arrow_alloc.buffer,
      // .offset = arrow_alloc.offset,
      // .range = current_arrows.byte_count(),};

      // MemoryCopy(arrow_alloc.mapped, current_arrows, sizeof(current_arrows));


      UniformBufferObject ubo{
          .model = glm::rotate(glm::mat4(1.0f),
                               accumulated_time * glm::radians(90.0f),
                               glm::vec3(0.0f, 0.0f, 1.0f)),
          .view = camera.view(),
          .proj =
          glm::perspective(glm::radians(45.0f),
                           static_cast<float>(vk_sc_state.extent.width) / static_cast<float>(
                             vk_sc_state.extent.height),
                           0.1f,
                           10.0f),
      };
      ubo.proj[1][1] *= -1;
      MemoryCopy(vk_uniform_buffers_mapped[current_frame], &ubo, sizeof(ubo));


      //~ Imgui UI
      {
        imgui_new_frame();
        debug_ui_palette_render(&palette_state);
        debug_ui_debug_ui(&clock.report);
      }

      //~ Begin command buffer
      vk::CommandBuffer vk_cmd = vk_sc_state.command_buffers[current_frame];
      vk_abort_if_error(vk_cmd.reset());
      vk_abort_if_error(vk_cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));

      vk::ImageLayout msaa_src = vk_sc_state.sc_res->image_initialized[vk_sc_state.image_index]
                                   ? vk::ImageLayout::eColorAttachmentOptimal
                                   : vk::ImageLayout::eUndefined;
      vk_transition_image_layout(vk_cmd,
                                 vk_sc_state.sc_res->msaa_images[vk_sc_state.image_index],
                                 msaa_src,
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 {},
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::ImageAspectFlagBits::eColor,
                                 1);
      vk::ImageLayout swapchain_src = vk_sc_state.sc_res->image_initialized[vk_sc_state.image_index]
                                        ? vk::ImageLayout::ePresentSrcKHR
                                        : vk::ImageLayout::eUndefined;
      vk_transition_image_layout(vk_cmd,
                                 vk_sc_state.sc_res->images[vk_sc_state.image_index],
                                 swapchain_src,
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 {},
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::ImageAspectFlagBits::eColor,
                                 1);
      vk_transition_image_layout(
          vk_cmd,
          vk_sc_state.depth.image,
          vk::ImageLayout::eUndefined,
          vk::ImageLayout::eDepthAttachmentOptimal,
          vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
          vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
          vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
          vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
          vk::ImageAspectFlagBits::eDepth,
          1);

      vk::RenderingAttachmentInfo vk_color_attachment{
          .imageView = vk_sc_state.sc_res->msaa_image_views[vk_sc_state.image_index],
          .imageLayout = vk::ImageLayout::eAttachmentOptimal,
          .resolveMode = vk::ResolveModeFlagBits::eAverage,
          .resolveImageView = vk_sc_state.sc_res->image_views[
            vk_sc_state.image_index],
          .resolveImageLayout = vk::ImageLayout::eAttachmentOptimal,
          .loadOp = vk::AttachmentLoadOp::eClear,
          .storeOp = vk::AttachmentStoreOp::eDontCare,
          .clearValue = {vk_clear_color}};
      vk::RenderingAttachmentInfo vk_depth_attachment = {.imageView = vk_sc_state.depth.image_view,
                                                         .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                                                         .loadOp = vk::AttachmentLoadOp::eClear,
                                                         .storeOp = vk::AttachmentStoreOp::eDontCare,
                                                         .clearValue = vk_clear_depth};
      vk::RenderingInfo vk_rendering_info{
          .renderArea = vk::Rect2D{{0, 0}, vk_sc_state.extent},
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
                                      static_cast<float>(vk_sc_state.extent.width),
                                      static_cast<float>(vk_sc_state.extent.height),
                                      0.0f,
                                      1.0f));
      vk_cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), vk_sc_state.extent));
      vk_cmd.bindVertexBuffers(0, vk_vertex_buffer, {0});
      vk_cmd.bindIndexBuffer(vk_index_buffer, 0, vk::IndexTypeValue<decltype(indices)::value_type>::value);
      vk_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                vk_ubo_and_sampler_layout,
                                0,
                                vk_descriptor_sets[current_frame],
                                nullptr);
      vk_cmd.drawIndexed(indices.size, 1, 0, 0, 0);


      vk_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, vk_line_pipeline);
      vk_cmd.setLineWidth(g_dbg_ctx.line_width);
      vk_cmd.bindVertexBuffers(0,
                               line_alloc.buffer,
                               {line_alloc.offset
                               });
      vk_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                vk_ubo_and_sampler_layout,
                                0,
                                vk_descriptor_sets[current_frame],
                                nullptr);
      vk_cmd.draw(line_vertices.size(), 1, 0, 0);

      //~ arrows
      // vk_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, vk_arrow_pipeline);
      // vk_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
      // arrow_layout,
      // 0,
      // arrow_descriptor_sets[current_frame],
      // nullptr);
      // vk_cmd.draw(Arrow::vertex_count_per_arrow(), current_arrows.size(), 0, 0);

      vk_cmd.endRendering();

      imgui_render(vk_cmd, vk_sc_state.sc_res->image_views[vk_sc_state.image_index], vk_sc_state.extent);


      vk_transition_image_layout(vk_cmd,
                                 vk_sc_state.sc_res->images[vk_sc_state.image_index],
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::ePresentSrcKHR,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 {},
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::PipelineStageFlagBits2::eBottomOfPipe,
                                 vk::ImageAspectFlagBits::eColor,
                                 1);
      vk_sc_state.sc_res->image_initialized[vk_sc_state.image_index] = true;
      vk_abort_if_error(vk_cmd.end());

      //~ vk submit

      vk::PipelineStageFlags vk_wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
      vk::SubmitInfo vk_submit_info{.waitSemaphoreCount = 1,
                                    .pWaitSemaphores = &vk_sc_config.acquire_sems[current_frame],
                                    .pWaitDstStageMask = &vk_wait_stage,
                                    .commandBufferCount = 1,
                                    .pCommandBuffers = &vk_cmd,
                                    .signalSemaphoreCount = 1,
                                    .pSignalSemaphores = &vk_sc_state.sc_res->render_finished_sems[vk_sc_state.
                                      image_index]};
      vk_abort_if_error(vk_graphics_queue.submit(1, &vk_submit_info, vk_sc_config.in_flight_fences[current_frame]));

      vk_present(&vk_sc_config, &vk_sc_state, vk_present_queue, &app_arena);

      clock.mark_work_done();
      {
        clock.wait_for_target();
      }

      absolute_frame_index++;
      current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
  }


  //~ Cleanup (reverse of creation order)
  {
    vk_abort_if_error(vk_device.waitIdle());
    imgui_shutdown();

    for (U32 i = 0; i < SC_MAX_GENERATIONS; ++i) {
      if (vk_sc_config.swapchain_pool[i].image_count > 0)
        vk_sc_config.swapchain_pool[i].sc_res_arena.drain();
    }
    vk_device.destroySwapchainKHR(vk_sc_state.swapchain);
#ifdef SD2_DEBUG
    if (vk_instance_result.debug_messenger) {
      auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          static_cast<VkInstance>(vk_instance),
          "vkDestroyDebugUtilsMessengerEXT");
      if (fn)
        fn(static_cast<VkInstance>(vk_instance), vk_instance_result.debug_messenger, nullptr);
    }
#endif
    {
      auto fn = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(
          static_cast<VkInstance>(vk_instance),
          "vkDestroySurfaceKHR");
      if (fn)
        fn(static_cast<VkInstance>(vk_instance), static_cast<VkSurfaceKHR>(vk_surface), nullptr);
    }
    app_arena.release();
    vk_destroy_gpu_arena(vk_device, &vk_sc_config.sc_arena);
    vk_destroy_gpu_arena(vk_device, &vk_device_arena);
    vk_destroy_gpu_arena(vk_device, &vk_host_arena);
    vk_device.destroy();
    vk_instance.destroy();

    // glfw
    glfwDestroyWindow(window.glfw_window);
    glfwTerminate();
    arena_release(frame_arena);
    thread_ctx_release();
  }
  save_debug_ui_state();
  return
      0;
}

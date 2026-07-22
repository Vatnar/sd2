#include <thread>
#include <fstream>
#include "../sd2_inc.hpp"

extern DebugCtx g_debug_ctx;

void glfw_error_callback(int ec, char const *desc) {
  printf("%d: %s\n", ec, desc);
}

internal AppWindow glfw_init_window(AppParams *app_params) {
  AppWindow res{};
  glfwSetErrorCallback(&glfw_error_callback);
  ASSERT_ALWAYS(glfwInit());
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  res.glfw_window = glfwCreateWindow(app_params->width, app_params->height, "sd2", nullptr, nullptr);
  GLFWwindow *glfw_window = res.glfw_window;

  glfwSetWindowUserPointer(glfw_window, &res);
  glfwSetFramebufferSizeCallback(glfw_window, AppWindow::dispatch_resize);
  glfwSetScrollCallback(glfw_window, AppWindow::dispatch_scroll);
  glfwSetWindowCloseCallback(glfw_window, AppWindow::dispatch_close);
  glfwSetKeyCallback(glfw_window, AppWindow::key_callback);
  glfwSetMouseButtonCallback(glfw_window, AppWindow::mouse_button_callback);
  glfwSetCursorPosCallback(glfw_window, AppWindow::cursor_callback);
  glfwSetWindowRefreshCallback(glfw_window, AppWindow::dispatch_refresh);
  glfwSetCharCallback(glfw_window, AppWindow::dispatch_char);
  return res;
}

String8 read_file_str8(Arena *arena, String8 filename) {
  TempScope scratch = scratch_begin_scoped(&arena, 1);
  // TODO: Should this read as binary or not?
  std::ifstream file(filename.c_str(arena), std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    ASSERT(!"Failed to open file");
    return {};
  }

  std::streampos const BUFFER_SIZE = file.tellg();
  U8 *buffer = arena->push_array<U8>(file.tellg());
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char *>(buffer), BUFFER_SIZE);
  file.close();

  return str8(buffer, BUFFER_SIZE);
}

SPIRVBlob read_spirv_blob(Arena *arena, String8 filename) {
  TempScope scratch = scratch_begin_scoped(&arena, 1);
  SPIRVBlob res{};
  std::ifstream file(filename.c_str(scratch), std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    ASSERT(!"Failed to open file");
    return {};
  }

  std::streampos const BUFFER_SIZE = file.tellg();
  ASSERT(BUFFER_SIZE % 4 == 0);
  char *bytes = arena->push_array<char>(file.tellg());
  file.seekg(0, std::ios::beg);
  file.read(bytes, BUFFER_SIZE);
  file.close();
  res.words = reinterpret_cast<U32 *>(bytes);
  res.byte_count = BUFFER_SIZE;
  res.word_count = BUFFER_SIZE / 8;
  return res;
}

void write_file(String8 filename, void *mem, U64 offset, U64 length) {
  if (filename.str == nullptr || filename.size == 0) {
    ASSERT(!"Invalid name");
  }

  if (mem == nullptr && length != 0) {
    ASSERT(!"Nulll memory with non zero length");
  }

  std::ofstream file(
      reinterpret_cast<char const *>(filename.str),
      std::ios::binary | std::ios::out | std::ios::trunc
      );

  if (!file.is_open()) {
    ASSERT(!"Failed to open file");
  }

  char const *bytes = static_cast<char const *>(mem) + offset;
  file.write(bytes, static_cast<std::streamsize>(length));

  if (!file.good()) {
    ASSERT(!"Failed to write file");
  }
}

FORCE_INLINE void FrameClock::submit_sim_sample(U32 fixed_steps,
                                                F32 frame_dt,
                                                F32 fixed_dt,
                                                F32 accumulator,
                                                F32 sim_work_ms,
                                                bool hit_max_steps) {
  F32 new_frame_dt_ms = frame_dt * 1000.0f;
  F32 new_fixed_dt_ms = fixed_dt * 1000.0f;
  F32 new_fixed_hz_target = fixed_dt > 0.0f ? (1.0f / fixed_dt) : 0.0f;
  F32 new_fixed_hz_actual = frame_dt > 0.0f ? (static_cast<F32>(fixed_steps) / frame_dt) : 0.0f;
  F32 new_sim_ms = static_cast<F32>(fixed_steps) * new_fixed_dt_ms;
  F32 new_steps_per_frame = static_cast<F32>(fixed_steps);
  F32 new_accumulator_ms = accumulator * 1000.0f;
  F32 new_accumulator_alpha = fixed_dt > 0.0f ? (accumulator / fixed_dt) : 0.0f;
  F32 new_dropped_ms = (hit_max_steps && accumulator >= fixed_dt) ? (accumulator * 1000.0f) : 0.0f;

  report.fixed_dt_ms = new_fixed_dt_ms;
  report.fixed_hz_target = new_fixed_hz_target;
  report.hit_max_steps = hit_max_steps;

  static bool first_sim_report = true;
  if (first_sim_report) {
    report.frame_dt_ms = new_frame_dt_ms;
    report.fixed_hz_actual = new_fixed_hz_actual;
    report.sim_ms = new_sim_ms;
    report.sim_work_ms = sim_work_ms;
    report.steps_per_frame = new_steps_per_frame;
    report.accumulator_ms = new_accumulator_ms;
    report.accumulator_alpha = new_accumulator_alpha;
    report.dropped_ms = new_dropped_ms;
    first_sim_report = false;
    return;
  }

  report.frame_dt_ms = ema_update(report.frame_dt_ms, new_frame_dt_ms, alpha);
  report.fixed_hz_actual = ema_update(report.fixed_hz_actual, new_fixed_hz_actual, alpha);
  report.sim_ms = ema_update(report.sim_ms, new_sim_ms, alpha);
  report.sim_work_ms = ema_update(report.sim_work_ms, sim_work_ms, alpha);
  report.steps_per_frame = ema_update(report.steps_per_frame, new_steps_per_frame, alpha);
  report.accumulator_ms = ema_update(report.accumulator_ms, new_accumulator_ms, alpha);
  report.accumulator_alpha = ema_update(report.accumulator_alpha, new_accumulator_alpha, alpha);
  report.dropped_ms = ema_update(report.dropped_ms, new_dropped_ms, alpha);
}

void FrameClock::wait_for_target() {
  do {
    clock_gettime(CLOCK_MONOTONIC, &frame_end);
    F64 remaining = target_ms - diff_ms(frame_start, frame_end);
    if (remaining > 0.3) {
      std::this_thread::sleep_for(std::chrono::duration<F64, std::milli>(remaining - 0.2));
    }
  } while (diff_ms(frame_start, frame_end) < target_ms);
  write_report();
}

FORCE_INLINE void FrameClock::write_report() {
  F32 new_target = static_cast<F32>(target_ms);
  F32 new_total = static_cast<F32>(total_ms());
  F32 new_wait = static_cast<F32>(wait_ms());
  F32 new_work = static_cast<F32>(work_ms());
  F32 new_fps = new_total > 0.0f ? (1000.0f / new_total) : 0.0f;
  F32 new_tfps = new_target > 0.0f ? (1000.0f / new_target) : 0.0f;

  report.target_ms = new_target;
  report.target_fps = new_tfps;
  report.alpha = alpha;

  static bool first_report = true;
  if (first_report) {
    report.total_ms = new_total;
    report.wait_ms = new_wait;
    report.work_ms = new_work;
    report.fps = new_fps;
    report.sim_utilization = report.total_ms > 0.0f ? (report.sim_work_ms / report.total_ms) : 0.0f;
    first_report = false;
    return;
  }

  report.total_ms = ema_update(report.total_ms, new_total, alpha);
  report.wait_ms = ema_update(report.wait_ms, new_wait, alpha);
  report.work_ms = ema_update(report.work_ms, new_work, alpha);
  report.fps = ema_update(report.fps, new_fps, alpha);
  report.sim_utilization = report.total_ms > 0.0f ? (report.sim_work_ms / report.total_ms) : 0.0f;
}

internal U32 glfw_get_required_extensions(char const **out_extensions, U32 max_extensions) {
  U32 glfw_count;
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


void Camera::transform(Vec3<F32> rotation, Vec3<F32> translation) {
  // TODO: z translate
  yaw -= glm::radians(rotation.x);
  pitch -= glm::radians(rotation.y);
  pitch = clamp(-glm::pi<F32>() / 2.0f + 0.05f, pitch, glm::pi<F32>() / 2.0f - 0.05f);

  glm::vec3 front{};
  front.x = glm::cos(yaw) * glm::cos(pitch);
  front.y = glm::sin(yaw) * glm::cos(pitch);
  front.z = glm::sin(pitch);
  camera_front = glm::normalize(front);

  camera_pos += camera_front * translation.x;
  camera_pos += right() * translation.y;
  camera_pos += up() * translation.z;
}

glm::vec3 Camera::right() const {
  return glm::normalize(glm::cross(camera_front, world_up()));
}

glm::vec3 Camera::up() const {
  return glm::normalize(glm::cross(right(), camera_front));
}

glm::mat4 Camera::view() const {
  return glm::lookAt(camera_pos, camera_pos + camera_front, world_up());
}

Camera Camera::from_pos(glm::vec3 pos) {
  Camera res{};
  res.camera_pos = pos;
  res.camera_front = glm::normalize(glm::vec3(0.0f) - pos);
  res.pitch = glm::asin(res.camera_front.z);
  res.yaw = glm::atan(res.camera_front.y, res.camera_front.x);

  return res;
}

void toggle_cursor() {
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

void show_cursor() {
  glfwSetInputMode(g_dbg_ctx.window->glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void hide_cursor() {
  glfwSetInputMode(g_dbg_ctx.window->glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void toggle_fullscreen() {
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

internal std::tuple<DynArray<TextureVertex>, DynArray<U32>> load_obj(Arena *arena, const char *obj_path) {
  DynArray<TextureVertex> vertices{};
  DynArray<U32> indices{};
  std::vector<TextureVertex> temp_verts{};
  std::vector<U32> temp_indices{};
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;
  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, obj_path)) {
    TRAP();
  }
  std::unordered_map<TextureVertex, U32> unique_vertices{};
  for (auto const &shape : shapes) {
    for (auto const &index : shape.mesh.indices) {
      TextureVertex vertex{
          .pos = {attrib.vertices[3 * index.vertex_index + 0],
                  attrib.vertices[3 * index.vertex_index + 1],
                  attrib.vertices[3 * index.vertex_index + 2]},
          .color = {1.0f, 1.0f, 1.0f},
          .tex_coord = {
              attrib.texcoords[2 * index.texcoord_index + 0],
              1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
          }
      };
      auto [it, inserted] = unique_vertices.insert({vertex, static_cast<U32>(temp_verts.size())});
      if (inserted) {
        temp_verts.push_back(vertex);
      }
      temp_indices.push_back(it->second);
    }
  }
  vertices.capacity = vertices.size = temp_verts.size();
  vertices.data = arena->push_array<decltype(vertices)::value_type>(vertices.capacity);
  for (U64 i = 0; i < temp_verts.size(); i++) {
    vertices.data[i] = temp_verts[i];
  }
  indices.capacity = indices.size = temp_indices.size();
  indices.data = arena->push_array<decltype(indices)::value_type>(indices.capacity);
  for (U64 i = 0; i < temp_indices.size(); i++) {
    indices.data[i] = temp_indices[i];
  }
  return {vertices, indices};
}

constexpr glm::vec3 Camera::world_up() {
  return {0.0f, 0.0f, 1.0f};
}

internal void find_target_ms(F64 *target_frame_ms, GLFWwindow *glfw_window, FrameClock *clock) {
  int wx, wy, ww, wh;
  glfwGetWindowPos(glfw_window, &wx, &wy);
  glfwGetWindowSize(glfw_window, &ww, &wh);
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
  *target_frame_ms = 1000.0 / monitor_hz;
  clock->target_ms = *target_frame_ms;
  printf("Monitor: %u Hz (target: %.3f ms/frame)\n", monitor_hz, *target_frame_ms);
}

#include <thread>
#include <fstream>
#include "../sd2_inc.hpp"

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

void FrameClock::write_report() {
  F32 new_target = static_cast<F32>(target_ms);
  F32 new_total = static_cast<F32>(total_ms());
  F32 new_wait = static_cast<F32>(wait_ms());
  F32 new_work = static_cast<F32>(work_ms());
  F32 new_fps = static_cast<F32>(1000.0f / total_ms());
  F32 new_tfps = static_cast<F32>(1000.0f / target_ms);

  report.target_ms = new_target;
  report.target_fps = new_tfps;
  report.alpha = alpha;

  static bool first_report = true;
  if (first_report) {
    report.total_ms = new_total;
    report.wait_ms = new_wait;
    report.work_ms = new_work;
    report.fps = new_fps;
    first_report = false;
    return;
  }
  // Blend the history with the new data scaled by recency
  report.total_ms = ema_update(report.total_ms, new_total, alpha);
  report.wait_ms = ema_update(report.wait_ms, new_wait, alpha);
  report.work_ms = ema_update(report.work_ms, new_work, alpha);
  report.fps = ema_update(report.fps, new_fps, alpha);
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

internal void handle_key_input(Key *input) {
  auto current = &input->current;
  auto previous = &input->previous;
  auto pressed = &input->pressed;
  auto released = &input->released;
  auto held = &input->held;
  for (U64 i = 0; i < current->size() >> 6; ++i) {
    U64 cur = current->bits[i];
    U64 prev = previous->bits[i];

    pressed->bits[i] = cur & ~prev;
    held->bits[i] = cur & prev;
    released->bits[i] = ~cur & prev;
    previous->bits[i] = cur;
  }
}

internal void handle_mouse_input(Mouse *mouse) {
  auto *current = &mouse->current;
  auto *previous = &mouse->previous;
  auto *pressed = &mouse->pressed;
  auto *released = &mouse->released;
  auto *held = &mouse->held;

  uint64_t m_cur = current->bits[0];
  uint64_t m_prev = previous->bits[0];

  pressed->bits[0] = m_cur & ~m_prev;
  held->bits[0] = m_cur & m_prev;
  released->bits[0] = ~m_cur & m_prev;

  previous->bits[0] = m_cur;

  mouse->delta_pos = mouse->current_pos - mouse->previous_pos;
  mouse->previous_pos = mouse->current_pos;

  mouse->delta_scroll = mouse->current_scroll;
  mouse->current_scroll = {.x = 0.0, .y = 0.0};
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

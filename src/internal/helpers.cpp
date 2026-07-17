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
  glfwSetKeyCallback(glfw_window, AppWindow::dispatch_key);
  glfwSetScrollCallback(glfw_window, AppWindow::dispatch_scroll);
  glfwSetCursorPosCallback(glfw_window, AppWindow::dispatch_cursor);
  glfwSetMouseButtonCallback(glfw_window, AppWindow::dispatch_mouse_button);
  glfwSetWindowCloseCallback(glfw_window, AppWindow::dispatch_close);
  glfwSetWindowRefreshCallback(glfw_window, AppWindow::dispatch_refresh);
  glfwSetCharCallback(glfw_window, AppWindow::dispatch_char);
  return res;
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

internal std::tuple<DynArray<Vertex>, DynArray<U32>> load_obj(Arena *arena, const char *obj_path) {
  DynArray<Vertex> vertices{};
  DynArray<U32> indices{};
  std::vector<Vertex> temp_verts{};
  std::vector<U32> temp_indices{};
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;
  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, obj_path)) {
    TRAP();
  }
  std::unordered_map<Vertex, U32> unique_vertices{};
  for (auto const &shape : shapes) {
    for (auto const &index : shape.mesh.indices) {
      Vertex vertex{
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

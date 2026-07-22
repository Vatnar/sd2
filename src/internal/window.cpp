#include "../sd2_inc.hpp"

Input AppWindow::get_frame_input() const {
  return {
      {
          .pressed = raw_key_input.pressed,
          .released = raw_key_input.released,
          .held = raw_key_input.held,
      },
      {
          .pressed = raw_mouse_input.pressed,
          .released = raw_mouse_input.released,
          .held = raw_mouse_input.held,

          .pos_absolute = raw_mouse_input.current_pos,
          .pos_delta = raw_mouse_input.delta_pos,
          .scroll_delta = raw_mouse_input.delta_scroll,
      }
  };
}

void AppWindow::handle_key_input() {
  auto current = &raw_key_input.current;
  auto previous = &raw_key_input.previous;
  auto pressed = &raw_key_input.pressed;
  auto released = &raw_key_input.released;
  auto held = &raw_key_input.held;
  for (U64 i = 0; i < current->size() >> 6; ++i) {
    U64 cur = current->bits[i];
    U64 prev = previous->bits[i];

    pressed->bits[i] = cur & ~prev;
    held->bits[i] = cur;
    released->bits[i] = ~cur & prev;
    previous->bits[i] = cur;
  }
}

void AppWindow::handle_mouse_input() {
  auto *current = &raw_mouse_input.current;
  auto *previous = &raw_mouse_input.previous;
  auto *pressed = &raw_mouse_input.pressed;
  auto *released = &raw_mouse_input.released;
  auto *held = &raw_mouse_input.held;

  uint64_t m_cur = current->bits[0];
  uint64_t m_prev = previous->bits[0];

  pressed->bits[0] = m_cur & ~m_prev;
  held->bits[0] = m_cur;
  released->bits[0] = ~m_cur & m_prev;

  previous->bits[0] = m_cur;

  raw_mouse_input.delta_pos = raw_mouse_input.current_pos - raw_mouse_input.previous_pos;
  raw_mouse_input.previous_pos = raw_mouse_input.current_pos;

  raw_mouse_input.delta_scroll = raw_mouse_input.current_scroll;
  raw_mouse_input.current_scroll = {.x = 0.0, .y = 0.0};
}

void AppWindow::dispatch_resize(GLFWwindow *glfw_window, S32 width, S32 height) {
  AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
  window->framebuffer_resized = true;
}

void AppWindow::dispatch_scroll(GLFWwindow *glfw_window, F64 xoffset, F64 yoffset) {
  AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
  window->raw_mouse_input.current_scroll += {.x = static_cast<F32>(xoffset), .y = static_cast<F32>(yoffset)};
}

void AppWindow::dispatch_close(GLFWwindow *glfw_window) {
  glfwSetWindowShouldClose(glfw_window, true);
}

void AppWindow::key_callback(GLFWwindow *glfw_window, int key, int scancode, int action, int mods) {
  if (key < 0 || key >= GLFW_KEY_LAST)
    return;
  AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
  RawKeyInput *key_input = &window->raw_key_input;
  if (action == GLFW_PRESS) {
    key_input->current.set(key);
  } else if (action == GLFW_RELEASE) {
    key_input->current.clear(key);
  }
}

void AppWindow::mouse_button_callback(GLFWwindow *glfw_window, int button, int action, int mods) {
  if (button < 0 || button >= GLFW_MOUSE_BUTTON_LAST)
    return;


  AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
  RawMouseInput *mouse_input = &window->raw_mouse_input;

  if (action == GLFW_PRESS) {
    mouse_input->current.set(button);
  } else if (action == GLFW_RELEASE) {
    mouse_input->current.clear(button);
  }
}

void AppWindow::cursor_callback(GLFWwindow *glfw_window, F64 xpos, F64 ypos) {
  AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
  window->raw_mouse_input.current_pos = {.x = static_cast<F32>(xpos), .y = static_cast<F32>(ypos)};
}

void AppWindow::dispatch_refresh(GLFWwindow *glfw_window) {
  // NOTE: For now we render every frame anyways so we just ignore this. However, in the future if we have places
  //  where we dont redraw for some reason. This needs to explicitly force a draw. Probably in some condition like
  //  `if (request_frame || something_changed) -> do drawing`
}

void AppWindow::dispatch_char(GLFWwindow *glfw_window, U32 keycode) {
  // TODO: figure out how to handle this..
}

#pragma once

struct AppParams {
  String8 name{};
  S32 width{};
  S32 height{};
};

struct Key {
  BitSet<GLFW_KEY_LAST> pressed{};
  BitSet<GLFW_KEY_LAST> released{};
  BitSet<GLFW_KEY_LAST> held{};

  BitSet<GLFW_KEY_LAST> previous{};
  BitSet<GLFW_KEY_LAST> current{};
};

struct Mouse {
  BitSet<GLFW_MOUSE_BUTTON_LAST> pressed{};
  BitSet<GLFW_MOUSE_BUTTON_LAST> released{};
  BitSet<GLFW_MOUSE_BUTTON_LAST> held{};

  BitSet<GLFW_MOUSE_BUTTON_LAST> previous{};
  BitSet<GLFW_MOUSE_BUTTON_LAST> current{};

  Vec2<F64> current_pos{};
  Vec2<F64> previous_pos{};
  Vec2<F64> delta_pos{};

  Vec2<F64> current_scroll{};
  Vec2<F64> delta_scroll{};
};

struct AppWindow {
  GLFWwindow *glfw_window{};
  bool fullscreen = false;
  int windowed_x = 0;
  int windowed_y = 0;
  int windowed_w = 800;
  int windowed_h = 600;
  bool framebuffer_resized = false;

  Key key{};
  Mouse mouse{};

  Vec2<F64> scroll_delta_pending{};
  // TODO: text char callback

  static void dispatch_resize(GLFWwindow *glfw_window, S32 width, S32 height) {
    AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
    window->framebuffer_resized = true;
  }

  static void dispatch_scroll(GLFWwindow *glfw_window, F64 xoffset, F64 yoffset) {
    AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
    window->mouse.current_scroll += {.x = xoffset, .y = yoffset};
  }

  static void dispatch_close(GLFWwindow *glfw_window) {
    glfwSetWindowShouldClose(glfw_window, true);
  }

  static void key_callback(GLFWwindow *glfw_window, int key, int scancode, int action, int mods) {
    if (key < 0 || key >= GLFW_KEY_LAST)
      return;
    AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
    Key *key_input = &window->key;
    if (action == GLFW_PRESS) {
      key_input->current.set(key);
    } else if (action == GLFW_RELEASE) {
      key_input->current.clear(key);
    }
  }

  static void mouse_button_callback(GLFWwindow *glfw_window, int button, int action, int mods) {
    if (button < 0 || button >= GLFW_MOUSE_BUTTON_LAST)
      return;


    AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
    Mouse *mouse_input = &window->mouse;

    if (action == GLFW_PRESS) {
      mouse_input->current.set(button);
    } else if (action == GLFW_RELEASE) {
      mouse_input->current.clear(button);
    }
  }

  static void cursor_callback(GLFWwindow *glfw_window, F64 xpos, F64 ypos) {
    AppWindow *window = static_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
    window->mouse.current_pos = {.x = xpos, .y = ypos};
  }


  static void dispatch_refresh(GLFWwindow *glfw_window) {
    // NOTE: For now we render every frame anyways so we just ignore this. However, in the future if we have places
    //  where we dont redraw for some reason. This needs to explicitly force a draw. Probably in some condition like
    //  `if (request_frame || something_changed) -> do drawing`
  }

  static void dispatch_char(GLFWwindow *glfw_window, U32 keycode) {
    // TODO: figure out how to handle this..
  }
};

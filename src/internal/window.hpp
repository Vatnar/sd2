#pragma once
#include "base_string.hpp"

struct AppParams {
  String8 name{};
  S32 width{};
  S32 height{};
};

struct RawKeyInput {
  BitSet<GLFW_KEY_LAST> pressed{};
  BitSet<GLFW_KEY_LAST> released{};
  BitSet<GLFW_KEY_LAST> held{};

  BitSet<GLFW_KEY_LAST> previous{};
  BitSet<GLFW_KEY_LAST> current{};
};

struct RawMouseInput {
  BitSet<GLFW_MOUSE_BUTTON_LAST> pressed{};
  BitSet<GLFW_MOUSE_BUTTON_LAST> released{};
  BitSet<GLFW_MOUSE_BUTTON_LAST> held{};

  BitSet<GLFW_MOUSE_BUTTON_LAST> previous{};
  BitSet<GLFW_MOUSE_BUTTON_LAST> current{};

  Vec2<F32> current_pos{};
  Vec2<F32> previous_pos{};
  Vec2<F32> delta_pos{};

  Vec2<F32> current_scroll{};
  Vec2<F32> delta_scroll{};
};

struct KeyInput {
  BitSet<GLFW_KEY_LAST> pressed{};
  BitSet<GLFW_KEY_LAST> released{};
  BitSet<GLFW_KEY_LAST> held{};
};

struct MouseInput {
  BitSet<GLFW_MOUSE_BUTTON_LAST> pressed{};
  BitSet<GLFW_MOUSE_BUTTON_LAST> released{};
  BitSet<GLFW_MOUSE_BUTTON_LAST> held{};
  Vec2<F32> pos_absolute{};
  Vec2<F32> pos_delta{};
  Vec2<F32> scroll_delta{};
};

struct Input {
  KeyInput key{};
  MouseInput mouse{};
  // GamepadInput gamepad{}; // TODO:
};

struct AppWindow {
  GLFWwindow *glfw_window{};
  bool fullscreen = false;
  int windowed_x = 0;
  int windowed_y = 0;
  int windowed_w = 800;
  int windowed_h = 600;
  bool framebuffer_resized = false;

  RawKeyInput raw_key_input{};
  RawMouseInput raw_mouse_input{};


  // should only be called once per frame
  void handle_input() {
    handle_key_input();
    handle_mouse_input();
  }

  [[nodiscard]] Input get_frame_input() const;


  void handle_key_input();
  void handle_mouse_input();

  //~ window-glfw callbacks
  static void dispatch_resize(GLFWwindow *glfw_window, S32 width, S32 height);
  static void dispatch_scroll(GLFWwindow *glfw_window, F64 xoffset, F64 yoffset);
  static void dispatch_close(GLFWwindow *glfw_window);
  static void key_callback(GLFWwindow *glfw_window, int key, int scancode, int action, int mods);
  static void mouse_button_callback(GLFWwindow *glfw_window, int button, int action, int mods);
  static void cursor_callback(GLFWwindow *glfw_window, F64 xpos, F64 ypos);
  static void dispatch_refresh(GLFWwindow *glfw_window);
  static void dispatch_char(GLFWwindow *glfw_window, U32 keycode);
};

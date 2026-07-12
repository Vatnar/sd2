#pragma once
enum class WindowEventType {
  NONE,
  RESIZE,
  KEY,
  SCROLL,
  CURSOR,
  MBUTTON,
  CLOSE,
  REFRESH,
  TEXT
};
struct WResizeEvent {
  S32 width, height;
};
struct WKeyEvent {
  S32 key, scancode, action, mods;
};
struct WScrollEvent {
  F64 xoffset, yoffset;
};
struct WCursorEvent {
  F64 xpos, ypos;
};
struct WMButtonEvent {
  S32 button, action, mods;
};
struct WTextEvent {
  U32 keycode;
};

struct WindowEvent {
  WindowEventType type;
  union {
    WResizeEvent  resize;
    WKeyEvent     key;
    WScrollEvent  scroll;
    WCursorEvent  cursor;
    WMButtonEvent mbutton;
    WTextEvent    text;
  };
};


struct Window {
  GLFWwindow *glfw_window{};
  // holds a ptr to the event system which callbacks can use
  bool                    framebuffer_resized = false;
  RingBuffer<WindowEvent> events;

  static void dispatch_resize(GLFWwindow *glfw_window, S32 width, S32 height) {
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    window->events.push(WindowEvent{
        .type   = WindowEventType::RESIZE,
        .resize = {width, height}
    });
  }
  static void dispatch_key(GLFWwindow *glfw_window, S32 key, S32 scancode, S32 action, S32 mods) {
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    window->events.push({
        .type = WindowEventType::KEY,
        .key  = {key, scancode, action, mods}
    });
  }
  static void dispatch_scroll(GLFWwindow *glfw_window, F64 xoffset, F64 yoffset) {
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    window->events.push({
        .type   = WindowEventType::SCROLL,
        .scroll = {xoffset, yoffset}
    });
  }
  static void dispatch_cursor(GLFWwindow *glfw_window, F64 xpos, F64 ypos) {
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    if (!window->events.empty()) {
      WindowEvent &last = window->events.get_last();
      if (last.type == WindowEventType::CURSOR) {
        last.cursor = {xpos, ypos};
        return;
      }
    }
    window->events.push({
        .type   = WindowEventType::CURSOR,
        .cursor = {xpos, ypos}
    });
  }
  static void dispatch_mouse_button(GLFWwindow *glfw_window, S32 button, S32 action, S32 mods) {
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    window->events.push({
        .type    = WindowEventType::MBUTTON,
        .mbutton = {button, action, mods}
    });
  }

  static void dispatch_close(GLFWwindow *glfw_window) {
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    window->events.push({.type = WindowEventType::CLOSE, .resize = {}});
  }
  static void dispatch_refresh(GLFWwindow *glfw_window) {
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    window->events.push({.type = WindowEventType::REFRESH, .resize = {}});
  }
  static void dispatch_char(GLFWwindow *glfw_window, U32 keycode) {
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    window->events.push({.type = WindowEventType::TEXT, .text = {keycode}});
  }
};

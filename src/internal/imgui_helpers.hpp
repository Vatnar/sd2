#pragma once

struct ImGuiInitParams {
  vk::Instance instance;
  vk::PhysicalDevice phys_dev;
  vk::Device device;
  VkFormat color_format;
  vk::Queue graphics_queue;
  GLFWwindow *glfw_window;
  U32 graphics_queue_family;
  U32 image_count;
  U32 min_image_count;
};

internal void imgui_init(ImGuiInitParams params);
internal void imgui_new_frame();
internal void imgui_render(vk::CommandBuffer cmd, vk::ImageView target_image_view, vk::Extent2D extent);
internal void imgui_shutdown();
internal void imgui_set_min_image_count(U32 count);
internal void imgui_setup_theme();

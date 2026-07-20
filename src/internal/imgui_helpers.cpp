#include "../sd2_inc.hpp"
#include "imgui_helpers.hpp"
#include <imgui.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

static void *imgui_alloc(size_t size, void *) {
  void *ptr = malloc(size);
#ifdef IMGUI_TRACE_ALLOC
  fprintf(stderr, "[imgui] alloc %zu -> %p\n", size, ptr);
#endif
  return ptr;
}

static void imgui_free(void *ptr, void *) {
#ifdef IMGUI_TRACE_ALLOC
  fprintf(stderr, "[imgui] free %p\n", ptr);
#endif
  free(ptr);
}

static void imgui_check_vk_result(VkResult err) {
  if (err != VK_SUCCESS)
    vk_abort_if_error(static_cast<vk::Result>(err));
}

internal void imgui_init(ImGuiInitParams params) {
  IMGUI_CHECKVERSION();
  ImGui::SetAllocatorFunctions(imgui_alloc, imgui_free);
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForVulkan(params.glfw_window, true);

  VkPipelineRenderingCreateInfoKHR pipeline_rendering_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &params.color_format,
  };

  ImGui_ImplVulkan_InitInfo init_info{
      .ApiVersion = VK_API_VERSION_1_4,
      .Instance = static_cast<VkInstance>(params.instance),
      .PhysicalDevice = static_cast<VkPhysicalDevice>(params.phys_dev),
      .Device = static_cast<VkDevice>(params.device),
      .QueueFamily = params.graphics_queue_family,
      .Queue = static_cast<VkQueue>(params.graphics_queue),
      .DescriptorPool = VK_NULL_HANDLE,
      .DescriptorPoolSize = 64,
      .MinImageCount = params.min_image_count,
      .ImageCount = params.image_count,
      .PipelineInfoMain = {
          .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
          .PipelineRenderingCreateInfo = pipeline_rendering_info,
      },
      .UseDynamicRendering = true,
      .CheckVkResultFn = imgui_check_vk_result,
  };
  ImGui_ImplVulkan_Init(&init_info);
}

internal void imgui_new_frame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

internal void imgui_render(vk::CommandBuffer cmd, vk::ImageView target_image_view, vk::Extent2D extent) {
  ImGui::Render();
  vk::RenderingAttachmentInfo color_attachment{
      .imageView = target_image_view,
      .imageLayout = vk::ImageLayout::eAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eLoad,
      .storeOp = vk::AttachmentStoreOp::eStore,
  };
  vk::RenderingInfo render_info{
      .renderArea = vk::Rect2D{{0, 0}, extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment,
  };
  cmd.beginRendering(render_info);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd));
  cmd.endRendering();
}

internal void imgui_shutdown() {
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

internal void imgui_set_min_image_count(U32 count) {
  ImGui_ImplVulkan_SetMinImageCount(count);
}

internal void imgui_setup_theme() {
  ImGuiStyle &s = ImGui::GetStyle();
  ImVec4 *c = s.Colors;
  c[ImGuiCol_WindowBg] = {0.04f, 0.04f, 0.04f, 1.00f};
  c[ImGuiCol_FrameBg] = {0.07f, 0.07f, 0.07f, 1.00f};
  c[ImGuiCol_FrameBgHovered] = {0.11f, 0.11f, 0.11f, 1.00f};
  c[ImGuiCol_Text] = {0.88f, 0.88f, 0.88f, 1.00f};
  c[ImGuiCol_TextDisabled] = {0.35f, 0.35f, 0.35f, 1.00f};
  c[ImGuiCol_Header] = {0.18f, 0.18f, 0.18f, 1.00f};
  c[ImGuiCol_HeaderHovered] = {0.24f, 0.24f, 0.24f, 1.00f};
  c[ImGuiCol_Border] = {0.08f, 0.08f, 0.08f, 1.00f};
  c[ImGuiCol_PopupBg] = {0.04f, 0.04f, 0.04f, 1.00f};
  c[ImGuiCol_ScrollbarBg] = {0.03f, 0.03f, 0.03f, 1.00f};
  c[ImGuiCol_ScrollbarGrab] = {0.15f, 0.15f, 0.15f, 1.00f};
  c[ImGuiCol_Button] = {0.07f, 0.07f, 0.07f, 1.00f};
  c[ImGuiCol_ButtonHovered] = {0.15f, 0.15f, 0.15f, 1.00f};
  c[ImGuiCol_SliderGrab] = {0.35f, 0.35f, 0.35f, 1.00f};
  s.FrameRounding = 2.0f;
  s.WindowRounding = 4.0f;
  s.GrabRounding = 2.0f;
}

// TODO, rather have a tostring
internal void imgui_draw_glm_vec32f(const char *label, glm::vec3 vec) {
  ImGui::Text("%s: { %.2f, %.2f, %.2f }",
              label,
              vec.x,
              vec.y,
              vec.z);
}

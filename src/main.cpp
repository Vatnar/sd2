#include <cstdio>
#include <cstring>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

// glfw and vulkan

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS         1
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS  1
#define VULKAN_HPP_NO_EXCEPTIONS           1

#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN 1
#include <GLFW/glfw3.h>
#include <bits/shared_ptr.h>

#include "internal/base.hpp"
#include "internal/base_types.hpp"
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
static vk::detail::DynamicLoader g_vulkan_dynamic_loader{};

// sd2 hpp

#include "internal/main2.hpp"
#define SD2_DEBUG                  1

#define MemoryCopy(dst, src, size) __builtin_memmove((dst), (src), (size))

// sd2 cpp


struct String8 {
  U8 *str;
  U64 size;
};
String8 str8(U8 *str, U64 size) {
  String8 result{str, size};
  return result;
}
#define str8_lit(S) str8((U8 *)(S), sizeof(S) - 1)

#include "arena.cpp"
#include "internal/thread_ctx.hpp"
#include "main2.cpp"


struct AppParams {
  String8 name{};
  S32     width{};
  S32     height{};
};
void glfw_error_callback(int ec, char const *desc) {
  printf("%d: %s\n", ec, desc);
}
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
// overwrites on full
template<typename T, U64 SIZE = 256>
  requires((SIZE & (SIZE - 1)) == 0)
struct RingBuffer {
  T buffer[SIZE]{};

  U64  head{};
  U64  tail{};
  bool full{false};

  static constexpr U64 mask = SIZE - 1;

  bool empty() const { return !full && head == tail; }

  void push(T item) {
    buffer[head & mask] = item;
    head++;
    if (full) {
      tail++;
    } else if ((head & mask) == (tail & mask)) {
      full = true;
    }
  }
  bool pop(T &out) {
    if (empty())
      return false;
    out = buffer[tail & mask];
    tail++;
    full = false;
    return true;
  }
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

void abort_if_vk_error(vk::Result res, std::string_view message = {}) {
  if (res != vk::Result::eSuccess) {
    std::fprintf(stderr,
                 "%.*s: %s\n",
                 static_cast<int>(message.size()),
                 message.data(),
                 vk::to_string(res).c_str());
    std::fflush(stderr);
    TRAP();
  }
}

template<typename T> concept VkResultValue = requires { typename vk::ResultValueType<T>::type; };
template<typename T>
  requires VkResultValue<T>
T abort_if_vk_error(vk::ResultValue<T> const &ret, std::string_view message = {}) {
  abort_if_vk_error(ret.result, message);
  return ret.value;
}


struct RatedDevice {
  vk::PhysicalDevice device;
  U32                score;
};

struct QueueFamilyIndices {
  U32 graphics_index{0xFFFFFFFF};
  U32 present_index{0xFFFFFFFF};

  [[nodiscard]] bool is_complete() const {
    return graphics_index != 0xFFFFFFFF && present_index != 0xFFFFFFFF;
  }
};


QueueFamilyIndices find_queue_families(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  Temp               scratch = scratch_begin(0, 0);
  QueueFamilyIndices indices{};

  U32 queue_family_count = 0;
  device.getQueueFamilyProperties(&queue_family_count, nullptr);


  auto *queue_family_properties =
      scratch.arena->push_array<vk::QueueFamilyProperties>(queue_family_count);
  device.getQueueFamilyProperties(&queue_family_count, queue_family_properties);
  for (U32 i = 0; i < queue_family_count; ++i) {
    vk::QueueFamilyProperties queue_family = queue_family_properties[i];

    if (queue_family.queueFlags & vk::QueueFlagBits::eGraphics) {
      indices.graphics_index = i;
    }

    vk::Bool32 present_support = VK_FALSE;
    auto       res             = device.getSurfaceSupportKHR(i, surface, &present_support);
    if (res == vk::Result::eSuccess && present_support) {
      indices.present_index = i;
    }

    if (indices.is_complete()) {
      break;
    }
  }
  scratch.end();
  return indices;
}

bool check_device_extension_support(vk::PhysicalDevice device, char const *extension_name) {
  Temp scratch = scratch_begin(0, 0);
  U32  ext_count; // real ext count is 279..
  bool result = false;
  abort_if_vk_error(device.enumerateDeviceExtensionProperties(nullptr, &ext_count, nullptr));

  auto *available = scratch.arena->push_array<vk::ExtensionProperties>(ext_count);
  abort_if_vk_error(device.enumerateDeviceExtensionProperties(nullptr, &ext_count, available));

  for (U32 i = 0; i < ext_count; ++i) {
    if (std::strcmp(available[i].extensionName, extension_name) == 0)
      result = true;
  }
  scratch.end();
  return result;
}

struct SwapchainSupport {
  vk::SurfaceCapabilitiesKHR capabilities;
  U32                        format_count;
  vk::SurfaceFormatKHR      *formats;
  U32                        mode_count;
  vk::PresentModeKHR        *modes;
};

SwapchainSupport
query_swapchain_support(vk::PhysicalDevice device, vk::SurfaceKHR surface, Arena *arena) {
  SwapchainSupport support{};
  support.capabilities = abort_if_vk_error(device.getSurfaceCapabilitiesKHR(surface));

  abort_if_vk_error(device.getSurfaceFormatsKHR(surface, &support.format_count, nullptr));
  support.formats = arena->push_array<vk::SurfaceFormatKHR>(support.format_count);

  abort_if_vk_error(device.getSurfaceFormatsKHR(surface, &support.format_count, support.formats));

  abort_if_vk_error(device.getSurfacePresentModesKHR(surface, &support.mode_count, nullptr));
  support.modes = arena->push_array<vk::PresentModeKHR>(support.mode_count);
  abort_if_vk_error(device.getSurfacePresentModesKHR(surface, &support.mode_count, support.modes));

  return support;
}

RatedDevice pick_best_physical_device(vk::Instance instance, vk::SurfaceKHR surface) {
  Temp scratch      = scratch_begin(0, 0);
  U32  device_count = 0;
  abort_if_vk_error(instance.enumeratePhysicalDevices(&device_count, nullptr));

  // TODO: remove — duplicate call
  abort_if_vk_error(instance.enumeratePhysicalDevices(&device_count, nullptr));
  auto *devices = scratch.arena->push_array<vk::PhysicalDevice>(device_count);
  abort_if_vk_error(instance.enumeratePhysicalDevices(&device_count, devices));

  vk::PhysicalDevice best_device = nullptr;
  U32                max_score   = 0;

  for (U32 i = 0; i < device_count; i++) {
    vk::PhysicalDevice &device = devices[i];

    QueueFamilyIndices indices = find_queue_families(device, surface);
    if (!indices.is_complete())
      continue;

    if (!check_device_extension_support(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
      continue;

    SwapchainSupport swapchain_support = query_swapchain_support(device, surface, scratch.arena);
    if (swapchain_support.format_count == 0 || swapchain_support.mode_count == 0)
      continue;

    vk::PhysicalDeviceProperties props = device.getProperties();

    U32 score = 0;
    if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
      score += 80000;
    else if (props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
      score += 5000;

    score += props.limits.maxImageDimension2D;

    vk::PhysicalDeviceMemoryProperties mem_props = device.getMemoryProperties();
    for (U32 j = 0; j < mem_props.memoryHeapCount; ++j) {
      if (mem_props.memoryHeaps[j].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
        score += static_cast<U32>(mem_props.memoryHeaps[j].size / (1024 * 1024));
    }

    if (score > max_score) {
      max_score   = score;
      best_device = device;
    }
  }
  scratch.end();
  return {best_device, max_score};
}

// TODO: swapchain_images/views arrays can overrun if image count increases on recreate.
// Need to re-allocate from app_arena or use a swapchain-specific arena.
void recreate_swapchain(GLFWwindow        *glfw_window,
                        vk::Device         vk_device,
                        vk::Image          swapchain_images[],
                        vk::ImageView      swapchain_image_view[],
                        U32               &swapchain_image_count,
                        vk::SwapchainKHR  &vk_swapchain,
                        vk::PhysicalDevice vk_phys_dev,
                        vk::SurfaceKHR     vk_surface,
                        vk::Extent2D      &swapchain_extent,
                        vk::Format         swapchain_image_format,
                        vk::ColorSpaceKHR  swapchain_color_space,
                        vk::PresentModeKHR present_mode) {
  S32 width = 0, height = 0;
  glfwGetFramebufferSize(glfw_window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(glfw_window, &width, &height);
    glfwWaitEvents();
  }

  abort_if_vk_error(vk_device.waitIdle());

  for (U32 i = 0; i < swapchain_image_count; ++i) {
    vk_device.destroyImageView(swapchain_image_view[i], nullptr);
    swapchain_image_view[i] = vk::ImageView{};
  }

  vk::SwapchainKHR old_swapchain = vk_swapchain;

  vk::SurfaceCapabilitiesKHR capabilities =
      abort_if_vk_error(vk_phys_dev.getSurfaceCapabilitiesKHR(vk_surface));
  vk::Extent2D new_extent = capabilities.currentExtent;

  // TODO: drivers rarely return 0xFFFFFFFF for currentextent, instead they return exact changing
  // physical pixel sizes. Which makes it take arbitrary dimensions the surface reported mid frame.
  if (new_extent.width == std::numeric_limits<U32>::max()) {
    new_extent.width  = std::clamp(static_cast<U32>(width),
                                  capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
    new_extent.height = std::clamp(static_cast<U32>(height),
                                   capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
  }

  swapchain_extent = new_extent;

  QueueFamilyIndices indices                = find_queue_families(vk_phys_dev, vk_surface);
  U32                queue_family_indices[] = {indices.graphics_index, indices.present_index};

  U32 desired_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && desired_count > capabilities.maxImageCount) {
    desired_count = capabilities.maxImageCount;
  }

  vk::SwapchainCreateInfoKHR swapchain_info{
      .surface          = vk_surface,
      .minImageCount    = desired_count,
      .imageFormat      = swapchain_image_format,
      .imageColorSpace  = swapchain_color_space,
      .imageExtent      = swapchain_extent,
      .imageArrayLayers = 1,
      .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
      .preTransform     = capabilities.currentTransform,
      .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode      = present_mode,
      .clipped          = true,
      .oldSwapchain     = old_swapchain,
  };

  if (indices.graphics_index != indices.present_index) {
    swapchain_info.imageSharingMode      = vk::SharingMode::eConcurrent;
    swapchain_info.queueFamilyIndexCount = 2;
    swapchain_info.pQueueFamilyIndices   = queue_family_indices;
  } else {
    swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
  }

  vk_swapchain = abort_if_vk_error(vk_device.createSwapchainKHR(swapchain_info));
  if (old_swapchain) {
    vk_device.destroySwapchainKHR(old_swapchain, nullptr);
  }

  abort_if_vk_error(
      vk_device.getSwapchainImagesKHR(vk_swapchain, &swapchain_image_count, swapchain_images));


  for (U64 i = 0; i < swapchain_image_count; ++i) {
    vk::ImageViewCreateInfo view_info{
        .image            = swapchain_images[i],
        .viewType         = vk::ImageViewType::e2D,
        .format           = swapchain_image_format,
        .subresourceRange = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1}
    };
    swapchain_image_view[i] = abort_if_vk_error(vk_device.createImageView(view_info));
  }
}

#ifdef SD2_DEBUG
static VKAPI_ATTR
    VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
                                       VkDebugUtilsMessageTypeFlagsEXT             message_type,
                                       const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
                                       void                                       *p_user_data) {
  if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    std::fprintf(stderr, "[VK] %s\n", p_callback_data->pMessage);
  return VK_FALSE;
}
#endif

bool check_validation_layer_support() {
  Temp scratch     = scratch_begin(0, 0);
  U32  layer_count = 0;
  bool result      = false;

  // TODO: consider not requerying this if this is called a lot of times. fine for one offs though
  abort_if_vk_error(vk::enumerateInstanceLayerProperties(&layer_count, nullptr));
  auto *available_layers = scratch.arena->push_array<vk::LayerProperties>(layer_count);
  abort_if_vk_error(vk::enumerateInstanceLayerProperties(&layer_count, available_layers));

  for (U32 i = 0; i < layer_count; ++i) {
    if (std::strcmp(available_layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
      result = true;
      break;
    }
  }
  scratch.end();
  return result;
}

constexpr U32 MAX_EXTENSIONS = 16;

U32 get_required_extensions(char const **out_extensions, U32 max_extensions) {
  U32          glfw_count;
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

int main() {
  thread_ctx_init();
  Arena *app_arena = arena_alloc({.name = "App arena"});
  Arena *frame_arena =
      arena_alloc({.flags = ArenaFlags::NO_CHAIN, // we rather wanna resize this from start then
                   .name  = "Frame arena"});
  Arena    *init_arena = arena_alloc({.name = "init"});
  AppParams params{
      .name   = str8_lit("sd2"),
      .width  = 800,
      .height = 600,
  };

  // create window
  glfwSetErrorCallback(&glfw_error_callback);
  if (glfwInit() == false) {
    return 1;
  }
  Window window{};
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  window.glfw_window      = glfwCreateWindow(params.width, params.height, "sd2", nullptr, nullptr);
  GLFWwindow *glfw_window = window.glfw_window;

  glfwSetWindowUserPointer(glfw_window, &window);

  // TODO: events

  glfwSetWindowSizeCallback(glfw_window, Window::dispatch_resize);
  glfwSetFramebufferSizeCallback(glfw_window, Window::dispatch_resize);
  glfwSetKeyCallback(glfw_window, Window::dispatch_key);
  glfwSetScrollCallback(glfw_window, Window::dispatch_scroll);
  glfwSetCursorPosCallback(glfw_window, Window::dispatch_cursor);
  glfwSetMouseButtonCallback(glfw_window, Window::dispatch_mouse_button);
  glfwSetWindowCloseCallback(glfw_window, Window::dispatch_close);
  glfwSetWindowRefreshCallback(glfw_window, Window::dispatch_refresh);
  glfwSetCharCallback(glfw_window, Window::dispatch_char);

  // TODO: decouple target from monitor refresh
  GLFWmonitor       *primary_monitor = glfwGetPrimaryMonitor();
  GLFWvidmode const *video_mode      = glfwGetVideoMode(primary_monitor);
  U32                monitor_hz      = video_mode->refreshRate > 0 ? video_mode->refreshRate : 60;
  F64                target_frame_ms = 1000.0 / monitor_hz;
  printf("Monitor: %u Hz (target: %.3f ms/frame)\n", monitor_hz, target_frame_ms);

  //~ vulkan init
  // init with window
  PFN_vkGetInstanceProcAddr vk_get_instance_proc_addr =
      g_vulkan_dynamic_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_get_instance_proc_addr);
  vk::ApplicationInfo app_info{.pApplicationName   = "sd2",
                               .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                               .pEngineName        = "NoEngine",
                               .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
                               .apiVersion         = VK_API_VERSION_1_4};

#ifdef SD2_DEBUG
  if (!check_validation_layer_support()) {
    std::fprintf(stderr, "VK_LAYER_KHRONOS_validation not available\n");
    TRAP();
  }

  // TODO: debug_create_info is gated by SD2_DEBUG but referenced unconditionally at .pNext below
  VkDebugUtilsMessengerCreateInfoEXT debug_create_info{
      .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .pNext           = nullptr,
      .flags           = 0,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debug_callback,
      .pUserData       = nullptr,
  };
#endif


  const char **extensions          = init_arena->push_array<const char *>(MAX_EXTENSIONS);
  U32          ext_count           = get_required_extensions(extensions, MAX_EXTENSIONS);
  char const  *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

  vk::InstanceCreateInfo inst_info{
      // TODO: pNext debug messenger + explicit vkCreateDebugUtilsMessengerEXT below = duplicate
      .pNext = &debug_create_info,
#ifdef SD2_DEBUG
      .pApplicationInfo        = &app_info,
      .enabledLayerCount       = 1,
      .ppEnabledLayerNames     = validation_layers,
      .enabledExtensionCount   = ext_count,
      .ppEnabledExtensionNames = extensions,
#else
      .enabledExtensionCount   = ext_count,
      .ppEnabledExtensionNames = extensions,
#endif
  };

  vk::Instance vk_instance =
      abort_if_vk_error(vk::createInstance(inst_info), "Failed to create vulkan instance");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_instance);

#ifdef SD2_DEBUG
  VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
  {
    // TODO: explicit messenger + pNext chain above = duplicate callbacks
    PFN_vkCreateDebugUtilsMessengerEXT func =
        (PFN_vkCreateDebugUtilsMessengerEXT)vk_get_instance_proc_addr(
            static_cast<VkInstance>(vk_instance),
            "vkCreateDebugUtilsMessengerEXT");
    if (func)
      func(static_cast<VkInstance>(vk_instance), &debug_create_info, nullptr, &debug_messenger);
  }
#endif

  VkSurfaceKHR raw_surface{};
  if (glfwCreateWindowSurface(vk_instance, glfw_window, nullptr, &raw_surface) != VK_SUCCESS) {
    TRAP();
  }
  vk::SurfaceKHR vk_surface = raw_surface;

  //~ vulkan phys dev
  RatedDevice rated_device = pick_best_physical_device(vk_instance, vk_surface);
  if (rated_device.score == 0) {
    TRAP(); // cant run
  }
  vk::PhysicalDevice vk_phys_dev = rated_device.device;

  float              queue_priority = 1.0f; // TODO: RESEARCH
  QueueFamilyIndices queue_indices  = find_queue_families(vk_phys_dev, vk_surface);

  vk::DeviceQueueCreateInfo queue_infos[2]{};
  U32                       queue_info_count = 0;

  queue_infos[queue_info_count++] = vk::DeviceQueueCreateInfo{
      .queueFamilyIndex = queue_indices.graphics_index,
      .queueCount       = 1,
      .pQueuePriorities = &queue_priority,
  };
  if (queue_indices.graphics_index != queue_indices.present_index) {
    queue_infos[queue_info_count++] = vk::DeviceQueueCreateInfo{
        .queueFamilyIndex = queue_indices.present_index,
        .queueCount       = 1,
        .pQueuePriorities = &queue_priority,
    };
  }

  char const *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  vk::PhysicalDeviceFeatures         device_features{};
  vk::PhysicalDeviceVulkan13Features device_features13{.pNext            = nullptr,
                                                       .synchronization2 = true,
                                                       .dynamicRendering = true};

  vk::DeviceCreateInfo dev_info{.pNext                   = &device_features13,
                                .queueCreateInfoCount    = queue_info_count,
                                .pQueueCreateInfos       = queue_infos,
                                .enabledExtensionCount   = 1,
                                .ppEnabledExtensionNames = device_extensions,
                                .pEnabledFeatures        = &device_features

  };


  //~ vulkan device
  vk::Device vk_device = abort_if_vk_error(vk_phys_dev.createDevice(dev_info));
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_device);
  vk::Queue graphics_queue{};
  vk::Queue present_queue{};

  vk_device.getQueue(queue_indices.graphics_index, 0, &graphics_queue);
  vk_device.getQueue(queue_indices.present_index, 0, &present_queue);
  // PFN_vkVoidFunction vk_get_device_proc_addr = vk_device.getProcAddr("vk_get_device_proc_addr");


  //~ swapchain
  vk::SurfaceCapabilitiesKHR capabilities{};
  // TODO: consider using getSurfaceCapabilities2KHR instead
  abort_if_vk_error(vk_phys_dev.getSurfaceCapabilitiesKHR(vk_surface, &capabilities));

  U32 format_count = 0;
  abort_if_vk_error(vk_phys_dev.getSurfaceFormatsKHR(vk_surface, &format_count, nullptr));
  auto *formats = init_arena->push_array<vk::SurfaceFormatKHR>(format_count);
  abort_if_vk_error(vk_phys_dev.getSurfaceFormatsKHR(vk_surface, &format_count, formats));

  U32 mode_count = 0;
  abort_if_vk_error(vk_phys_dev.getSurfacePresentModesKHR(vk_surface, &mode_count, nullptr));
  auto *present_modes = init_arena->push_array<vk::PresentModeKHR>(mode_count);
  abort_if_vk_error(vk_phys_dev.getSurfacePresentModesKHR(vk_surface, &mode_count, present_modes));

  vk::Format        chosen_format      = vk::Format::eB8G8R8A8Srgb;
  vk::ColorSpaceKHR chosen_color_space = vk::ColorSpaceKHR::eSrgbNonlinear;

  bool format_found = false;
  for (U32 i = 0; i < format_count; ++i) {
    if (formats[i].format == chosen_format && formats[i].colorSpace == chosen_color_space) {
      format_found = true;
      break;
    }
  }

  if (!format_found && format_count > 0) {
    chosen_format      = formats[0].format;
    chosen_color_space = formats[0].colorSpace;
  }

  vk::PresentModeKHR chosen_present_mode = vk::PresentModeKHR::eImmediate;
  bool               mode_found          = false;
  for (U32 i = 0; i < mode_count; ++i) {
    if (present_modes[i] == chosen_present_mode) {
      mode_found = true;
      break;
    }
  }

  if (!mode_found) {
    chosen_present_mode = vk::PresentModeKHR::eFifo;
  }

  U32 desired_image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && desired_image_count > capabilities.maxImageCount) {
    desired_image_count = capabilities.maxImageCount;
  }

  vk::Extent2D swapchain_extent = capabilities.currentExtent;
  if (swapchain_extent.width == 0xFFFFFFFF) {
    swapchain_extent.width  = params.width;
    swapchain_extent.height = params.height;
  }

  U32 queue_indices_array[] = {queue_indices.graphics_index, queue_indices.present_index};

  vk::SharingMode sharing_mode             = vk::SharingMode::eExclusive;
  U32             queue_family_index_count = 0;
  U32            *p_queue_family_indices   = nullptr;

  if (queue_indices.graphics_index != queue_indices.present_index) {
    sharing_mode             = vk::SharingMode::eConcurrent;
    queue_family_index_count = 2;
    p_queue_family_indices   = queue_indices_array;
  }


  vk::SwapchainCreateInfoKHR swapchain_info{
      .surface               = vk_surface,
      .minImageCount         = desired_image_count,
      .imageFormat           = chosen_format,
      .imageColorSpace       = chosen_color_space,
      .imageExtent           = swapchain_extent,
      .imageArrayLayers      = 1,
      .imageUsage            = vk::ImageUsageFlagBits::eColorAttachment,
      .imageSharingMode      = sharing_mode,
      .queueFamilyIndexCount = queue_family_index_count,
      .pQueueFamilyIndices   = p_queue_family_indices,
      .preTransform          = capabilities.currentTransform,
      .compositeAlpha        = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode           = chosen_present_mode,
      .clipped               = true, // TODO: check
      .oldSwapchain          = nullptr

  };
  vk::SwapchainKHR vk_swapchain = abort_if_vk_error(vk_device.createSwapchainKHR(swapchain_info));

  //~ swapchain images

  U32 swapchain_image_count = 0;
  abort_if_vk_error(vk_device.getSwapchainImagesKHR(vk_swapchain, &swapchain_image_count, nullptr));


  // TODO: these must be re-allocable on swapchain recreate (image count can change)
  auto *swapchain_images      = app_arena->push_array<vk::Image>(swapchain_image_count);
  auto *swapchain_image_views = app_arena->push_array<vk::ImageView>(swapchain_image_count);
  auto *image_initialized     = app_arena->push_array<bool>(swapchain_image_count);
  auto *images_in_flight      = app_arena->push_array<vk::Fence>(swapchain_image_count);

  abort_if_vk_error(
      vk_device.getSwapchainImagesKHR(vk_swapchain, &swapchain_image_count, swapchain_images));

  for (U32 i = 0; i < swapchain_image_count; ++i) {
    vk::ImageViewCreateInfo view_info{
        .image            = swapchain_images[i],
        .viewType         = vk::ImageViewType::e2D,
        .format           = chosen_format,
        .components       = vk::ComponentMapping{.r = vk::ComponentSwizzle::eIdentity,
                                                 .g = vk::ComponentSwizzle::eIdentity,
                                                 .b = vk::ComponentSwizzle::eIdentity,
                                                 .a = vk::ComponentSwizzle::eIdentity},
        .subresourceRange = vk::ImageSubresourceRange{.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                 .baseMipLevel   = 0,
                                                 .levelCount     = 1,
                                                 .baseArrayLayer = 0,
                                                 .layerCount     = 1}
    };

    swapchain_image_views[i] = abort_if_vk_error(vk_device.createImageView(view_info));
  }
  //~ SYNC SETUP
  constexpr U32 MAX_FRAMES_IN_FLIGHT = 2;
  vk::Semaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT]{};
  vk::Semaphore render_finished_semaphores[MAX_FRAMES_IN_FLIGHT]{};
  vk::Fence     in_flight_fences[MAX_FRAMES_IN_FLIGHT]{};

  vk::FenceCreateInfo     fence_info{.flags = vk::FenceCreateFlagBits::eSignaled};
  vk::SemaphoreCreateInfo sempahore_info{};

  for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    image_available_semaphores[i] = abort_if_vk_error(vk_device.createSemaphore(sempahore_info));
    render_finished_semaphores[i] = abort_if_vk_error(vk_device.createSemaphore(sempahore_info));
    in_flight_fences[i]           = abort_if_vk_error(vk_device.createFence(fence_info));
  }

  //~ command allocator and buffers
  vk::CommandPoolCreateInfo pool_info{
      .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      .queueFamilyIndex = queue_indices.graphics_index,
  };
  vk::CommandPool command_pool = abort_if_vk_error((vk_device.createCommandPool(pool_info)));
  vk::CommandBufferAllocateInfo alloc_info{.commandPool        = command_pool,
                                           .level              = vk::CommandBufferLevel::ePrimary,
                                           .commandBufferCount = MAX_FRAMES_IN_FLIGHT};

  vk::CommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT]{};
  abort_if_vk_error(vk_device.allocateCommandBuffers(&alloc_info, command_buffers));
  init_arena = arena_release(init_arena);

  //~ window loop
  U64 absolute_frame_index = 0;
  (void)absolute_frame_index;
  U32                 current_frame = 0;
  bool                toggle_blue   = false;
  bool                toggle_orange = false;
  vk::ClearColorValue clear_color{};
  while (!glfwWindowShouldClose(glfw_window)) {
    struct timespec t0, t_wait, t1, t2;
    U64             c0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    c0 = __rdtsc();
    frame_arena->clear();
    glfwPollEvents();
    WindowEvent event{};
    while (window.events.pop(event)) {
      switch (event.type) {
        using enum WindowEventType;
        case NONE: {
          break;
        }
        case RESIZE: {
          window.framebuffer_resized = true;
          break;
        }
        case KEY: {
          WKeyEvent &key = event.key;
          printf("%d, %d, %d, %d\n", key.key, key.action, key.mods, key.scancode);
          if (key.action == GLFW_PRESS) {
            if (key.key == GLFW_KEY_R) {
              clear_color = {{std::array{0.15f, 0.05f, 0.05f, 1.0f}}};
            }
            if (key.key == GLFW_KEY_G) {
              clear_color = {{std::array{0.05f, 0.15f, 0.05f, 1.0f}}};
            }
            if (key.key == GLFW_KEY_B) {
              clear_color = {{std::array{0.05f, 0.05f, 0.15f, 1.0f}}};
            }
          }
          break;
        }
        case SCROLL: {
          break;
        }
        case CURSOR: {
          break;
        }
        case MBUTTON: {
          WMButtonEvent &mbutton = event.mbutton;
          char const    *action  = mbutton.action == GLFW_PRESS ? "Pressed" : "Released";
          switch (mbutton.button) {
            default: {
              printf("other mouse button: %d\n", mbutton.button);
              break;
            }
            case 0: {
              printf("left click %s\n", action);
              break;
            }
            case 1: {
              printf("right click %s\n", action);
              break;
            }
            case 2: {
              printf("middle click %s\n", action);
              break;
            }
          }
          switch (mbutton.action)
            break;
        }
        case CLOSE: {
          break;
        }
        case REFRESH: {
          break;
        }
        case TEXT: {
          break;
        }
      }
    }
    Window *window = static_cast<Window *>(glfwGetWindowUserPointer(glfw_window));
    clock_gettime(CLOCK_MONOTONIC, &t_wait);
    abort_if_vk_error(
        vk_device.waitForFences(1, &in_flight_fences[current_frame], VK_TRUE, UINT64_MAX));

    bool swapchain_needs_rebuild = false;
    U32  image_index             = 0;

    VkResult acquire_res = vkAcquireNextImageKHR(vk_device,
                                                 vk_swapchain,
                                                 UINT64_MAX,
                                                 image_available_semaphores[current_frame],
                                                 VK_NULL_HANDLE,
                                                 &image_index);

    if (acquire_res == VK_ERROR_OUT_OF_DATE_KHR) {
      recreate_swapchain(glfw_window,
                         vk_device,
                         swapchain_images,
                         swapchain_image_views,
                         swapchain_image_count,
                         vk_swapchain,
                         vk_phys_dev,
                         vk_surface,
                         swapchain_extent,
                         chosen_format,
                         chosen_color_space,
                         chosen_present_mode);
      for (U32 i = 0; i < swapchain_image_count; ++i) {
        images_in_flight[i]  = vk::Fence{};
        image_initialized[i] = false;
      }
      continue;
    }
    if (acquire_res == VK_SUBOPTIMAL_KHR) {
      swapchain_needs_rebuild = true;
    } else if (acquire_res != VK_SUCCESS) {
      std::fprintf(stderr, "vkAcquireNextImageKHR failed: %d\n", acquire_res);
      TRAP();
    }
    //~ wait for previous work on this specific image to complete
    if (images_in_flight[image_index]) {
      abort_if_vk_error(
          vk_device.waitForFences(1, &images_in_flight[image_index], VK_TRUE, UINT64_MAX));
    }
    images_in_flight[image_index] = in_flight_fences[current_frame];
    clock_gettime(CLOCK_MONOTONIC, &t1);
    //~ safe to submit
    abort_if_vk_error(vk_device.resetFences(1, &in_flight_fences[current_frame]));

    vk::CommandBuffer cmd = command_buffers[current_frame];
    abort_if_vk_error(cmd.reset());
    vk::CommandBufferBeginInfo begin_info{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};

    abort_if_vk_error(cmd.begin(begin_info));

    vk::ImageMemoryBarrier2 layout_to_render_barrier{
        .srcStageMask     = image_initialized[image_index]
                                ? vk::PipelineStageFlagBits2::eColorAttachmentOutput
                                : vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask    = vk::AccessFlagBits2::eNone,
        .dstStageMask     = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask    = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout        = image_initialized[image_index] ? vk::ImageLayout::ePresentSrcKHR
                                                           : vk::ImageLayout::eUndefined,
        .newLayout        = vk::ImageLayout::eAttachmentOptimal,
        .image            = swapchain_images[image_index],
        .subresourceRange = vk::ImageSubresourceRange{.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                      .levelCount = 1,
                                                      .layerCount = 1}
    };
    vk::DependencyInfo dep_info_render{.imageMemoryBarrierCount = 1,
                                       .pImageMemoryBarriers    = &layout_to_render_barrier};
    cmd.pipelineBarrier2(dep_info_render);


    vk::RenderingAttachmentInfo color_attachment{.imageView   = swapchain_image_views[image_index],
                                                 .imageLayout = vk::ImageLayout::eAttachmentOptimal,
                                                 .loadOp      = vk::AttachmentLoadOp::eClear,
                                                 .storeOp     = vk::AttachmentStoreOp::eStore,
                                                 .clearValue  = clear_color};

    vk::RenderingInfo rendering_info{
        .renderArea           = vk::Rect2D{{0, 0}, swapchain_extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_attachment
    };

    cmd.beginRendering(rendering_info);

    // TODO: drawing pipelines here

    cmd.endRendering();

    vk::ImageMemoryBarrier2 layout_to_present_barrier{
        .srcStageMask     = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask    = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask     = vk::PipelineStageFlagBits2::eBottomOfPipe,
        .dstAccessMask    = vk::AccessFlagBits2::eNone,
        .oldLayout        = vk::ImageLayout::eAttachmentOptimal,
        .newLayout        = vk::ImageLayout::ePresentSrcKHR,
        .image            = swapchain_images[image_index],
        .subresourceRange = vk::ImageSubresourceRange{.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                      .levelCount = 1,
                                                      .layerCount = 1}
    };
    vk::DependencyInfo dep_info_present{.imageMemoryBarrierCount = 1,
                                        .pImageMemoryBarriers    = &layout_to_present_barrier};
    cmd.pipelineBarrier2(dep_info_present);
    image_initialized[image_index] = true;
    abort_if_vk_error(cmd.end());

    vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo         submit_info{.waitSemaphoreCount = 1,
                                       .pWaitSemaphores = &image_available_semaphores[current_frame],
                                       .pWaitDstStageMask    = wait_stages,
                                       .commandBufferCount   = 1,
                                       .pCommandBuffers      = &cmd,
                                       .signalSemaphoreCount = 1,
                                       .pSignalSemaphores = &render_finished_semaphores[current_frame]};
    abort_if_vk_error(graphics_queue.submit(1, &submit_info, in_flight_fences[current_frame]));

    // 5. Present the fully rendered target back to our operating system window
    VkPresentInfoKHR present_info{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores =
            reinterpret_cast<VkSemaphore const *>(&render_finished_semaphores[current_frame]),
        .swapchainCount = 1,
        .pSwapchains    = reinterpret_cast<VkSwapchainKHR const *>(&vk_swapchain),
        .pImageIndices  = &image_index,
        .pResults       = nullptr};

    VkResult present_res = vkQueuePresentKHR(static_cast<VkQueue>(present_queue), &present_info);

    if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR ||
        window->framebuffer_resized || swapchain_needs_rebuild) {
      recreate_swapchain(glfw_window,
                         vk_device,
                         swapchain_images,
                         swapchain_image_views,
                         swapchain_image_count,
                         vk_swapchain,
                         vk_phys_dev,
                         vk_surface,
                         swapchain_extent,
                         chosen_format,
                         chosen_color_space,
                         chosen_present_mode);
      for (U32 i = 0; i < swapchain_image_count; ++i) {
        images_in_flight[i]  = vk::Fence{};
        image_initialized[i] = false;
      }
      window->framebuffer_resized = false;
    } else if (present_res != VK_SUCCESS) {
      std::fprintf(stderr, "vkQueuePresentKHR failed: %d\n", present_res);
      TRAP();
    }
    clock_gettime(CLOCK_MONOTONIC, &t2);
    U64 c2 = __rdtsc();

    //~ FPS limit (temporary: tied to monitor refresh)
    {
      F64 elapsed   = (t2.tv_sec - t0.tv_sec) * 1000.0 + (t2.tv_nsec - t0.tv_nsec) / 1e6;
      F64 remaining = target_frame_ms - elapsed;
      if (remaining > 2.0) {
        struct timespec ts = {.tv_sec = 0, .tv_nsec = static_cast<long>((remaining - 1.5) * 1e6)};
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
      }
      if (remaining > 0) {
        struct timespec now;
        do {
          clock_gettime(CLOCK_MONOTONIC, &now);
        } while ((now.tv_sec - t0.tv_sec) * 1000.0 + (now.tv_nsec - t0.tv_nsec) / 1e6 <
                 target_frame_ms);
      }
    }
    struct timespec t_frame_end;
    clock_gettime(CLOCK_MONOTONIC, &t_frame_end);

    // Toggle frames-in-flight trackers
    absolute_frame_index++;
    current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

    // --- TIMING ---
    F64 total_ms =
        (t_frame_end.tv_sec - t0.tv_sec) * 1000.0 + (t_frame_end.tv_nsec - t0.tv_nsec) / 1e6;
    F64 work_ms = (t_wait.tv_sec - t0.tv_sec) * 1000.0 + (t_wait.tv_nsec - t0.tv_nsec) / 1e6 +
                  (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_nsec - t1.tv_nsec) / 1e6;
    F64 wait_ms      = total_ms - work_ms;
    U64 total_cycles = c2 - c0;

    local_persist F64 debug_accum_ms  = 0;
    local_persist U32 debug_frames    = 0;
    local_persist F64 debug_work_sum  = 0;
    local_persist U64 debug_cycle_sum = 0;

    debug_accum_ms += total_ms;
    debug_frames++;
    debug_work_sum += work_ms;
    debug_cycle_sum += total_cycles;

    if (debug_accum_ms >= 1000.0) {
      F64 fps = debug_frames / (debug_accum_ms / 1000.0);
      printf("Frames: %-4u | work avg: %6.3f ms | total avg: %6.3f ms | cycles avg: %-8llu | FPS: "
             "%6.1f\n",
             debug_frames,
             debug_work_sum / debug_frames,
             debug_accum_ms / debug_frames,
             static_cast<unsigned long long>(debug_cycle_sum / debug_frames),
             fps);
      debug_accum_ms  = 0;
      debug_frames    = 0;
      debug_work_sum  = 0;
      debug_cycle_sum = 0;
    }
  }

  //~ cleanup
  abort_if_vk_error(vk_device.waitIdle());

  for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    vk_device.destroySemaphore(image_available_semaphores[i]);
    vk_device.destroySemaphore(render_finished_semaphores[i]);
    vk_device.destroyFence(in_flight_fences[i]);
  }
  vk_device.destroyCommandPool(command_pool);
  for (U32 i = 0; i < swapchain_image_count; ++i) {
    if (swapchain_image_views[i]) {
      vk_device.destroyImageView(swapchain_image_views[i]);
    }
  }
  vk_device.destroySwapchainKHR(vk_swapchain);
  // graphics arena destruction
  vk_device.destroy();
#ifdef SD2_DEBUG
  if (debug_messenger) {
    PFN_vkDestroyDebugUtilsMessengerEXT func =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vk_get_instance_proc_addr(
            static_cast<VkInstance>(vk_instance),
            "vkDestroyDebugUtilsMessengerEXT");
    if (func)
      func(static_cast<VkInstance>(vk_instance), debug_messenger, nullptr);
  }
#endif
  vk_instance.destroySurfaceKHR(vk_surface);
  vk_instance.destroy();
  glfwDestroyWindow(window.glfw_window);
  glfwTerminate();
  arena_release(frame_arena);
  arena_release(app_arena);
  thread_ctx_release();
  return 0;
}

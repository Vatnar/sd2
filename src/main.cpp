#include <cstdio>
#include <cstring>
#include <thread>
#include <unistd.h>


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
  U8* str;
  U64 size;
};
String8 str8(U8* str, U64 size) {
  String8 result{str, size};
  return result;
}
#define str8_lit(S) str8((U8*)(S), sizeof(S) - 1)

#include "arena.cpp"
#include "main2.cpp"


struct AppParams {
  String8 name{};
  S32     width{};
  S32     height{};
};
void glfw_error_callback(int ec, const char* desc) {
  printf("%d: %s\n", ec, desc);
}

struct Window {
  GLFWwindow* glfw_window{};
  // holds a ptr to the event system which callbacks can use
  bool framebuffer_resized = false;


  static void dispatch_resize(GLFWwindow* glfw_window, S32 width, S32 height) {
    Window* window              = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
    window->framebuffer_resized = true;
  }
  static void dispatch_key(GLFWwindow* glfw_window, S32 key, S32 scancode, S32 action, S32 mods) {
    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
  }
  static void dispatch_scroll(GLFWwindow* glfw_window, F64 xoffset, F64 yoffset) {
    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
  }
  static void dispatch_cursor(GLFWwindow* glfw_window, F64 xpos, F64 ypos) {
    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
  }
  static void dispatch_mouse_button(GLFWwindow* glfw_window, S32 button, S32 action, S32 mods) {
    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
  }

  static void dispatch_close(GLFWwindow* glfw_window) {
    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
  }
  static void dispatch_refresh(GLFWwindow* glfw_window) {
    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
  }
  static void dispatch_char(GLFWwindow* glfw_window, U32 keycode) {
    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
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
T abort_if_vk_error(const vk::ResultValue<T>& ret, std::string_view message = {}) {
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


constexpr U32      MAX_QUEUE_FAMILY_COUNT = 8;
QueueFamilyIndices find_queue_families(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  QueueFamilyIndices indices{};
  U32                queue_family_count = 0;
  device.getQueueFamilyProperties(&queue_family_count, nullptr);
  vk::QueueFamilyProperties queue_family_properties[MAX_QUEUE_FAMILY_COUNT]{};
  device.getQueueFamilyProperties(&queue_family_count, queue_family_properties);
  ASSERT(queue_family_count <= MAX_QUEUE_FAMILY_COUNT);

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
  return indices;
}

bool check_device_extension_support(vk::PhysicalDevice device, const char* extension_name) {
  U32 ext_count; // real ext count is 279..
  abort_if_vk_error(device.enumerateDeviceExtensionProperties(nullptr, &ext_count, nullptr));

  // TODO: replace with arena dyn array thingy later
  vk::ExtensionProperties available[300];
  U32                     count = 300;
  abort_if_vk_error(device.enumerateDeviceExtensionProperties(nullptr, &count, available));

  for (U32 i = 0; i < count; ++i) {
    if (std::strcmp(available[i].extensionName, extension_name) == 0)
      return true;
  }
  return false;
}

struct SwapchainSupport {
  vk::SurfaceCapabilitiesKHR capabilities;
  U32                        format_count;
  vk::SurfaceFormatKHR       formats[16];
  U32                        mode_count;
  vk::PresentModeKHR         modes[8];
};

SwapchainSupport query_swapchain_support(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  SwapchainSupport support{};
  support.capabilities = abort_if_vk_error(device.getSurfaceCapabilitiesKHR(surface));

  support.format_count = 16;
  abort_if_vk_error(device.getSurfaceFormatsKHR(surface, &support.format_count, nullptr));
  if (support.format_count > 16)
    support.format_count = 16;
  if (support.format_count > 0)
    abort_if_vk_error(device.getSurfaceFormatsKHR(surface, &support.format_count, support.formats));

  support.mode_count = 8;
  abort_if_vk_error(device.getSurfacePresentModesKHR(surface, &support.mode_count, nullptr));
  if (support.mode_count > 8)
    support.mode_count = 8;
  if (support.mode_count > 0)
    abort_if_vk_error(
        device.getSurfacePresentModesKHR(surface, &support.mode_count, support.modes));

  return support;
}

RatedDevice pick_best_physical_device(vk::Instance instance, vk::SurfaceKHR surface) {
  U32 device_count = 0;
  abort_if_vk_error(instance.enumeratePhysicalDevices(&device_count, nullptr));

  vk::PhysicalDevice devices[10];
  abort_if_vk_error(instance.enumeratePhysicalDevices(&device_count, devices));

  vk::PhysicalDevice best_device = nullptr;
  U32                max_score   = 0;

  for (U32 i = 0; i < device_count; i++) {
    vk::PhysicalDevice& device = devices[i];

    QueueFamilyIndices indices = find_queue_families(device, surface);
    if (!indices.is_complete())
      continue;

    if (!check_device_extension_support(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
      continue;

    SwapchainSupport swapchain_support = query_swapchain_support(device, surface);
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
  return {best_device, max_score};
}

constexpr U32 MAX_SWAPCHAIN_IMAGES = 8;

void recreate_swapchain(GLFWwindow*        glfw_window,
                        vk::Device         vk_device,
                        vk::Image          swapchain_images[],
                        vk::ImageView      swapchain_image_view[],
                        U32&               swapchain_image_count,
                        vk::SwapchainKHR&  vk_swapchain,
                        vk::PhysicalDevice vk_phys_dev,
                        vk::SurfaceKHR     vk_surface,
                        vk::Extent2D&      swapchain_extent,
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

  U32 actual_image_count = 0;
  abort_if_vk_error(vk_device.getSwapchainImagesKHR(vk_swapchain, &actual_image_count, nullptr));
  ASSERT_ALWAYS(actual_image_count <= MAX_SWAPCHAIN_IMAGES);
  swapchain_image_count = actual_image_count;
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
                                       const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
                                       void*                                       p_user_data) {
  if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    std::fprintf(stderr, "[VK] %s\n", p_callback_data->pMessage);
  return VK_FALSE;
}
#endif

bool check_validation_layer_support() {
  U32 layer_count;
  if (vkEnumerateInstanceLayerProperties(&layer_count, nullptr) != VK_SUCCESS)
    return false;

  VkLayerProperties available_layers[64];
  U32               count = layer_count > 64 ? 64 : layer_count;
  if (vkEnumerateInstanceLayerProperties(&count, available_layers) != VK_SUCCESS)
    return false;

  for (U32 i = 0; i < count; ++i) {
    if (std::strcmp(available_layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0)
      return true;
  }
  return false;
}

constexpr U32 MAX_EXTENSIONS = 16;

U32 get_required_extensions(const char** out_extensions, U32 max_extensions) {
  U32          glfw_count;
  const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_count);

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
  Arena* app_arena = arena_alloc({.name = "App arena"});
  Arena* frame_arena =
      arena_alloc({.flags = ArenaFlags::NO_CHAIN, // we rather wanna resize this from start then
                   .name  = "Frame arena"});
  Arena*    init_arena = arena_alloc({.name = "init"});
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
  GLFWwindow* glfw_window = window.glfw_window;

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


  // TODO: arena stuff
  const char** extensions          = init_arena->push_array<const char*>(MAX_EXTENSIONS);
  U32          ext_count           = get_required_extensions(extensions, MAX_EXTENSIONS);
  const char*  validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

  vk::InstanceCreateInfo inst_info{
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

  const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

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
  // TODO: do this in scratch
  vk::SurfaceCapabilitiesKHR capabilities{};
  abort_if_vk_error(vk_phys_dev.getSurfaceCapabilitiesKHR(vk_surface, &capabilities));
  constexpr U32 MAX_FORMATS = 16;
  constexpr U32 MAX_MODES   = 8;

  // TODO: scratch arena alloc
  vk::SurfaceFormatKHR formats[MAX_FORMATS];
  vk::PresentModeKHR   present_modes[MAX_MODES];


  U32 format_count = MAX_FORMATS;
  abort_if_vk_error(vk_phys_dev.getSurfaceFormatsKHR(vk_surface, &format_count, nullptr));
  if (format_count > MAX_FORMATS)
    format_count = MAX_FORMATS;
  if (format_count != 0) {
    abort_if_vk_error(vk_phys_dev.getSurfaceFormatsKHR(vk_surface, &format_count, formats));
  }

  U32 mode_count = MAX_MODES;
  abort_if_vk_error(vk_phys_dev.getSurfacePresentModesKHR(vk_surface, &mode_count, nullptr));
  if (mode_count > MAX_MODES)
    mode_count = MAX_MODES;
  if (mode_count != 0) {
    abort_if_vk_error(
        vk_phys_dev.getSurfacePresentModesKHR(vk_surface, &mode_count, present_modes));
  }

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
  U32*            p_queue_family_indices   = nullptr;

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

  if (swapchain_image_count > MAX_SWAPCHAIN_IMAGES) {
    swapchain_image_count = MAX_SWAPCHAIN_IMAGES;
  }

  vk::Image     swapchain_images[MAX_SWAPCHAIN_IMAGES];
  vk::ImageView swapchain_image_views[MAX_SWAPCHAIN_IMAGES]{};
  bool          image_initialized[MAX_SWAPCHAIN_IMAGES]{};
  vk::Fence     images_in_flight[MAX_SWAPCHAIN_IMAGES]{};
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
  U32 current_frame = 0;
  while (!glfwWindowShouldClose(glfw_window)) {
    frame_arena->clear();
    glfwPollEvents();
    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
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

    vk::ClearValue clear_color{
        std::array{0.05f, 0.05f, 0.15f, 1.0f}
    }; // Dark blue background
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
            reinterpret_cast<const VkSemaphore*>(&render_finished_semaphores[current_frame]),
        .swapchainCount = 1,
        .pSwapchains    = reinterpret_cast<const VkSwapchainKHR*>(&vk_swapchain),
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
    // Toggle frames-in-flight trackers
    current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
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
  return 0;
}

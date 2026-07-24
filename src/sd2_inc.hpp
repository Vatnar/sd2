#pragma once


#define VK_EXT_debug_utils 1
#define VK_KHR_surface 1
#define VK_KHR_swapchain 1
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS         1
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS  1
#define VULKAN_HPP_NO_EXCEPTIONS           1
#include <vulkan/vulkan.hpp>


#define GLFW_INCLUDE_VULKAN 1
#include <GLFW/glfw3.h>

//~ glm
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// stb
#include <stb_image.h>

// tinyobjloader
#include <tiny_obj_loader.h>
// sd2
#include "internal/base.hpp"
#include "internal/window.hpp"
#include "internal/helpers.hpp"
#include "internal/profiler.hpp"
#include "internal/thread_ctx.hpp"
#include "internal/vk_helpers.hpp"
#include "internal/base_arena.hpp"
#include "internal/base_array.hpp"
#include "internal/base_string.hpp"
#include "internal/vk_destructor_stack.hpp"
#include "internal/vk_arena.hpp"
#include "internal/descriptor_schema.hpp"
#include "internal/descriptor_arena.hpp"
#include "internal/descriptor_writer.hpp"
#include "internal/material_table.hpp"
#include "internal/vk_descriptor_cmd.hpp"
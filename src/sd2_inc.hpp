#pragma once


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
#include <stb/stb_image.h>

// tinyobjloader
#include <tiny_obj_loader.h>
//
// sd2
#include "internal/base.hpp"
#include "internal/window.hpp"
#include "internal/helpers.hpp"
#include "internal/profiler.hpp"
#include "internal/thread_ctx.hpp"
#include "internal/vk_helpers.hpp"
#include "internal/base_arena.hpp"
#include "internal/base_array.hpp"

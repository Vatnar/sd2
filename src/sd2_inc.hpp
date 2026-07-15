#pragma once


#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS         1
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS  1
#define VULKAN_HPP_NO_EXCEPTIONS           1
#include <vulkan/vulkan.hpp>


#define GLFW_INCLUDE_VULKAN 1
#include <GLFW/glfw3.h>

//~ glm
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// stb
#include <stb/stb_image.h>

// sd2
#include "internal/base.hpp"
#include "internal/helpers.hpp"
#include "internal/profiler.hpp"
#include "internal/thread_ctx.hpp"
#include "internal/vk_helpers.hpp"
#include "internal/window.hpp"

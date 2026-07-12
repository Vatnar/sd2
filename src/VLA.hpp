#pragma once

#include "VLA/Matrix.hpp"
#include "VLA/Vector.hpp"

namespace VLA {} // namespace VLA

inline constexpr VLA::Matrix4x4f RotationShear() {
  // clang-format off
  return VLA::Matrix4x4f{
    1.0, 3.5, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    -5.0, 3.0, 1.0, 3.0,
    2.0, 0.0, 0.0, 1.0
  };
  // clang-format on
}

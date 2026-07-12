#pragma once
#include <cmath>
#include <iosfwd>
#include <print>
#include <string>

// TODO: Testing
namespace VLA {


template<class T, size_t N>
  requires std::is_arithmetic_v<T>
struct Vector {
  T v[N];
  constexpr Vector operator+(const Vector& other) const {
    Vector result;
    for (std::size_t i{0}; i < N; i++) {
      result.v[i] = v[i] + other.v[i];
    }
    return result;
  }

  constexpr Vector operator-(const Vector& other) const {
    Vector result;
    for (std::size_t i{0}; i < N; i++) {
      result.v[i] = v[i] - other.v[i];
    }

    return result;
  }
  constexpr Vector operator*(const T& scalar) const {
    Vector result;
    for (std::size_t i{0}; i < N; i++) {
      result.v[i] = v[i] * scalar;
    }
    return result;
  }
  bool operator==(const Vector& vector) const = default;
  static constexpr T Dot(const Vector& v1, const Vector& v2) {
    T result{};
    for (std::size_t i{0}; i < N; i++) {
      result += v1[i] * v2[i];
    }
    return result;
  }

  static constexpr Vector<T, 3> Cross(const Vector<T, 3>& v1, const Vector<T, 3>& v2) {
    Vector<T, 3> result{};
    result[0] = v1[1] * v2[2] - v1[2] * v2[1];
    result[1] = v1[2] * v2[0] - v1[0] * v2[2];
    result[2] = v1[0] * v2[1] - v1[1] * v2[0];
    return result;
  }

  constexpr T Length() {
    T result{};
    for (std::size_t i{0}; i < N; i++) {
      result += v[i] * v[i];
    }
    return std::sqrt(result);
  }


  constexpr T& operator[](std::size_t idx) { return v[idx]; }
  constexpr const T& operator[](std::size_t idx) const { return v[idx]; }

  constexpr friend std::ostream& operator<<(std::ostream& os, const Vector& v) {
    os << std::string("[ ");
    for (int i = 0; i < N; i++) {
      os << v.v[i] << " ";
    }
    os << std::string("]");
    return os;
  }
};


template<class T, std::size_t N>
constexpr Vector<T, N> operator*(const T& scalar, const Vector<T, N>& v) {
  return v * scalar;
}

template<typename T>
using Vector4 = Vector<T, 4>;
template<typename T>
using Vector3 = Vector<T, 3>;

template<typename T>
using Vector2 = Vector<T, 2>;

using Vector4f = Vector4<float>;
using Vector3f = Vector3<float>;
using Vector2f = Vector2<float>;

using Vector4i = Vector4<int>;
using Vector3i = Vector3<int>;
using Vector2i = Vector2<int>;
} // namespace VLA

namespace std {

template<typename T, size_t N>
struct formatter<VLA::Vector<T, N>, char> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

  template<typename Ctx>
  auto format(const VLA::Vector<T, N>& v, Ctx& ctx) const {
    auto out = ctx.out();
    *out++ = '[';
    for (std::size_t i = 0; i < N; ++i) {
      out = format_to(out, "{}{}", v.v[i], (i + 1 < N ? ' ' : ']'));
    }
    return out;
  }
};
} // namespace std

#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <ranges>

#include "Vector.hpp"


// TODO: Core transforms:
//  translation, rotation, scaling, view(camera), projection

// Column-major to interact natively with Vulkan without transposing.
namespace VLA {

/**
 * Column-major matrix
 * @tparam T Primitive type
 * @tparam M Row count
 * @tparam N Column count
 */
template<typename T, std::size_t M, std::size_t N>
struct Matrix {
  // NOTE: STD::ARRAY is zero overhead over raw C pointers.
  std::array<T, M * N> A;


  // ranges
  using iterator       = T *;
  using const_iterator = T const *;
  constexpr iterator begin() noexcept { return A.data(); }
  // NOTE: end() should point to one past the last element
  constexpr iterator                     end() noexcept { return A.data() + M * N; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return A.data(); }
  // NOTE: end() should point to one past the last element
  [[nodiscard]] constexpr const_iterator end() const noexcept { return A.data() + M * N; }
  [[nodiscard]] constexpr Matrix         operator+(Matrix const &rhs) const {
    Matrix result{*this};
    result += rhs;
    return result;
  }

  constexpr Matrix &operator+=(Matrix const &rhs) {
    for (std::size_t i = 0; i < M * N; ++i) {
      A[i] += rhs.A[i];
    }
    return *this;
  }


  struct RowProxy {
    Matrix     &mat;
    std::size_t index;
    constexpr   operator Vector<T, N>() const {
      Vector<T, N> out{};
      for (std::size_t i = 0; i < N; ++i)
        out[i] = mat(index, i);
      return out;
    }
    constexpr RowProxy &operator=(Vector<T, N> const &v) {
      for (std::size_t i = 0; i < N; ++i)
        mat(index, i) = v[i];
      return *this;
    }
    constexpr RowProxy &operator=(std::initializer_list<T> list) {
      std::size_t i = 0;
      for (auto v : list)
        mat(index, i++) = v;
      return *this;
    }
  };

  struct ColumnProxy {
    Matrix     &mat;
    std::size_t index;
    constexpr   operator Vector<T, M>() const {
      Vector<T, M> out{};
      for (std::size_t i = 0; i < M; ++i)
        out[i] = mat(i, index);
      return out;
    }
    constexpr ColumnProxy &operator=(Vector<T, M> const &v) {
      for (std::size_t i = 0; i < M; ++i)
        mat(i, index) = v[i];
      return *this;
    }
    constexpr ColumnProxy &operator=(std::initializer_list<T> list) {
      std::size_t i = 0;
      for (auto v : list)
        mat(i++, index) = v;
      return *this;
    }
  };

  [[nodiscard]] constexpr Vector<T, N> Row(std::size_t const index) const & {
    assert(index < M && "Row index specified cannot be higher than the amount of rows.");
    Vector<T, N> result{};

    for (std::size_t i{0}; i < N; i++) {
      result[i] = A[i * M + index];
    }
    return result;
  }

  [[nodiscard]] constexpr RowProxy Row(std::size_t const index) & {
    assert(index < M);

    return {*this, index};
  }

  [[nodiscard]] constexpr Vector<T, M> Column(std::size_t const index) const & {
    assert(index < N && "Column index specified cannot be higher than the amount of columns");
    Vector<T, M> result{};

    for (std::size_t i{0}; i < M; i++) {
      result[i] = A[index * M + i];
    }
    return result;
  }

  [[nodiscard]] constexpr ColumnProxy Column(std::size_t const index) & {
    assert(index < N);
    return {*this, index};
  }

  [[nodiscard]] constexpr Matrix<T, N, M> Transposed() const {
    Matrix<T, N, M> out{};

    for (std::size_t r = 0; r < M; ++r) {
      for (std::size_t c = 0; c < N; ++c) {
        out.A[r * N + c] = A[c * M + r];
      }
    }
    return out;
  }

  // NOTE: Was thinking about implementing determinants, however they are not really that useful.
  // Rather implement echelon operations for reducing matrices into identity matrix to find inverse.
  // Or to triangular matrices for determinants, which i presume might be faster since it doesnt
  // require recursion.

  [[nodiscard]] constexpr T Determinant() const {
    // TODO: row reduce until triangular.    //  then calculate determinant.

    // no need for this yet.
    return {};
  }

  // TODO: General determinant calculation for N x M matrix
  // STUDY: https://en.wikipedia.org/wiki/Leibniz_formula_for_determinants
  //  https://en.wikipedia.org/wiki/Determinant

  constexpr bool operator==(Matrix const &other) const { return A == other.A; }
  // operator overloads


  constexpr T       &operator[](std::size_t idx) { return A[idx]; }
  constexpr T const &operator[](std::size_t idx) const { return A[idx]; }

  template<std::size_t R>
  constexpr Matrix<T, M, R> operator*(Matrix<T, N, R> const right) const {
    Matrix<T, M, R> result;

    for (std::size_t i{0}; i < M; i++) {
      for (std::size_t j{0}; j < R; j++) {
        result.A[j * M + i] = Vector<T, N>::Dot(Row(i), right.Column(j));
      }
    }
    return result;
  }


  constexpr Vector<T, M> operator*(Vector<T, N> const &v) const {
    Vector<T, M> result{};

    for (std::size_t row{0}; row < M; row++) {
      for (std::size_t col{0}; col < N; col++) {
        result[row] += v[col] * A[col * M + row];
      }
    }

    return result;
  }

  constexpr Matrix operator*(T const &scalar) const {
    Matrix result{};
    std::ranges::copy(A | std::views::transform([scalar](T elem) { return elem * scalar; }),
                      std::data(result.A));
    return result;
  }


  constexpr friend std::ostream &operator<<(std::ostream &os, Matrix const &a) {
    auto const colCount = N;

    for (std::size_t i{0}; i < colCount; i++) {
      auto v = a.Row(i);
      os << v << '\n';
    }
    return os;
  }
  constexpr T       &operator()(size_t row, size_t col) { return A[col * M + row]; }
  constexpr T const &operator()(size_t row, size_t col) const { return A[col * M + row]; }

  [[nodiscard]] static constexpr Matrix Identity() {
    Matrix m{};
    for (size_t i = 0; i < std::min(M, N); ++i) {
      m(i, i) = T(1);
    }
    return m;
  }

  static constexpr Matrix Ortho(T left, T right, T bottom, T top, T near, T far) {
    Matrix m = Identity();
    m(0, 0)  = T(2) / (right - left);
    m(1, 1) =
        T(2) / (top - bottom); // Standard Ortho, Y-flip handled by caller or viewport if needed
    m(2, 2) = T(1) / (far - near);
    m(0, 3) = -(right + left) / (right - left);
    m(1, 3) = -(top + bottom) / (top - bottom);
    m(2, 3) = -near / (far - near);
    return m;
  }

  static constexpr Matrix Perspective(T fovY, T aspect, T near, T far) {
    T      focalLength = T(1) / std::tan(fovY / T(2));
    Matrix m{};
    m(0, 0) = focalLength / aspect;
    m(1, 1) = focalLength;
    m(2, 2) = far / (far - near);
    m(2, 3) = -(far * near) / (far - near);
    m(3, 2) = T(1);
    m(3, 3) = T(0);
    return m;
  }

  //~ Transformations
  [[nodiscard]] static constexpr Matrix Translation(Vector<T, 3> const &v) {
    Matrix m = Identity();
    m(0, 3)  = v[0];
    m(1, 3)  = v[1];
    m(2, 3)  = v[2];
    return m;
  }
  [[nodiscard]] static constexpr Matrix RotationX(T angle) {
    T      c = std::cos(angle);
    T      s = std::sin(angle);
    Matrix m = Identity();
    m(1, 1)  = c;
    m(1, 2)  = -s;
    m(2, 1)  = s;
    m(2, 2)  = c;
    return m;
  }

  [[nodiscard]] static constexpr Matrix RotationY(T angle) {
    T      c = std::cos(angle);
    T      s = std::sin(angle);
    Matrix m = Identity();
    m(0, 0)  = c;
    m(0, 2)  = s;
    m(2, 0)  = -s;
    m(2, 2)  = c;
    return m;
  }

  [[nodiscard]] static constexpr Matrix RotationZ(T angle) {
    T      c = std::cos(angle);
    T      s = std::sin(angle);
    Matrix m = Identity();
    m(0, 0)  = c;
    m(0, 1)  = -s;
    m(1, 0)  = s;
    m(1, 1)  = c;
    return m;
  }
  [[nodiscard]] static constexpr Matrix Scale(Vector<T, 3> const &v) {
    Matrix m = Identity();
    m(0, 0)  = v[0];
    m(1, 1)  = v[1];
    m(2, 2)  = v[2];
    return m;
  }
  [[nodiscard]] static constexpr Matrix
  LookAt(Vector<T, 3> const &eye, Vector<T, 3> const &center, Vector<T, 3> const &up) {
    Vector<T, 3> f   = center - eye;
    T            len = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f                = f * (T(1) / len);
    Vector<T, 3> s   = Vector<T, 3>::Cross(f, up);
    len              = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s                = s * (T(1) / len);
    Vector<T, 3> u   = Vector<T, 3>::Cross(s, f);

    Matrix m{};
    m(0, 0) = s[0];
    m(0, 1) = s[1];
    m(0, 2) = s[2];
    m(1, 0) = u[0];
    m(1, 1) = u[1];
    m(1, 2) = u[2];
    m(2, 0) = -f[0];
    m(2, 1) = -f[1];
    m(2, 2) = -f[2];
    m(0, 3) = -Vector<T, 3>::Dot(s, eye);
    m(1, 3) = -Vector<T, 3>::Dot(u, eye);
    m(2, 3) = Vector<T, 3>::Dot(f, eye);
    return m;
  }
  [[nodiscard]] constexpr Matrix ModelMatrix(Vector<T, 3> const &position,
                                             Vector<T, 3> const &rotation,
                                             Vector<T, 3> const &scale) const {
    return Translation(position) * RotationX(rotation[0]) * RotationY(rotation[1]) *
           RotationZ(rotation[2]) * Scale(scale);
  }

  // TODO: billboarding
  // TODO: shear
  // TODO: inverse/ transpose
  // TODO: decomposition
};

template<class T, std::size_t M, std::size_t N>
constexpr Matrix<T, M, N> operator*(T const &scalar, Matrix<T, M, N> const &a) {
  return a * scalar;
}

using Matrix4x4f = Matrix<float, 4, 4>;

} // namespace VLA

namespace std {

template<>
struct formatter<VLA::Matrix4x4f, char> : formatter<std::string_view, char> {
  using base = formatter<std::string_view, char>;

  constexpr auto parse(std::format_parse_context &ctx) {
    return base::parse(ctx); // enables {:<30}, {:^30}, etc.
  }

  auto format(VLA::Matrix4x4f const &m, std::format_context &ctx) const {
    // Example: translation only (adjust for your matrix convention)
    auto const t = m.Column(3);

    std::array<char, 64> buf{};
    auto                 it =
        std::format_to_n(buf.begin(), buf.size() - 1, "T({:.2f},{:.2f},{:.2f})", t[0], t[1], t[2])
            .out;
    std::string_view sv{buf.data(), static_cast<size_t>(it - buf.begin())};

    return base::format(sv, ctx); // applies the outer field width/alignment
  }
};

} // namespace std
// NOTE: TESTS

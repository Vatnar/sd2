#pragma once
#include <cstdint>

using S8 = std::int8_t;
using S16 = std::int16_t;
using S32 = std::int32_t;
using S64 = std::int64_t;

using U8 = std::uint8_t;
using U16 = std::uint16_t;
using U32 = std::uint32_t;
using U64 = std::uint64_t;

using F32 = float;
using F64 = double;

#if defined(_MSC_VER)
#define TRAP() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define TRAP() __builtin_trap()
#else
#error Unknown trap intrinsic for compiler.
#endif
#define ASSERT_ALWAYS(x) \
  do {                   \
    if (!(x)) {          \
      TRAP();            \
    }                    \
  } while (0)

#ifdef SD_DEBUG
#define ASSERT(x) ASSERT_ALWAYS(x)
#else
#define ASSERT(x) (void)(x)
#endif
#define INVALID_PATH    ASSERT_ALWAYS(!"Invalid Path!")
#define NOT_IMPLEMENTED ASSERT_ALWAYS(!"Not Implemented!")
#define NO_OP           ((void)0)


#if defined(__clang__)
#define FORCE_INLINE [[gnu::always_inline]] [[gnu::gnu_inline]] extern inline
#elif defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#pragma warning(error: 4714) // 'function' marked as __forceinline not inlined
#define FORCE_INLINE __forceinline
#else
#error "FORCE_INLINE not supported on this compiler"
#endif


#define internal      static
#define local_persist static


#if defined(__GNUC__) || defined(__clang__) || (defined(_MSC_VER) && (_MSC_VER >= 1926))
#define SD_FILE __builtin_FILE()
#define SD_LINE __builtin_LINE()
#else
#error "__buitin_FILE() not defined for your compiler"
#endif

template<typename T>
FORCE_INLINE T constexpr align_pow2(T x, T b) {
  return (x + b - 1) & ~(b - 1);
}

template<typename T>
FORCE_INLINE T constexpr next_pow2(T x) {
  if (x <= 1)
    return 1;
  --x;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  if constexpr (sizeof(T) == 8) {
    x |= x >> 32;
  }
  return x + 1;
}

consteval U64 kb(U64 n) {
  return n << 10;
}

consteval U64 mb(U64 n) {
  return n << 20;
}

consteval U64 gb(U64 n) {
  return n << 30;
}

consteval U64 tb(U64 n) {
  return n << 40;
}

template<typename T>
FORCE_INLINE T constexpr min(T a, T b) {
  return a < b ? a : b;
}

template<typename T>
FORCE_INLINE T constexpr max(T a, T b) {
  return a > b ? a : b;
}

template<typename T>
FORCE_INLINE T constexpr clamp_top(T a, T x) {
  return min(a, x);
}

template<typename T>
FORCE_INLINE T constexpr clamp_bot(T x, T b) {
  return max(x, b);
}

template<typename T>
FORCE_INLINE T constexpr clamp(T a, T x, T b) {
  if (x < a) {
    return a;
  }
  if (x > b) {
    return b;
  }
  return x;
}

FORCE_INLINE bool checked_add_u64(U64 a, U64 b, U64 *out) {
  if (a > UINT64_MAX - b)
    return false;
  *out = a + b;
  return true;
}

FORCE_INLINE bool checked_mul_u64(U64 a, U64 b, U64 *out) {
  if (a != 0 && b > UINT64_MAX / a)
    return false;
  *out = a * b;
  return true;
}

FORCE_INLINE bool checked_align_pow2_u64(U64 x, U64 align, U64 *out) {
  ASSERT_ALWAYS(align != 0);
  ASSERT_ALWAYS(std::has_single_bit(align));
  U64 add = align - 1;
  if (x > UINT64_MAX - add)
    return false;
  *out = (x + add) & ~add;
  return true;
}


//~ SourceLocation
struct SourceLocation {
  char const *file;
  unsigned line;

  static consteval SourceLocation current(char const *file = __builtin_FILE(), unsigned line = __builtin_LINE()) {
    return {file, line};
  }
};

template<typename T>
constexpr bool any(T val) {
  return std::to_underlying(val) != 0;
}

#define MemoryCopy(dst, src, size) __builtin_memmove((dst), (src), (size))


//~ String8
struct String8 {
  U8 *str;
  U64 size;
};

internal String8 str8(U8 *str, U64 size) {
  String8 result{str, size};
  return result;
}

#define str8_lit(S) str8((U8 *)(S), sizeof(S) - 1)

inline std::string to_std_string(String8 s) {
  return {reinterpret_cast<char const *>(s.str), s.size};
}

//~ Ringbuffer
// overwrites on full
template<typename T, U64 SIZE = 256>
  requires((SIZE & (SIZE - 1)) == 0)
struct RingBuffer {
  T buffer[SIZE]{};

  U64 head{};
  U64 tail{};
  bool full{false};

  static constexpr U64 MASK = SIZE - 1;

  [[nodiscard]] bool empty() const { return !full && head == tail; }

  void push(T item) {
    buffer[head & MASK] = item;
    head++;
    if (full) {
      tail++;
    } else if ((head & MASK) == (tail & MASK)) {
      full = true;
    }
  }

  bool pop(T &out) {
    if (empty())
      return false;
    out = buffer[tail & MASK];
    tail++;
    full = false;
    return true;
  }

  // only valid for nonempty, caller needs to check
  T &get_last() { return buffer[(head - 1) & MASK]; }
};

//~ vecs
template<typename T>
struct Vec2 {
  union {
    T x;
    T u;
  };

  union {
    T y;
    T v;
  };


  Vec2 &operator+=(const Vec2 &other) {
    this->x += other.x;
    this->y += other.y;
    return *this;
  }

  Vec2 operator+(const Vec2 &other) {
    Vec2 result = *this;
    result += other;
    return result;
  }

  Vec2 &operator-=(const Vec2 &other) {
    this->x -= other.x;
    this->y -= other.y;
    return *this;
  }

  Vec2 operator-(const Vec2 &other) {
    Vec2 result = *this;
    result -= other;
    return result;
  }
};

template<typename T>
struct Vec3 {
  union {
    T x;
    T u;
    T r;
  };

  union {
    T y;
    T v;
    T g;
  };

  union {
    T z;
    T w;
    T b;
  };
};

template<typename T>
struct Vec4 {
  union {
    T i;
    T r;
  };

  union {
    T j;
    T g;
  };

  union {
    T k;
    T b;
  };

  union {
    T l;
    T a;
  };
};

template<U64 SIZE>
struct BitSet {
  U64 bits[max(1UL, next_pow2(SIZE) >> 6)];

  FORCE_INLINE void set(U64 idx) {
    ASSERT(idx < SIZE);
    bits[idx >> 6] |= 1ULL << (idx & 63);
  }

  FORCE_INLINE void clear(U64 idx) {
    ASSERT(idx < SIZE);
    bits[idx >> 6] &= ~(1ULL << (idx & 63));
  }

  FORCE_INLINE bool get(U64 idx) const {
    ASSERT(idx < SIZE);
    return (bits[idx >> 6] & (1ULL << (idx & 63))) != 0;
  }

  FORCE_INLINE bool operator[](U64 idx) const {
    return get(idx);
  }

  static U64 constexpr size() {
    return next_pow2(SIZE);
  }
};

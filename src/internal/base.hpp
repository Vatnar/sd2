#pragma once
#include <cstdint>

using S8  = std::int8_t;
using S16 = std::int16_t;
using S32 = std::int32_t;
using S64 = std::int64_t;

using U8  = std::uint8_t;
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
#define INVALID_PATH    ASSERT(!"Invalid Path!")
#define NOT_IMPLEMENTED ASSERT(!"Not Implemented!")
#define NO_OP           ((void)0)

#if defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE [[gnu::always_inline]] inline
#else
#error "Not defined for your compiler"
#endif

#define internal      static
#define local_persist static


#if defined(__GNUC__) || defined(__clang__) || (defined(_MSC_VER) && (_MSC_VER >= 1926))
#define SD_FILE __builtin_FILE()
#define SD_LINE __builtin_LINE()
#else
#error "__buitin_FILE() not defined for your compiler"
#endif

//~ SourceLocation
struct SourceLocation {
  char const *file;
  unsigned    line;

  static consteval SourceLocation current(char const *file = __builtin_FILE(),
                                          unsigned    line = __builtin_LINE()) {
    return {file, line};
  }
};

template<typename T>
constexpr bool any(T val) {
  return std::to_underlying(val) != 0;
}

#define MemoryCopy(dst, src, size) __builtin_memmove((dst), (src), (size))

//~ Array
template<typename T, U64 SIZE>
struct Array {
  T                    data[SIZE];
  static constexpr U64 size() { return SIZE; }
  static constexpr U64 array_size() { return sizeof(T) * SIZE; }
  using value_type = T;
};

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

  U64  head{};
  U64  tail{};
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

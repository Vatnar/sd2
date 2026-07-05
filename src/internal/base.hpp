#pragma once
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

struct SourceLocation {
  const char* file;
  unsigned    line;

  static consteval SourceLocation current(const char* file = __builtin_FILE(),
                                          unsigned    line = __builtin_LINE()) {
    return {file, line};
  }
};

template<typename T>
constexpr bool any(T val) {
  return std::to_underlying(val) != 0;
}

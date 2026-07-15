#pragma once
#include "base.hpp"

template<typename T>
FORCE_INLINE T constexpr align_pow2(T x, T b) {
  return (x + b - 1) & ~(b - 1);
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

enum class ArenaFlags : U64 {
  NONE        = 0,
  NO_CHAIN    = (1 << 0),
  LARGE_PAGES = (1 << 1),
};
constexpr ArenaFlags operator|(ArenaFlags lhs, ArenaFlags rhs) {
  return static_cast<ArenaFlags>(static_cast<U64>(lhs) | static_cast<U64>(rhs));
}

constexpr ArenaFlags operator&(ArenaFlags lhs, ArenaFlags rhs) {
  return static_cast<ArenaFlags>(static_cast<U64>(lhs) & static_cast<U64>(rhs));
}

struct ArenaParams {
  ArenaFlags     flags                   = ArenaFlags::NONE;
  U64            reserve_size            = mb(64uz);
  U64            commit_size             = kb(64uz);
  void          *optional_backing_buffer = nullptr;
  SourceLocation location                = SourceLocation::current();
  char const    *name                    = nullptr;
};


struct Arena;
struct Temp {
  Arena *arena;
  U64    pos;

  void end();
};
struct TempScope {
  Temp t;
  explicit TempScope(Temp temp) : t(temp) {}
  TempScope(TempScope const &)            = delete;
  TempScope &operator=(TempScope const &) = delete;
  TempScope(TempScope &&other) noexcept : t(other.t) {
    other.t.arena = nullptr;
    other.t.pos   = 0;
  }
  TempScope &operator=(TempScope &&other) noexcept {
    if (this != &other) {
      if (t.arena)
        t.end();
      t             = other.t;
      other.t.arena = nullptr;
      other.t.pos   = 0;
    }
    return *this;
  }
  ~TempScope() {
    if (t.arena)
      t.end();
  }
};

struct Arena {
  Arena         *prev;
  Arena         *current;
  ArenaFlags     flags;
  U64            commit_size;
  U64            reserve_size;
  U64            base_position;
  U64            position;
  U64            committed;
  U64            reserved;
  SourceLocation location;
  char const    *name;
  bool           owns_memory;

  void *push(U64 size, U64 align, bool zero);
  U64   pos();
  void  pop_to(U64 pos);
  void  clear();
  void  pop(U64 amount);

  [[nodiscard]] Temp      temp_begin();
  [[nodiscard]] TempScope temp_scope();

  //~ helpers
  template<typename T>
  T *push_array_no_zero_aligned(U64 count, U64 align) {
    U64 size = 0;
    ASSERT_ALWAYS(checked_mul_u64(sizeof(T), count, &size));
    return static_cast<T *>(push(size, align, false));
  }
  template<typename T>
  T *push_array_aligned(U64 count, U64 align) {
    U64 size = 0;
    ASSERT_ALWAYS(checked_mul_u64(sizeof(T), count, &size));
    return static_cast<T *>(push(size, align, true));
  }
  template<typename T>
  T *push_array_no_zero(U64 count) {
    return push_array_no_zero_aligned<T>(count, max(8uz, alignof(T)));
  }
  template<typename T>
  T *push_array(U64 count) {
    return push_array_aligned<T>(count, max(8uz, alignof(T)));
  }
};

template<typename T>
T *arena_push(Arena *arena) {
  return arena->push_array<T>(1);
}
template<typename T>
T *arena_push_no_zero(Arena *arena) {
  return arena->push_array_no_zero<T>(1);
}

Arena *arena_alloc(ArenaParams params = {});
Arena *arena_release(Arena *arena);

#pragma once
#include "base.hpp"
#include "base_arena.hpp"


//~ String8
struct String8 {
  U8 *str;
  U64 size;
  bool operator==(const String8 &other) const;

  const char *to_c_str(Arena *arena) const {
    char *c_str = arena->push_array<char>(size + 1);
    for (U64 i = 0; i < size; i++) {
      c_str[i] = static_cast<char>(str[i]);
    }
    c_str[size] = '\0';
    return c_str;
  }
};

inline bool String8::operator==(const String8 &other) const {
  if (size != other.size)
    return false;
  if (str == other.str)
    return true;
  if (size == 0 && other.size == 0)
    return true;
  return std::memcmp(str, other.str, size) == 0;
}

internal String8 str8(U8 *str, U64 size) {
  String8 result{str, size};
  return result;
}

#define str8_lit(S) str8((U8 *)(S), sizeof(S) - 1)

inline std::string to_std_string(String8 s) {
  return {reinterpret_cast<char const *>(s.str), s.size};
}

#pragma once

#include <initializer_list>

#include "base.hpp"
#include "base_arena.hpp"

//~ Array
template<typename T, U64 SIZE>
struct Array {
  T data[SIZE];
  static constexpr U64 size() { return SIZE; }
  static constexpr U64 array_size() { return sizeof(T) * SIZE; }
  using value_type = T;

  operator T *() { return data; }
  operator T const *() const { return data; }

  T &operator[](U64 idx) {
    if (idx >= SIZE) {
      INVALID_PATH;
    }
    return data[idx];
  }
};

template<typename T, typename... U>
Array(T, U...) -> Array<T, 1 + sizeof...(U)>;

template<typename T, U64 SIZE>
void fill_array(Array<T, SIZE> &array, T value) {
  for (U64 i = 0; i < SIZE; i++) {
    array.data[i] = value;
  }
}

//~ DynArray
template<typename T>
struct DynArray {
  T *data{};
  U64 size{};
  U64 capacity{};

  using value_type = T;

  constexpr U64 byte_size() { return sizeof(T) * size; }
  constexpr bool empty() const { return size == 0; }

  T &operator[](U64 idx) {
    if (idx >= capacity) {
      INVALID_PATH;
    }
    return data[idx];
  }

  static DynArray with_capacity(Arena *arena, U64 cap) {
    DynArray out{};
    out.data = arena->push_array<T>(cap);
    out.size = 0;
    out.capacity = cap;
    return out;
  }

  static DynArray from_init(Arena *arena, std::initializer_list<T> init) {
    DynArray out{};
    out.data = arena->push_array<T>(init.size());
    out.size = init.size();
    out.capacity = init.size();

    U64 i = 0;
    for (auto const &e : init) {
      out.data[i++] = e;
    }
    return out;
  }

  operator T *() { return data; }
  operator T const *() const { return data; }
};

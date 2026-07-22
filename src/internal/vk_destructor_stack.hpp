#pragma once

#include "base_arena.hpp"

struct VKDtorEntry {
  VKDtorEntry *next{};
  void (*destroy)(void *data){};
  void *data{};
};

struct VKDtorStack;

struct VKDtorTemp {
  VKDtorStack *stack;
  VKDtorEntry *saved_head;

  void end();

  operator VKDtorStack *() const { return stack; }
};

struct DtorScopedTemp : VKDtorTemp {
  ~DtorScopedTemp() { end(); }
};

// Prepends vk destructor to stack to destroy in reverse order
struct VKDtorStack {
  VKDtorEntry *head{};
  Arena *arena{};

  template<typename T>
  T push(vk::UniqueHandle<T, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE> unique) {
    T raw = unique.get();
    using UT = vk::UniqueHandle<T, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>;
    UT *stored = arena->push_array_no_zero<UT>(1);
    new(stored) UT(std::move(unique));
    VKDtorEntry *entry = arena_push_no_zero<VKDtorEntry>(arena);
    entry->data = stored;
    entry->destroy = [](void *p) { static_cast<UT *>(p)->~UT(); };
    entry->next = head;
    head = entry;
    return raw;
  }

  void push_raw(void *data, void (*destroy)(void *)) {
    VKDtorEntry *entry = arena_push_no_zero<VKDtorEntry>(arena);
    entry->data = data;
    entry->destroy = destroy;
    entry->next = head;
    head = entry;
  }

  [[nodiscard]] VKDtorTemp temp_begin() { return {this, head}; }
  [[nodiscard]] DtorScopedTemp scoped() { return {this, head}; }

  void drain() {
    while (head) {
      head->destroy(head->data);
      head = head->next;
    }
  }

  ~VKDtorStack() {
    drain();
  }
};

inline void VKDtorTemp::end() {
  if (!stack)
    return;
  while (stack->head && stack->head != saved_head) {
    VKDtorEntry *entry = stack->head;
    entry->destroy(entry->data);
    stack->head = entry->next;
  }
  stack = nullptr;
}
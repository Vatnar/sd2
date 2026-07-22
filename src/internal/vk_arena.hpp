#pragma once

#include "base_arena.hpp"
#include "vk_destructor_stack.hpp"

// Like a normal arena, but supports a DtorStack, which is used for automatically freeing vulkan objects when the arena is freed
struct VKArena;

struct VKArenaTemp {
  VKArena *va;
  U64 arena_pos;
  VKDtorEntry *ds_head;

  void end();

  operator Arena *() const;
  operator VKDtorStack *() const;
  operator VKArena *() const;
};

struct VKArenaScoped : VKArenaTemp {
  ~VKArenaScoped() { end(); }
};

struct VKArena {
  Arena *arena{};
  VKDtorStack ds{};

  VKArena() = default;

  VKArena(Arena *a) {
    init(a);
  }

  void init(Arena *a) {
    arena = a;
    ds = {.head = nullptr, .arena = a};
  }

  void drain() {
    ds.drain();
    if (arena)
      arena->clear();
  }

  void release() {
    ds.drain();
    arena_release(arena);
    arena = nullptr;
  }

  operator Arena *() const { return arena; }

  [[nodiscard]] VKArenaTemp temp_begin() { return {this, arena->pos(), ds.head}; }
  [[nodiscard]] VKArenaScoped scoped() { return {this, arena->pos(), ds.head}; }
};

inline void VKArenaTemp::end() {
  if (!va)
    return;
  while (va->ds.head && va->ds.head != ds_head) {
    VKDtorEntry *entry = va->ds.head;
    entry->destroy(entry->data);
    va->ds.head = entry->next;
  }
  va->arena->pop_to(arena_pos);
  va = nullptr;
}

inline VKArenaTemp::operator Arena *() const { return va->arena; }
inline VKArenaTemp::operator VKDtorStack *() const { return &va->ds; }
inline VKArenaTemp::operator VKArena *() const { return va; }

//--- Scratch VK Arena system (thread-local, 2 arenas, conflict-avoiding) ---

inline thread_local VKArena g_vk_scratch_arenas[2] = {};

inline void vk_scratch_init() {
  g_vk_scratch_arenas[0].init(arena_alloc({.name = "vk-scratch-0"}));
  g_vk_scratch_arenas[1].init(arena_alloc({.name = "vk-scratch-1"}));
}

inline void vk_scratch_release() {
  if (g_vk_scratch_arenas[0].arena) {
    g_vk_scratch_arenas[1].release();
    g_vk_scratch_arenas[0].release();
  }
}

inline VKArena *vk_get_scratch(Arena **conflicts, U64 count) {
  if (!g_vk_scratch_arenas[0].arena)
    vk_scratch_init();
  for (auto &g_vk_scratch_arena : g_vk_scratch_arenas) {
    bool has_conflict = false;
    for (U64 j = 0; j < count; j++) {
      if (g_vk_scratch_arena.arena == conflicts[j]) {
        has_conflict = true;
        break;
      }
    }
    if (!has_conflict)
      return &g_vk_scratch_arena;
  }
  TRAP();
}

#define vk_scratch_begin(conflicts, count) \
  vk_get_scratch((conflicts), (count))->temp_begin()
#define vk_scratch_begin_scoped(conflicts, count) \
  vk_get_scratch((conflicts), (count))->scoped()
#define vk_scratch_end(scratch) \
  (scratch).end()

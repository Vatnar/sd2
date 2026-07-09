#pragma once


#include "arena.hpp"


struct ThreadCtx {
  Arena* scratch_arenas[2];
};

inline thread_local ThreadCtx g_thread_ctx = {};

inline void thread_ctx_init() {
  g_thread_ctx.scratch_arenas[0] = arena_alloc({.name = "scratch-0"});
  g_thread_ctx.scratch_arenas[1] = arena_alloc({.name = "scratch-1"});
}

inline void thread_ctx_release() {
  if (g_thread_ctx.scratch_arenas[0]) {
    arena_release(g_thread_ctx.scratch_arenas[1]);
    arena_release(g_thread_ctx.scratch_arenas[0]);
    g_thread_ctx = {};
  }
}

inline Arena* get_scratch(Arena** conflicts, U64 count) {
  if (!g_thread_ctx.scratch_arenas[0]) {
    thread_ctx_init();
  }
  for (U64 i = 0; i < 2; i++) {
    bool has_conflict = false;
    for (U64 j = 0; j < count; j++) {
      if (g_thread_ctx.scratch_arenas[i] == conflicts[j]) {
        has_conflict = true;
        break;
      }
    }
    if (!has_conflict) {
      return g_thread_ctx.scratch_arenas[i];
    }
  }
  TRAP();
}

#define scratch_begin(conflicts, count) \
  get_scratch((conflicts), (count))->temp_begin()
#define scratch_end(scratch) \
  (scratch).end()

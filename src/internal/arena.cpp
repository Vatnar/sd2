#include <bit>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <sys/mman.h>

#include "../sd2_inc.hpp"


#define SLLStackPush_N(f, n, next) ((n)->next = (f), (f) = (n))
#define SLLStackPop_N(f, next)     ((f) = (f)->next)

#define SLLStackPush(f, n)         SLLStackPush_N(f, n, next)
#define SLLStackPop(f)             SLLStackPop_N(f, next)

#define MemoryZero(s, z)           memset((s), 0, (z))

#if defined(__SANITIZE_ADDRESS__)
extern "C" void __asan_poison_memory_region(void const volatile *addr, size_t size);
extern "C" void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
#define AsanPoisonMemoryRegion(addr, size)   __asan_poison_memory_region((addr), (size))
#define AsanUnpoisonMemoryRegion(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#define AsanPoisonMemoryRegion(addr, size)   ((void)(addr), (void)(size))
#define AsanUnpoisonMemoryRegion(addr, size) ((void)(addr), (void)(size))
#endif


void *reserve_memory(U64 size) {
  void *result = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (result == MAP_FAILED) {
    result = nullptr;
  }
  return result;
}
void *reserve_memory_large(U64 size) {
  void *result = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
  if (result == MAP_FAILED) {
    result = nullptr;
  }
  return result;
}

bool commit_memory(void *ptr, U64 size) {
  return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
}

bool commit_memory_large(void *ptr, U64 size) {
  return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
}

bool release_memory(void *ptr, U64 size) {
  return munmap(ptr, size) == 0;
}

U64 get_large_page_size() {
  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp) {
    TRAP();
  }

  char label[64]{};
  U64  value{};
  char unit[16]{};
  bool found = false;

  while (fscanf(fp, "%63s %" SCNu64 " %15s", label, &value, unit) != EOF) {
    if (strcmp(label, "Hugepagesize:") == 0) {
      found = true;
      break;
    }
  }
  fclose(fp);

  if (!found) {
    TRAP();
  }
  return value * 1024uz;
}

U64 get_page_size() {
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size == -1) {
    TRAP();
  }
  return static_cast<U64>(page_size);
}


Arena *arena_alloc(ArenaParams const params) {
  U64 reserve_size = params.reserve_size;
  U64 commit_size  = params.commit_size;

  local_persist U64 large_page_size = get_large_page_size();
  local_persist U64 page_size       = get_page_size();

  void *base = params.optional_backing_buffer;
  if (base == nullptr) {
    if (any(params.flags & ArenaFlags::LARGE_PAGES)) {
      reserve_size = align_pow2(reserve_size, large_page_size);
      commit_size  = align_pow2(commit_size, large_page_size);
    } else {
      reserve_size = align_pow2(reserve_size, page_size);
      commit_size  = align_pow2(commit_size, page_size);
    }
    ASSERT_ALWAYS(reserve_size >= sizeof(Arena));
    ASSERT_ALWAYS(commit_size >= sizeof(Arena));
    ASSERT_ALWAYS(commit_size <= reserve_size);

    if (any(params.flags & ArenaFlags::LARGE_PAGES)) {
      base = reserve_memory_large(reserve_size);
      ASSERT_ALWAYS(base != nullptr);
      ASSERT_ALWAYS(commit_memory_large(base, commit_size));
    } else {
      base = reserve_memory(reserve_size);
      ASSERT_ALWAYS(base != nullptr);
      ASSERT_ALWAYS(commit_memory(base, commit_size));
    }

    AsanPoisonMemoryRegion(base, commit_size);
  } else {
    ASSERT_ALWAYS(params.reserve_size >= sizeof(Arena));
    ASSERT_ALWAYS(params.commit_size <= params.reserve_size);
    ASSERT_ALWAYS(commit_size >= sizeof(Arena));
    ASSERT_ALWAYS(params.optional_backing_buffer == nullptr ||
                  any(params.flags & ArenaFlags::NO_CHAIN));
    AsanPoisonMemoryRegion(base, params.reserve_size);
  }

  if (base == nullptr) [[unlikely]] {
    TRAP();
  }

  AsanUnpoisonMemoryRegion(base, sizeof(Arena));
  Arena *arena         = static_cast<Arena *>(base);
  arena->current       = arena;
  arena->flags         = params.flags;
  arena->commit_size   = commit_size;
  arena->reserve_size  = reserve_size;
  arena->base_position = 0;
  arena->position      = sizeof(Arena);
  arena->committed     = commit_size;
  arena->reserved      = reserve_size;
  arena->owns_memory   = (params.optional_backing_buffer == nullptr);
  arena->location      = params.location;
  arena->name          = params.name;

  // TODO: arenatable debug
  return arena;
}

Arena *arena_release(Arena *arena) {
  if (!arena)
    return nullptr;
  for (Arena *n{arena->current}, *prev = nullptr; n != nullptr; n = prev) {
    prev = n->prev;
    AsanUnpoisonMemoryRegion(n, n->owns_memory ? n->committed : n->reserved);
    if (n->owns_memory) {
      ASSERT_ALWAYS(release_memory(n, n->reserved));
    }
  }
  return nullptr;
}

void *Arena::push(U64 size, U64 align, bool zero) {
  // zero-size is allowed: returns current aligned position without advancing
  ASSERT_ALWAYS(align != 0);
  ASSERT_ALWAYS(std::has_single_bit(align));
  Arena *current  = this->current;
  U64    pos_pre  = 0;
  U64    pos_post = 0;
  ASSERT_ALWAYS(checked_align_pow2_u64(current->position, align, &pos_pre));
  ASSERT_ALWAYS(checked_add_u64(pos_pre, size, &pos_post));

  U64 size_to_zero = 0;
  if (zero) {
    U64 zero_end = min(current->committed, pos_post);
    size_to_zero = (zero_end > pos_pre) ? (zero_end - pos_pre) : 0;
  }

  if (current->reserved < pos_post && !any(this->flags & ArenaFlags::NO_CHAIN)) {
    Arena *new_block = nullptr;
    // TODO: free-list optional thing

    if (new_block == nullptr) {
      U64 res_size          = current->reserve_size;
      U64 commit_size       = current->commit_size;
      U64 min_block_size    = 0;
      U64 block_granularity = current->commit_size;

      ASSERT_ALWAYS(std::has_single_bit(block_granularity));
      ASSERT_ALWAYS(checked_add_u64(size, sizeof(Arena), &min_block_size));

      if (min_block_size > res_size) {
        ASSERT_ALWAYS(checked_align_pow2_u64(min_block_size, block_granularity, &res_size));
        commit_size = res_size;
      }

      new_block = arena_alloc({
          .flags        = current->flags,
          .reserve_size = res_size,
          .commit_size  = commit_size,
          .location     = current->location,
          .name         = current->name,
      });

      size_to_zero = 0;
    } else {
      size_to_zero = size;
    }

    ASSERT_ALWAYS(
        checked_add_u64(current->base_position, current->reserved, &new_block->base_position));
    SLLStackPush_N(this->current, new_block, prev);

    current = new_block;

    ASSERT_ALWAYS(checked_align_pow2_u64(current->position, align, &pos_pre));
    ASSERT_ALWAYS(checked_add_u64(pos_pre, size, &pos_post));
  }

  // commit new page
  if (current->committed < pos_post) {
    U64 commit_post_aligned = 0;
    ASSERT_ALWAYS(checked_add_u64(pos_post, current->commit_size - 1, &commit_post_aligned));
    commit_post_aligned -= commit_post_aligned % current->commit_size;

    U64 commit_post = min(commit_post_aligned, current->reserved);
    ASSERT_ALWAYS(commit_post >= current->committed);
    ASSERT_ALWAYS(commit_post >= pos_post);
    U64 commit_size = commit_post - current->committed;

    if (commit_size > 0) {
      U8 *commit_ptr = reinterpret_cast<U8 *>(current) + current->committed;
      if (any(current->flags & ArenaFlags::LARGE_PAGES)) {
        ASSERT_ALWAYS(commit_memory_large(commit_ptr, commit_size));
      } else {
        ASSERT_ALWAYS(commit_memory(commit_ptr, commit_size));
      }
      AsanPoisonMemoryRegion(commit_ptr, commit_size);
    }
    current->committed = commit_post;
  }

  void *result = nullptr;
  if (current->committed >= pos_post) {
    result            = reinterpret_cast<U8 *>(current) + pos_pre;
    current->position = pos_post;
    AsanUnpoisonMemoryRegion(result, size);
    MemoryZero(result, size_to_zero);
  }

  if (result == nullptr) [[unlikely]] {
    TRAP();
  }
  return result;
}

U64 Arena::pos() {
  U64 pos = current->base_position + current->position;
  return pos;
}
void Arena::pop_to(U64 pos) {
  U64    target  = max<U64>(sizeof(Arena), pos);
  Arena *current = this->current;

#if ARENA_FREE_LIST
#else
  while (current->prev != nullptr && target < current->base_position) {
    Arena *prev = current->prev;
    AsanUnpoisonMemoryRegion(current,
                             current->owns_memory ? current->committed : current->reserved);
    if (current->owns_memory) {
      ASSERT_ALWAYS(release_memory(current, current->reserved));
    }
    current = prev;
  }
#endif
  this->current = current;
  ASSERT_ALWAYS(target >= current->base_position);
  U64 new_pos = target - current->base_position;
  ASSERT_ALWAYS(new_pos >= sizeof(Arena));
  ASSERT_ALWAYS(new_pos <= current->position);
  AsanPoisonMemoryRegion(reinterpret_cast<U8 *>(current) + new_pos, (current->position - new_pos));
  current->position = new_pos;
}

void Arena::clear() {
  this->pop_to(0);
}

void Arena::pop(U64 amount) {
  U64 pos_old = this->pos();
  U64 pos_new = pos_old;
  if (amount < pos_old) {
    pos_new = pos_old - amount;
  }
  this->pop_to(pos_new);
}

Temp Arena::temp_begin() {
  Temp temp = {this, this->pos()};
  return temp;
}
TempScope Arena::temp_scope() {
  return TempScope{this->temp_begin()};
}

void Temp::end() {
  ASSERT_ALWAYS(this->arena != nullptr);
  ASSERT_ALWAYS(this->pos <= this->arena->pos());
  this->arena->pop_to(this->pos);
#if SD2_DEBUG
  this->arena = nullptr;
  this->pos   = 0;
#endif
}

#pragma once
#include <x86intrin.h>

struct Profile {
  U64         start{};
  char const *name{};

  Profile() = default;

  Profile(char const *name) : name{name} {
    _mm_lfence();
    start = __rdtsc();
  }

  ~Profile() noexcept {
    // silent — no logging
  }

  void begin(char const *new_name) {
    name = new_name;
    _mm_lfence();
    start = __rdtsc();
  }

  U64 end() {
    U32 aux;
    U64 end_tsc = __rdtscp(&aux);
    _mm_lfence();
    U64 elapsed = end_tsc - start;
    name        = nullptr;
    return elapsed;
  }

  Profile(Profile const &)            = delete;
  Profile &operator=(Profile const &) = delete;
  Profile(Profile &&)                 = delete;
  Profile &operator=(Profile &&)      = delete;
};

inline thread_local Profile g_active_profile{};

#define PROFILE(name)           \
  Profile _profile_##__LINE__ { \
    name                        \
  }
#define PROFILE_FUNCTION()  PROFILE(__func__)
#define PROFILE_START(name) g_active_profile.begin(name)
#define PROFILE_END()       g_active_profile.end()

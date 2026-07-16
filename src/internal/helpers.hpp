#pragma once
#include <ctime>
#include "arena.hpp"

internal String8 read_file(Arena *arena, String8 filename) {
  std::string   string_filename = to_std_string(filename);
  std::ifstream file(string_filename, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    ASSERT_ALWAYS((false && "Failed to open file"));
  }

  std::streampos const BUFFER_SIZE = file.tellg();
  U8                  *buffer      = arena->push_array<U8>(file.tellg());
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char *>(buffer), BUFFER_SIZE);
  file.close();
  return str8(buffer, BUFFER_SIZE);
}
internal F64 diff_ms(timespec const &a, timespec const &b) {
  return static_cast<F64>(b.tv_sec - a.tv_sec) * 1000.0 +
         static_cast<F64>(b.tv_nsec - a.tv_nsec) / 1e6;
}

struct FrameClock {
  timespec frame_start = {};
  timespec work_end    = {};
  timespec frame_end   = {};
  F64      target_ms{};

  void start() { clock_gettime(CLOCK_MONOTONIC, &frame_start); }

  void mark_work_done() { clock_gettime(CLOCK_MONOTONIC, &work_end); }

  void wait_for_target() {
    do {
      clock_gettime(CLOCK_MONOTONIC, &frame_end);
      F64 remaining = target_ms - diff_ms(frame_start, frame_end);
      if (remaining > 0.3) {
        std::this_thread::sleep_for(std::chrono::duration<F64, std::milli>(remaining - 0.2));
      }
    } while (diff_ms(frame_start, frame_end) < target_ms);
  }

  F64 total_ms() const { return diff_ms(frame_start, frame_end); }
  F64 work_ms() const { return diff_ms(frame_start, work_end); }
  F64 wait_ms() const { return total_ms() - work_ms(); }
};

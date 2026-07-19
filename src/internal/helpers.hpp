#pragma once
#include <ctime>
#include "base_arena.hpp"

internal AppWindow glfw_init_window(AppParams *app_params);

internal String8 read_file(Arena *arena, String8 filename) {
  std::string string_filename = to_std_string(filename);
  std::ifstream file(string_filename, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    ASSERT_ALWAYS((false && "Failed to open file"));
  }

  std::streampos const BUFFER_SIZE = file.tellg();
  U8 *buffer = arena->push_array<U8>(file.tellg());
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char *>(buffer), BUFFER_SIZE);
  file.close();
  return str8(buffer, BUFFER_SIZE);
}

internal F64 diff_ms(timespec const &a, timespec const &b) {
  return static_cast<F64>(b.tv_sec - a.tv_sec) * 1000.0 +
         static_cast<F64>(b.tv_nsec - a.tv_nsec) / 1e6;
}

FORCE_INLINE internal F32 ema_update(F32 current_avg, F32 new_val, F32 alpha) {
  return (alpha * new_val) + ((1.0f - alpha) * current_avg);
}

struct TimeReport {
  F32 total_ms{}, target_ms{}, work_ms{}, wait_ms{};
  F32 fps{}, target_fps{}, alpha{};
};

// Snapshots performance every report_interval waits
// TODO: consider averaging the report for last 10 frames or whatever, or like for all time scaled by recency
struct FrameClock {
  F64 target_ms{};

  TimeReport report{};
  F32 alpha = 0.05f;

  timespec frame_start = {};
  timespec work_end = {};
  timespec frame_end = {};

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
    write_report();
  }

  F64 total_ms() const { return diff_ms(frame_start, frame_end); }
  F64 work_ms() const { return diff_ms(frame_start, work_end); }
  F64 wait_ms() const { return total_ms() - work_ms(); }

  void write_report() {
    F32 new_target = static_cast<F32>(target_ms);
    F32 new_total = static_cast<F32>(total_ms());
    F32 new_wait = static_cast<F32>(wait_ms());
    F32 new_work = static_cast<F32>(work_ms());
    F32 new_fps = static_cast<F32>(1000.0f / total_ms());
    F32 new_tfps = static_cast<F32>(1000.0f / target_ms);

    report.target_ms = new_target;
    report.target_fps = new_tfps;
    report.alpha = alpha;

    static bool first_report = true;
    if (first_report) {
      report.total_ms = new_total;
      report.wait_ms = new_wait;
      report.work_ms = new_work;
      report.fps = new_fps;
      first_report = false;
      return;
    }
    // Blend the history with the new data scaled by recency
    report.total_ms = ema_update(report.total_ms, new_total, alpha);
    report.wait_ms = ema_update(report.wait_ms, new_wait, alpha);
    report.work_ms = ema_update(report.work_ms, new_work, alpha);
    report.fps = ema_update(report.fps, new_fps, alpha);
  }
};

constexpr U32 MAX_INSTANCE_EXTENSIONS = 16;
internal U32 glfw_get_required_extensions(char const **out_extensions, U32 max_extensions);

internal void handle_key_input(Key *input);
internal void handle_mouse_input(Mouse *mouse);

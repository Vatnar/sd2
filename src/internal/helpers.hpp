#pragma once
#include <ctime>
#include "base_arena.hpp"
#include "base_string.hpp"

internal AppWindow glfw_init_window(AppParams *app_params);


internal String8 read_file_str8(Arena *arena, String8 filename);

struct SPIRVBlob {
  U32 *words;
  U64 word_count;
  U64 byte_count;
};

internal SPIRVBlob read_spirv_blob(Arena *arena, String8 filename);

internal void write_file(String8 filename, void *mem, U64 offset, U64 length);

FORCE_INLINE internal F64 diff_ms(timespec const &a, timespec const &b) {
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

  FORCE_INLINE void start() { clock_gettime(CLOCK_MONOTONIC, &frame_start); }

  FORCE_INLINE void mark_work_done() { clock_gettime(CLOCK_MONOTONIC, &work_end); }

  void wait_for_target();

  [[nodiscard]] FORCE_INLINE F64 total_ms() const { return diff_ms(frame_start, frame_end); }
  [[nodiscard]] FORCE_INLINE F64 work_ms() const { return diff_ms(frame_start, work_end); }
  [[nodiscard]] FORCE_INLINE F64 wait_ms() const { return total_ms() - work_ms(); }

  void write_report();
};

constexpr U32 MAX_INSTANCE_EXTENSIONS = 16;
internal U32 glfw_get_required_extensions(char const **out_extensions, U32 max_extensions);

internal void handle_key_input(Key *input);
internal void handle_mouse_input(Mouse *mouse);

struct Camera {
  glm::vec3 camera_pos;
  glm::vec3 camera_front;

  F32 yaw;
  F32 pitch;
  F32 roll;

  static constexpr glm::vec3 world_up();

  void transform(Vec3<F32> rotation, Vec3<F32> translation);

  [[nodiscard]] glm::vec3 right() const;

  [[nodiscard]] glm::vec3 up() const;

  [[nodiscard]] glm::mat4 view() const;

  static Camera from_pos(glm::vec3 pos);
};

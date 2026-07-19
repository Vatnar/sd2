#pragma once

#include <meta>

struct PaletteAction {
  char const *name;

  enum class Kind : U8 {
    CALL,
    CALL_MEM,
  };

  union {
    void (*call0)();
    void (*call1)(void *);
  };

  void *mem{};
  Kind kind{Kind::CALL};

  void run() const {
    switch (kind) {
      case Kind::CALL:
        call0();
        break;
      case Kind::CALL_MEM:
        call1(mem);
        break;
      default: INVALID_PATH;
    }
  }

  static void toggle_bool_ptr(void *p) {
    bool &b = **static_cast<bool **>(p);
    b = !b;
  }

  template<std::meta::info Member>
  static consteval char const *member_name_cstr() {
    constexpr auto id = std::meta::identifier_of(Member);

    std::array<char, id.size() + 1> buf{};
    for (std::size_t i = 0; i < id.size(); ++i) {
      buf[i] = (id[i] == '_') ? ' ' : id[i];
    }
    buf[id.size()] = '\0';

    return std::define_static_string(buf);
  }

  template<std::meta::info Member>
  static PaletteAction toggle(DebugCtx &ctx) {
    static_assert(std::meta::is_nonstatic_data_member(Member));
    using member_type = [:std::meta::type_of(Member):];
    static_assert(std::is_same_v<member_type, bool *>);

    return {
        .name = member_name_cstr<Member>(),
        .call1 = &toggle_bool_ptr,
        .mem = &ctx.[:Member:],
        .kind = Kind::CALL_MEM
    };
  }

  static PaletteAction call(char const *name, void (*fn)()) {
    return {.name = name, .call0 = fn, .mem = nullptr, .kind = Kind::CALL};
  }

  static PaletteAction call_mem(char const *name, void (*fn)(void *), void *mem) {
    return {.name = name, .call1 = fn, .mem = mem, .kind = Kind::CALL_MEM};
  }
};

struct PaletteState {
  bool open;
  int selected;
  int prev_selected;
  char search[256];
  DynArray<PaletteAction> actions;
  float width;
};

internal void debug_ui_palette_init(PaletteState *state, DynArray<PaletteAction> actions);
internal void debug_ui_palette_toggle(PaletteState *state);
internal void debug_ui_palette_render(PaletteState *state);

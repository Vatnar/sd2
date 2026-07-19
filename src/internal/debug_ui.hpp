#pragma once

struct PaletteAction {
  char const *name;
  void (*fn)();
};

struct PaletteState {
  bool open;
  int selected;
  int prev_selected;
  char search[256];
  DynArray<PaletteAction> actions;
  float width;
};

internal void debug_ui_palette_init(PaletteState *state, Arena *arena, DynArray<PaletteAction> actions);
internal void debug_ui_palette_toggle(PaletteState *state);
internal void debug_ui_palette_render(PaletteState *state);

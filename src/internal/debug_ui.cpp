#include "../sd2_inc.hpp"
#include "debug_ui.hpp"
#include <imgui.h>

static bool fuzzy_match(char const *query, char const *target) {
  if (!query || !*query) return true;
  while (*target && *query) {
    if ((*target | 0x20) == (*query | 0x20)) ++query;
    ++target;
  }
  return !*query;
}

internal void debug_ui_palette_init(PaletteState *state, Arena *arena, DynArray<PaletteAction> actions) {
  state->actions.capacity = state->actions.size = actions.size;
  state->actions.data = arena->push_array<PaletteAction>(actions.size);
  U64 max_len = 0;
  for (U64 i = 0; i < actions.size; i++) {
    state->actions.data[i] = actions.data[i];
    U64 len = strlen(actions.data[i].name);
    if (len > max_len) max_len = len;
  }
  state->width = max_len * 8.0f + 60.0f;
}

internal void debug_ui_palette_toggle(PaletteState *state) {
  state->open = !state->open;
  if (state->open) {
    state->search[0] = '\0';
    state->selected = 0;
    state->prev_selected = 0;
  }
}

internal void debug_ui_palette_render(PaletteState *state) {
  ZoneScopedN("Palette");
  if (!state->open) return;

  float h = ImGui::GetMainViewport()->Size.y * 0.25f;
  if (h < 100) h = 100;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
  ImGui::SetNextWindowSize({state->width, h});
  ImGui::Begin("Palette", &state->open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
  ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputText("##s", state->search, sizeof(state->search));

  int visible_count = 0;
  for (int i = 0; i < (int)state->actions.size; i++)
    if (fuzzy_match(state->search, state->actions.data[i].name))
      visible_count++;

  if (visible_count && state->selected >= visible_count) state->selected = visible_count - 1;
  if (state->selected < 0) state->selected = 0;

  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && state->selected > 0) state->selected--;
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && state->selected < visible_count - 1) state->selected++;

  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0);
  ImGui::BeginChild("##list", {0, 0});
  int vi = 0;
  for (int i = 0; i < (int)state->actions.size; i++) {
    if (!fuzzy_match(state->search, state->actions.data[i].name)) continue;
    if (vi == state->selected && vi != state->prev_selected)
      ImGui::SetScrollHereY();
    if (ImGui::Selectable(state->actions.data[i].name, vi == state->selected)) {
      state->actions.data[i].fn(state->actions.data[i].ctx);
      state->open = false;
      state->search[0] = '\0';
    }
    if (vi == state->selected) ImGui::SetItemDefaultFocus();
    vi++;
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  state->prev_selected = state->selected;

  if (ImGui::IsKeyPressed(ImGuiKey_Enter) && visible_count) {
    int vi = 0;
    for (int i = 0; i < (int)state->actions.size; i++) {
      if (!fuzzy_match(state->search, state->actions.data[i].name)) continue;
      if (vi == state->selected) {
        state->actions.data[i].fn(state->actions.data[i].ctx);
        state->open = false;
        state->search[0] = '\0';
        break;
      }
      vi++;
    }
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    if (state->search[0]) state->search[0] = '\0';
    else state->open = false;
  }
  ImGui::End();
}

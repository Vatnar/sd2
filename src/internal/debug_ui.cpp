#include "../sd2_inc.hpp"
#include "debug_ui.hpp"
#include <imgui.h>

extern DebugCtx g_dbg_ctx;
internal bool fuzzy_match(char const *query, char const *target) {
  constexpr char LOWER_SPACE = 0x20;
  if (!query || !*query)
    return true;
  while (*target && *query) {
    if ((*target | LOWER_SPACE) == (*query | LOWER_SPACE))
      ++query;
    ++target;
  }
  return !*query;
}

internal void debug_ui_palette_init(PaletteState *state, DynArray<PaletteAction> actions) {
  state->actions = actions;
  U64 max_len = 0;
  for (U64 i = 0; i < actions.size; i++) {
    if (U64 len = strlen(actions.data[i].name); len > max_len)
      max_len = len;
  }
  state->width = static_cast<F32>(max_len) * 8.0f + 60.0f;
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
  if (!state->open)
    return;

  int visible_count = 0;
  for (U64 i = 0; i < state->actions.size; i++)
    if (fuzzy_match(state->search, state->actions.data[i].name))
      visible_count++;

  float max_h = ImGui::GetMainViewport()->Size.y * 0.25f;
  if (max_h < 100)
    max_h = 100;

  float item_h = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
  float content_h = ImGui::GetFrameHeightWithSpacing() + static_cast<F32>(visible_count) * item_h + ImGui::GetStyle().
                    WindowPadding.y * 2;
  float h = (content_h < max_h) ? content_h : max_h;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
  ImGui::SetNextWindowSize({state->width, h});
  ImGui::Begin("Palette", &state->open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
  ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputText("##s", state->search, sizeof(state->search));

  if (visible_count && state->selected >= visible_count)
    state->selected = visible_count - 1;
  if (state->selected < 0)
    state->selected = 0;

  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && state->selected > 0)
    state->selected--;
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && state->selected < visible_count - 1)
    state->selected++;

  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0);
  ImGui::BeginChild("##list", {0, 0});
  int vi = 0;
  for (U64 i = 0; i < state->actions.size; i++) {
    if (!fuzzy_match(state->search, state->actions.data[i].name))
      continue;
    if (vi == state->selected && vi != state->prev_selected)
      ImGui::SetScrollHereY();
    if (ImGui::Selectable(state->actions.data[i].name, vi == state->selected)) {
      state->actions.data[i].run();
      state->open = false;
      state->search[0] = '\0';
    }
    if (vi == state->selected)
      ImGui::SetItemDefaultFocus();
    vi++;
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  state->prev_selected = state->selected;

  if (ImGui::IsKeyPressed(ImGuiKey_Enter) && visible_count) {
    int vi = 0;
    for (U64 i = 0; i < state->actions.size; i++) {
      if (!fuzzy_match(state->search, state->actions.data[i].name))
        continue;
      if (vi == state->selected) {
        state->actions.data[i].run();
        g_dbg_ctx.last_command = state->actions.data[i].name;
        state->open = false;
        state->search[0] = '\0';
        break;
      }
      vi++;
    }
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    if (state->search[0])
      state->search[0] = '\0';
    else
      state->open = false;
  }
  ImGui::End();
}

internal void debug_ui_debug_ui(TimeReport *report) {
  local_persist bool debug_show_ui = true;
  local_persist bool debug_show_timings = true;
  local_persist bool debug_show_last_command = false;
  local_persist bool debug_show_cursor_info = false;

  [[unlikely]] if (!g_dbg_ctx.debug_show_window)
    g_dbg_ctx.debug_show_window = &debug_show_ui;
  [[unlikely]] if (!g_dbg_ctx.debug_show_last_command)
    g_dbg_ctx.debug_show_last_command = &debug_show_last_command;
  [[unlikely]] if (!g_dbg_ctx.debug_show_cursor_info)
    g_dbg_ctx.debug_show_cursor_info = &debug_show_cursor_info;
  [[unlikely]] if (!g_dbg_ctx.debug_show_timings)
    g_dbg_ctx.debug_show_timings = &debug_show_timings;

  //~ Debug UI
  if (debug_show_ui) {
    if (ImGui::Begin("debug",
                     nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_AlwaysAutoResize)) {
      if (debug_show_timings) {
        if (*g_dbg_ctx.paused) {
          ImGui::TextColored({0.0f, 0.2f, 0.2f, 1.0f}, "Paused");
        }
        char label[32];
        sprintf(label, "Timings  (bias: %d%%)", static_cast<S32>(report->alpha * 100.0f));
        ImGui::SeparatorText(label);
        if (ImGui::BeginTable("TimingsTable", 4, ImGuiTableFlags_SizingFixedFit)) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("ms:");
          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%.2f", report->total_ms);
          ImGui::TableSetColumnIndex(2);
          ImGui::Text("target:");
          ImGui::TableSetColumnIndex(3);
          ImGui::Text("%.2f", report->target_ms);

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("work:");
          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%.2f", report->work_ms);
          ImGui::TableSetColumnIndex(2);
          ImGui::Text("wait:");
          ImGui::TableSetColumnIndex(3);
          ImGui::Text("%.2f", report->wait_ms);

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("fps:");
          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%.2f", 1000.0 / report->total_ms);
          ImGui::TableSetColumnIndex(2);
          ImGui::Text("target:");
          ImGui::TableSetColumnIndex(3);
          ImGui::Text("%.2f", 1000.0 / report->target_ms);

          ImGui::EndTable();
        }
      }
      if (debug_show_last_command) {
        ImGui::SeparatorText("Last command:");
        ImGui::Text("%s", g_dbg_ctx.last_command);
      }
      if (debug_show_cursor_info) {
        ImGui::SeparatorText("Cursor");
        Mouse *mouse = &g_dbg_ctx.window->mouse;
        ImGui::Text("Current pos: { %f, %f }", mouse->current_pos.x, mouse->current_pos.y);
        ImGui::Text("Delta pos: { %f, %f }", mouse->delta_pos.x, mouse->delta_pos.y);
      }
    }
    ImGui::End();
  }
}

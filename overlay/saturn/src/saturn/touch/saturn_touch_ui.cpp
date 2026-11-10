// Touch HUD for Saturn 64 Mobile: game buttons, stick, toolbar (eye + menu), touch menu and layout editor.
// Called once per frame from saturn_imgui_update(), right before ImGui::Render().
// Everything is drawn with ImGui's foreground draw list, so hiding the controls (or Saturn's UI) leaves the
// game image clean for screenshots and videos.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "saturn/libs/imgui/imgui.h"
#include "saturn/imgui/saturn_imgui_chroma.h"   // schroma_imgui_init()

extern "C" {
#include "pc/controller/controller_touch.h"
}

extern bool showMenu, showStatusBars, windowStats, windowCcEditor, windowAnimPlayer, windowSettings;
extern bool autoChroma, k_popout_open;

// Screenshot: every panel is hidden for a few frames, one clean frame is saved as a PNG (see gfx_sdl2.c), then
// everything comes back. Started from the "Take screenshot" button in the touch menu.
extern "C" {
    extern int android_screenshot_request;
    extern int android_screenshot_result;
    extern char android_screenshot_name[96];
}
static int g_shot_state = 0;    // 0 = idle, counts down while a screenshot is in progress
static int g_shot_toast = 0;    // frames left to show the "saved" message
static struct { bool menu, bars, stats, cc, anim, settings, popout; int hide; } g_shot_saved;

static void screenshot_begin(void) {
    if (g_shot_state > 0) return;
    g_shot_saved.menu = showMenu;           g_shot_saved.bars = showStatusBars;
    g_shot_saved.stats = windowStats;       g_shot_saved.cc = windowCcEditor;
    g_shot_saved.anim = windowAnimPlayer;   g_shot_saved.settings = windowSettings;
    g_shot_saved.popout = k_popout_open;    g_shot_saved.hide = touch_hide_state;
    showMenu = false; showStatusBars = false; windowStats = false; windowCcEditor = false;
    windowAnimPlayer = false; windowSettings = false; k_popout_open = false;
    touch_hide_state = 1;
    touch_menu_open = 0;
    android_screenshot_result = 0;
    g_shot_toast = 0;
    g_shot_state = 6;
}

static void screenshot_step(void) {
    if (g_shot_state <= 0) return;
    g_shot_state--;
    if (g_shot_state == 2) android_screenshot_request = 1;   // this frame is saved right before it is shown
    if (g_shot_state == 0) {
        showMenu = g_shot_saved.menu;           showStatusBars = g_shot_saved.bars;
        windowStats = g_shot_saved.stats;       windowCcEditor = g_shot_saved.cc;
        windowAnimPlayer = g_shot_saved.anim;   windowSettings = g_shot_saved.settings;
        k_popout_open = g_shot_saved.popout;    touch_hide_state = g_shot_saved.hide;
        g_shot_toast = 150;
    }
}

static void draw_screenshot_toast(ImDrawList *dl, ImVec2 ds) {
    if (g_shot_toast <= 0 || g_shot_state > 0 || android_screenshot_result == 0) return;
    g_shot_toast--;
    char msg[160];
    if (android_screenshot_result > 0) snprintf(msg, sizeof(msg), "Saved: %s", android_screenshot_name);
    else snprintf(msg, sizeof(msg), "Screenshot failed");
    ImVec2 ts = ImGui::CalcTextSize(msg);
    ImVec2 p0((ds.x - ts.x) * 0.5f - 12.f, ds.y * 0.06f);
    ImVec2 p1(p0.x + ts.x + 24.f, p0.y + ts.y + 16.f);
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 200), 8.f);
    dl->AddText(ImVec2(p0.x + 12.f, p0.y + 8.f), IM_COL32(255, 255, 255, 255), msg);
}

static ImU32 rgba(float r, float g, float b, float a) {
    if (r > 255.f) r = 255.f; if (g > 255.f) g = 255.f; if (b > 255.f) b = 255.f;
    if (a > 1.f) a = 1.f; if (a < 0.f) a = 0.f;
    return IM_COL32((int)r, (int)g, (int)b, (int)(a * 255.f));
}

struct Rgb { float r, g, b; };

static Rgb default_color(int i) {
    switch (i) {
        case TB_A:      return { 70.f, 120.f, 255.f };
        case TB_B:      return { 70.f, 200.f, 100.f };
        case TB_Z:      return { 165.f, 165.f, 178.f };
        case TB_START:  return { 235.f, 70.f, 70.f };
        case TB_CUP: case TB_CDOWN: case TB_CLEFT: case TB_CRIGHT: return { 255.f, 214.f, 64.f };
        default:        return { 120.f, 120.f, 140.f };   // L, R
    }
}

static Rgb button_color(int i) {
    if (touch_buttons[i].use_color) return { (float)touch_buttons[i].cr, (float)touch_buttons[i].cg, (float)touch_buttons[i].cb };
    return default_color(i);
}

static float luma(Rgb k) { return 0.299f * k.r + 0.587f * k.g + 0.114f * k.b; }

static void draw_arrow(ImDrawList *dl, ImVec2 c, float s, float dx, float dy, ImU32 col) {
    float px = -dy, py = dx;
    ImVec2 tip(c.x + dx * s, c.y + dy * s);
    ImVec2 b1(c.x - dx * s * 0.7f + px * s, c.y - dy * s * 0.7f + py * s);
    ImVec2 b2(c.x - dx * s * 0.7f - px * s, c.y - dy * s * 0.7f - py * s);
    dl->AddTriangleFilled(tip, b1, b2, col);
}

static void draw_label(ImDrawList *dl, ImVec2 fc, float rr, int idx, ImU32 ink, float a) {
    float s = rr * 0.40f;
    if (idx == TB_CUP) draw_arrow(dl, fc, s, 0.f, -1.f, ink);
    else if (idx == TB_CDOWN) draw_arrow(dl, fc, s, 0.f, 1.f, ink);
    else if (idx == TB_CLEFT) draw_arrow(dl, fc, s, -1.f, 0.f, ink);
    else if (idx == TB_CRIGHT) draw_arrow(dl, fc, s, 1.f, 0.f, ink);
    else {
        const char *label = touch_buttons[idx].label;
        ImFont *font = ImGui::GetFont();
        float fs = rr * (idx == TB_START ? 0.46f : 0.95f);
        ImVec2 ts = font->CalcTextSizeA(fs, 100000.f, 0.f, label);
        ImVec2 p(fc.x - ts.x * 0.5f, fc.y - ts.y * 0.5f);
        dl->AddText(font, fs, ImVec2(p.x + 1.5f, p.y + 1.5f), rgba(0, 0, 0, 0.45f * a), label);
        dl->AddText(font, fs, p, ink, label);
    }
}

static void draw_game_button(ImDrawList *dl, ImVec2 c, float r, int idx, bool pressed, bool selected, bool disabled) {
    float a = touch_opacity;
    if (disabled) a *= 0.30f;
    Rgb k = button_color(idx);
    float rr = pressed ? r * 0.94f : r;

    if (touch_style == 1) {
        // Flat style (similar to sm64coopdx): translucent disc, white ring, solid white flash when pressed
        dl->AddCircleFilled(c, rr, rgba(k.r, k.g, k.b, (pressed ? 0.90f : 0.45f) * a), 40);
        dl->AddCircle(c, rr, selected ? rgba(255, 210, 40, 1.f) : rgba(255, 255, 255, 0.95f * a), 40,
                      selected ? 5.f : (rr * 0.07f < 2.5f ? 2.5f : rr * 0.07f));
        ImU32 ink = pressed ? rgba(20, 20, 30, a + 0.3f) : rgba(255, 255, 255, a + 0.3f);
        draw_label(dl, c, rr, idx, ink, a);
        return;
    }

    // Glossy style
    float lift = pressed ? 0.f : rr * 0.07f;
    dl->AddCircleFilled(ImVec2(c.x, c.y + rr * 0.10f), rr, rgba(0, 0, 0, 0.35f * a), 40);                 // shadow
    dl->AddCircleFilled(c, rr, rgba(k.r * 0.55f, k.g * 0.55f, k.b * 0.55f, a), 40);                       // dark rim
    float boost = pressed ? 45.f : 0.f;
    dl->AddCircleFilled(ImVec2(c.x, c.y - lift), rr * 0.88f, rgba(k.r + boost, k.g + boost, k.b + boost, a), 40); // face
    dl->AddCircleFilled(ImVec2(c.x - rr * 0.24f, c.y - rr * 0.32f - lift), rr * 0.30f, rgba(255, 255, 255, 0.20f * a), 24); // shine
    dl->AddCircle(c, rr, selected ? rgba(255, 210, 40, 1.f) : rgba(255, 255, 255, 0.55f * a), 40, selected ? 5.f : 2.f);
    ImU32 ink = luma(k) > 150.f ? rgba(40, 40, 55, a + 0.25f) : rgba(255, 255, 255, a + 0.25f);
    draw_label(dl, ImVec2(c.x, c.y - lift), rr, idx, ink, a);
}

static void draw_stick(ImDrawList *dl, ImVec2 ds, bool selected) {
    float a = touch_opacity;
    float sr = touch_stick_size * ds.y;
    ImVec2 base = touch_stick_active ? ImVec2(touch_stick_ox * ds.x, touch_stick_oy * ds.y)
                                     : ImVec2(touch_stick_x * ds.x, touch_stick_y * ds.y);
    Rgb kc = touch_stick_use_color ? Rgb{ (float)touch_stick_cr, (float)touch_stick_cg, (float)touch_stick_cb } : Rgb{ 235.f, 235.f, 245.f };
    dl->AddCircleFilled(base, sr, rgba(0, 0, 0, 0.28f * a), 48);
    dl->AddCircle(base, sr, selected ? rgba(255, 210, 40, 1.f) : rgba(255, 255, 255, 0.60f * a), 48, selected ? 5.f : 3.f);
    dl->AddCircle(base, sr * 0.55f, rgba(255, 255, 255, 0.18f * a), 48, 2.f);
    ImVec2 knob(base.x + touch_stick_dx * sr * 0.85f, base.y + touch_stick_dy * sr * 0.85f);
    if (touch_style == 1) {
        dl->AddCircleFilled(knob, sr * 0.42f, rgba(kc.r, kc.g, kc.b, 0.55f * a), 32);
        dl->AddCircle(knob, sr * 0.42f, rgba(255, 255, 255, 0.95f * a), 32, 3.f);
    } else {
        dl->AddCircleFilled(ImVec2(knob.x, knob.y + sr * 0.04f), sr * 0.42f, rgba(0, 0, 0, 0.35f * a), 32);
        dl->AddCircleFilled(knob, sr * 0.42f, rgba(kc.r, kc.g, kc.b, 0.80f * a), 32);
        dl->AddCircleFilled(ImVec2(knob.x - sr * 0.10f, knob.y - sr * 0.13f), sr * 0.14f, rgba(255, 255, 255, 0.55f * a), 20);
    }
}

static void draw_toolbar(ImDrawList *dl, ImVec2 ds) {
    if (g_shot_state > 0) return;   // nothing on screen while a screenshot is taken
    for (int i = 0; i < 2; i++) {
        ImVec2 c(touch_tool_x[i] * ds.x, touch_tool_y * ds.y);
        float r = touch_tool_r * ds.y;
        bool down = touch_tool_down[i] != 0;
        if (touch_hide_state != 0) {
            if (i != 0) continue;
            dl->AddCircleFilled(c, r * 0.28f, rgba(255, 255, 255, 0.25f), 20);   // tiny dot: tap to bring everything back
            continue;
        }
        float a = 0.80f;
        dl->AddCircleFilled(c, r, rgba(15, 15, 25, down ? 0.80f : 0.55f), 36);
        dl->AddCircle(c, r, rgba(255, 255, 255, a), 36, 2.5f);
        ImU32 ink = rgba(255, 255, 255, 0.95f);
        if (i == 0) {   // eye
            ImVec2 pts[24];
            for (int k = 0; k < 24; k++) {
                float t = (float)k / 24.f * 6.2831853f;
                pts[k] = ImVec2(c.x + cosf(t) * r * 0.56f, c.y + sinf(t) * r * 0.30f);
            }
            dl->AddPolyline(pts, 24, ink, ImDrawFlags_Closed, r * 0.09f);
            dl->AddCircleFilled(c, r * 0.17f, ink, 16);
        } else {        // menu (3 lines)
            for (int k = -1; k <= 1; k++)
                dl->AddLine(ImVec2(c.x - r * 0.42f, c.y + k * r * 0.30f), ImVec2(c.x + r * 0.42f, c.y + k * r * 0.30f), ink, r * 0.13f);
        }
    }
}

static bool big_button(const char *label) {
    return ImGui::Button(label, ImVec2(-1.f, 44.f));
}

// Touch menu: uses Saturn's own theme (colours, title bar, close button) so it looks like the rest of the UI.
static void draw_quick_menu(ImVec2 ds) {
    static bool was_open = false;
    if (!touch_menu_open) { was_open = false; return; }
    ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.40f, ds.y), ImGuiCond_Always);
    if (!was_open) ImGui::SetNextWindowFocus();
    was_open = true;

    bool open = true;
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Touch menu###touch_quick_menu", &open, fl)) {
        ImGui::TextDisabled("TOUCH CONTROLS");
        ImGui::Separator();
        if (big_button("Hide controls (clean shot)###q_hide")) {
            touch_hide_state = 1;       // everything hidden; a tiny dot (top right) brings it back
            touch_menu_open = 0;
            touch_layout_save();
        }
        if (big_button("Take screenshot (PNG)###q_shot")) screenshot_begin();
        if (big_button("Edit layout and colors###q_edit")) {
            touch_edit = 1;
            touch_selected = -2;
            touch_menu_open = 0;
        }
        bool ena = touch_enabled != 0;
        if (ImGui::Checkbox("Controls enabled", &ena)) touch_enabled = ena ? 1 : 0;
        ImGui::SliderFloat("Opacity", &touch_opacity, 0.15f, 1.0f);
        ImGui::SliderFloat("Interface size", &touch_ui_mult, 0.6f, 2.0f);
        ImGui::TextWrapped("Eye icon (top right): hide / show the controls.");

        ImGui::Spacing();
        ImGui::TextDisabled("SATURN");
        ImGui::Separator();
        if (big_button(showMenu ? "Hide Saturn UI###q_ui" : "Show Saturn UI###q_ui")) showMenu = !showMenu;
        if (big_button("Settings###q_settings")) { windowSettings = !windowSettings; k_popout_open = false; }
        if (big_button("Timeline (keyframes)###q_timeline")) {
            k_popout_open = !k_popout_open;
            if (k_popout_open) windowSettings = false;
        }
        if (big_button("Chroma key###q_chroma")) {
            schroma_imgui_init();
            autoChroma = !autoChroma;
            windowCcEditor = false;
            windowAnimPlayer = false;
        }
        if (big_button(showStatusBars ? "Hide status bars###q_bars" : "Show status bars###q_bars")) showStatusBars = !showStatusBars;
        if (big_button(windowStats ? "Hide stats###q_stats" : "Show stats###q_stats")) windowStats = !windowStats;
    }
    ImGui::End();
    if (!open) { touch_menu_open = 0; touch_layout_save(); }
}

static const float PALETTE[8][3] = {
    { 70, 120, 255 }, { 70, 200, 100 }, { 255, 214, 64 }, { 235, 70, 70 },
    { 165, 165, 178 }, { 255, 255, 255 }, { 255, 140, 40 }, { 170, 90, 220 }
};

// Colour chooser: 8 quick swatches + a full picker + "Default".
static void color_chooser(unsigned char *cr, unsigned char *cg, unsigned char *cb, int *use, Rgb current) {
    ImGui::TextUnformatted("Color");
    for (int i = 0; i < 8; i++) {
        ImGui::PushID(i);
        if (ImGui::ColorButton("##sw", ImVec4(PALETTE[i][0] / 255.f, PALETTE[i][1] / 255.f, PALETTE[i][2] / 255.f, 1.f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(34.f, 34.f))) {
            *cr = (unsigned char)PALETTE[i][0]; *cg = (unsigned char)PALETTE[i][1]; *cb = (unsigned char)PALETTE[i][2];
            *use = 1;
        }
        ImGui::PopID();
        ImGui::SameLine();
    }
    float col[3] = { current.r / 255.f, current.g / 255.f, current.b / 255.f };
    if (ImGui::ColorEdit3("##custom_color", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel)) {
        *cr = (unsigned char)(col[0] * 255.f); *cg = (unsigned char)(col[1] * 255.f); *cb = (unsigned char)(col[2] * 255.f);
        *use = 1;
    }
    if (ImGui::Button("Default color")) *use = 0;
}

static void draw_edit_panel(ImVec2 ds) {
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.12f), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGuiWindowFlags fl = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Edit touch controls###touch_edit_panel", NULL, fl)) {
        ImGui::TextDisabled("Drag a button to move it");
        ImGui::Separator();
        if (touch_selected >= 0) {
            TouchButton &b = touch_buttons[touch_selected];
            ImGui::Text("Selected: %s", b.name);
            ImGui::SliderFloat("Size", &b.size, 0.03f, 0.20f);
            bool vis = b.visible != 0;
            if (ImGui::Checkbox("Show this button", &vis)) b.visible = vis ? 1 : 0;
            color_chooser(&b.cr, &b.cg, &b.cb, &b.use_color, button_color(touch_selected));
        } else if (touch_selected == -1) {
            ImGui::TextUnformatted("Selected: stick");
            ImGui::SliderFloat("Size", &touch_stick_size, 0.08f, 0.35f);
            color_chooser(&touch_stick_cr, &touch_stick_cg, &touch_stick_cb, &touch_stick_use_color,
                          touch_stick_use_color ? Rgb{ (float)touch_stick_cr, (float)touch_stick_cg, (float)touch_stick_cb } : Rgb{ 235.f, 235.f, 245.f });
        } else {
            ImGui::TextUnformatted("Tap a button or the stick to edit it.");
        }
        ImGui::Separator();
        ImGui::SliderFloat("Opacity", &touch_opacity, 0.15f, 1.0f);
        ImGui::TextUnformatted("Style");
        ImGui::SameLine();
        if (ImGui::RadioButton("Glossy", touch_style == 0)) touch_style = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Flat", touch_style == 1)) touch_style = 1;
        if (ImGui::Button("Mirror")) touch_layout_mirror();
        ImGui::SameLine();
        if (ImGui::Button("Reset all")) { touch_layout_reset(); touch_selected = -2; }
        ImGui::SameLine();
        if (ImGui::Button("Done")) { touch_edit = 0; touch_selected = -2; touch_layout_save(); }
    }
    ImGui::End();
}

extern "C" void saturn_touch_ui(void) {
    ImGuiIO &io = ImGui::GetIO();
    touch_ui_capture = io.WantCaptureMouse ? 1 : 0;
    ImVec2 ds = io.DisplaySize;
    ImDrawList *dl = ImGui::GetForegroundDrawList();

    // Toolbar taps
    int act = touch_take_action();
    if (act == TA_EYE) {
        touch_hide_state = touch_hide_state ? 0 : 1;   // eye: hide / show everything
        touch_menu_open = 0;
    } else if (act == TA_MENU && touch_hide_state == 0) {
        touch_menu_open = touch_menu_open ? 0 : 1;
        if (!touch_menu_open) touch_layout_save();
    }

    screenshot_step();

    bool show_game = (touch_hide_state == 0) || touch_edit;
    if (show_game) {
        for (int i = 0; i < TB_COUNT; i++) {
            if (!touch_buttons[i].visible && !touch_edit) continue;
            float br = touch_buttons[i].size * ds.y;
            // only big panels (side sheets, timeline, the touch menu) hide a button; bars and Stats never do
            if (!touch_edit && touch_circle_overlaps_ui(touch_buttons[i].x, touch_buttons[i].y, br / ds.x, br / ds.y)) continue;
            ImVec2 c(touch_buttons[i].x * ds.x, touch_buttons[i].y * ds.y);
            draw_game_button(dl, c, br, i, touch_buttons[i].pressed != 0,
                             touch_edit && touch_selected == i, touch_buttons[i].visible == 0);
        }
        float sr = touch_stick_size * ds.y;
        if (touch_edit || touch_stick_active || !touch_circle_overlaps_ui(touch_stick_x, touch_stick_y, sr / ds.x, sr / ds.y))
            draw_stick(dl, ds, touch_edit && touch_selected == -1);
    }

    draw_toolbar(dl, ds);
    draw_screenshot_toast(dl, ds);
    draw_quick_menu(ds);
    if (touch_edit) draw_edit_panel(ds);
}

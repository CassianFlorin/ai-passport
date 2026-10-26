// Standalone live dashboard with persistent speaker volume and mute.
//
// The page samples only existing BSP/system APIs. It does not start Wi-Fi or
// allocate a second display buffer, which keeps the dashboard useful on C3
// boards with no PSRAM.
#include "performance_dashboard.h"

#include "app_audio.h"
#include "app_settings.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "performance_metrics.h"

static const uint32_t COLOR_BG = 0x07111F;
static const uint32_t COLOR_CARD = 0x0E2035;
static const uint32_t COLOR_CARD_ALT = 0x102A42;
static const uint32_t COLOR_CYAN = 0x3EE9FF;
static const uint32_t COLOR_MAGENTA = 0xF95DFF;
static const uint32_t COLOR_LIME = 0xB9F227;
static const uint32_t COLOR_ORANGE = 0xFFB454;
static const uint32_t COLOR_TEXT = 0xE7F7FF;
static const uint32_t COLOR_MUTED = 0x7796AA;

static lv_obj_t *s_scr;
static lv_obj_t *s_battery;
static lv_obj_t *s_heap_value;
static lv_obj_t *s_largest_value;
static lv_obj_t *s_min_value;
static lv_obj_t *s_battery_value;
static lv_obj_t *s_uptime_value;
static lv_obj_t *s_sound_card;
static lv_obj_t *s_sound_value;
static lv_obj_t *s_heap_bar;
static lv_obj_t *s_volume_bar;
static lv_obj_t *s_sound_hint;
static lv_obj_t *s_mute_action;
static lv_obj_t *s_key_guides[3];
static bool s_audio_ready;
static const char *s_feedback;

// The line points are shared by all three callouts; LVGL keeps the pointer for
// the lifetime of the line object, so static storage is required here.
static const lv_point_precise_t RIGHT_ARROW[] = {
    {0, 4}, {8, 4}, {5, 1}, {8, 4}, {5, 7},
};

static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, uint32_t color,
                              int x, int y, int width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    if (width > 0) lv_obj_set_width(label, width);
    return label;
}

static lv_obj_t *create_card(lv_obj_t *parent, int x, int y, int w, int h,
                             uint32_t border_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    return card;
}

static void create_metric_row(int y, const char *caption, uint32_t accent,
                              lv_obj_t **value)
{
    lv_obj_t *card = create_card(s_scr, 12, y, 152, 36, accent);
    create_label(card, caption, &lv_font_montserrat_14, COLOR_MUTED, 8, 9, 74);
    *value = create_label(card, "--", &lv_font_montserrat_14, COLOR_TEXT, 84, 9, 58);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_RIGHT, 0);
}

static void refresh_sound_style(void)
{
    uint8_t output = app_settings_output_volume();
    bool enabled = output > 0;
    if (enabled) lv_label_set_text_fmt(s_sound_value, "%u%%", (unsigned)output);
    else lv_label_set_text(s_sound_value, "MUTE");
    lv_label_set_text(s_mute_action, enabled ? "MUTE" : "ON");
    if (s_feedback) lv_label_set_text(s_sound_hint, s_feedback);
    else if (enabled) lv_label_set_text(s_sound_hint, "HOLD OK: TEST");
    else lv_label_set_text_fmt(s_sound_hint, "OK: %u%%",
                               (unsigned)app_settings_volume());
    lv_obj_set_style_text_color(s_sound_value,
                                lv_color_hex(enabled ? COLOR_LIME : COLOR_MAGENTA), 0);
    lv_obj_set_style_border_color(s_sound_card,
                                  lv_color_hex(enabled ? COLOR_LIME : COLOR_MAGENTA), 0);
    lv_bar_set_value(s_volume_bar, output, LV_ANIM_ON);
}

static lv_obj_t *create_button_guide(bsp_btn_t button, int y, const char *action,
                                     const char *key_name, uint32_t color)
{
    // Product views place the three keys on the right edge. Keep a dedicated
    // rail clear of metrics; arrows end near that edge instead of at fake keys.
    lv_obj_t *guide = create_card(s_scr, 172, y, 54, 42, COLOR_CARD_ALT);
    s_key_guides[button] = guide;
    lv_obj_t *action_label = create_label(guide, action, &lv_font_montserrat_14,
                                          color, 0, 2, 52);
    lv_obj_set_style_text_align(action_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *key_label = create_label(guide, key_name, &lv_font_montserrat_14,
                                       COLOR_MUTED, 0, 22, 52);
    lv_obj_set_style_text_align(key_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *arrow = lv_line_create(s_scr);
    lv_line_set_points(arrow, RIGHT_ARROW,
                       sizeof(RIGHT_ARROW) / sizeof(RIGHT_ARROW[0]));
    lv_obj_set_pos(arrow, 229, y + 7);
    lv_obj_set_size(arrow, 9, 9);
    lv_obj_set_style_line_color(arrow, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(arrow, 1, 0);
    lv_obj_set_style_line_rounded(arrow, true, 0);

    return action_label;
}

void performance_dashboard_refresh(void)
{
    // Battery I2C transactions must not block the LVGL task or hold its lock.
    const uint32_t heap_caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
    size_t total_heap = heap_caps_get_total_size(heap_caps);
    size_t free_heap = heap_caps_get_free_size(heap_caps);
    size_t largest_block = heap_caps_get_largest_free_block(heap_caps);
    size_t minimum_heap = heap_caps_get_minimum_free_size(heap_caps);
    int used_percent = performance_used_percent(total_heap, free_heap);
    int soc = bsp_battery_soc();
    int mv = bsp_battery_mv();
    char free_text[16];
    char largest_text[16];
    char minimum_text[16];
    char uptime_text[24];

    performance_format_bytes(free_text, sizeof(free_text), free_heap);
    performance_format_bytes(largest_text, sizeof(largest_text), largest_block);
    performance_format_bytes(minimum_text, sizeof(minimum_text), minimum_heap);
    performance_format_uptime(uptime_text, sizeof(uptime_text),
                              (uint64_t)(esp_timer_get_time() / 1000000LL));

    if (!bsp_lvgl_lock(250)) return;
    lv_label_set_text(s_heap_value, free_text);
    lv_label_set_text(s_largest_value, largest_text);
    lv_label_set_text(s_min_value, minimum_text);
    lv_label_set_text(s_uptime_value, uptime_text);
    if (soc < 0) {
        lv_label_set_text(s_battery, "BATT --");
    } else {
        lv_label_set_text_fmt(s_battery, "BATT %d%%", soc);
    }
    if (mv < 0) lv_label_set_text(s_battery_value, "--");
    else lv_label_set_text_fmt(s_battery_value, "%d.%02dV", mv / 1000, (mv % 1000) / 10);
    lv_bar_set_value(s_heap_bar, used_percent, LV_ANIM_ON);
    refresh_sound_style();
    bsp_lvgl_unlock();
}

void performance_dashboard_create(bool audio_ready, bool settings_ready)
{
    s_audio_ready = audio_ready;
    s_feedback = !settings_ready ? "SETTINGS ERROR"
                 : !audio_ready ? "AUDIO OFFLINE" : NULL;
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    create_label(s_scr, "SYSTEM", &lv_font_montserrat_20,
                 COLOR_CYAN, 16, 12, 110);
    create_label(s_scr, "ESP32-C3", &lv_font_montserrat_14,
                 COLOR_MUTED, 16, 36, 132);
    s_battery = create_label(s_scr, "BATT --", &lv_font_montserrat_14,
                             COLOR_ORANGE, 140, 16, 84);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);

    s_battery_value = create_label(s_scr, "--", &lv_font_montserrat_14,
                                   COLOR_MUTED, 154, 36, 70);
    lv_obj_set_style_text_align(s_battery_value, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *heap_card = create_card(s_scr, 12, 60, 152, 64, COLOR_CYAN);
    create_label(heap_card, "FREE HEAP", &lv_font_montserrat_14,
                 COLOR_MUTED, 10, 7, 130);
    s_heap_value = create_label(heap_card, "--", &lv_font_montserrat_20,
                                COLOR_TEXT, 10, 27, 130);
    create_metric_row(132, "MAX BLK", COLOR_MAGENTA, &s_largest_value);
    create_metric_row(176, "MIN HEAP", COLOR_ORANGE, &s_min_value);

    lv_obj_t *uptime_card = create_card(s_scr, 12, 220, 152, 36, COLOR_ORANGE);
    create_label(uptime_card, "UP", &lv_font_montserrat_14, COLOR_MUTED, 8, 9, 24);
    s_uptime_value = create_label(uptime_card, "--:--:--", &lv_font_montserrat_14,
                                  COLOR_TEXT, 34, 9, 108);
    lv_label_set_long_mode(s_uptime_value, LV_LABEL_LONG_DOT);

    s_heap_bar = lv_bar_create(heap_card);
    lv_obj_set_pos(s_heap_bar, 10, 54);
    lv_obj_set_size(s_heap_bar, 130, 4);
    lv_bar_set_range(s_heap_bar, 0, 100);
    lv_bar_set_value(s_heap_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_heap_bar, lv_color_hex(COLOR_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_heap_bar, lv_color_hex(COLOR_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_heap_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_heap_bar, 2, LV_PART_INDICATOR);

    s_sound_card = create_card(s_scr, 12, 260, 152, 52, COLOR_LIME);
    create_label(s_sound_card, "VOLUME", &lv_font_montserrat_14,
                 COLOR_MUTED, 8, 6, 70);
    s_sound_value = create_label(s_sound_card, "--", &lv_font_montserrat_20,
                                 COLOR_LIME, 80, 2, 62);
    lv_obj_set_style_text_align(s_sound_value, LV_TEXT_ALIGN_RIGHT, 0);
    s_volume_bar = lv_bar_create(s_sound_card);
    lv_obj_set_pos(s_volume_bar, 8, 28);
    lv_obj_set_size(s_volume_bar, 134, 4);
    lv_bar_set_range(s_volume_bar, 0, 100);
    lv_obj_set_style_bg_color(s_volume_bar, lv_color_hex(COLOR_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume_bar, lv_color_hex(COLOR_LIME), LV_PART_INDICATOR);
    s_sound_hint = create_label(s_sound_card, "", &lv_font_montserrat_14,
                                COLOR_MUTED, 8, 34, 134);

    // Match the right-upper/middle/lower callouts in the user's device photo.
    // Exact alignment and BSP-to-physical-key mapping still need device acceptance.
    create_button_guide(BSP_BTN_UP, 50, "VOL +", "UP", COLOR_CYAN);
    create_button_guide(BSP_BTN_DOWN, 150, "VOL -", "DOWN", COLOR_MAGENTA);
    s_mute_action = create_button_guide(BSP_BTN_OK, 270, "MUTE", "OK", COLOR_LIME);

    refresh_sound_style();
    lv_screen_load(s_scr);
}

void performance_dashboard_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn < BSP_BTN_UP || btn > BSP_BTN_OK) return;
    if (ev == BSP_BTN_PRESS || ev == BSP_BTN_RELEASE) {
        // Highlight the same physical-key callout while it is held. Raw edges
        // never change volume or mute and never start a sound preview.
        if (!bsp_lvgl_lock(250)) return;
        lv_obj_set_style_bg_color(s_key_guides[btn],
                                  lv_color_hex(ev == BSP_BTN_PRESS
                                               ? 0x254660 : COLOR_CARD), 0);
        bsp_lvgl_unlock();
        return;
    }
    esp_err_t err;
    const bool is_click = ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE;
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        // The input worker serializes codec access and NVS writes. Never preview
        // inside a UI lock, or at boot/volume changes where sound is unexpected.
        err = s_audio_ready ? app_audio_preview() : ESP_ERR_INVALID_STATE;
        s_feedback = err != ESP_OK ? "TEST FAILED"
                     : app_settings_sound_enabled() ? "TEST DONE" : "SOUND MUTED";
    } else if (is_click) {
        if (btn == BSP_BTN_OK) {
            err = app_settings_set_sound_enabled(!app_settings_sound_enabled());
        } else if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            err = app_settings_step_volume(btn == BSP_BTN_UP);
        } else return;

        if (err == ESP_OK && s_audio_ready) {
            bsp_audio_set_volume(app_settings_output_volume());
        }
        // Keep failures visible across telemetry refreshes until the next action.
        s_feedback = err != ESP_OK ? "SAVE FAILED"
                     : !s_audio_ready ? "AUDIO OFFLINE" : NULL;
    } else {
        return;
    }
    if (!bsp_lvgl_lock(250)) return;
    refresh_sound_style();
    bsp_lvgl_unlock();
}

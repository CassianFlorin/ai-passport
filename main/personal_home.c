// A pocket instrument: personal identity, live telemetry and sound settings.
// Only the current page exists in the LVGL pool. Artwork/font bitmaps stay in
// Flash; no second frame buffer, radio, per-page task or timer is needed.
#include "personal_home.h"

#include "app_audio.h"
#include "app_navigation.h"
#include "app_settings.h"
#include "ai_conversation_state.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "cassian_assets.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "performance_metrics.h"
#include "usb_config.h"

enum {
    COLOR_BACKGROUND = 0x0B1115,
    COLOR_SURFACE = 0x131E23,
    COLOR_SELECTED = 0x1D2B22,
    COLOR_INK = 0xE2EBE8,
    COLOR_ACCENT = 0xBDF56A,
    COLOR_MUTED = 0x8A9FA6,
    COLOR_LINE = 0x2B3C43,
    COLOR_GRID = 0x172329,
    COLOR_WARM = 0xE6C77B,
    COLOR_ERROR = 0xFF8D7E,
    COLOR_PORTRAIT_INK = 0x243B30,
    CONTENT_WIDTH = 216,
    HOME_ART_TOP = 14,
};

static lv_obj_t *s_scr;
static lv_obj_t *s_body;
static lv_obj_t *s_title;
static lv_obj_t *s_battery;
static lv_obj_t *s_menu_rows[APP_MENU_COUNT];
static lv_obj_t *s_menu_labels[APP_MENU_COUNT];
static lv_obj_t *s_menu_descriptions[APP_MENU_COUNT];
static lv_obj_t *s_menu_icons[APP_MENU_COUNT];
static lv_obj_t *s_menu_heap;
static lv_obj_t *s_menu_volume;
static lv_obj_t *s_menu_uptime;
static lv_obj_t *s_heap_value;
static lv_obj_t *s_largest_value;
static lv_obj_t *s_min_value;
static lv_obj_t *s_battery_value;
static lv_obj_t *s_uptime_value;
static lv_obj_t *s_heap_bar;
static lv_obj_t *s_heap_percent;
static lv_obj_t *s_sound_value;
static lv_obj_t *s_sound_state;
static lv_obj_t *s_volume_bar;
static lv_obj_t *s_sound_hint;
static lv_obj_t *s_ai_wifi;
static lv_obj_t *s_ai_service;
static lv_obj_t *s_ai_hint;
static usb_config_status_t s_connection;
static app_navigation_t s_navigation;
static bool s_audio_ready;
static const char *s_feedback;
static bool s_feedback_error;

// Snapshots are sampled by the same input worker that changes pages. A newly
// opened page can immediately use them without doing I2C while holding LVGL.
static struct {
    int soc;
    int mv;
    int used_percent;
    bool heap_valid;
    char free[16];
    char largest[16];
    char minimum[16];
    char uptime[24];
} s_metrics = {
    .soc = -1, .mv = -1,
    .free = "--", .largest = "--", .minimum = "--", .uptime = "--:--:--",
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

static lv_obj_t *create_panel(lv_obj_t *parent, int x, int y, int w, int h,
                              uint32_t background)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    return panel;
}

static void draw_line(lv_layer_t *layer, uint32_t color, int width,
                      int x1, int y1, int x2, int y2)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(color);
    line.width = width;
    line.p1 = (lv_point_precise_t){x1, y1};
    line.p2 = (lv_point_precise_t){x2, y2};
    lv_draw_line(layer, &line);
}

static void draw_grid(lv_event_t *event)
{
    // One static draw callback supplies the etched grid, without allocating an
    // object per line or running an animation/task while the device is idle.
    lv_layer_t *layer = lv_event_get_layer(event);
    for (int x = 12; x <= 228; x += 54) {
        draw_line(layer, COLOR_GRID, 1, x, 44, x, 286);
    }
    for (int y = 58; y < 286; y += 38) {
        draw_line(layer, COLOR_GRID, 1, 12, y, 228, y);
    }
}

static lv_obj_t *create_bar(lv_obj_t *parent, int y)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 10, y);
    lv_obj_set_size(bar, CONTENT_WIDTH - 20, 4);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    return bar;
}

static void refresh_selection(void)
{
    for (unsigned i = 0; i < APP_MENU_COUNT; ++i) {
        bool selected = i == s_navigation.selection;
        lv_obj_set_style_bg_color(s_menu_rows[i],
                                  lv_color_hex(selected ? COLOR_SELECTED : COLOR_SURFACE), 0);
        lv_obj_set_style_border_color(s_menu_rows[i],
                                      lv_color_hex(selected ? COLOR_ACCENT : COLOR_LINE), 0);
        lv_obj_set_style_text_color(s_menu_labels[i],
                                    lv_color_hex(selected ? COLOR_ACCENT : COLOR_INK), 0);
        lv_obj_set_style_text_color(s_menu_descriptions[i],
                                    lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_color(s_menu_icons[i],
                                    lv_color_hex(selected ? COLOR_ACCENT : COLOR_MUTED), 0);
    }
}

static void build_home(void)
{
    // The homepage is an identity card; function entries live on their own page.
    lv_obj_t *portrait = lv_image_create(s_body);
    lv_image_set_src(portrait, &cassian_portrait);
    lv_obj_set_pos(portrait, 12, HOME_ART_TOP);
    create_label(s_body, "CASSIAN\nFLORIN", &cassian_display_28,
                 COLOR_PORTRAIT_INK, 20, HOME_ART_TOP + 5, CONTENT_WIDTH - 16);
    create_panel(s_body, 12, HOME_ART_TOP + 168, CONTENT_WIDTH, 2, COLOR_ACCENT);
    create_panel(s_body, 12, 196, 2, 34, COLOR_ACCENT);
    lv_obj_t *tagline = create_label(s_body,
                                     "造些有趣的东西\n留下自己的痕迹",
                                     &cassian_ui_14, COLOR_INK,
                                     22, 192, CONTENT_WIDTH - 16);
    lv_obj_set_style_text_line_space(tagline, 0, 0);
}

static void draw_menu_icon(lv_event_t *event)
{
    lv_obj_t *icon = lv_event_get_target(event);
    lv_area_t area;
    lv_obj_get_coords(icon, &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    uint32_t color = lv_color_to_u32(lv_obj_get_style_text_color(icon, 0));
    const unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    const int x = area.x1, y = area.y1;
    if (index == APP_MENU_AI) {
        // A speech bubble is a vector mark; no icon font or image decoder needed.
        draw_line(layer, color, 1, x + 1, y + 3, x + 26, y + 3);
        draw_line(layer, color, 1, x + 1, y + 3, x + 1, y + 21);
        draw_line(layer, color, 1, x + 26, y + 3, x + 26, y + 21);
        draw_line(layer, color, 1, x + 1, y + 21, x + 7, y + 21);
        draw_line(layer, color, 1, x + 7, y + 21, x + 7, y + 27);
        draw_line(layer, color, 1, x + 7, y + 27, x + 13, y + 21);
        draw_line(layer, color, 1, x + 13, y + 21, x + 26, y + 21);
        for (int i = 0; i < 3; ++i) {
            draw_line(layer, color, 2, x + 7 + i * 6, y + 12,
                       x + 8 + i * 6, y + 12);
        }
    } else if (index == APP_MENU_SYSTEM) {
        draw_line(layer, color, 1, x, y + 3, x + 27, y + 3);
        draw_line(layer, color, 1, x, y + 3, x, y + 24);
        draw_line(layer, color, 1, x + 27, y + 3, x + 27, y + 24);
        draw_line(layer, color, 1, x, y + 24, x + 27, y + 24);
        const int trace[][2] = {{4, 16}, {9, 16}, {12, 9}, {16, 20}, {20, 13}, {24, 13}};
        for (unsigned i = 1; i < sizeof(trace) / sizeof(trace[0]); ++i) {
            draw_line(layer, color, 2, x + trace[i - 1][0], y + trace[i - 1][1],
                       x + trace[i][0], y + trace[i][1]);
        }
    } else {
        const int knobs[] = {7, 20, 12};
        for (unsigned i = 0; i < 3; ++i) {
            int line_y = y + 5 + 9 * (int)i;
            draw_line(layer, color, 1, x, line_y, x + 27, line_y);
            draw_line(layer, color, 3, x + knobs[i], line_y - 3, x + knobs[i], line_y + 3);
        }
    }
}

static void build_menu(void)
{
    create_label(s_body, "ESP32-C3", &cassian_mono_12, COLOR_MUTED, 12, 2, CONTENT_WIDTH);
    const char *entries[APP_MENU_COUNT] = { "AI 对话", "系统状态", "设置" };
    const char *descriptions[APP_MENU_COUNT] = {
        "尚未接入 / 查看配置",
        "内存 / 电量 / 运行",
        "音量 / 静音 / 试听",
    };
    for (unsigned i = 0; i < APP_MENU_COUNT; ++i) {
        s_menu_rows[i] = create_panel(s_body, 12, 24 + 64 * (int)i,
                                      CONTENT_WIDTH, 58, COLOR_SURFACE);
        lv_obj_set_style_border_width(s_menu_rows[i], 1, 0);
        // Each vector icon is drawn by one owned object. Its callback disappears
        // with the row, and never keeps pointers to a deleted page.
        s_menu_icons[i] = lv_obj_create(s_menu_rows[i]);
        lv_obj_remove_style_all(s_menu_icons[i]);
        lv_obj_set_pos(s_menu_icons[i], 12, 14);
        lv_obj_set_size(s_menu_icons[i], 28, 28);
        lv_obj_add_event_cb(s_menu_icons[i], draw_menu_icon, LV_EVENT_DRAW_MAIN,
                            (void *)(uintptr_t)i);
        s_menu_labels[i] = create_label(s_menu_rows[i], entries[i],
                                        &cassian_ui_18,
                                        COLOR_INK, 54, 4, CONTENT_WIDTH - 66);
        s_menu_descriptions[i] = create_label(s_menu_rows[i], descriptions[i],
                                              &cassian_ui_14, COLOR_MUTED,
                                              54, 31, CONTENT_WIDTH - 66);
    }
    create_panel(s_body, 12, 220, CONTENT_WIDTH, 1, COLOR_LINE);
    s_menu_heap = create_label(s_body, "--", &cassian_mono_12,
                               COLOR_INK, 12, 230, 70);
    s_menu_volume = create_label(s_body, "--", &cassian_mono_12,
                                 COLOR_ACCENT, 88, 230, 50);
    lv_obj_set_style_text_align(s_menu_volume, LV_TEXT_ALIGN_RIGHT, 0);
    s_menu_uptime = create_label(s_body, "--:--:--", &cassian_mono_12,
                                 COLOR_INK, 150, 230, 78);
    lv_obj_set_style_text_align(s_menu_uptime, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_menu_uptime, LV_LABEL_LONG_DOT);
    refresh_selection();
}

static void build_ai(void)
{
    create_label(s_body, "VOICE / AI", &cassian_mono_12,
                 COLOR_MUTED, 12, 2, CONTENT_WIDTH);
    lv_obj_t *notice = create_panel(s_body, 12, 26, CONTENT_WIDTH, 70, COLOR_SURFACE);
    lv_obj_set_style_border_width(notice, 1, 0);
    lv_obj_set_style_border_color(notice, lv_color_hex(COLOR_LINE), 0);
    create_label(notice, "对话尚未接入", &cassian_ui_18, COLOR_WARM, 10, 8, 196);
    create_label(notice, "暂不能开始录音", &cassian_ui_14, COLOR_MUTED, 10, 40, 196);
    create_label(s_body, "Wi-Fi 网络", &cassian_ui_14, COLOR_MUTED, 12, 112, 110);
    s_ai_wifi = create_label(s_body, "--", &cassian_ui_14, COLOR_INK, 128, 112, 100);
    lv_obj_set_style_text_align(s_ai_wifi, LV_TEXT_ALIGN_RIGHT, 0);
    create_label(s_body, "AI 服务", &cassian_ui_14, COLOR_MUTED, 12, 150, 110);
    s_ai_service = create_label(s_body, "--", &cassian_ui_14, COLOR_INK, 128, 150, 100);
    lv_obj_set_style_text_align(s_ai_service, LV_TEXT_ALIGN_RIGHT, 0);
    create_panel(s_body, 12, 184, CONTENT_WIDTH, 1, COLOR_LINE);
    s_ai_hint = create_label(s_body, "", &cassian_ui_14, COLOR_MUTED, 12, 200, CONTENT_WIDTH);
}

static void refresh_ai(void)
{
    const char *wifi = "未配置";
    if (s_connection.transport_error) wifi = "不可用";
    else if (!s_connection.ready) wifi = "读取中";
    else if (s_connection.radio_error) wifi = "不可用";
    else if (s_connection.wifi_state == USB_WIFI_CONNECTED) wifi = "已连接";
    else if (s_connection.wifi_state == USB_WIFI_CONNECTING) wifi = "连接中";
    else if (s_connection.wifi_state == USB_WIFI_RETRYING) wifi = "重试中";
    else if (s_connection.wifi_state == USB_WIFI_ERROR) wifi = "不可用";
    lv_label_set_text(s_ai_wifi, wifi);
    // Saving an endpoint does not authenticate it or start a conversation.
    lv_label_set_text(s_ai_service, s_connection.transport_error ? "不可用"
                     : !s_connection.ready ? "读取中"
                     : s_connection.storage_error ? "读取失败"
                     : s_connection.service_configured ? "已保存" : "未配置");
    const char *hint;
    switch (ai_conversation_guidance(&s_connection)) {
    case AI_GUIDANCE_STARTING: hint = "正在读取设备配置"; break;
    case AI_GUIDANCE_USB_UNAVAILABLE: hint = "USB 配置不可用"; break;
    case AI_GUIDANCE_STORAGE_ERROR: hint = "配置读取失败\n请用电脑 USB 重新保存"; break;
    case AI_GUIDANCE_WIFI_REQUIRED: hint = "请用电脑 USB 配置网络"; break;
    case AI_GUIDANCE_WIFI_ERROR: hint = "网络不可用\n请用电脑 USB 检查配置"; break;
    case AI_GUIDANCE_WIFI_CONNECTING: hint = "正在连接 Wi-Fi"; break;
    case AI_GUIDANCE_WIFI_RETRYING: hint = "网络连接失败\n请检查网络名称和密码"; break;
    case AI_GUIDANCE_SERVICE_REQUIRED: hint = "请用电脑 USB 配置服务"; break;
    default: hint = "服务地址已保存\n对话功能尚未接入"; break;
    }
    lv_label_set_text(s_ai_hint, hint);
}

static void create_metric_row(int y, const char *caption, lv_obj_t **value)
{
    lv_obj_t *card = create_panel(s_body, 12, y, CONTENT_WIDTH, 30, COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_LINE), 0);
    create_label(card, caption, &cassian_ui_14, COLOR_MUTED, 0, 5, 108);
    *value = create_label(card, "--", &cassian_mono_14,
                          COLOR_INK, CONTENT_WIDTH - 88, 6, 88);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_RIGHT, 0);
}

static void build_system(void)
{
    create_label(s_body, "ESP32-C3", &cassian_mono_12, COLOR_MUTED, 12, 0, 85);
    s_battery_value = create_label(s_body, "--V", &cassian_mono_12,
                                   COLOR_MUTED, 108, 0, 120);
    lv_obj_set_style_text_align(s_battery_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *heap = create_panel(s_body, 12, 24, CONTENT_WIDTH, 86, COLOR_SURFACE);
    lv_obj_set_style_border_width(heap, 1, 0);
    lv_obj_set_style_border_color(heap, lv_color_hex(COLOR_LINE), 0);
    create_label(heap, "可用内存", &cassian_ui_14,
                 COLOR_MUTED, 10, 7, 108);
    s_heap_percent = create_label(heap, "--", &cassian_mono_12,
                                  COLOR_ACCENT, 138, 11, 68);
    lv_obj_set_style_text_align(s_heap_percent, LV_TEXT_ALIGN_RIGHT, 0);
    s_heap_value = create_label(heap, "--", &cassian_mono_32,
                                COLOR_INK, 10, 30, CONTENT_WIDTH - 20);
    s_heap_bar = create_bar(heap, 76);
    create_metric_row(122, "最大连续块", &s_largest_value);
    create_metric_row(160, "最低可用", &s_min_value);
    create_metric_row(198, "运行时间", &s_uptime_value);
    lv_obj_set_width(s_uptime_value, CONTENT_WIDTH);
    lv_obj_set_pos(s_uptime_value, 0, 25);
    lv_label_set_long_mode(s_uptime_value, LV_LABEL_LONG_DOT);
    lv_obj_set_height(lv_obj_get_parent(s_uptime_value), 48);
}

static void draw_volume_meter(lv_event_t *event)
{
    lv_obj_t *bar = lv_event_get_target(event);
    lv_area_t area;
    lv_obj_get_coords(bar, &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    int value = lv_bar_get_value(bar);
    lv_draw_rect_dsc_t segment;
    lv_draw_rect_dsc_init(&segment);
    for (int i = 0; i < 20; ++i) {
        segment.bg_color = lv_color_hex(value >= (i + 1) * 5 ? COLOR_ACCENT : COLOR_LINE);
        lv_area_t rect = {area.x1 + i * 11, area.y1 + 3,
                          area.x1 + i * 11 + 6, area.y2 - 3};
        lv_draw_rect(layer, &segment, &rect);
    }
}

static void build_settings(void)
{
    create_label(s_body, "输出音量", &cassian_ui_14,
                 COLOR_MUTED, 12, 2, CONTENT_WIDTH);
    s_sound_value = create_label(s_body, "", &cassian_mono_32,
                                 COLOR_ACCENT, 12, 28, CONTENT_WIDTH);
    s_volume_bar = lv_bar_create(s_body);
    lv_obj_remove_style_all(s_volume_bar);
    lv_obj_set_pos(s_volume_bar, 12, 86);
    lv_obj_set_size(s_volume_bar, CONTENT_WIDTH, 40);
    lv_bar_set_range(s_volume_bar, 0, 100);
    // One LVGL bar owns its current value and all twenty drawn segments. Page
    // cleanup releases the callback; no segment objects or producer task exist.
    lv_obj_add_event_cb(s_volume_bar, draw_volume_meter, LV_EVENT_DRAW_MAIN, NULL);
    const char *ticks[] = {"0", "25", "50", "75", "100"};
    for (int i = 0; i < 5; ++i) {
        int x = i == 4 ? 204 : 12 + i * 52;
        create_label(s_body, ticks[i], &cassian_mono_12, COLOR_MUTED, x, 134, 24);
    }
    create_panel(s_body, 12, 168, CONTENT_WIDTH, 1, COLOR_LINE);
    create_label(s_body, "声音", &cassian_ui_14,
                 COLOR_MUTED, 12, 182, 64);
    s_sound_state = create_label(s_body, "", &cassian_ui_14,
                                 COLOR_ACCENT, CONTENT_WIDTH - 68, 182, 80);
    lv_obj_set_style_text_align(s_sound_state, LV_TEXT_ALIGN_RIGHT, 0);
    s_sound_hint = create_label(s_body, "", &cassian_ui_14,
                                COLOR_MUTED, 12, 220, CONTENT_WIDTH);
}

static void refresh_page(void)
{
    if (s_metrics.soc < 0) lv_label_set_text(s_battery, "--%");
    else lv_label_set_text_fmt(s_battery, "%d%%", s_metrics.soc);

    if (s_navigation.page == APP_PAGE_MENU) {
        refresh_selection();
        lv_label_set_text(s_menu_heap, s_metrics.free);
        if (s_audio_ready) {
            lv_label_set_text_fmt(s_menu_volume, "%u%%", (unsigned)app_settings_output_volume());
        } else {
            lv_label_set_text(s_menu_volume, "--");
        }
        lv_label_set_text(s_menu_uptime, s_metrics.uptime);
    } else if (s_navigation.page == APP_PAGE_AI) {
        refresh_ai();
    } else if (s_navigation.page == APP_PAGE_SYSTEM) {
        lv_label_set_text(s_heap_value, s_metrics.free);
        lv_label_set_text(s_largest_value, s_metrics.largest);
        lv_label_set_text(s_min_value, s_metrics.minimum);
        lv_label_set_text(s_uptime_value, s_metrics.uptime);
        if (s_metrics.mv < 0) lv_label_set_text(s_battery_value, "--V");
        else lv_label_set_text_fmt(s_battery_value, "%d.%02dV",
                                    s_metrics.mv / 1000, (s_metrics.mv % 1000) / 10);
        // This gauge is the free fraction, matching the large free-heap value.
        // Before the first valid sample, show an unavailable gauge instead of
        // inventing a fully available heap from the zero-initialized snapshot.
        int free_percent = s_metrics.heap_valid ? 100 - s_metrics.used_percent : 0;
        if (s_metrics.heap_valid) lv_label_set_text_fmt(s_heap_percent, "%d%%", free_percent);
        else lv_label_set_text(s_heap_percent, "--");
        lv_bar_set_value(s_heap_bar, free_percent, LV_ANIM_OFF);
    } else if (s_navigation.page == APP_PAGE_SETTINGS) {
        uint8_t output = app_settings_output_volume();
        // Each label selects a font with real coverage; numeric typography stays
        // separate from the two Chinese glyphs used by the large mute state.
        lv_obj_set_style_text_font(s_sound_value,
                                   output > 0 ? &cassian_mono_32 : &cassian_ui_28, 0);
        lv_obj_set_style_text_color(s_sound_value,
                                    lv_color_hex(output > 0 ? COLOR_ACCENT : COLOR_WARM), 0);
        if (output > 0) lv_label_set_text_fmt(s_sound_value, "%u%%", (unsigned)output);
        else lv_label_set_text(s_sound_value, "静音");
        lv_label_set_text(s_sound_state, output > 0 ? "已开启" : "已静音");
        lv_obj_set_style_text_color(s_sound_state,
                                    lv_color_hex(output > 0 ? COLOR_ACCENT : COLOR_WARM), 0);
        lv_bar_set_value(s_volume_bar, output, LV_ANIM_OFF);
        if (s_feedback) lv_label_set_text(s_sound_hint, s_feedback);
        else if (output == 0) lv_label_set_text_fmt(s_sound_hint, "上次音量：%u%%",
                                                   (unsigned)app_settings_volume());
        else lv_label_set_text(s_sound_hint, "已保存到设备");
        lv_obj_set_style_text_color(s_sound_hint,
                                    lv_color_hex(s_feedback_error ? COLOR_ERROR : COLOR_MUTED), 0);
    }
}

static void build_page(void)
{
    // All page objects live below this one parent. Deleting its children under
    // the LVGL lock also cancels their animations; no stale widget is refreshed.
    lv_obj_clean(s_body);
    const char *title;
    switch (s_navigation.page) {
    case APP_PAGE_HOME:
        title = "个人主页";
        build_home();
        break;
    case APP_PAGE_MENU:
        title = "功能列表";
        build_menu();
        break;
    case APP_PAGE_AI:
        title = "AI 对话";
        build_ai();
        break;
    case APP_PAGE_SYSTEM:
        title = "系统状态";
        build_system();
        break;
    default:
        title = "设置";
        build_settings();
        break;
    }
    lv_label_set_text(s_title, title);
    refresh_page();
}

void personal_home_create(bool audio_ready, bool settings_ready)
{
    s_audio_ready = audio_ready;
    s_feedback = !settings_ready ? "设置读取失败"
                 : !audio_ready ? "音频不可用" : NULL;
    s_feedback_error = !settings_ready || !audio_ready;
    s_navigation = (app_navigation_t){0};
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x172329), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(0x070C0F), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_scr, draw_grid, LV_EVENT_DRAW_MAIN, NULL);
    create_panel(s_scr, 14, 16, 3, 14, COLOR_ACCENT);
    s_title = create_label(s_scr, "个人主页", &cassian_ui_14,
                           COLOR_INK, 24, 12, 126);
    s_battery = create_label(s_scr, "--%", &cassian_mono_12,
                             COLOR_ACCENT, 158, 16, 66);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    create_panel(s_scr, 12, 37, CONTENT_WIDTH, 1, COLOR_LINE);
    create_panel(s_scr, 12, 291, CONTENT_WIDTH, 1, COLOR_LINE);
    // Every page uses the available width, without an instructional rail.
    s_body = create_panel(s_scr, 0, 40, 240, 248, COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    create_label(s_scr, "cassianflorin.com", &cassian_mono_12,
                 COLOR_MUTED, 16, 299, 180);
    create_label(s_scr, "CF", &cassian_mono_12, COLOR_ACCENT, 208, 299, 20);
    build_page();
    lv_screen_load(s_scr);
}

void personal_home_refresh(void)
{
    const uint32_t caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
    size_t total = heap_caps_get_total_size(caps);
    size_t free = heap_caps_get_free_size(caps);
    s_metrics.heap_valid = total > 0;
    if (s_metrics.heap_valid) {
        performance_format_bytes(s_metrics.free, sizeof(s_metrics.free), free);
    } else {
        (void)lv_snprintf(s_metrics.free, sizeof(s_metrics.free), "--");
    }
    performance_format_bytes(s_metrics.largest, sizeof(s_metrics.largest),
                              heap_caps_get_largest_free_block(caps));
    performance_format_bytes(s_metrics.minimum, sizeof(s_metrics.minimum),
                              heap_caps_get_minimum_free_size(caps));
    performance_format_uptime(s_metrics.uptime, sizeof(s_metrics.uptime),
                              (uint64_t)(esp_timer_get_time() / 1000000LL));
    s_metrics.used_percent = performance_used_percent(total, free);
    // Battery reads can perform I2C; keep them outside the UI lock.
    s_metrics.soc = bsp_battery_soc();
    s_metrics.mv = bsp_battery_mv();
    usb_config_get_status(&s_connection);
    if (!bsp_lvgl_lock(250)) return;
    refresh_page();
    bsp_lvgl_unlock();
}

void personal_home_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn < BSP_BTN_UP || btn > BSP_BTN_OK) return;
    if (ev == BSP_BTN_PRESS || ev == BSP_BTN_RELEASE) {
        // Raw edges are not user actions, including after a long-press return.
        return;
    }
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_DOUBLE && ev != BSP_BTN_LONG) return;
    const app_key_t keys[] = { APP_KEY_UP, APP_KEY_DOWN, APP_KEY_OK };
    const app_gesture_t gesture = ev == BSP_BTN_DOUBLE ? APP_GESTURE_DOUBLE
                                  : ev == BSP_BTN_LONG ? APP_GESTURE_LONG
                                  : APP_GESTURE_CLICK;
    app_navigation_t next = s_navigation;
    app_action_t action = app_navigation_handle(&next, keys[btn], gesture);
    if (action == APP_ACTION_NONE) return;

    if (action == APP_ACTION_PAGE || action == APP_ACTION_SELECTION) {
        usb_config_get_status(&s_connection);
        // Publish navigation only after obtaining the lock. On contention the
        // old screen and its button semantics must remain one consistent pair.
        if (!bsp_lvgl_lock(250)) return;
        s_navigation = next;
        if (action == APP_ACTION_PAGE) build_page();
        else refresh_selection();
        bsp_lvgl_unlock();
        return;
    }

    // This code runs on the input worker, never the shared button timer. Audio
    // and Flash writes are serialized outside the LVGL lock and only in settings.
    esp_err_t err;
    if (action == APP_ACTION_PREVIEW) {
        err = s_audio_ready ? app_audio_preview() : ESP_ERR_INVALID_STATE;
        s_feedback = err != ESP_OK ? "试听失败"
                     : app_settings_sound_enabled() ? "试听完成" : "当前已静音";
        s_feedback_error = err != ESP_OK;
    } else {
        err = action == APP_ACTION_MUTE
            ? app_settings_set_sound_enabled(!app_settings_sound_enabled())
            : app_settings_step_volume(action == APP_ACTION_VOLUME_UP);
        if (err == ESP_OK && s_audio_ready) {
            bsp_audio_set_volume(app_settings_output_volume());
        }
        s_feedback = err != ESP_OK ? "保存失败"
                     : !s_audio_ready ? "音频不可用" : NULL;
        s_feedback_error = err != ESP_OK || !s_audio_ready;
    }
    if (!bsp_lvgl_lock(250)) return;
    refresh_page();
    bsp_lvgl_unlock();
}

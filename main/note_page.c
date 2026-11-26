// main/note_page.c - Record a spoken note and list the ones waiting for sync.
//
// Every Chinese string here uses cassian_chat_14 (GB2312 coverage), which also
// renders arbitrary transcripts; tools/check_cassian_fonts.py verifies that the
// fixed literals below are covered.
#include "note_page.h"

#include "ai_client.h"
#include "ai_conversation_state.h"
#include "ai_page.h"
#include "cassian_assets.h"
#include "note_store.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

// Same palette as personal_home.c and ai_page.c.
enum {
    COLOR_SURFACE = 0x131E23,
    COLOR_INK = 0xE2EBE8,
    COLOR_ACCENT = 0xBDF56A,
    COLOR_MUTED = 0x8A9FA6,
    COLOR_LINE = 0x2B3C43,
    COLOR_WARM = 0xE6C77B,
    COLOR_ERROR = 0xFF8D7E,
    CONTENT_WIDTH = 216,
    SCROLL_STEP = 60,
    // Newest notes kept on screen; bounded so the list's LVGL objects stay small.
    NOTE_PAGE_SHOWN = 5,
};

// Times are shown in China Standard Time; the stored epoch stays UTC.
#define DISPLAY_UTC_OFFSET (8 * 3600)

static lv_obj_t *s_count;
static lv_obj_t *s_card;
static lv_obj_t *s_title;
static lv_obj_t *s_hint;
static lv_obj_t *s_list;
static bool s_built;
static uint32_t s_list_revision;

// Snapshot of the newest notes; only the input worker reads or writes it.
static note_t s_cache[NOTE_PAGE_SHOWN];
static size_t s_cache_count;
static size_t s_total;
static bool s_store_ready;
static bool s_cache_valid;
static uint32_t s_cache_revision;

void note_page_prepare(void)
{
    uint32_t revision = note_store_revision();
    if (s_cache_valid && revision == s_cache_revision) return;
    s_total = note_store_count();
    s_store_ready = note_store_ready();
    s_cache_count = 0;
    while (s_cache_count < s_total && s_cache_count < NOTE_PAGE_SHOWN &&
           note_store_newest(s_cache_count, &s_cache[s_cache_count])) {
        s_cache_count++;
    }
    s_cache_revision = revision;
    s_cache_valid = true;
}

size_t note_page_pending(void)
{
    return s_total;
}

bool note_page_store_ready(void)
{
    return s_store_ready;
}

static lv_obj_t *label(lv_obj_t *parent, uint32_t color, int x, int y, int width)
{
    lv_obj_t *item = lv_label_create(parent);
    lv_label_set_text(item, "");
    lv_obj_set_style_text_font(item, &cassian_chat_14, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(color), 0);
    lv_obj_set_pos(item, x, y);
    lv_obj_set_width(item, width);
    return item;
}

static void format_when(char *out, size_t capacity, const note_t *note)
{
    if (!note->epoch) {
        // Saved before the clock was set; a sync dates it on delivery.
        snprintf(out, capacity, "#%lu", (unsigned long)note->seq);
        return;
    }
    time_t local = (time_t)note->epoch + DISPLAY_UTC_OFFSET;
    struct tm parts;
    gmtime_r(&local, &parts);
    snprintf(out, capacity, "#%lu  %02d-%02d %02d:%02d", (unsigned long)note->seq,
             parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min);
}

static void rebuild_list(void)
{
    // Drawn from the snapshot only; note_page_prepare did the Flash reads.
    s_list_revision = s_cache_revision;
    lv_obj_clean(s_list);
    if (!s_total) {
        lv_obj_t *empty = label(s_list, COLOR_MUTED, 0, 0, CONTENT_WIDTH - 6);
        lv_label_set_text(empty, s_store_ready ? "还没有随手记。说一句，就存在设备里。"
                                               : "随手记存储读取失败。");
        return;
    }
    for (size_t i = 0; i < s_cache_count; ++i) {
        char when[32];
        format_when(when, sizeof(when), &s_cache[i]);
        lv_obj_t *head = label(s_list, COLOR_MUTED, 0, 0, CONTENT_WIDTH - 6);
        lv_obj_set_style_text_font(head, &cassian_mono_12, 0);
        lv_label_set_text(head, when);
        lv_obj_t *body = label(s_list, COLOR_INK, 0, 0, CONTENT_WIDTH - 6);
        lv_label_set_text(body, s_cache[i].text);
    }
    if (s_total > s_cache_count) {
        lv_obj_t *more = label(s_list, COLOR_MUTED, 0, 0, CONTENT_WIDTH - 6);
        lv_label_set_text_fmt(more, "还有 %u 条较早的随手记", (unsigned)(s_total - s_cache_count));
    }
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
}

void note_page_build(lv_obj_t *parent)
{
    // A real mode switch clears the chat page's last exchange from the shared client.
    ai_client_set_mode(AI_MODE_NOTE);

    lv_obj_t *caption = label(parent, COLOR_MUTED, 12, 0, 100);
    lv_obj_set_style_text_font(caption, &cassian_mono_12, 0);
    lv_label_set_text(caption, "VOICE / NOTE");
    s_count = label(parent, COLOR_MUTED, 104, -2, 124);
    lv_obj_set_style_text_align(s_count, LV_TEXT_ALIGN_RIGHT, 0);

    s_card = lv_obj_create(parent);
    lv_obj_remove_style_all(s_card);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_card, 12, 22);
    lv_obj_set_size(s_card, CONTENT_WIDTH, 52);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_border_side(s_card, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(COLOR_ACCENT), 0);
    s_title = label(s_card, COLOR_INK, 10, 6, CONTENT_WIDTH - 16);
    s_hint = label(s_card, COLOR_MUTED, 10, 27, CONTENT_WIDTH - 16);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_DOT);

    s_list = lv_obj_create(parent);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, 12, 84);
    lv_obj_set_size(s_list, CONTENT_WIDTH, 160);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 4, 0);
    lv_obj_set_style_pad_right(s_list, 6, 0);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(COLOR_LINE), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_list, 2, LV_PART_SCROLLBAR);
    s_built = true;
    rebuild_list();
}

void note_page_refresh(const usb_config_status_t *connection)
{
    if (!s_built || !lv_obj_is_valid(s_card)) {
        s_built = false;
        return;
    }
    ai_client_status_t status;
    ai_client_get_status(&status);
    ai_conversation_guidance_t guidance = ai_conversation_guidance(connection);
    size_t count = s_total;
    bool ready = s_store_ready;
    bool full = count >= NOTE_STORE_CAPACITY;
    if (ready) {
        lv_label_set_text_fmt(s_count, "待同步 %u/%u", (unsigned)count,
                              (unsigned)NOTE_STORE_CAPACITY);
    } else {
        lv_label_set_text(s_count, "存储异常");
    }

    char title[64];
    char hint[96];
    uint32_t accent = COLOR_ACCENT;
    snprintf(hint, sizeof(hint), "OK 取消");
    switch (status.stage) {
    case AI_STAGE_CONNECTING:
        snprintf(title, sizeof(title), "正在连接语音服务");
        break;
    case AI_STAGE_LISTENING:
        snprintf(title, sizeof(title), "正在听 · %u 秒", (unsigned)status.seconds);
        snprintf(hint, sizeof(hint), "说完按 OK 保存，最长 %d 秒", AI_CLIENT_RECORD_SECONDS);
        accent = COLOR_WARM;
        break;
    case AI_STAGE_TRANSCRIBING:
        snprintf(title, sizeof(title), "正在识别语音");
        break;
    case AI_STAGE_ERROR: {
        accent = COLOR_ERROR;
        bool no_asr = status.error == AI_ERROR_NOT_FOUND &&
                      status.failed_stage != AI_STAGE_CONNECTING;
        if (status.error == AI_ERROR_STORAGE_FULL) {
            snprintf(title, sizeof(title), "设备已存满，这条没有保存");
            snprintf(hint, sizeof(hint), "同步后才能继续记录");
        } else if (status.error == AI_ERROR_STORAGE) {
            snprintf(title, sizeof(title), "保存失败，这条没有保存");
            snprintf(hint, sizeof(hint), "无法写入设备存储 · OK 重试");
        } else if (no_asr) {
            snprintf(title, sizeof(title), "服务不支持语音识别");
            snprintf(hint, sizeof(hint), "或模型不存在 · OK 重试");
        } else {
            snprintf(title, sizeof(title), "%s",
                     status.failed_stage == AI_STAGE_CONNECTING ? "无法开始录音"
                                                                 : "语音识别失败");
            if (status.error == AI_ERROR_HTTP) {
                snprintf(hint, sizeof(hint), "服务返回 HTTP %d · OK 重试", status.http_status);
            } else {
                snprintf(hint, sizeof(hint), "%s · OK 重试", ai_error_text(status.error));
            }
        }
        break;
    }
    case AI_STAGE_DONE:
        snprintf(title, sizeof(title), "已存到设备");
        snprintf(hint, sizeof(hint), "按 OK 再记一条 · UP/DOWN 翻看");
        break;
    default:
        if (status.busy) {
            snprintf(title, sizeof(title), "正在停止上一次请求");
            snprintf(hint, sizeof(hint), "稍候即可重新开始");
            accent = COLOR_MUTED;
        } else if (!ready) {
            snprintf(title, sizeof(title), "随手记存储读取失败");
            snprintf(hint, sizeof(hint), "已有内容不会被改动");
            accent = COLOR_ERROR;
        } else if (full) {
            snprintf(title, sizeof(title), "设备已存满 %u 条", (unsigned)NOTE_STORE_CAPACITY);
            snprintf(hint, sizeof(hint), "同步后才能继续记录");
            accent = COLOR_WARM;
        } else if (guidance != AI_GUIDANCE_READY) {
            snprintf(title, sizeof(title), "%s", ai_guidance_text(guidance));
            snprintf(hint, sizeof(hint), "完成后按 OK 开始记录");
            accent = COLOR_WARM;
        } else {
            snprintf(title, sizeof(title), "按 OK 说一句");
            snprintf(hint, sizeof(hint), "说完再按 OK · 长按 OK 返回");
        }
        break;
    }
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_set_style_border_color(s_card, lv_color_hex(accent), 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(
        status.stage == AI_STAGE_ERROR ? COLOR_ERROR : COLOR_INK), 0);

    if (s_cache_revision != s_list_revision) rebuild_list();
}

void note_page_scroll(int steps)
{
    if (!s_built || !lv_obj_is_valid(s_list)) return;
    lv_obj_scroll_by_bounded(s_list, 0, steps * SCROLL_STEP, LV_ANIM_ON);
}

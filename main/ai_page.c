// Every string on this page uses cassian_chat_14, whose GB2312 coverage also
// renders arbitrary transcripts and replies. tools/check_cassian_fonts.py
// verifies that the fixed literals below are covered.
#include "ai_page.h"

#include "ai_client.h"
#include "ai_conversation_state.h"
#include "cassian_assets.h"

#include <stdio.h>
#include <string.h>

// Same palette as personal_home.c.
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
};

static lv_obj_t *s_wifi;
static lv_obj_t *s_card;
static lv_obj_t *s_title;
static lv_obj_t *s_hint;
static lv_obj_t *s_log;
static lv_obj_t *s_user;
static lv_obj_t *s_reply;
static bool s_built;

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

void ai_page_build(lv_obj_t *parent)
{
    lv_obj_t *caption = label(parent, COLOR_MUTED, 12, 0, 100);
    lv_obj_set_style_text_font(caption, &cassian_mono_12, 0);
    lv_label_set_text(caption, "VOICE / AI");
    s_wifi = label(parent, COLOR_MUTED, 104, -2, 124);
    lv_obj_set_style_text_align(s_wifi, LV_TEXT_ALIGN_RIGHT, 0);

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

    // A single scrollable column; UP/DOWN scroll it, so no focus handling.
    s_log = lv_obj_create(parent);
    lv_obj_remove_style_all(s_log);
    lv_obj_set_pos(s_log, 12, 84);
    lv_obj_set_size(s_log, CONTENT_WIDTH, 160);
    lv_obj_set_flex_flow(s_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_log, 8, 0);
    lv_obj_set_style_pad_right(s_log, 6, 0);
    lv_obj_set_scrollbar_mode(s_log, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(s_log, lv_color_hex(COLOR_LINE), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_log, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_log, 2, LV_PART_SCROLLBAR);
    s_user = label(s_log, COLOR_MUTED, 0, 0, CONTENT_WIDTH - 6);
    s_reply = label(s_log, COLOR_INK, 0, 0, CONTENT_WIDTH - 6);
    s_built = true;
}

static const char *error_text(ai_error_t error)
{
    switch (error) {
    case AI_ERROR_NOT_READY: return "网络或服务未就绪";
    case AI_ERROR_CONFIG: return "Base URL 或模型配置无效";
    case AI_ERROR_NETWORK: return "无法连接服务";
    case AI_ERROR_AUTH: return "API Key 无效或无权限";
    case AI_ERROR_NOT_FOUND: return "接口地址或模型不存在";
    case AI_ERROR_RATE_LIMIT: return "请求过多或额度不足";
    case AI_ERROR_SERVER: return "服务暂时不可用";
    case AI_ERROR_RESPONSE: return "服务回复格式不支持";
    case AI_ERROR_NO_SPEECH: return "没有听清，请再说一次";
    case AI_ERROR_MEMORY: return "设备内存不足";
    case AI_ERROR_AUDIO: return "麦克风或扬声器故障";
    case AI_ERROR_AUDIO_BUSY: return "音频正在使用";
    default: return "服务返回错误";
    }
}

static const char *guidance_text(ai_conversation_guidance_t guidance)
{
    switch (guidance) {
    case AI_GUIDANCE_STARTING: return "正在读取设备配置";
    case AI_GUIDANCE_USB_UNAVAILABLE: return "配置服务不可用";
    case AI_GUIDANCE_STORAGE_ERROR: return "配置读取失败，请用电脑重新保存";
    case AI_GUIDANCE_WIFI_REQUIRED: return "请用电脑 USB 配置 Wi-Fi";
    case AI_GUIDANCE_WIFI_ERROR: return "Wi-Fi 不可用，请检查配置";
    case AI_GUIDANCE_WIFI_CONNECTING: return "正在连接 Wi-Fi";
    case AI_GUIDANCE_WIFI_RETRYING: return "Wi-Fi 连接失败，正在重试";
    case AI_GUIDANCE_SERVICE_REQUIRED: return "请用电脑 USB 配置 AI 服务";
    default: return "";
    }
}

static const char *wifi_text(const usb_config_status_t *connection)
{
    if (connection->transport_error || connection->radio_error ||
        connection->wifi_state == USB_WIFI_ERROR) return "Wi-Fi 不可用";
    if (!connection->ready) return "读取中";
    switch (connection->wifi_state) {
    case USB_WIFI_CONNECTED: return "Wi-Fi 已连接";
    case USB_WIFI_CONNECTING: return "Wi-Fi 连接中";
    case USB_WIFI_RETRYING: return "Wi-Fi 重试中";
    default: return "Wi-Fi 未配置";
    }
}

static void show_text(const char *user, const char *reply, void *arg)
{
    (void)arg;
    // Reassigning unchanged text would reset layout and the reader's scroll.
    bool changed = strcmp(lv_label_get_text(s_reply), reply) != 0;
    if (changed) lv_label_set_text(s_reply, reply);
    char shown[AI_CLIENT_USER_MAX + 8];
    if (user[0]) snprintf(shown, sizeof(shown), "你：%s", user);
    else snprintf(shown, sizeof(shown), "对话内容会显示在这里。");
    if (strcmp(lv_label_get_text(s_user), shown)) {
        lv_label_set_text(s_user, shown);
        changed = true;
    }
    if (changed) lv_obj_scroll_to_y(s_log, 0, LV_ANIM_OFF);
}

void ai_page_refresh(const usb_config_status_t *connection)
{
    if (!s_built || !lv_obj_is_valid(s_card)) {
        s_built = false;
        return;
    }
    ai_client_status_t status;
    ai_client_get_status(&status);
    ai_conversation_guidance_t guidance = ai_conversation_guidance(connection);
    lv_label_set_text(s_wifi, wifi_text(connection));

    char title[64];
    char hint[96];
    uint32_t accent = COLOR_ACCENT;
    snprintf(hint, sizeof(hint), "OK 取消");
    switch (status.stage) {
    case AI_STAGE_CONNECTING:
        snprintf(title, sizeof(title), "正在连接服务");
        break;
    case AI_STAGE_LISTENING:
        snprintf(title, sizeof(title), "正在听 · %u 秒", (unsigned)status.seconds);
        snprintf(hint, sizeof(hint), "说完按 OK 发送，最长 %d 秒", AI_CLIENT_RECORD_SECONDS);
        accent = COLOR_WARM;
        break;
    case AI_STAGE_TRANSCRIBING:
        snprintf(title, sizeof(title), "正在识别语音");
        break;
    case AI_STAGE_THINKING:
        snprintf(title, sizeof(title), "正在思考");
        break;
    case AI_STAGE_SPEAKING:
        snprintf(title, sizeof(title), "正在朗读回复");
        snprintf(hint, sizeof(hint), "OK 停止朗读 · UP/DOWN 滚动");
        break;
    case AI_STAGE_ERROR:
        accent = COLOR_ERROR;
        snprintf(title, sizeof(title), "%s",
                 status.failed_stage == AI_STAGE_THINKING ? "对话失败"
                 : status.failed_stage == AI_STAGE_CONNECTING ? "无法开始录音"
                 : "语音识别失败");
        if (status.error == AI_ERROR_HTTP) {
            snprintf(hint, sizeof(hint), "服务返回 HTTP %d · OK 重试", status.http_status);
        } else {
            snprintf(hint, sizeof(hint), "%s · OK 重试", error_text(status.error));
        }
        break;
    case AI_STAGE_DONE:
        snprintf(title, sizeof(title), "按 OK 继续对话");
        if (status.speech_error == AI_ERROR_HTTP) {
            snprintf(hint, sizeof(hint), "语音播放失败：HTTP %d", status.http_status);
        } else if (status.speech_error != AI_ERROR_NONE) {
            snprintf(hint, sizeof(hint), "语音播放失败：%s", error_text(status.speech_error));
        } else if (status.speech_skipped) {
            snprintf(hint, sizeof(hint), "已静音，仅显示文字");
        } else {
            snprintf(hint, sizeof(hint), "UP/DOWN 滚动 · 长按 UP 清空");
        }
        if (status.speech_error != AI_ERROR_NONE) accent = COLOR_WARM;
        break;
    default:
        if (status.busy) {
            snprintf(title, sizeof(title), "正在停止上一次请求");
            snprintf(hint, sizeof(hint), "稍候即可重新开始");
            accent = COLOR_MUTED;
        } else if (guidance != AI_GUIDANCE_READY) {
            snprintf(title, sizeof(title), "%s", guidance_text(guidance));
            snprintf(hint, sizeof(hint), "完成后按 OK 开始对话");
            accent = COLOR_WARM;
        } else {
            snprintf(title, sizeof(title), "按 OK 开始说话");
            snprintf(hint, sizeof(hint), "长按 UP 清空 · 长按 OK 返回");
        }
        break;
    }
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_set_style_border_color(s_card, lv_color_hex(accent), 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(
        status.stage == AI_STAGE_ERROR ? COLOR_ERROR : COLOR_INK), 0);

    (void)ai_client_read_text(show_text, NULL);
}

void ai_page_scroll(int steps)
{
    if (!s_built || !lv_obj_is_valid(s_log)) return;
    lv_obj_scroll_by_bounded(s_log, 0, steps * SCROLL_STEP, LV_ANIM_ON);
}

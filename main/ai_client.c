// main/ai_client.c - Speech -> transcript -> chat reply -> speech, one turn per task.
//
// Memory budget without PSRAM: one HTTPS connection at a time, an 8 KB work
// buffer, and a 16 KB capture ring only while recording. Audio is streamed as
// chunked multipart upload and played while the speech response downloads.
#include "ai_client.h"

#include "ai_protocol.h"
#include "app_audio.h"
#include "app_settings.h"
#include "bsp_audio.h"
#include "usb_config.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 16000
#define RECORD_LIMIT_US ((int64_t)AI_CLIENT_RECORD_SECONDS * 1000000)
#define RECORD_MIN_BYTES (SAMPLE_RATE * 2 * 3 / 10) // 0.3 s
#define CAPTURE_RING_BYTES (16 * 1024)              // 0.5 s absorbs Wi-Fi/TLS stalls
#define CAPTURE_CHUNK 512
#define UPLOAD_CHUNK 1024
#define CHUNK_HEADROOM 16
#define WORK_BUFFER 8192
#define HTTP_TIMEOUT_MS 30000
#define TURN_TASK_STACK 8192
#define CAPTURE_TASK_STACK 3072
#define HISTORY_TURNS 2

static const char *TAG = "ai_client";

// Replies are shown on a 240x320 screen and spoken, so ask for short plain text.
static const char *SYSTEM_PROMPT =
    "你是 AI Passport 掌上设备里的语音助手。你的回答会显示在很小的屏幕上并朗读出来。"
    "请用简洁、口语化的语言回答，默认使用用户所说的语言，一般不超过 80 个字。"
    "不要使用 Markdown、列表符号、表格、代码块或表情符号。";

typedef struct {
    char user[AI_CLIENT_USER_MAX];
    char reply[AI_CLIENT_REPLY_MAX];
} turn_t;

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static ai_client_status_t s_status;
static SemaphoreHandle_t s_text_lock;
// Written only by the turn task while holding s_text_lock.
static char s_user[AI_CLIENT_USER_MAX];
static char s_reply[AI_CLIENT_REPLY_MAX];
// Owned by the turn task while s_running; by the input worker otherwise.
static turn_t s_history[HISTORY_TURNS];
static size_t s_history_count;

static atomic_bool s_running, s_send, s_cancel, s_stop_speaking;
// Set with s_running while a model list is fetched for the configuration page.
static atomic_bool s_fetching;
static atomic_bool s_capture_stop, s_capture_failed;
static atomic_uint s_capture_dropped;
static StreamBufferHandle_t s_ring;
static SemaphoreHandle_t s_capture_done;

static void publish(void (*update)(ai_client_status_t *, int), int value)
{
    taskENTER_CRITICAL(&s_status_lock);
    update(&s_status, value);
    s_status.revision++;
    taskEXIT_CRITICAL(&s_status_lock);
}

static void update_stage(ai_client_status_t *status, int stage)
{
    status->stage = (ai_stage_t)stage;
}

static void update_seconds(ai_client_status_t *status, int seconds)
{
    status->seconds = (uint8_t)seconds;
}

// After a cancel the UI already shows the idle page; a closing turn must not
// replace it with a stale stage, error or result.
static void set_stage(ai_stage_t stage)
{
    if (!atomic_load(&s_cancel)) publish(update_stage, stage);
}

static void fail(ai_error_t error, int http_status)
{
    if (atomic_load(&s_cancel)) return;
    taskENTER_CRITICAL(&s_status_lock);
    s_status.failed_stage = s_status.stage;
    s_status.stage = AI_STAGE_ERROR;
    s_status.error = error;
    s_status.http_status = http_status;
    s_status.revision++;
    taskEXIT_CRITICAL(&s_status_lock);
}

static void speech_failed(ai_error_t error, int http_status)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.speech_error = error;
    s_status.http_status = http_status;
    s_status.revision++;
    taskEXIT_CRITICAL(&s_status_lock);
}

static void set_text(char *target, size_t capacity, const char *text)
{
    xSemaphoreTake(s_text_lock, portMAX_DELAY);
    ai_utf8_copy(target, capacity, text);
    xSemaphoreGive(s_text_lock);
}

typedef struct {
    const char *url;
    const char *token;
} service_t;

static service_t chat_service(const usb_config_values_t *config)
{
    return (service_t){ config->service_url, config->service_token };
}

// Chat-only providers (for example DeepSeek) pair with a separate speech
// provider; an empty speech URL means the chat service offers all endpoints.
static service_t speech_service(const usb_config_values_t *config)
{
    return config->speech_url[0]
         ? (service_t){ config->speech_url, config->speech_token }
         : chat_service(config);
}

static esp_http_client_handle_t open_request(service_t service, esp_http_client_method_t method,
                                             const char *path, const char *content_type,
                                             int length, ai_error_t *error)
{
    char url[USB_CONFIG_URL_MAX + 32];
    if (!ai_endpoint(url, sizeof(url), service.url, path)) {
        *error = AI_ERROR_CONFIG;
        return NULL;
    }
    const esp_http_client_config_t http = {
        .url = url,
        .method = method,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        // Request line plus a long Authorization header must fit.
        .buffer_size_tx = 1536,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (!client) {
        *error = AI_ERROR_MEMORY;
        return NULL;
    }
    esp_err_t err = content_type
                  ? esp_http_client_set_header(client, "Content-Type", content_type) : ESP_OK;
    if (err == ESP_OK && service.token[0]) {
        char authorization[USB_CONFIG_TOKEN_MAX + 8];
        snprintf(authorization, sizeof(authorization), "Bearer %s", service.token);
        err = esp_http_client_set_header(client, "Authorization", authorization);
        usb_config_clear(authorization, sizeof(authorization));
    }
    if (err == ESP_OK) err = esp_http_client_open(client, length);
    if (err != ESP_OK) {
        // Never log the URL: it is user configuration and may identify a tenant.
        ESP_LOGW(TAG, "%s: connect failed: %s", path, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        *error = err == ESP_ERR_NO_MEM ? AI_ERROR_MEMORY : AI_ERROR_NETWORK;
        return NULL;
    }
    return client;
}

static esp_http_client_handle_t open_post(service_t service, const char *path,
                                          const char *content_type, int length,
                                          ai_error_t *error)
{
    return open_request(service, HTTP_METHOD_POST, path, content_type, length, error);
}

static bool write_all(esp_http_client_handle_t client, const void *data, size_t length)
{
    return esp_http_client_write(client, data, (int)length) == (int)length;
}

// data must have CHUNK_HEADROOM writable bytes before it and two after it.
static bool write_chunk(esp_http_client_handle_t client, char *data, size_t length)
{
    char prefix[12];
    size_t prefix_length = ai_chunk_prefix(prefix, length);
    if (!prefix_length || prefix_length > CHUNK_HEADROOM) return false;
    // One TLS record per chunk instead of three small writes.
    memcpy(data - prefix_length, prefix, prefix_length);
    memcpy(data + length, "\r\n", 2);
    return write_all(client, data - prefix_length, prefix_length + length + 2);
}

// Returns the HTTP status, or -1 when the transport fails. The body is
// NUL-terminated in work; a body longer than the buffer is truncated.
static int read_response(esp_http_client_handle_t client, char *work, size_t *length)
{
    *length = 0;
    work[0] = '\0';
    if (esp_http_client_fetch_headers(client) < 0) return -1;
    int status = esp_http_client_get_status_code(client);
    while (*length + 1 < WORK_BUFFER && !atomic_load(&s_cancel)) {
        int count = esp_http_client_read(client, work + *length, WORK_BUFFER - 1 - *length);
        if (count < 0) return -1;
        if (count == 0) break;
        *length += (size_t)count;
    }
    work[*length] = '\0';
    return status;
}

static void log_service_error(const char *path, int status, const char *body, size_t length)
{
    char message[96];
    if (!ai_json_find_string(body, length, "error", "message", message, sizeof(message))) {
        message[0] = '\0';
    }
    ESP_LOGW(TAG, "%s: HTTP %d %s", path, status, message);
}

static void capture_task(void *arg)
{
    (void)arg;
    int16_t pcm[CAPTURE_CHUNK / 2];
    while (!atomic_load(&s_capture_stop)) {
        if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
            atomic_store(&s_capture_failed, true);
            break;
        }
        // Only this task writes, so free space cannot shrink before the send.
        // Dropping a whole chunk keeps the uploaded samples aligned.
        if (xStreamBufferSpacesAvailable(s_ring) < sizeof(pcm) ||
            xStreamBufferSend(s_ring, pcm, sizeof(pcm), 0) != sizeof(pcm)) {
            atomic_fetch_add(&s_capture_dropped, 1);
        }
    }
    xSemaphoreGive(s_capture_done);
    vTaskDelete(NULL);
}

static ai_error_t start_capture(void)
{
    atomic_store(&s_capture_stop, false);
    atomic_store(&s_capture_failed, false);
    atomic_store(&s_capture_dropped, 0);
    if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) return AI_ERROR_AUDIO;
    s_ring = xStreamBufferCreate(CAPTURE_RING_BYTES, 1);
    if (!s_ring) return AI_ERROR_MEMORY;
    if (xTaskCreate(capture_task, "ai_capture", CAPTURE_TASK_STACK, NULL, 6, NULL) != pdPASS) {
        vStreamBufferDelete(s_ring);
        s_ring = NULL;
        return AI_ERROR_MEMORY;
    }
    return AI_ERROR_NONE;
}

static void stop_capture(void)
{
    atomic_store(&s_capture_stop, true);
    // The read in progress completes within one 16 ms chunk.
    xSemaphoreTake(s_capture_done, portMAX_DELAY);
}

// Stream the recording until OK, the time limit, or cancel. Returns bytes sent,
// or -1 when the upload failed.
static int upload_recording(esp_http_client_handle_t client, char *work, ai_error_t *error)
{
    char *data = work + CHUNK_HEADROOM;
    int64_t started = esp_timer_get_time();
    int sent = 0;
    int shown = 0;
    bool draining = false;
    for (;;) {
        if (atomic_load(&s_cancel)) break;
        if (!draining) {
            int64_t elapsed = esp_timer_get_time() - started;
            if (atomic_load(&s_send) || elapsed >= RECORD_LIMIT_US ||
                atomic_load(&s_capture_failed)) {
                stop_capture();
                draining = true;
            } else if (elapsed / 1000000 != shown) {
                shown = (int)(elapsed / 1000000);
                publish(update_seconds, shown);
            }
        }
        size_t count = xStreamBufferReceive(s_ring, data, UPLOAD_CHUNK,
                                            draining ? 0 : pdMS_TO_TICKS(50));
        if (!count) {
            if (draining) break;
            continue;
        }
        if (!write_chunk(client, data, count)) {
            *error = AI_ERROR_NETWORK;
            if (!draining) stop_capture();
            return -1;
        }
        sent += (int)count;
    }
    if (!draining) stop_capture();
    if (atomic_load(&s_capture_failed)) {
        *error = AI_ERROR_AUDIO;
        return -1;
    }
    unsigned dropped = atomic_load(&s_capture_dropped);
    if (dropped) ESP_LOGW(TAG, "recording dropped %u chunks during network stalls", dropped);
    return sent;
}

static bool transcribe(const usb_config_values_t *config, char *work)
{
    const char *path = "/audio/transcriptions";
    size_t head = ai_transcription_head(work + CHUNK_HEADROOM, WORK_BUFFER - CHUNK_HEADROOM - 2,
                                        ai_model_or_default(config->asr_model,
                                                            AI_DEFAULT_ASR_MODEL));
    if (!head) {
        fail(AI_ERROR_CONFIG, 0);
        return false;
    }
    if (!app_audio_acquire(1000)) {
        fail(AI_ERROR_AUDIO_BUSY, 0);
        return false;
    }
    ai_error_t error = AI_ERROR_NONE;
    esp_http_client_handle_t client = open_post(
        speech_service(config), path, "multipart/form-data; boundary=" AI_MULTIPART_BOUNDARY,
        -1, &error);
    if (!client) {
        app_audio_release();
        fail(error, 0);
        return false;
    }
    bool ok = write_chunk(client, work + CHUNK_HEADROOM, head);
    if (ok) {
        uint8_t *header = (uint8_t *)work + CHUNK_HEADROOM;
        ai_wav_header(header, SAMPLE_RATE, 16, 1);
        ok = write_chunk(client, (char *)header, AI_WAV_HEADER_SIZE);
    }
    if (!ok) error = AI_ERROR_NETWORK;
    // Start the microphone only once the service is reachable, so the prompt
    // to speak is shown when speech can actually be captured.
    if (ok) {
        error = start_capture();
        ok = error == AI_ERROR_NONE;
    }
    int recorded = -1;
    if (ok) {
        set_stage(AI_STAGE_LISTENING);
        recorded = upload_recording(client, work, &error);
        vStreamBufferDelete(s_ring);
        s_ring = NULL;
    }
    app_audio_release();
    if (atomic_load(&s_cancel) || recorded < 0) {
        esp_http_client_cleanup(client);
        if (recorded < 0) fail(error, 0);
        return false;
    }
    if (recorded < RECORD_MIN_BYTES) {
        esp_http_client_cleanup(client);
        fail(AI_ERROR_NO_SPEECH, 0);
        return false;
    }
    set_stage(AI_STAGE_TRANSCRIBING);
    size_t tail_length = strlen(ai_transcription_tail());
    memcpy(work + CHUNK_HEADROOM, ai_transcription_tail(), tail_length);
    ok = write_chunk(client, work + CHUNK_HEADROOM, tail_length) &&
         write_all(client, "0\r\n\r\n", 5);
    size_t length = 0;
    int status = ok ? read_response(client, work, &length) : -1;
    esp_http_client_cleanup(client);
    if (atomic_load(&s_cancel)) return false;
    if (status < 0) {
        fail(AI_ERROR_NETWORK, 0);
        return false;
    }
    if (ai_http_error(status) != AI_ERROR_NONE) {
        log_service_error(path, status, work, length);
        fail(ai_http_error(status), status);
        return false;
    }
    char text[AI_CLIENT_USER_MAX];
    if (!ai_json_find_string(work, length, NULL, "text", text, sizeof(text))) {
        fail(AI_ERROR_RESPONSE, status);
        return false;
    }
    ai_text_clean(text);
    if (!text[0]) {
        fail(AI_ERROR_NO_SPEECH, 0);
        return false;
    }
    set_text(s_user, sizeof(s_user), text);
    return true;
}

static bool chat(const usb_config_values_t *config, char *work)
{
    const char *path = "/chat/completions";
    set_stage(AI_STAGE_THINKING);
    ai_turn_t turns[HISTORY_TURNS];
    for (size_t i = 0; i < s_history_count; i++) {
        turns[i] = (ai_turn_t){ s_history[i].user, s_history[i].reply };
    }
    size_t length = ai_chat_body(work, WORK_BUFFER,
                                 ai_model_or_default(config->chat_model, AI_DEFAULT_CHAT_MODEL),
                                 SYSTEM_PROMPT, turns, s_history_count, s_user);
    if (!length) {
        fail(AI_ERROR_MEMORY, 0);
        return false;
    }
    ai_error_t error = AI_ERROR_NONE;
    esp_http_client_handle_t client = open_post(chat_service(config), path, "application/json",
                                                (int)length, &error);
    if (!client) {
        fail(error, 0);
        return false;
    }
    int status = write_all(client, work, length) ? read_response(client, work, &length) : -1;
    esp_http_client_cleanup(client);
    if (atomic_load(&s_cancel)) return false;
    if (status < 0) {
        fail(AI_ERROR_NETWORK, 0);
        return false;
    }
    if (ai_http_error(status) != AI_ERROR_NONE) {
        log_service_error(path, status, work, length);
        fail(ai_http_error(status), status);
        return false;
    }
    char *reply = malloc(AI_CLIENT_REPLY_MAX);
    if (!reply) {
        fail(AI_ERROR_MEMORY, 0);
        return false;
    }
    bool found = ai_json_find_string(work, length, "message", "content",
                                     reply, AI_CLIENT_REPLY_MAX);
    if (found) ai_text_clean(reply);
    if (!found || !reply[0]) {
        free(reply);
        fail(AI_ERROR_RESPONSE, status);
        return false;
    }
    set_text(s_reply, sizeof(s_reply), reply);
    free(reply);

    // Keep the newest completed exchanges as context for the next question.
    if (s_history_count == HISTORY_TURNS) {
        memmove(&s_history[0], &s_history[1], sizeof(s_history[0]) * (HISTORY_TURNS - 1));
        s_history_count--;
    }
    memcpy(s_history[s_history_count].user, s_user, sizeof(s_user));
    memcpy(s_history[s_history_count].reply, s_reply, sizeof(s_reply));
    s_history_count++;
    return true;
}

// Writes whole frames, downmixing stereo to the mono codec path in place.
static esp_err_t play_frames(uint8_t *pcm, size_t length, uint16_t channels)
{
    if (channels == 2) {
        int16_t *samples = (int16_t *)pcm;
        size_t frames = length / 4;
        for (size_t i = 0; i < frames; i++) {
            samples[i] = (int16_t)(((int32_t)samples[2 * i] + samples[2 * i + 1]) / 2);
        }
        length = frames * 2;
    }
    return length ? bsp_audio_write(pcm, length) : ESP_OK;
}

static void speak(const usb_config_values_t *config, char *work)
{
    const char *path = "/audio/speech";
    uint8_t volume = app_settings_output_volume();
    if (volume == 0) {
        taskENTER_CRITICAL(&s_status_lock);
        s_status.speech_skipped = true;
        taskEXIT_CRITICAL(&s_status_lock);
        return;
    }
    set_stage(AI_STAGE_SPEAKING);
    size_t length = ai_speech_body(work, WORK_BUFFER,
                                   ai_model_or_default(config->tts_model, AI_DEFAULT_TTS_MODEL),
                                   ai_model_or_default(config->tts_voice, AI_DEFAULT_TTS_VOICE),
                                   s_reply);
    if (!length) {
        speech_failed(AI_ERROR_MEMORY, 0);
        return;
    }
    if (!app_audio_acquire(1000)) {
        speech_failed(AI_ERROR_AUDIO_BUSY, 0);
        return;
    }
    ai_error_t error = AI_ERROR_NONE;
    esp_http_client_handle_t client = open_post(speech_service(config), path, "application/json",
                                                (int)length, &error);
    if (!client) {
        app_audio_release();
        speech_failed(error, 0);
        return;
    }
    int status = -1;
    if (write_all(client, work, length) && esp_http_client_fetch_headers(client) >= 0) {
        status = esp_http_client_get_status_code(client);
    }
    if (status < 0 || ai_http_error(status) != AI_ERROR_NONE) {
        if (status >= 0) {
            read_response(client, work, &length);
            log_service_error(path, status, work, length);
        }
        esp_http_client_cleanup(client);
        app_audio_release();
        speech_failed(status < 0 ? AI_ERROR_NETWORK : ai_http_error(status), status);
        return;
    }

    ai_wav_parser_t parser;
    ai_wav_init(&parser);
    uint8_t carry[4];
    size_t carried = 0;
    bool started = false;
    error = AI_ERROR_NONE;
    while (!atomic_load(&s_stop_speaking) && !atomic_load(&s_cancel)) {
        int count = esp_http_client_read(client, work, 2048);
        if (count < 0) {
            error = AI_ERROR_NETWORK;
            break;
        }
        if (count == 0) break;
        const uint8_t *cursor = (const uint8_t *)work;
        size_t remaining = (size_t)count;
        ai_wav_result_t result = ai_wav_feed(&parser, &cursor, &remaining);
        if (result == AI_WAV_INVALID) {
            // Many services default to MP3; this firmware only plays PCM WAV.
            error = AI_ERROR_RESPONSE;
            break;
        }
        if (result == AI_WAV_NEED_MORE || !remaining) continue;
        if (!started) {
            if (bsp_audio_set_format(parser.rate, 16, 1) != ESP_OK) {
                error = AI_ERROR_AUDIO;
                break;
            }
            bsp_audio_set_volume(app_settings_output_volume());
            started = true;
        }
        // HTTP reads are not frame aligned; carry a partial frame forward.
        uint8_t *pcm = (uint8_t *)cursor;
        size_t frame = parser.channels * 2U;
        esp_err_t err = ESP_OK;
        if (carried) {
            size_t take = frame - carried < remaining ? frame - carried : remaining;
            memcpy(carry + carried, pcm, take);
            carried += take;
            pcm += take;
            remaining -= take;
            if (carried == frame) {
                err = play_frames(carry, frame, parser.channels);
                carried = 0;
            }
        }
        size_t whole = remaining - remaining % frame;
        if (err == ESP_OK) err = play_frames(pcm, whole, parser.channels);
        memcpy(carry + carried, pcm + whole, remaining - whole);
        carried += remaining - whole;
        if (err != ESP_OK) {
            error = AI_ERROR_AUDIO;
            break;
        }
    }
    esp_http_client_cleanup(client);
    app_audio_release();
    if (error != AI_ERROR_NONE && !atomic_load(&s_cancel)) speech_failed(error, status);
}

static void run_turn(usb_config_values_t *config, char *work)
{
    usb_config_status_t connection;
    usb_config_get_status(&connection);
    if (ai_conversation_guidance(&connection) != AI_GUIDANCE_READY ||
        !usb_config_copy_service(config) || !config->service_url[0]) {
        fail(AI_ERROR_NOT_READY, 0);
        return;
    }
    if (!transcribe(config, work) || atomic_load(&s_cancel)) return;
    if (!chat(config, work) || atomic_load(&s_cancel)) return;
    speak(config, work);
    set_stage(AI_STAGE_DONE);
}

static void turn_task(void *arg)
{
    (void)arg;
    usb_config_values_t *config = calloc(1, sizeof(*config));
    char *work = malloc(WORK_BUFFER);
    if (config && work) run_turn(config, work);
    else fail(AI_ERROR_MEMORY, 0);
    if (config) {
        // The copy holds the API key.
        usb_config_clear(config, sizeof(*config));
        free(config);
    }
    free(work);
    ESP_LOGI(TAG, "turn finished; free heap %u, minimum %u",
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    taskENTER_CRITICAL(&s_status_lock);
    atomic_store(&s_running, false);
    s_status.revision++;
    taskEXIT_CRITICAL(&s_status_lock);
    vTaskDelete(NULL);
}

esp_err_t ai_client_init(void)
{
    if (s_text_lock) return ESP_OK;
    s_text_lock = xSemaphoreCreateMutex();
    s_capture_done = xSemaphoreCreateBinary();
    if (!s_text_lock || !s_capture_done) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static void start_turn(void)
{
    if (!s_text_lock || atomic_exchange(&s_running, true)) return;
    atomic_store(&s_send, false);
    atomic_store(&s_cancel, false);
    atomic_store(&s_stop_speaking, false);
    xSemaphoreTake(s_text_lock, portMAX_DELAY);
    s_user[0] = s_reply[0] = '\0';
    xSemaphoreGive(s_text_lock);
    taskENTER_CRITICAL(&s_status_lock);
    s_status = (ai_client_status_t){
        .stage = AI_STAGE_CONNECTING,
        .revision = s_status.revision + 1,
    };
    taskEXIT_CRITICAL(&s_status_lock);
    // The task and its TLS stack exist only during a turn, keeping idle heap free.
    if (xTaskCreate(turn_task, "ai_turn", TURN_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        fail(AI_ERROR_MEMORY, 0);
        atomic_store(&s_running, false);
    }
}

void ai_client_command(ai_command_t command)
{
    switch (command) {
    case AI_COMMAND_START:
        start_turn();
        break;
    case AI_COMMAND_SEND:
        atomic_store(&s_send, true);
        break;
    case AI_COMMAND_STOP_SPEAKING:
        atomic_store(&s_stop_speaking, true);
        break;
    case AI_COMMAND_CANCEL:
        ai_client_cancel();
        break;
    default:
        break;
    }
}

void ai_client_cancel(void)
{
    if (!atomic_load(&s_running)) return;
    atomic_store(&s_stop_speaking, true);
    atomic_store(&s_cancel, true);
    taskENTER_CRITICAL(&s_status_lock);
    s_status.stage = AI_STAGE_IDLE;
    s_status.revision++;
    taskEXIT_CRITICAL(&s_status_lock);
}

bool ai_client_reset(void)
{
    if (!s_text_lock || atomic_load(&s_running)) return false;
    memset(s_history, 0, sizeof(s_history));
    s_history_count = 0;
    xSemaphoreTake(s_text_lock, portMAX_DELAY);
    s_user[0] = s_reply[0] = '\0';
    xSemaphoreGive(s_text_lock);
    taskENTER_CRITICAL(&s_status_lock);
    s_status = (ai_client_status_t){ .revision = s_status.revision + 1 };
    taskEXIT_CRITICAL(&s_status_lock);
    return true;
}

void ai_client_get_status(ai_client_status_t *status)
{
    taskENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    status->busy = atomic_load(&s_running) && !atomic_load(&s_fetching);
    taskEXIT_CRITICAL(&s_status_lock);
}

uint32_t ai_client_revision(void)
{
    taskENTER_CRITICAL(&s_status_lock);
    uint32_t revision = s_status.revision;
    taskEXIT_CRITICAL(&s_status_lock);
    return revision;
}

bool ai_client_read_text(void (*reader)(const char *user, const char *reply, void *arg),
                         void *arg)
{
    if (!s_text_lock || xSemaphoreTake(s_text_lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    reader(s_user, s_reply, arg);
    xSemaphoreGive(s_text_lock);
    return true;
}

#define MODELS_OUTPUT 6000 // Keeps one USB reply line below the page's 8 KB limit.

typedef struct {
    uint32_t id;
    ai_models_done_t done;
    char url[USB_CONFIG_URL_MAX + 1];
    char token[USB_CONFIG_TOKEN_MAX + 1];
} models_job_t;

static const char *models_error(ai_error_t error)
{
    switch (error) {
    case AI_ERROR_CONFIG: return "invalid_url";
    case AI_ERROR_AUTH: return "auth";
    case AI_ERROR_NOT_FOUND: return "not_found";
    case AI_ERROR_MEMORY: return "no_memory";
    case AI_ERROR_RESPONSE: return "invalid_response";
    case AI_ERROR_NETWORK: return "network";
    default: return "http";
    }
}

static void models_task(void *arg)
{
    models_job_t *job = arg;
    char *output = malloc(MODELS_OUTPUT);
    char *chunk = malloc(1024);
    ai_model_scanner_t *scanner = malloc(sizeof(*scanner));
    ai_error_t error = AI_ERROR_NONE;
    int status = 0;
    if (!output || !chunk || !scanner) {
        error = AI_ERROR_MEMORY;
    } else {
        ai_model_scanner_init(scanner, output, MODELS_OUTPUT);
        esp_http_client_handle_t client = open_request(
            (service_t){ job->url, job->token }, HTTP_METHOD_GET, "/models", NULL, 0, &error);
        if (client) {
            status = esp_http_client_fetch_headers(client) < 0
                   ? -1 : esp_http_client_get_status_code(client);
            if (status < 0) {
                error = AI_ERROR_NETWORK;
            } else if (ai_http_error(status) != AI_ERROR_NONE) {
                error = ai_http_error(status);
            } else {
                for (;;) {
                    int count = esp_http_client_read(client, chunk, 1024);
                    if (count < 0) error = AI_ERROR_NETWORK;
                    if (count <= 0) break;
                    ai_model_scanner_feed(scanner, chunk, (size_t)count);
                }
                if (error == AI_ERROR_NONE && !scanner->count) error = AI_ERROR_RESPONSE;
            }
            esp_http_client_cleanup(client);
        }
    }
    usb_config_clear(job->token, sizeof(job->token));
    if (error == AI_ERROR_NONE) {
        job->done(job->id, output, scanner->truncated, NULL, status);
    } else {
        ESP_LOGW(TAG, "/models: %s (HTTP %d)", models_error(error), status);
        job->done(job->id, NULL, false, models_error(error), status);
    }
    free(scanner);
    free(chunk);
    free(output);
    free(job);
    atomic_store(&s_fetching, false);
    atomic_store(&s_running, false);
    vTaskDelete(NULL);
}

bool ai_client_fetch_models(uint32_t id, const char *url, const char *token,
                            ai_models_done_t done)
{
    if (!url || !token || !done || atomic_exchange(&s_running, true)) return false;
    models_job_t *job = calloc(1, sizeof(*job));
    if (job) {
        job->id = id;
        job->done = done;
        ai_utf8_copy(job->url, sizeof(job->url), url);
        ai_utf8_copy(job->token, sizeof(job->token), token);
        atomic_store(&s_fetching, true);
        if (xTaskCreate(models_task, "ai_models", TURN_TASK_STACK, job, 4, NULL) == pdPASS) {
            return true;
        }
        usb_config_clear(job, sizeof(*job));
        free(job);
        atomic_store(&s_fetching, false);
    }
    atomic_store(&s_running, false);
    done(id, NULL, false, "no_memory", 0);
    return true;
}

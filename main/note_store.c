// main/note_store.c - NVS persistence for the pending voice-note ring.
//
// Layout in namespace "notes": "meta" holds note_queue_t, and "nNN" holds one
// record per slot (seq, epoch, then the UTF-8 text without its terminator).
// A record is written before the header that makes it visible, so losing power
// between the two leaves an unreferenced slot rather than a damaged note.
#include "note_store.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "note_store";

#define NOTE_NAMESPACE "notes"
#define NOTE_META_KEY "meta"
#define NOTE_RECORD_HEAD 8 // seq + epoch

static SemaphoreHandle_t s_lock;
static note_queue_t s_queue;
static bool s_ready;
static uint32_t s_revision;

static void slot_key(char key[8], uint8_t slot)
{
    snprintf(key, 8, "n%02u", (unsigned)slot);
}

static void put_u32(uint8_t *out, uint32_t value)
{
    for (int i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t get_u32(const uint8_t *in)
{
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 |
           (uint32_t)in[3] << 24;
}

esp_err_t note_store_init(void)
{
    if (s_ready) return ESP_OK;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    note_queue_t queue;
    note_queue_init(&queue);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NOTE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // First use: nothing is written until the first note is saved.
        err = ESP_OK;
    } else if (err == ESP_OK) {
        size_t size = sizeof(queue);
        err = nvs_get_blob(handle, NOTE_META_KEY, &queue, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            note_queue_init(&queue);
            err = ESP_OK;
        } else if (err == ESP_OK && (size != sizeof(queue) || !note_queue_valid(&queue))) {
            // Never reinterpret or wipe a header we do not understand: the
            // notes behind it may still be recoverable by a later firmware.
            err = ESP_ERR_INVALID_STATE;
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "load notes failed: %s", esp_err_to_name(err));
        return err;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_queue = queue;
    s_ready = true;
    s_revision++;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%u pending notes", (unsigned)queue.count);
    return ESP_OK;
}

esp_err_t note_store_add(const char *text, uint32_t epoch)
{
    if (!text || !text[0]) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    size_t length = strnlen(text, NOTE_TEXT_MAX - 1);
    // Cut a long note on a UTF-8 boundary so the stored text stays valid.
    if (text[length]) {
        while (length && ((unsigned char)text[length] & 0xC0) == 0x80) length--;
    }
    if (!length) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (!s_ready) err = ESP_ERR_INVALID_STATE;
    else if (note_queue_full(&s_queue)) err = ESP_ERR_NO_MEM;

    nvs_handle_t handle = 0;
    bool opened = false;
    if (err == ESP_OK) {
        err = nvs_open(NOTE_NAMESPACE, NVS_READWRITE, &handle);
        opened = err == ESP_OK;
    }
    note_queue_t next = s_queue;
    if (err == ESP_OK) {
        uint8_t record[NOTE_RECORD_HEAD + NOTE_TEXT_MAX];
        put_u32(record, next.next_seq);
        put_u32(record + 4, epoch);
        memcpy(record + NOTE_RECORD_HEAD, text, length);
        char key[8];
        slot_key(key, note_queue_tail(&next));
        err = nvs_set_blob(handle, key, record, NOTE_RECORD_HEAD + length);
    }
    if (err == ESP_OK) {
        note_queue_commit_push(&next);
        err = nvs_set_blob(handle, NOTE_META_KEY, &next, sizeof(next));
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    if (opened) nvs_close(handle);
    if (err == ESP_OK) {
        s_queue = next;
        s_revision++;
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK && err != ESP_ERR_NO_MEM) {
        ESP_LOGE(TAG, "save note failed: %s", esp_err_to_name(err));
    }
    return err;
}

size_t note_store_count(void)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = s_ready ? s_queue.count : 0;
    xSemaphoreGive(s_lock);
    return count;
}

bool note_store_newest(size_t index, note_t *out)
{
    if (!out || !s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t slot;
    bool ok = s_ready && index <= UINT8_MAX &&
              note_queue_newest_slot(&s_queue, (unsigned)index, &slot);
    nvs_handle_t handle;
    if (ok) ok = nvs_open(NOTE_NAMESPACE, NVS_READONLY, &handle) == ESP_OK;
    if (ok) {
        uint8_t record[NOTE_RECORD_HEAD + NOTE_TEXT_MAX];
        size_t size = sizeof(record);
        char key[8];
        slot_key(key, slot);
        ok = nvs_get_blob(handle, key, record, &size) == ESP_OK &&
             size > NOTE_RECORD_HEAD && size < sizeof(record);
        nvs_close(handle);
        if (ok) {
            out->seq = get_u32(record);
            out->epoch = get_u32(record + 4);
            memcpy(out->text, record + NOTE_RECORD_HEAD, size - NOTE_RECORD_HEAD);
            out->text[size - NOTE_RECORD_HEAD] = '\0';
        }
    }
    xSemaphoreGive(s_lock);
    return ok;
}

uint32_t note_store_revision(void)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t revision = s_revision;
    xSemaphoreGive(s_lock);
    return revision;
}

bool note_store_ready(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ready = s_ready;
    xSemaphoreGive(s_lock);
    return ready;
}

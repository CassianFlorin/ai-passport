// main/note_store.h - Voice notes kept in NVS until a sync delivers them.
//
// Notes survive reboots and are never overwritten: a full store refuses new
// notes instead of dropping ones that were not delivered yet. All calls may
// touch Flash, so call them from worker tasks, never from button callbacks.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "note_queue.h"

#define NOTE_STORE_CAPACITY NOTE_QUEUE_CAPACITY
#define NOTE_TEXT_MAX 256 // bytes including the terminator, as AI_CLIENT_USER_MAX

typedef struct {
    uint32_t seq;
    uint32_t epoch; // UTC seconds when recorded, or 0 if the clock was not set
    char text[NOTE_TEXT_MAX];
} note_t;

// Load the queue header once at startup, after NVS is initialized.
esp_err_t note_store_init(void);

// ESP_ERR_NO_MEM when full, ESP_ERR_INVALID_ARG for empty text,
// ESP_ERR_INVALID_STATE when the store did not load.
esp_err_t note_store_add(const char *text, uint32_t epoch);

size_t note_store_count(void);
// index 0 is the newest note.
bool note_store_newest(size_t index, note_t *out);
// Changes whenever the pending set changes, for cheap UI refresh checks.
uint32_t note_store_revision(void);
// False when init failed; the UI reports a storage error instead of a count.
bool note_store_ready(void);

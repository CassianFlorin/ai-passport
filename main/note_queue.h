// main/note_queue.h - Index arithmetic for the pending voice-note ring.
//
// Pure logic so host tests cover every wrap-around and corruption case; the
// NVS layer in note_store.c only persists this header and one record per slot.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NOTE_QUEUE_CAPACITY 20
#define NOTE_QUEUE_VERSION 1

typedef struct {
    uint8_t version;
    uint8_t head;      // slot of the oldest pending note
    uint8_t count;     // pending notes, oldest first from head
    uint8_t reserved;
    uint32_t next_seq; // given to the next note and never reused, so a future
                       // sync can acknowledge notes by number
} note_queue_t;

static inline void note_queue_init(note_queue_t *q)
{
    *q = (note_queue_t){ .version = NOTE_QUEUE_VERSION, .next_seq = 1 };
}

// Reject a header that could index outside the ring or reuse a sequence number.
static inline bool note_queue_valid(const note_queue_t *q)
{
    return q->version == NOTE_QUEUE_VERSION && q->head < NOTE_QUEUE_CAPACITY &&
           q->count <= NOTE_QUEUE_CAPACITY && q->reserved == 0 &&
           q->next_seq > q->count;
}

static inline bool note_queue_full(const note_queue_t *q)
{
    return q->count >= NOTE_QUEUE_CAPACITY;
}

// Slot the next note is written to; meaningful only while not full.
static inline uint8_t note_queue_tail(const note_queue_t *q)
{
    return (uint8_t)((q->head + q->count) % NOTE_QUEUE_CAPACITY);
}

// Call only after the record at note_queue_tail() is safely stored.
static inline void note_queue_commit_push(note_queue_t *q)
{
    q->count++;
    q->next_seq++;
}

// index 0 is the newest note. Returns false past the last pending note.
static inline bool note_queue_newest_slot(const note_queue_t *q, unsigned index,
                                          uint8_t *slot)
{
    if (index >= q->count) return false;
    *slot = (uint8_t)((q->head + q->count - 1u - index) % NOTE_QUEUE_CAPACITY);
    return true;
}

// Drop the oldest note once a sync has delivered it.
static inline bool note_queue_pop(note_queue_t *q)
{
    if (!q->count) return false;
    q->head = (uint8_t)((q->head + 1u) % NOTE_QUEUE_CAPACITY);
    q->count--;
    return true;
}

// Execute the voice-note ring and its NVS store against an in-memory,
// fault-injected NVS, including reboots between and during writes.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/note_store.c"

int test_mutex_depth;
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test error"; }

#define MAX_ENTRIES 32
typedef struct {
    char key[16];
    uint8_t data[300];
    size_t length;
    bool used;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static bool namespace_exists;
static unsigned sets, commits, opens, closes;
static int fail_set_at; // 1-based index of the set_blob call that fails, 0 = none
static esp_err_t open_error, commit_error;

static entry_t *find(const char *key)
{
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (entries[i].used && strcmp(entries[i].key, key) == 0) return &entries[i];
    }
    return NULL;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "notes") == 0);
    if (open_error) return open_error;
    if (!namespace_exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    if (mode == NVS_READWRITE) namespace_exists = true;
    ++opens;
    *handle = 7;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length)
{
    assert(handle == 7);
    entry_t *entry = find(key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < entry->length) return ESP_ERR_INVALID_SIZE;
    memcpy(out, entry->data, entry->length);
    *length = entry->length;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length)
{
    assert(handle == 7 && length <= sizeof(entries[0].data));
    ++sets;
    if (fail_set_at && (int)sets == fail_set_at) return ESP_FAIL;
    entry_t *entry = find(key);
    for (int i = 0; !entry && i < MAX_ENTRIES; ++i) {
        if (!entries[i].used) {
            entry = &entries[i];
            entry->used = true;
            snprintf(entry->key, sizeof(entry->key), "%s", key);
        }
    }
    assert(entry);
    memcpy(entry->data, data, length);
    entry->length = length;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 7);
    ++commits;
    return commit_error;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 7);
    ++closes;
}

// Forget everything held in RAM, as a reboot does; Flash entries remain.
static void reboot(void)
{
    s_ready = false;
    note_queue_init(&s_queue);
    sets = commits = 0;
    fail_set_at = 0;
    open_error = commit_error = ESP_OK;
}

static void wipe(void)
{
    memset(entries, 0, sizeof(entries));
    namespace_exists = false;
    reboot();
}

static void assert_balanced(void)
{
    assert(test_mutex_depth == 0);
    assert(opens == closes);
}

static void check_queue(void)
{
    note_queue_t q;
    note_queue_init(&q);
    assert(note_queue_valid(&q) && q.count == 0 && q.next_seq == 1);
    uint8_t slot;
    assert(!note_queue_newest_slot(&q, 0, &slot));
    assert(!note_queue_pop(&q));

    for (int i = 0; i < NOTE_QUEUE_CAPACITY; ++i) {
        assert(!note_queue_full(&q) && note_queue_tail(&q) == i);
        note_queue_commit_push(&q);
    }
    assert(note_queue_full(&q) && note_queue_valid(&q));
    assert(note_queue_newest_slot(&q, 0, &slot) && slot == NOTE_QUEUE_CAPACITY - 1);
    assert(note_queue_newest_slot(&q, NOTE_QUEUE_CAPACITY - 1, &slot) && slot == 0);
    assert(!note_queue_newest_slot(&q, NOTE_QUEUE_CAPACITY, &slot));

    // Delivering the two oldest frees their slots; the tail wraps to slot 0.
    assert(note_queue_pop(&q) && note_queue_pop(&q));
    assert(q.head == 2 && note_queue_tail(&q) == 0);
    note_queue_commit_push(&q);
    assert(note_queue_newest_slot(&q, 0, &slot) && slot == 0);
    assert(note_queue_newest_slot(&q, 1, &slot) && slot == NOTE_QUEUE_CAPACITY - 1);
    assert(q.next_seq == NOTE_QUEUE_CAPACITY + 2);

    // Headers that could index outside the ring or reuse a number are rejected.
    note_queue_t bad = q; bad.version = 2; assert(!note_queue_valid(&bad));
    bad = q; bad.head = NOTE_QUEUE_CAPACITY; assert(!note_queue_valid(&bad));
    bad = q; bad.count = NOTE_QUEUE_CAPACITY + 1; assert(!note_queue_valid(&bad));
    bad = q; bad.next_seq = bad.count; assert(!note_queue_valid(&bad));
    bad = q; bad.reserved = 1; assert(!note_queue_valid(&bad));
}

static void check_persistence(void)
{
    wipe();
    assert(note_store_init() == ESP_OK && note_store_ready());
    // First use writes nothing until a note is saved.
    assert(note_store_count() == 0 && sets == 0 && !namespace_exists);
    uint32_t revision = note_store_revision();

    assert(note_store_add("买牛奶", 1764000000u) == ESP_OK);
    assert(note_store_add("给打印机换纸", 0) == ESP_OK);
    assert(note_store_count() == 2 && note_store_revision() != revision);
    note_t note;
    assert(note_store_newest(0, &note) && note.seq == 2 && note.epoch == 0 &&
           strcmp(note.text, "给打印机换纸") == 0);
    assert(note_store_newest(1, &note) && note.seq == 1 && note.epoch == 1764000000u &&
           strcmp(note.text, "买牛奶") == 0);
    assert(!note_store_newest(2, &note));
    assert_balanced();

    // Notes and numbering survive a reboot.
    reboot();
    assert(note_store_init() == ESP_OK && note_store_count() == 2);
    assert(note_store_add("第三条", 5) == ESP_OK);
    assert(note_store_newest(0, &note) && note.seq == 3);
    assert(note_store_newest(2, &note) && strcmp(note.text, "买牛奶") == 0);
    assert_balanced();
}

static void check_full(void)
{
    wipe();
    assert(note_store_init() == ESP_OK);
    char text[16];
    for (int i = 0; i < NOTE_STORE_CAPACITY; ++i) {
        snprintf(text, sizeof(text), "note %d", i);
        assert(note_store_add(text, 0) == ESP_OK);
    }
    // A full store refuses the new note and leaves Flash untouched.
    unsigned before = sets;
    assert(note_store_add("one more", 0) == ESP_ERR_NO_MEM);
    assert(sets == before && note_store_count() == NOTE_STORE_CAPACITY);
    note_t note;
    assert(note_store_newest(NOTE_STORE_CAPACITY - 1, &note) && strcmp(note.text, "note 0") == 0);
    assert_balanced();
}

static void check_faults(void)
{
    note_t note;
    for (int failing = 1; failing <= 2; ++failing) {
        // 1 = the record, 2 = the header that would publish it.
        wipe();
        assert(note_store_init() == ESP_OK && note_store_add("kept", 0) == ESP_OK);
        uint32_t revision = note_store_revision();
        fail_set_at = (int)sets + failing;
        assert(note_store_add("lost", 0) == ESP_FAIL);
        assert(note_store_count() == 1 && note_store_revision() == revision);
        assert(note_store_newest(0, &note) && strcmp(note.text, "kept") == 0);
        assert_balanced();

        // After power loss at that point only the published note exists, and
        // the next note takes the unpublished slot and number.
        reboot();
        assert(note_store_init() == ESP_OK && note_store_count() == 1);
        assert(note_store_add("next", 0) == ESP_OK);
        assert(note_store_newest(0, &note) && note.seq == 2 && strcmp(note.text, "next") == 0);
        assert(note_store_newest(1, &note) && strcmp(note.text, "kept") == 0);
        assert_balanced();
    }

    wipe();
    assert(note_store_init() == ESP_OK);
    open_error = ESP_FAIL;
    assert(note_store_add("x", 0) == ESP_FAIL && note_store_count() == 0);
    assert(!note_store_newest(0, &note));
    open_error = ESP_OK;
    commit_error = ESP_FAIL;
    assert(note_store_add("x", 0) == ESP_FAIL && note_store_count() == 0);
    assert_balanced();

    // Loading fails on a read error, and nothing can be added afterwards.
    wipe();
    open_error = ESP_FAIL;
    assert(note_store_init() == ESP_FAIL && !note_store_ready());
    open_error = ESP_OK;
    assert(note_store_add("x", 0) == ESP_ERR_INVALID_STATE && note_store_count() == 0);
    assert_balanced();
}

static void check_corrupt_header(void)
{
    // A header of the wrong size, or one that fails validation, is kept as is.
    const uint8_t short_header[3] = { 1, 0, 0 };
    note_queue_t invalid;
    note_queue_init(&invalid);
    invalid.head = NOTE_QUEUE_CAPACITY;
    const struct { const void *data; size_t length; } cases[] = {
        { short_header, sizeof(short_header) },
        { &invalid, sizeof(invalid) },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        wipe();
        namespace_exists = true;
        nvs_handle_t handle;
        assert(nvs_open("notes", NVS_READWRITE, &handle) == ESP_OK);
        assert(nvs_set_blob(handle, "meta", cases[i].data, cases[i].length) == ESP_OK);
        nvs_close(handle);
        reboot();
        assert(note_store_init() == ESP_ERR_INVALID_STATE && !note_store_ready());
        assert(note_store_add("x", 0) == ESP_ERR_INVALID_STATE && sets == 0);
        assert(find("meta")->length == cases[i].length);
        assert_balanced();
    }
}

static void check_text_limits(void)
{
    wipe();
    assert(note_store_init() == ESP_OK);
    assert(note_store_add("", 0) == ESP_ERR_INVALID_ARG);
    assert(note_store_add(NULL, 0) == ESP_ERR_INVALID_ARG);

    // 1 + 100 * 3 bytes: byte 255 is inside a character, so the cut backs off
    // to the start of that character and the stored text stays valid UTF-8.
    char text[1 + 100 * 3 + 1] = "a";
    for (int i = 0; i < 100; ++i) memcpy(text + 1 + i * 3, "你", 3);
    text[sizeof(text) - 1] = '\0';
    assert(note_store_add(text, 0) == ESP_OK);
    note_t note;
    assert(note_store_newest(0, &note));
    assert(strlen(note.text) == 253 && strncmp(note.text, text, 253) == 0);

    // Exactly 255 bytes is kept whole.
    char exact[256];
    memset(exact, 'b', 255);
    exact[255] = '\0';
    assert(note_store_add(exact, 0) == ESP_OK);
    assert(note_store_newest(0, &note) && strcmp(note.text, exact) == 0);
    assert_balanced();
}

static void check_wraparound_keys(void)
{
    // Resume from a header whose tail is the last slot: the next notes are
    // written to n19 and then n00.
    wipe();
    note_queue_t q;
    note_queue_init(&q);
    q.head = NOTE_QUEUE_CAPACITY - 1;
    q.next_seq = 50;
    namespace_exists = true;
    nvs_handle_t handle;
    assert(nvs_open("notes", NVS_READWRITE, &handle) == ESP_OK);
    assert(nvs_set_blob(handle, "meta", &q, sizeof(q)) == ESP_OK);
    nvs_close(handle);
    reboot();
    assert(note_store_init() == ESP_OK);
    assert(note_store_add("first", 0) == ESP_OK && find("n19"));
    assert(note_store_add("second", 0) == ESP_OK && find("n00"));
    note_t note;
    assert(note_store_newest(0, &note) && note.seq == 51 && strcmp(note.text, "second") == 0);
    assert(note_store_newest(1, &note) && note.seq == 50 && strcmp(note.text, "first") == 0);
    assert_balanced();
}

int main(void)
{
    check_queue();
    check_persistence();
    check_full();
    check_faults();
    check_corrupt_header();
    check_text_limits();
    check_wraparound_keys();
    puts("voice note queue and store: PASS");
    return 0;
}

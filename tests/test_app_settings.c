// Execute the production settings store against transactional, fault-injected NVS.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/app_settings.c"

static bool namespace_exists, audio_exists, legacy_exists;
static uint16_t stored_audio, pending_audio;
static uint8_t legacy_sound;
static bool pending;
static unsigned commits, writes, closes;
static esp_err_t prepare_error, open_error, read_error, write_error, commit_error;

void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test error"; }
esp_err_t demo_radio_nvs_prepare(void) { return prepare_error; }

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "app") == 0);
    if (open_error) return open_error;
    if (!namespace_exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1;
    pending = false;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    assert(handle == 1 && strcmp(key, "sound") == 0);
    if (read_error) return read_error;
    if (!legacy_exists) return ESP_ERR_NVS_NOT_FOUND;
    *value = legacy_sound;
    return ESP_OK;
}

esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    assert(handle == 1 && strcmp(key, "audio") == 0);
    if (read_error) return read_error;
    if (!audio_exists) return ESP_ERR_NVS_NOT_FOUND;
    *value = stored_audio;
    return ESP_OK;
}

esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value)
{
    assert(handle == 1 && strcmp(key, "audio") == 0);
    ++writes;
    if (write_error) return write_error;
    pending_audio = value;
    pending = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1 && pending);
    ++commits;
    if (commit_error) return commit_error;
    stored_audio = pending_audio;
    namespace_exists = audio_exists = true;
    pending = false;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 1);
    ++closes;
    pending = false;
}

static void reboot(void)
{
    s_loaded = false;
    atomic_store(&s_audio, AUDIO_DEFAULT);
}

int main(void)
{
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_output_volume() == 80 && writes == 0);
    assert(app_settings_set_volume(65) == ESP_OK);
    assert(app_settings_volume() == 65 && app_settings_output_volume() == 65);
    assert(commits == 1 && closes == 1);
    assert(app_settings_set_volume(65) == ESP_OK && commits == 1);

    // Mute and reboot must retain the previous nonzero level for OK-to-restore.
    assert(app_settings_set_sound_enabled(false) == ESP_OK);
    reboot();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_volume() == 65 && app_settings_output_volume() == 0);
    assert(app_settings_set_sound_enabled(true) == ESP_OK);
    assert(app_settings_output_volume() == 65);
    assert(app_settings_set_volume(0) == ESP_OK);
    assert(app_settings_output_volume() == 0 && app_settings_volume() == 65);
    assert(app_settings_step_volume(true) == ESP_OK);
    assert(app_settings_output_volume() == 5);

    // Check saturation in both directions and no Flash writes at a limit.
    for (int i = 0; i < 25; ++i) assert(app_settings_step_volume(true) == ESP_OK);
    assert(app_settings_output_volume() == 100);
    unsigned before = writes;
    assert(app_settings_step_volume(true) == ESP_OK && writes == before);
    for (int i = 0; i < 25; ++i) assert(app_settings_step_volume(false) == ESP_OK);
    assert(app_settings_output_volume() == 0 && app_settings_volume() == 5);
    before = writes;
    assert(app_settings_step_volume(false) == ESP_OK && writes == before);
    assert(app_settings_set_volume(101) == ESP_ERR_INVALID_ARG && writes == before);
    assert(app_settings_set_volume(255) == ESP_ERR_INVALID_ARG && writes == before);

    assert(app_settings_set_volume(60) == ESP_OK);
    uint16_t saved = stored_audio;
    write_error = ESP_FAIL;
    assert(app_settings_set_volume(90) == ESP_FAIL);
    write_error = ESP_OK;
    assert(app_settings_output_volume() == 60 && stored_audio == saved && !pending);
    commit_error = ESP_FAIL;
    assert(app_settings_set_sound_enabled(false) == ESP_FAIL);
    commit_error = ESP_OK;
    assert(app_settings_output_volume() == 60 && stored_audio == saved && !pending);
    open_error = ESP_FAIL;
    assert(app_settings_set_volume(40) == ESP_FAIL);
    open_error = ESP_OK;
    assert(app_settings_output_volume() == 60);
    reboot();
    assert(app_settings_init() == ESP_OK && app_settings_output_volume() == 60);

    // Migrate legacy sound-only state lazily, without writing during startup.
    for (legacy_sound = 0; legacy_sound <= 1; ++legacy_sound) {
        namespace_exists = legacy_exists = true;
        audio_exists = false;
        before = writes;
        reboot();
        assert(app_settings_init() == ESP_OK);
        assert(app_settings_volume() == 80);
        assert(app_settings_sound_enabled() == (legacy_sound != 0));
        assert(writes == before);
        assert(app_settings_set_volume(35) == ESP_OK);
        reboot();
        assert(app_settings_init() == ESP_OK && app_settings_output_volume() == 35);
    }

    // Missing keys, malformed persisted values and read failures are distinct.
    audio_exists = legacy_exists = false;
    reboot();
    assert(app_settings_init() == ESP_OK && app_settings_output_volume() == 80);
    static const uint16_t malformed[] = {0, 101, 0x100, 0x400 | 50};
    for (unsigned i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
        audio_exists = true;
        stored_audio = malformed[i];
        reboot();
        assert(app_settings_init() == ESP_ERR_INVALID_STATE && !s_loaded);
        assert(app_settings_output_volume() == 80);
    }
    stored_audio = AUDIO_ENABLED_BIT | 45;
    reboot();
    read_error = ESP_FAIL;
    assert(app_settings_init() == ESP_FAIL && !s_loaded);
    read_error = ESP_OK;
    prepare_error = ESP_FAIL;
    assert(app_settings_set_volume(30) == ESP_FAIL);
    prepare_error = ESP_OK;
    assert(app_settings_init() == ESP_OK && app_settings_output_volume() == 45);
    puts("audio settings, migration, boundaries and persistence failures: PASS");
    return 0;
}

#include "demo_stubs/demo_runtime.c"
#include "../main/demo_audio.c"
#include "../main/app_audio.c"

static void cancel_recording(void) { s_cancel = true; }

int main(void) {
    lv_obj_t status;
    s_status = &status;
    test_create_fails = true;
    assert(demo_audio_start() == ESP_ERR_NO_MEM);
    assert(!s_task && !s_stopped);
    test_create_fails = false;
    assert(demo_audio_start() == ESP_OK);
    TaskHandle_t first = s_task;
    assert(demo_audio_stop() == ESP_ERR_TIMEOUT);
    assert(s_task == first && !first->deleted && s_cancel);
    test_run_worker(first); // Ack arrives after stop's timeout.
    assert(s_task == first && !first->deleted && s_stopped->available);
    assert(demo_audio_stop() == ESP_OK); // A retry still has a live task handle.
    assert(first->deleted && !s_task && !s_stopped);
    assert(demo_audio_start() == ESP_OK);
    assert(s_task != first && !s_task->deleted);
    test_auto_ack = true;
    assert(demo_audio_stop() == ESP_OK);
    assert(demo_audio_stop() == ESP_OK);
    assert(test_task_deletes == 2);

    // First-chunk and partial recording failures must not play or report done.
    for (unsigned fail_at = 1; fail_at <= 2; fail_at++) {
        s_cancel = false;
        test_read_calls = test_write_calls = 0;
        test_read_fail_at = fail_at;
        record_and_play();
        assert(test_read_calls == fail_at && test_write_calls == 0);
        assert(strcmp(test_status, "recording failed") == 0);
    }
    test_read_calls = test_write_calls = 0;
    test_read_fail_at = 0;
    test_write_fail_at = 1;
    record_and_play();
    assert(strcmp(test_status, "playback failed") == 0);
    test_read_calls = test_write_calls = test_write_fail_at = 0;
    record_and_play();
    assert(strncmp(test_status, "done.", 5) == 0);
    assert(test_read_calls == 94 && test_write_calls == 94);
    test_read_calls = test_write_calls = 0;
    test_read_hook = cancel_recording;
    record_and_play();
    assert(test_read_calls == 1 && test_write_calls == 0);
    assert(strncmp(test_status, "done.", 5) != 0);

    // Every playback entry must honor effective volume, including mute.
    test_read_hook = NULL;
    s_cancel = false;
    const uint8_t volumes[] = {0, 5, 65, 100};
    for (unsigned i = 0; i < sizeof(volumes) / sizeof(volumes[0]); ++i) {
        test_audio_volume = volumes[i];
        test_applied_volume = 255;
        test_write_calls = test_write_fail_at = 0;
        play_tone();
        assert(test_applied_volume == volumes[i] && test_write_calls > 0);
        test_applied_volume = 255;
        record_and_play();
        assert(test_applied_volume == volumes[i]);
    }

    // A muted preview must not start the codec or send PCM. Preview failures
    // propagate so the dashboard cannot report a successful sound test.
    test_auto_ack = false; // The preview lock has no worker to acknowledge.
    test_audio_volume = 0;
    test_write_calls = 0;
    test_format_result = ESP_FAIL;
    assert(app_audio_preview() == ESP_OK && test_write_calls == 0);
    test_audio_volume = 35;
    // Without the startup lock, and while a conversation holds it, preview is silent.
    assert(app_audio_preview() == ESP_ERR_TIMEOUT && test_write_calls == 0);
    assert(app_audio_init() == ESP_OK && app_audio_init() == ESP_OK);
    assert(app_audio_acquire(0));
    assert(app_audio_preview() == ESP_ERR_TIMEOUT && test_write_calls == 0);
    app_audio_release();
    assert(app_audio_preview() == ESP_FAIL && test_write_calls == 0);
    test_format_result = ESP_OK;
    test_write_fail_at = 1;
    assert(app_audio_preview() == ESP_FAIL && test_write_calls == 1);
    test_write_calls = test_write_fail_at = 0;
    assert(app_audio_preview() == ESP_OK);
    assert(test_applied_volume == 35 && test_write_calls == 20);
    assert(app_audio_acquire(0)); // Every preview path released the lock.
    app_audio_release();
    puts("audio worker lifecycle and recording fault tests: PASS");
    return 0;
}

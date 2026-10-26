#include "app_audio.h"

#include "app_settings.h"
#include "bsp_audio.h"

#include <stdint.h>

esp_err_t app_audio_preview(void)
{
    uint8_t volume = app_settings_output_volume();
    if (volume == 0) return ESP_OK;

    esp_err_t err = bsp_audio_set_format(16000, 16, 1);
    if (err != ESP_OK) return err;
    bsp_audio_set_volume(volume);

    // A quiet 500 Hz triangle with a fade at each end avoids a sharp PCM edge.
    // Use a 256-byte stack buffer and serialize preview/save on the input worker.
    int16_t pcm[128];
    const unsigned samples = 2560; // 160 ms; do not loop or record the microphone.
    for (unsigned offset = 0; offset < samples; offset += 128) {
        for (unsigned i = 0; i < 128; ++i) {
            unsigned sample = offset + i;
            unsigned phase = sample % 32;
            int wave = phase < 16 ? (int)phase - 8 : 24 - (int)phase;
            unsigned envelope = sample < 256 ? sample
                                : samples - 1 - sample < 256 ? samples - 1 - sample
                                : 256;
            pcm[i] = (int16_t)(wave * 400 * (int)envelope / 256);
        }
        err = bsp_audio_write(pcm, sizeof(pcm));
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

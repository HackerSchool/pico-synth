#include "i2s_init.hpp"
// #include "pico/audio.h"
// #include "pico/audio_i2s.h"
#include "pico/stdlib.h"

const uint32_t PIN_DCDC_PSM_CTRL = 23;

audio_buffer_pool_t *ap;
bool decode_flg = false;
static constexpr int32_t DAC_ZERO = 1;

#define audio_pio __CONCAT(pio, PICO_AUDIO_I2S_PIO)
#define SAMPLES_PER_BUFFER 1156 // Samples / channel

static audio_format_t audio_format = {.sample_freq = 44100,
                                      .pcm_format = AUDIO_PCM_FORMAT_S32,
                                      .channel_count = AUDIO_CHANNEL_STEREO};

static audio_buffer_format_t producer_format = {.format = &audio_format,
                                                .sample_stride = 8};

static audio_i2s_config_t i2s_config = {.data_pin = PICO_AUDIO_I2S_DATA_PIN,
                                        .clock_pin_base =
                                            PICO_AUDIO_I2S_CLOCK_PIN_BASE,
                                        .dma_channel0 = 0,
                                        .dma_channel1 = 1,
                                        .pio_sm = 0};

static inline uint32_t _millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

void i2s_audio_deinit() {
    decode_flg = false;

    audio_i2s_set_enabled(false);
    audio_i2s_end();

    audio_buffer_t *ab;
    ab = take_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = take_audio_buffer(ap, false);
    }
    ab = get_free_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_free_audio_buffer(ap, false);
    }
    ab = get_full_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_full_audio_buffer(ap, false);
    }
    free(ap);
    ap = nullptr;
}

audio_buffer_pool_t *i2s_audio_init(uint32_t sample_freq) {
    audio_format.sample_freq = sample_freq;

    audio_buffer_pool_t *producer_pool =
        audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);
    ap = producer_pool;

    bool __unused ok;
    const audio_format_t *output_format;

    output_format = audio_i2s_setup(&audio_format, &audio_format, &i2s_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    { // initial buffer data
        audio_buffer_t *ab = take_audio_buffer(producer_pool, true);
        int32_t *samples = (int32_t *)ab->buffer->bytes;
        for (uint i = 0; i < ab->max_sample_count; i++) {
            samples[i * 2 + 0] = DAC_ZERO;
            samples[i * 2 + 1] = DAC_ZERO;
        }
        ab->sample_count = ab->max_sample_count;
        give_audio_buffer(producer_pool, ab);
    }
    audio_i2s_set_enabled(true);

    decode_flg = true;
    return producer_pool;
}

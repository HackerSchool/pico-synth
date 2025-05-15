#ifndef I2S_AUDIO_HPP
#define I2S_AUDIO_HPP

#include "pico/audio.h"
#include "pico/audio_i2s.h"
#include "pico/stdlib.h"
#include "config.hpp"

extern audio_buffer_pool_t *ap;
extern bool decode_flg;
extern const uint32_t PIN_DCDC_PSM_CTRL;

// Initializes I2S audio and returns a pointer to the buffer pool
audio_buffer_pool_t *i2s_audio_init(uint32_t sample_freq);

// Deinitializes I2S audio, freeing resources
void i2s_audio_deinit();

#endif // I2S_AUDIO_HPP

#include "KarplusStrong.hpp"
#include "MidiHandler.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace {
constexpr uint16_t kQ15Max = 32767;
constexpr uint16_t kReleaseDecayQ15 = 32112; // ~0.98
constexpr int kSilenceThreshold = 24;
constexpr uint16_t kDispersionMaxQ15 = 19661; // ~0.60
constexpr uint16_t kBodyMixMaxQ15 = 16384;    // 0.5
constexpr uint16_t kBodyFeedbackBaseQ15 = 18022; // ~0.55
constexpr uint16_t kBodyFeedbackRangeQ15 = 9830; // +0.30

inline int16_t clamp_i16(int32_t value) {
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(value);
}

inline int abs_int(int value) {
    return value < 0 ? -value : value;
}

inline uint16_t filter_gain_to_q15(uint8_t value) {
    const int mapped = 2048 + static_cast<int>(value) * 220;
    return static_cast<uint16_t>(mapped > kQ15Max ? kQ15Max : mapped);
}

inline uint16_t decay_to_q15(uint8_t value) {
    const float x = static_cast<float>(value) / 127.0f;
    const float factor = 0.78f + (0.2195f * x * x * x);
    const uint32_t mapped = static_cast<uint32_t>(factor * 32767.0f);
    return static_cast<uint16_t>(mapped > 32720u ? 32720u : mapped);
}

inline uint16_t scale_param_q15(uint8_t value, uint16_t max_q15) {
    return static_cast<uint16_t>((static_cast<uint32_t>(value) * max_q15) /
                                 127u);
}

} // namespace

const char *karplus_impulse_to_string(KarplusImpulseType type) {
    switch (type) {
    case KarplusImpulseType::WhiteNoise:
        return "Noise";
    case KarplusImpulseType::PinkNoise:
        return "Pink";
    case KarplusImpulseType::SineChirp:
        return "Sine Chirp";
    case KarplusImpulseType::SquareChirp:
        return "Square Chirp";
    case KarplusImpulseType::SawChirp:
        return "Saw Chirp";
    case KarplusImpulseType::Click:
        return "Click";
    case KarplusImpulseType::MetallicBurst:
        return "Metal Burst";
    default:
        return "Unknown";
    }
}

uint16_t KarplusVoice::tuned_delay_samples_for_note(uint8_t midi_note) {
    // Clamp MIDI note to valid range
    if (midi_note > MIDI_MAX) midi_note = MIDI_MAX;
    
    const float freq = midi_frequencies[midi_note];
    if (freq <= 0.0f) return 2;

    const float tuned_delay =
        (static_cast<float>(SAMPLE_RATE) / freq) - 0.5f;
    int delay = static_cast<int>(tuned_delay);
    
    // Guard against invalid delay values
    if (delay < 2) delay = 2;
    if (delay >= MAX_DELAY_SAMPLES) delay = MAX_DELAY_SAMPLES - 1;
    
    // Ensure delay is never 0 to prevent division by zero in modulo operations
    if (delay <= 0) delay = 2;
    
    return static_cast<uint16_t>(delay);
}

bool KarplusVoice::matches(uint8_t channel, uint8_t note) const {
    return active && midi_channel == channel && midi_note == note;
}

void KarplusVoice::apply_patch(const KarplusPatch &patch) {
    impulse_type = patch.impulse_type;
    filter_gain_q15 = filter_gain_to_q15(patch.filter_gain);
    decay_q15 = decay_to_q15(patch.decay);
    impulse_length_samples = static_cast<uint16_t>(
        1 + ((static_cast<uint32_t>(delay_samples - 1) * patch.impulse_length) /
             127u));
    pick_offset_samples = patch.pick_position == 0
                              ? 0
                              : static_cast<uint16_t>(
                                    1 + ((static_cast<uint32_t>(delay_samples - 2) *
                                          patch.pick_position) /
                                         127u));
    dispersion_q15 = scale_param_q15(patch.dispersion, kDispersionMaxQ15);
    body_mix_q15 = scale_param_q15(patch.body_resonance, kBodyMixMaxQ15);
    body_feedback_q15 = static_cast<uint16_t>(
        kBodyFeedbackBaseQ15 +
        ((static_cast<uint32_t>(patch.body_resonance) *
          kBodyFeedbackRangeQ15) /
         127u));

    const float freq = midi_frequencies[midi_note];
    float body_freq = freq * 1.9f;
    if (body_freq < 180.0f) body_freq = 180.0f;
    if (body_freq > 1200.0f) body_freq = 1200.0f;
    int body_delay = static_cast<int>((static_cast<float>(SAMPLE_RATE) /
                                       body_freq) +
                                      0.5f);
    if (body_delay < 4) body_delay = 4;
    if (body_delay >= MAX_BODY_SAMPLES) body_delay = MAX_BODY_SAMPLES - 1;
    body_delay_samples = body_delay;
}

void KarplusVoice::start(uint8_t midi_note_, uint8_t midi_channel_,
                         uint8_t velocity_, const KarplusPatch &patch) {
    midi_note = midi_note_;
    midi_channel = midi_channel_;
    delay_samples = tuned_delay_samples_for_note(midi_note_);
    write_index = 0;
    lowpass_state = 0;
    previous_delayed = 0;
    dispersion_x1 = 0;
    dispersion_y1 = 0;
    body_line.fill(0);
    body_write_index = 0;
    silent_samples = 0;
    active = true;
    released = false;
    rng_state = 0x9E3779B9u ^ (static_cast<uint32_t>(midi_note_) << 16) ^
                (static_cast<uint32_t>(midi_channel_) << 8) ^ velocity_;

    apply_patch(patch);
    excite(velocity_);
}

void KarplusVoice::note_off() {
    released = true;
}

void KarplusVoice::reset() {
    delay_line.fill(0);
    delay_samples = 1;
    write_index = 0;
    lowpass_state = 0;
    previous_delayed = 0;
    dispersion_x1 = 0;
    dispersion_y1 = 0;
    body_line.fill(0);
    body_delay_samples = 16;
    body_write_index = 0;
    active = false;
    released = false;
    silent_samples = 0;
}

void KarplusVoice::excite(uint8_t velocity) {
    const uint16_t amplitude_q15 =
        static_cast<uint16_t>(8192 + (static_cast<int>(velocity) * 188));

    switch (impulse_type) {
    case KarplusImpulseType::WhiteNoise:
        excite_noise(amplitude_q15);
        break;
    case KarplusImpulseType::PinkNoise:
        excite_noise(static_cast<uint16_t>((static_cast<uint32_t>(amplitude_q15) *
                                            3u) /
                                           4u));
        for (int i = 1; i < delay_samples; ++i) {
            delay_line[i] = clamp_i16(
                (static_cast<int32_t>(delay_line[i]) +
                 (static_cast<int32_t>(delay_line[i - 1]) * 3)) >>
                2);
        }
        break;
    case KarplusImpulseType::SineChirp:
        excite_chirp(sine_wave_table, amplitude_q15);
        break;
    case KarplusImpulseType::SquareChirp:
        excite_chirp(square_wave_table, amplitude_q15);
        break;
    case KarplusImpulseType::SawChirp:
        excite_chirp(sawtooth_wave_table, amplitude_q15);
        break;
    case KarplusImpulseType::Click:
        delay_line.fill(0);
        delay_line[0] = clamp_i16(amplitude_q15 << 1);
        if (delay_samples > 1) {
            delay_line[1] = clamp_i16(-static_cast<int32_t>(amplitude_q15));
        }
        apply_pick_position();
        break;
    case KarplusImpulseType::MetallicBurst:
        delay_line.fill(0);
        for (int i = 0; i < delay_samples; ++i) {
            if (i >= impulse_length_samples) {
                delay_line[i] = 0;
                continue;
            }

            rng_state = (rng_state * 1664525u) + 1013904223u;
            const int16_t noise =
                static_cast<int16_t>((rng_state >> 16) ^ (rng_state & 0xFFFFu));
            const int16_t square =
                ((i * 13) & 16) ? 32767 : -32767;
            const int32_t env_q15 =
                ((impulse_length_samples - i) * static_cast<int32_t>(kQ15Max)) /
                impulse_length_samples;
            const int32_t mix =
                ((static_cast<int32_t>(noise) >> 1) +
                 (static_cast<int32_t>(square) >> 1));
            delay_line[i] = clamp_i16(
                ((mix * env_q15) >> 15) * amplitude_q15 >> 15);
        }
        apply_pick_position();
        break;
    }
}

void KarplusVoice::excite_noise(uint16_t amplitude_q15) {
    delay_line.fill(0);

    const int excited_samples =
        impulse_length_samples < delay_samples ? impulse_length_samples
                                               : delay_samples;
    for (int i = 0; i < delay_samples; ++i) {
        if (i >= excited_samples) {
            delay_line[i] = 0;
            continue;
        }
        rng_state = (rng_state * 1664525u) + 1013904223u;
        const int16_t noise =
            static_cast<int16_t>((rng_state >> 16) ^ (rng_state & 0xFFFFu));
        const int32_t env_q15 =
            ((excited_samples - i) * static_cast<int32_t>(kQ15Max)) /
            excited_samples;
        const int32_t sample =
            (((static_cast<int32_t>(noise) * env_q15) >> 15) * amplitude_q15) >>
            15;
        delay_line[i] = clamp_i16(sample);
    }

    apply_pick_position();
}

void KarplusVoice::excite_chirp(
    const std::array<int16_t, WAVE_TABLE_LEN> &table,
    uint16_t amplitude_q15) {
    delay_line.fill(0);
    uint32_t phase_q16 = 0;
    const int excited_samples =
        impulse_length_samples < delay_samples ? impulse_length_samples
                                               : delay_samples;
    const uint32_t base_step_q16 = static_cast<uint32_t>(
        ((static_cast<uint64_t>(WAVE_TABLE_LEN) << 16) /
         static_cast<uint64_t>(excited_samples)));

    for (int i = 0; i < delay_samples; ++i) {
        if (i >= excited_samples) {
            delay_line[i] = 0;
            continue;
        }
        const uint32_t sweep = 1u + static_cast<uint32_t>(
            (5u * static_cast<uint32_t>(excited_samples - i)) /
            static_cast<uint32_t>(excited_samples));
        phase_q16 += base_step_q16 * sweep;

        const int16_t wave =
            table[(phase_q16 >> 16) & (WAVE_TABLE_LEN - 1)];
        const int32_t env_q15 =
            ((excited_samples - i) * static_cast<int32_t>(kQ15Max)) /
            excited_samples;
        const int32_t sample =
            (((static_cast<int32_t>(wave) * env_q15) >> 15) * amplitude_q15) >>
            15;
        delay_line[i] = clamp_i16(sample);
    }

    apply_pick_position();
}

void KarplusVoice::apply_pick_position() {
    apply_pick_position_to_buffer(delay_samples);
}

void KarplusVoice::apply_pick_position_to_buffer(int total_samples) {
    if (pick_offset_samples <= 0 || pick_offset_samples >= total_samples) {
        return;
    }

    for (int i = total_samples - 1; i >= static_cast<int>(pick_offset_samples);
         --i) {
        const int32_t shaped =
            static_cast<int32_t>(delay_line[i]) -
            static_cast<int32_t>(delay_line[i - pick_offset_samples]);
        delay_line[i] = clamp_i16(shaped);
    }
}

int16_t KarplusVoice::apply_dispersion(int16_t sample) {
    if (dispersion_q15 == 0) {
        return sample;
    }

    const int32_t out =
        (-static_cast<int32_t>(dispersion_q15) * sample +
         (static_cast<int32_t>(dispersion_x1) << 15) +
         static_cast<int32_t>(dispersion_q15) * dispersion_y1) >>
        15;
    dispersion_x1 = sample;
    dispersion_y1 = clamp_i16(out);
    return dispersion_y1;
}

int16_t KarplusVoice::apply_body_resonator(int16_t sample) {
    if (body_mix_q15 == 0) {
        return sample;
    }

    int read_index = body_write_index - body_delay_samples;
    if (read_index < 0) {
        read_index += MAX_BODY_SAMPLES;
    }

    const int16_t body_delayed = body_line[read_index];
    const int32_t body_input =
        static_cast<int32_t>(sample) +
        ((static_cast<int32_t>(body_delayed) * body_feedback_q15) >> 15);
    body_line[body_write_index] = clamp_i16(body_input);

    ++body_write_index;
    if (body_write_index >= MAX_BODY_SAMPLES) {
        body_write_index = 0;
    }

    return clamp_i16(static_cast<int32_t>(sample) +
                     ((static_cast<int32_t>(body_delayed) * body_mix_q15) >>
                      15));
}

void KarplusVoice::render(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {
    buffer.fill(0);
    if (!active) return;

    const uint16_t loop_decay_q15 =
        released && decay_q15 > kReleaseDecayQ15 ? kReleaseDecayQ15 : decay_q15;

    for (int i = 0; i < SAMPLES_PER_BUFFER; ++i) {
        const int16_t delayed = delay_line[write_index];
        const int32_t averaged =
            (static_cast<int32_t>(delayed) + previous_delayed) >> 1;
        previous_delayed = delayed;

        const int32_t filtered =
            ((static_cast<int32_t>(averaged) * filter_gain_q15) +
             (static_cast<int32_t>(lowpass_state) *
             static_cast<int32_t>(kQ15Max - filter_gain_q15))) >>
            15;
        lowpass_state = clamp_i16(filtered);
        const int16_t dispersed = apply_dispersion(lowpass_state);

        delay_line[write_index] = clamp_i16(
            (static_cast<int32_t>(dispersed) * loop_decay_q15) >> 15);
        buffer[i] = apply_body_resonator(delayed);

        ++write_index;
        if (write_index >= delay_samples) {
            write_index = 0;
        }

        if (abs_int(delayed) < kSilenceThreshold) {
            ++silent_samples;
            if (silent_samples >
                static_cast<uint32_t>(delay_samples * 2)) {
                active = false;
                break;
            }
        } else {
            silent_samples = 0;
        }
    }
}

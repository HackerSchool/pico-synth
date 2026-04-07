#include "Modal.hpp"
#include "MidiHandler.hpp"
#include "Wavetable.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kNyquistSafety = 0.45f;
constexpr int kSilenceThreshold = 50;
constexpr float kReleaseScale = 0.99965f;
constexpr float kOutputScale = 23000.0f;

struct ModalMode {
    float ratio;
    float gain;
    float bandwidth_hz;
};

struct ModalFamily {
    const char *name;
    std::array<ModalMode, ModalVoice::MODE_COUNT> modes;
};

constexpr std::array<ModalFamily, 5> kModalFamilies = {{
    {"String",
     {{{1.000f, 1.000f, 2.6f},
       {2.000f, 0.620f, 3.4f},
       {3.000f, 0.420f, 4.2f},
       {4.000f, 0.300f, 5.2f},
       {5.000f, 0.215f, 6.0f},
       {6.000f, 0.160f, 7.0f},
       {7.000f, 0.120f, 8.2f},
       {8.000f, 0.090f, 9.4f},
       {9.000f, 0.070f, 10.8f},
       {10.000f, 0.052f, 12.2f}}}},
    {"Bar",
     {{{1.000f, 1.000f, 3.2f},
       {2.756f, 0.560f, 5.6f},
       {5.404f, 0.310f, 8.2f},
       {8.933f, 0.205f, 11.8f},
       {13.344f, 0.145f, 16.2f},
       {18.637f, 0.105f, 21.4f},
       {24.812f, 0.082f, 27.0f},
       {31.870f, 0.064f, 33.6f},
       {39.810f, 0.050f, 40.8f},
       {48.632f, 0.040f, 49.0f}}}},
    {"Plate",
     {{{1.000f, 1.000f, 3.8f},
       {1.620f, 0.700f, 5.0f},
       {2.330f, 0.500f, 6.6f},
       {3.180f, 0.340f, 8.8f},
       {4.120f, 0.240f, 11.8f},
       {5.160f, 0.175f, 15.2f},
       {6.350f, 0.125f, 19.2f},
       {7.720f, 0.092f, 24.0f},
       {9.240f, 0.067f, 29.4f},
       {10.920f, 0.050f, 35.6f}}}},
    {"Bell",
     {{{0.500f, 0.660f, 2.4f},
       {1.000f, 1.000f, 2.8f},
       {2.706f, 0.500f, 4.2f},
       {4.000f, 0.280f, 6.0f},
       {5.410f, 0.210f, 8.2f},
       {6.795f, 0.155f, 11.0f},
       {8.930f, 0.110f, 15.0f},
       {10.850f, 0.080f, 20.0f},
       {13.330f, 0.060f, 26.0f},
       {16.230f, 0.045f, 33.0f}}}},
    {"Bowl",
     {{{1.000f, 1.000f, 2.2f},
       {1.510f, 0.920f, 2.8f},
       {2.020f, 0.560f, 4.0f},
       {2.430f, 0.310f, 5.8f},
       {3.000f, 0.220f, 8.2f},
       {3.790f, 0.150f, 12.0f},
       {4.640f, 0.105f, 17.0f},
       {5.520f, 0.078f, 23.0f},
       {6.560f, 0.056f, 30.0f},
       {7.720f, 0.040f, 38.0f}}}},
}};

inline float clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

inline int16_t clamp_i16(int32_t value) {
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(value);
}

inline int abs_int(int value) {
    return value < 0 ? -value : value;
}

inline float lerp_float(float a, float b, float t) {
    return a + ((b - a) * t);
}

inline float modal_param_to_unit(uint8_t value) {
    return static_cast<float>(value) / 127.0f;
}

ModalMode interpolated_mode(uint8_t structure, int mode_index) {
    const float structure_norm = modal_param_to_unit(structure);
    const float scaled =
        structure_norm * static_cast<float>(kModalFamilies.size() - 1);
    int left = static_cast<int>(scaled);
    if (left < 0) left = 0;
    if (left >= static_cast<int>(kModalFamilies.size()) - 1) {
        left = static_cast<int>(kModalFamilies.size()) - 1;
    }

    const int right =
        left < static_cast<int>(kModalFamilies.size()) - 1 ? left + 1 : left;
    const float blend = right == left ? 0.0f : scaled - static_cast<float>(left);

    const ModalMode &a = kModalFamilies[left].modes[mode_index];
    const ModalMode &b = kModalFamilies[right].modes[mode_index];
    return {
        lerp_float(a.ratio, b.ratio, blend),
        lerp_float(a.gain, b.gain, blend),
        lerp_float(a.bandwidth_hz, b.bandwidth_hz, blend),
    };
}
} // namespace

const char *modal_exciter_to_string(ModalExciterType type) {
    switch (type) {
    case ModalExciterType::NoiseBurst:
        return "Noise";
    case ModalExciterType::Click:
        return "Click";
    case ModalExciterType::HardStrike:
        return "Hard";
    case ModalExciterType::SoftStrike:
        return "Soft";
    default:
        return "Unknown";
    }
}

const char *modal_structure_to_string(uint8_t structure) {
    const float structure_norm = modal_param_to_unit(structure);
    int index = static_cast<int>(
        (structure_norm * static_cast<float>(kModalFamilies.size() - 1)) + 0.5f);
    if (index < 0) index = 0;
    if (index >= static_cast<int>(kModalFamilies.size())) {
        index = static_cast<int>(kModalFamilies.size()) - 1;
    }
    return kModalFamilies[index].name;
}

bool ModalVoice::matches(uint8_t channel, uint8_t note) const {
    return active && midi_channel == channel && midi_note == note;
}

void ModalVoice::apply_patch(const ModalPatch &patch) {
    exciter_type = patch.exciter_type;
    brightness = modal_param_to_unit(patch.brightness);
    damping = modal_param_to_unit(patch.damping);
    position = 0.08f + (0.84f * modal_param_to_unit(patch.position));
    configure_modes(patch);
}

void ModalVoice::start(uint8_t midi_note_, uint8_t midi_channel_,
                       uint8_t velocity_, const ModalPatch &patch) {
    midi_note = midi_note_;
    midi_channel = midi_channel_;
    rng_state = 0xA511E9B3u ^ (static_cast<uint32_t>(midi_note_) << 16) ^
                (static_cast<uint32_t>(midi_channel_) << 8) ^ velocity_;
    active = true;
    released = false;
    silent_samples = 0;
    exciter_index = 0;
    exciter_length = 0;
    mode_y1.fill(0.0f);
    mode_y2.fill(0.0f);
    apply_patch(patch);
    generate_excitation(velocity_);
}

void ModalVoice::note_off() { released = true; }

void ModalVoice::reset() {
    mode_c1.fill(0.0f);
    mode_c2.fill(0.0f);
    mode_b0.fill(0.0f);
    mode_y1.fill(0.0f);
    mode_y2.fill(0.0f);
    exciter_buffer.fill(0.0f);
    exciter_length = 0;
    exciter_index = 0;
    brightness = 0.0f;
    damping = 0.0f;
    position = 0.0f;
    active = false;
    released = false;
    silent_samples = 0;
}

void ModalVoice::configure_modes(const ModalPatch &patch) {
    const float base_frequency = midi_frequencies[midi_note];
    const float damping_scale = 1.95f - (1.60f * damping);

    for (int i = 0; i < MODE_COUNT; ++i) {
        const ModalMode mode = interpolated_mode(patch.structure, i);
        const float mode_norm =
            static_cast<float>(i) / static_cast<float>(MODE_COUNT - 1);
        const float mode_frequency = base_frequency * mode.ratio;

        if (mode_frequency <= 0.0f ||
            mode_frequency >= (static_cast<float>(SAMPLE_RATE) * kNyquistSafety)) {
            mode_c1[i] = 0.0f;
            mode_c2[i] = 0.0f;
            mode_b0[i] = 0.0f;
            mode_y1[i] = 0.0f;
            mode_y2[i] = 0.0f;
            continue;
        }

        const float strike_weight = std::fabs(
            std::sin(kPi * position * static_cast<float>(i + 1)));
        const float position_gain = 0.04f + (0.96f * strike_weight);
        const float spectral_tilt =
            0.30f + (0.70f * brightness) + (0.90f * brightness * mode_norm);
        float bandwidth_hz =
            mode.bandwidth_hz * damping_scale *
            (1.35f - (0.78f * brightness * mode_norm));
        bandwidth_hz = clamp_float(bandwidth_hz, 0.60f, 240.0f);

        const float radius =
            std::exp((-kPi * bandwidth_hz) / static_cast<float>(SAMPLE_RATE));
        const float omega =
            (2.0f * kPi * mode_frequency) / static_cast<float>(SAMPLE_RATE);

        mode_c1[i] = 2.0f * radius * std::cos(omega);
        mode_c2[i] = -(radius * radius);
        mode_b0[i] = mode.gain * position_gain * spectral_tilt * 0.032f;
    }
}

void ModalVoice::generate_excitation(uint8_t velocity) {
    exciter_buffer.fill(0.0f);
    exciter_index = 0;

    const float velocity_scale =
        0.18f + (0.82f * (static_cast<float>(velocity) / 127.0f));
    const float hardness = clamp_float((brightness * 0.65f) + 0.25f, 0.0f, 1.0f);

    switch (exciter_type) {
    case ModalExciterType::NoiseBurst:
        exciter_length = static_cast<uint16_t>(28 + static_cast<int>(brightness * 36.0f));
        break;
    case ModalExciterType::Click:
        exciter_length = 8;
        break;
    case ModalExciterType::HardStrike:
        exciter_length = static_cast<uint16_t>(18 + static_cast<int>(brightness * 18.0f));
        break;
    case ModalExciterType::SoftStrike:
    default:
        exciter_length = static_cast<uint16_t>(40 + static_cast<int>(brightness * 20.0f));
        break;
    }

    if (exciter_length > MAX_EXCITER_SAMPLES) {
        exciter_length = MAX_EXCITER_SAMPLES;
    }

    float smooth_noise = 0.0f;
    float previous = 0.0f;

    for (uint16_t i = 0; i < exciter_length; ++i) {
        rng_state = (rng_state * 1664525u) + 1013904223u;
        const float noise =
            static_cast<float>(static_cast<int16_t>(rng_state >> 16)) / 32768.0f;
        const float env =
            1.0f - (static_cast<float>(i) / static_cast<float>(exciter_length));
        float sample = 0.0f;

        switch (exciter_type) {
        case ModalExciterType::NoiseBurst:
            sample = noise * env;
            break;
        case ModalExciterType::Click:
            if (i == 0) sample = 1.0f;
            if (i == 1) sample = -0.45f;
            break;
        case ModalExciterType::HardStrike:
            sample = (noise - previous) * (0.85f + (0.55f * hardness)) * env;
            previous = noise;
            break;
        case ModalExciterType::SoftStrike:
        default:
            smooth_noise = (smooth_noise * 0.82f) + (noise * 0.18f);
            sample = smooth_noise * std::sqrt(env);
            break;
        }

        sample *= velocity_scale;
        if (exciter_type == ModalExciterType::HardStrike) {
            sample *= 1.25f;
        } else if (exciter_type == ModalExciterType::SoftStrike) {
            sample *= 0.80f;
        }
        exciter_buffer[i] = sample;
    }
}

void ModalVoice::render(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {
    buffer.fill(0);
    if (!active) return;

    for (int i = 0; i < SAMPLES_PER_BUFFER; ++i) {
        float excitation = 0.0f;
        if (exciter_index < exciter_length) {
            excitation = exciter_buffer[exciter_index++];
        }

        float modal_sum = 0.0f;
        for (int mode = 0; mode < MODE_COUNT; ++mode) {
            const float y = (mode_b0[mode] * excitation) +
                            (mode_c1[mode] * mode_y1[mode]) +
                            (mode_c2[mode] * mode_y2[mode]);
            mode_y2[mode] = mode_y1[mode];
            mode_y1[mode] = released ? y * kReleaseScale : y;
            modal_sum += mode_y1[mode];
        }

        const int16_t sample =
            clamp_i16(static_cast<int32_t>(modal_sum * kOutputScale));
        buffer[i] = sample;

        if (abs_int(sample) < kSilenceThreshold) {
            ++silent_samples;
            if (silent_samples > static_cast<uint32_t>(SAMPLE_RATE)) {
                active = false;
                break;
            }
        } else {
            silent_samples = 0;
        }
    }
}

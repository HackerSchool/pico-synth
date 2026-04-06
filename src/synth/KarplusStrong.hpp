#ifndef KARPLUS_STRONG_HPP
#define KARPLUS_STRONG_HPP

#include "Wavetable.hpp"
#include "config.hpp"
#include <array>
#include <cstdint>

enum class KarplusImpulseType : uint8_t {
    WhiteNoise = 0,
    PinkNoise = 1,
    SineChirp = 2,
    SquareChirp = 3,
    SawChirp = 4,
    Click = 5,
    MetallicBurst = 6,
    HandPan = 7
};

const char *karplus_impulse_to_string(KarplusImpulseType type);

struct KarplusPatch {
    KarplusImpulseType impulse_type = KarplusImpulseType::WhiteNoise;
    uint8_t filter_gain = 92;
    uint8_t decay = 108;
    uint8_t impulse_length = 72;
    uint8_t pick_position = 32;
    uint8_t dispersion = 24;
    uint8_t body_resonance = 40;
};

class KarplusVoice {
  public:
    static constexpr int MAX_DELAY_SAMPLES = 6144;
    static constexpr int MODAL_MODE_COUNT = 8;

    void start(uint8_t midi_note_, uint8_t midi_channel_, uint8_t velocity_,
               const KarplusPatch &patch);
    void apply_patch(const KarplusPatch &patch);
    void note_off();
    void reset();
    void render(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);

    bool matches(uint8_t channel, uint8_t note) const;
    bool is_active() const { return active; }

    static uint16_t tuned_delay_samples_for_note(uint8_t midi_note);

    uint8_t midi_note = 0;
    uint8_t midi_channel = 0;

  private:
    void excite(uint8_t velocity);
    void excite_noise(uint16_t amplitude_q15);
    void excite_chirp(const std::array<int16_t, WAVE_TABLE_LEN> &table,
                      uint16_t amplitude_q15);
    void apply_pick_position();
    void apply_pick_position_to_buffer(int total_samples);
    int16_t apply_dispersion(int16_t sample);
    int16_t apply_body_resonator(int16_t sample);
    void configure_handpan_modes(const KarplusPatch &patch);
    int16_t render_handpan_sample();

    std::array<int16_t, MAX_DELAY_SAMPLES> delay_line{};
    static constexpr int MAX_BODY_SAMPLES = 256;
    std::array<int16_t, MAX_BODY_SAMPLES> body_line{};
    int delay_samples = 1;
    int write_index = 0;
    int body_delay_samples = 16;
    int body_write_index = 0;
    uint32_t rng_state = 1;
    int16_t lowpass_state = 0;
    int16_t previous_delayed = 0;
    int16_t dispersion_x1 = 0;
    int16_t dispersion_y1 = 0;
    uint16_t filter_gain_q15 = 22000;
    uint16_t decay_q15 = 32000;
    uint16_t impulse_length_samples = 16;
    uint16_t pick_offset_samples = 0;
    uint16_t dispersion_q15 = 0;
    uint16_t body_mix_q15 = 0;
    uint16_t body_feedback_q15 = 0;
    KarplusImpulseType impulse_type = KarplusImpulseType::WhiteNoise;
    bool handpan_enabled = false;
    bool active = false;
    bool released = false;
    uint32_t silent_samples = 0;
    uint16_t handpan_excitation_length = 0;
    uint16_t handpan_excitation_index = 0;
    std::array<float, MODAL_MODE_COUNT> handpan_mode_c1{};
    std::array<float, MODAL_MODE_COUNT> handpan_mode_c2{};
    std::array<float, MODAL_MODE_COUNT> handpan_mode_b0{};
    std::array<float, MODAL_MODE_COUNT> handpan_mode_y1{};
    std::array<float, MODAL_MODE_COUNT> handpan_mode_y2{};
};

#endif // KARPLUS_STRONG_HPP

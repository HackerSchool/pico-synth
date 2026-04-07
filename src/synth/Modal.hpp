#ifndef MODAL_HPP
#define MODAL_HPP

#include "config.hpp"
#include <array>
#include <cstdint>

enum class ModalExciterType : uint8_t {
    NoiseBurst = 0,
    Click = 1,
    HardStrike = 2,
    SoftStrike = 3
};

const char *modal_exciter_to_string(ModalExciterType type);
const char *modal_structure_to_string(uint8_t structure);

struct ModalPatch {
    uint8_t structure = 0;
    uint8_t brightness = 92;
    uint8_t damping = 96;
    uint8_t position = 36;
    ModalExciterType exciter_type = ModalExciterType::SoftStrike;
};

class ModalVoice {
  public:
    static constexpr int MODE_COUNT = 10;
    static constexpr int MAX_EXCITER_SAMPLES = 192;

    void start(uint8_t midi_note_, uint8_t midi_channel_, uint8_t velocity_,
               const ModalPatch &patch);
    void apply_patch(const ModalPatch &patch);
    void note_off();
    void reset();
    void render(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);

    bool matches(uint8_t channel, uint8_t note) const;
    bool is_active() const { return active; }

    uint8_t midi_note = 0;
    uint8_t midi_channel = 0;

  private:
    void configure_modes(const ModalPatch &patch);
    void generate_excitation(uint8_t velocity);

    std::array<float, MODE_COUNT> mode_c1{};
    std::array<float, MODE_COUNT> mode_c2{};
    std::array<float, MODE_COUNT> mode_b0{};
    std::array<float, MODE_COUNT> mode_y1{};
    std::array<float, MODE_COUNT> mode_y2{};
    std::array<float, MAX_EXCITER_SAMPLES> exciter_buffer{};
    ModalExciterType exciter_type = ModalExciterType::SoftStrike;
    float brightness = 0.0f;
    float damping = 0.0f;
    float position = 0.0f;
    uint16_t exciter_length = 0;
    uint16_t exciter_index = 0;
    uint32_t rng_state = 1;
    uint32_t silent_samples = 0;
    bool active = false;
    bool released = false;
};

#endif // MODAL_HPP

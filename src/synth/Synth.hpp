#ifndef SYNTH_HPP
#define SYNTH_HPP

#include "Envelope.hpp"
#include "Filter.hpp"
#include "KarplusStrong.hpp"
#include "Modal.hpp"
#include "MidiHandler.hpp"
#include "Operator.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "Delay.hpp"

// Effect classes are implemented in Synth.cpp (private helper types)
class Distortion;
class Reverb;
class Chorus;
class ReverbScFx;

#include "config.hpp"
#include "tusb.h"

#include <atomic>
#include <bitset>
#include <cstdint>

#define NUM_VOICES 32

extern const WaveType channel_wave_map[16];

enum class SynthEngine : uint8_t {
    FM = 0,
    KarplusStrong = 1,
    Modal = 2
};

// // Channel-specific parameters (16 MIDI channels)
// struct ChannelParams {
//     uint8_t attack = 5;
//     uint8_t decay = 5;
//     uint16_t sustain = 64 << 8;
//     uint8_t release = 5;
//     uint8_t filter_cutoff_msb = 64; // Default to mid-range
//     uint8_t filter_cutoff_lsb = 0;
//     uint8_t filter_q_msb = 16; // Default to lower Q
//     uint8_t filter_q_lsb = 0;
// };

class Synth {
  public:
    static constexpr int FX_SLOT_COUNT = 5;
    static constexpr int KARPLUS_VOICE_COUNT = 8;
    static constexpr int MODAL_VOICE_COUNT = 8;

    Synth();
    ~Synth();
    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);
    void process_fx(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);

    // TODO: make it into an array of buffers, max 6 should be enough for all
    // algos
    std::array<int16_t, SAMPLES_PER_BUFFER> flow_buffer{0};
    void initialize_patches();
    void initialize_karplus_patches();
    void initialize_modal_patches();

    void out_interp();
    std::array<int16_t, SAMPLES_PER_BUFFER> &get_output();
    void process_midi_packet(uint8_t packet[4]);

    void cycle_wave_type(int delta);

    void note_on(uint8_t channel, uint8_t note, uint8_t velocity);
    void note_off(uint8_t channel, uint8_t note, uint8_t velocity);
    const char *get_notes_playing_names();
    std::bitset<128> get_notes_bitmask() const { return notes_playing_bitset; }
    void reset_runtime_state();
    void set_engine(SynthEngine engine);
    SynthEngine get_engine() const { return current_engine; }

    FilterFIR low_pass = FilterFIR(1000.f);
    FilterCheb low_pass_cheb = FilterCheb(5000.f, 0.5f, 44100.f);
    Delay delay_effect;
    Distortion *distortion_effect = nullptr;
    Reverb *reverb_effect = nullptr;
    Chorus *chorus_effect = nullptr;
    ReverbScFx *reverb_sc_effect = nullptr;
    bool fx_enabled[FX_SLOT_COUNT] = { true, false, false, false, false };
    // std::array<ChannelParams, 16> channel_params;

    // patches
    std::array<Patch, 16> patch_storage; // Editable from Core0
    std::array<std::atomic<Patch *>, 16>
        active_patch; // Synth reads from this (Core1-safe)
    std::bitset<16>
        patch_dirty_flags; // Core0 sets dirty bit when editing patch
    std::array<KarplusPatch, 16> karplus_patch_storage;
    std::array<std::atomic<KarplusPatch *>, 16> active_karplus_patch;
    std::bitset<16> karplus_patch_dirty_flags;
    std::array<ModalPatch, 16> modal_patch_storage;
    std::array<std::atomic<ModalPatch *>, 16> active_modal_patch;
    std::bitset<16> modal_patch_dirty_flags;

    // voice arrays
    std::array<Voice, NUM_VOICES> voice;
    std::array<KarplusVoice, KARPLUS_VOICE_COUNT> karplus_voice;
    std::array<ModalVoice, MODAL_VOICE_COUNT> modal_voice;

    void cycle_filter_type();

    // FX control
    void enable_fx(int fx_id, bool enabled);
    void set_fx_params(int fx_id, int p1, int p2, int mix);

    void set_filter_cutoff(float cutoff, float q = 0.5f);
    void update_filter_cutoff(uint8_t channel);
    void update_filter_q(uint8_t channel);
    void set_filter_type(uint8_t type_value);

    float get_filter_cutoff();
    FilterType current_filter_type = FILTER_OFF; // Default to Chebyshev

  private:
    void clear_fm_voices();
    void clear_karplus_voices();
    void clear_modal_voices();

    std::bitset<128> notes_playing_bitset;
    SynthEngine current_engine = SynthEngine::FM;
};

#endif // !SYNTH_HPP

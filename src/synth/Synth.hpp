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

class Delay;
class Distortion;
class Reverb;
class Chorus;
class ReverbScFx;
class Compressor;

#include "config.hpp"
#include "tusb.h"

#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>

#define NUM_VOICES 32

extern const WaveType channel_wave_map[16];

enum class SynthEngine : uint8_t {
    FM = 0,
    KarplusStrong = 1,
    Modal = 2
};

class Synth {
  public:
    static constexpr int FX_SLOT_COUNT = 6;
    static constexpr int KARPLUS_VOICE_COUNT = 8;
    static constexpr int MODAL_VOICE_COUNT = 8;

    Synth();
    ~Synth();
    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);
    void process_fx(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);

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
    bool fx_enabled[FX_SLOT_COUNT] = { false, false, false, false, false, true };

    std::array<Patch, 16> patch_storage;
    std::array<std::atomic<Patch *>, 16> active_patch;
    std::bitset<16> patch_dirty_flags;
    std::array<KarplusPatch, 16> karplus_patch_storage;
    std::array<std::atomic<KarplusPatch *>, 16> active_karplus_patch;
    std::bitset<16> karplus_patch_dirty_flags;
    std::array<ModalPatch, 16> modal_patch_storage;
    std::array<std::atomic<ModalPatch *>, 16> active_modal_patch;
    std::bitset<16> modal_patch_dirty_flags;

    void cycle_filter_type();

    void enable_fx(int fx_id, bool enabled);
    void set_fx_params(int fx_id, int p1, int p2, int mix);

    void set_filter_cutoff(float cutoff, float q = 0.5f);
    void update_filter_cutoff(uint8_t channel);
    void update_filter_q(uint8_t channel);
    void set_filter_type(uint8_t type_value);

    float get_filter_cutoff();
    std::size_t active_runtime_bytes() const;
    std::size_t active_fx_bytes() const;
    FilterType current_filter_type = FILTER_OFF;

  private:
    struct FxParams {
        int p1;
        int p2;
        int mix;
    };

    void clear_fm_voices();
    void clear_karplus_voices();
    void clear_modal_voices();
    void ensure_engine_runtime(SynthEngine engine);
    void release_engine_runtimes();

    Delay *ensure_delay_effect();
    Distortion *ensure_distortion_effect();
    Reverb *ensure_reverb_effect();
    Chorus *ensure_chorus_effect();
    ReverbScFx *ensure_reverb_sc_effect();
    Compressor *ensure_compressor_effect();
    void reset_fx_slot(int fx_id);
    void destroy_fx_slot(int fx_id);

    std::bitset<128> notes_playing_bitset;
    SynthEngine current_engine = SynthEngine::FM;

    std::array<FxParams, FX_SLOT_COUNT> fx_param_cache{};
    Delay *delay_effect = nullptr;
    Compressor *compressor_effect = nullptr;
    Distortion *distortion_effect = nullptr;
    Reverb *reverb_effect = nullptr;
    Chorus *chorus_effect = nullptr;
    ReverbScFx *reverb_sc_effect = nullptr;

    std::unique_ptr<std::array<Voice, NUM_VOICES>> fm_voices;
    std::unique_ptr<std::array<KarplusVoice, KARPLUS_VOICE_COUNT>>
        karplus_voices;
    std::unique_ptr<std::array<ModalVoice, MODAL_VOICE_COUNT>> modal_voices;
};

#endif // !SYNTH_HPP

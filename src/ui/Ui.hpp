#ifndef UI_STATE_HPP
#define UI_STATE_HPP

#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sampler.hpp"
#include "Sequencer.hpp"
#include "Synth.hpp"
#include <array>
#include <cstdint>

// When adding a new state here watch out for the order
// sine enums are basically ints
typedef enum UiState {
    UI_STATE_MAIN,          // = 0
    UI_STATE_FM_EDIT,       // = 1
    UI_STATE_FX_EDIT,       // = 2
    UI_STATE_ANALOG,        // = 3
    UI_STATE_MIDI_SETTINGS, // = 4
    UI_STATE_SEQUENCER,     // = 5
    UI_STATE_SAMPLER,       // = 6
    UI_STATE_SEQUENCER_EDIT,
    UI_STATE_CHOOSE,
    UI_STATE_ENGINE_SELECT,
    UI_STATE_KARPLUS_EDIT,
    UI_STATE_COUNT // helpful for bounds checking
} UiState;

#define NUM_USABLE_STATES                                                      \
    (7) // 7 states (MAIN, FM_EDIT, FX_EDIT, ANALOG, MIDI, SEQUENCER, SAMPLER) (states listed in
        // the menu)

class UiHandler;

typedef void (*EncoderHandler)(UiHandler &);
typedef void (*SwitchHandler)(UiHandler &);
typedef void (*DisplayHandler)(UiHandler &);

typedef struct {
    EncoderHandler handle_encoders;
    SwitchHandler handle_switches;
    DisplayHandler handle_display;
} UiDispatchEntry;

class UiHandler {

  public:
    UiHandler(HardwareManager &hw, MidiHandler &midi_handler, Sequencer &seq,
              Sampler &sampler, Synth &synth);

    // void init();
    void update(); // called every loop
    //
    // main state
    static void main_handle_encoders(UiHandler &self);
    static void main_handle_switches(UiHandler &self);
    static void main_update_display(UiHandler &self);

    // midi state
    static void midi_handle_encoders(UiHandler &self);
    static void midi_update_display(UiHandler &self);

    // sequencer state
    static void sequencer_handle_encoders(UiHandler &self);
    static void sequencer_update_display(UiHandler &self);

    // sequencer edit state
    static void sequencer_note_edit_handle_switches(UiHandler &self);
    static void sequencer_note_edit_handle_encoders(UiHandler &self);
    static void sequencer_note_edit_update_display(UiHandler &self);
    void sequencer_note_edit_enter(UiHandler &self);

    // choose state
    static void choose_handle_encoders(UiHandler &self);
    static void choose_update_display(UiHandler &self);

    // engine select state
    static void engine_select_handle_encoders(UiHandler &self);
    static void engine_select_update_display(UiHandler &self);

    // sampler state
    static void sampler_handle_encoders(UiHandler &self);
    static void sampler_update_display(UiHandler &self);

    // FM edit state
    static void fm_edit_handle_encoders(UiHandler &self);
    static void fm_edit_handle_switches(UiHandler &self);
    static void fm_edit_update_display(UiHandler &self);
    static void karplus_edit_handle_encoders(UiHandler &self);
    static void karplus_edit_handle_switches(UiHandler &self);
    static void karplus_edit_update_display(UiHandler &self);

    static void fx_handle_encoders(UiHandler &self);
    static void fx_update_display(UiHandler &self);
    static void analog_handle_encoders(UiHandler &self);
    static void analog_update_display(UiHandler &self);

    // helpers:

    void set_adsr_param(int param, uint8_t value);
    uint8_t get_adsr_param(int param);

    void set_delay_param(int delay_ms, int feedback, int mix);

  private:
    static bool encoder_moved(int32_t delta);
    static int encoder_velocity_step(int32_t delta, int slow_step, int medium_step,
                                     int fast_step);
    static int encoder_velocity_delta(int32_t delta, int slow_step, int medium_step,
                                      int fast_step);
    static void randomize_current_engine_patch(UiHandler &self);
    static void randomize_fm_patch(UiHandler &self);
    static void randomize_karplus_patch(UiHandler &self);

    HardwareManager &hw;
    MidiHandler &midi;
    Sequencer &seq;
    Sampler &sampler;
    Synth &synth;

    UiState ui_state = UI_STATE_MAIN;
    UiDispatchEntry ui_dispatch_table[UI_STATE_COUNT];

    // shit I need for the main state
    int current_adsr_param = 0; // 0=A, 1=D, 2=S, 3=R
    // Channel-specific parameters (16 MIDI channels)

    // std::array<ChannelParams, 16> channel_params;

    uint8_t adsr[4] = {64, 64, 64,
                       64}; // Attack, Decay, Sustain, Release (MIDI 7-bit)
    std::bitset<128> last_note_state;
    uint16_t prev_switches = 0;
    WaveType last_wave_type = static_cast<WaveType>(-1);
    bool adsr_dirty = 1;
    bool channel_dirty = 1;
    uint8_t midi_channel = 0;
    int8_t octave = 0;

    uint8_t filter_cutoff_msb = 64; // Default mid-range
    uint8_t filter_cutoff_lsb = 0;
    uint8_t filter_q_msb = 16; // Default lower Q
    uint8_t filter_q_lsb = 0;
    int8_t filter_type = 0; // 0=Off, 1=FIR, 2=Cheby
    bool filter_dirty = 1;
    bool main_dirty = 1;
    uint32_t waveform_animation_last_ms = 0;
    uint16_t waveform_animation_phase = 0;

    // midi state
    bool midi_in = false;             // midi into pico synth
    bool midi_out = false;            // switches to midi out
    bool switches_in = true;          // switches into pico synth
    bool sequencer_in = true;         // sequencer synth in
    bool sequencer_out = true;        // sequencer midi out
    bool midi_settings_dirty = false; // sequencer midi out
    //

    // sequencer state
    bool sequencer_settings_dirty = true;
    bool sequencer_playing = false;
    uint32_t max_tempo = 400;
    uint32_t display_tempo = 120;
    uint8_t display_current_step = 0;

    // sequencer edit state
    uint8_t current_sequencer_step = 0;
    bool auto_stepping_enabled = false;
    bool sequencer_dirty = true;

    // FM edit state
    uint8_t selected_operator = 0; // 0-1 for the two operators
    uint8_t fm_edit_mode = 0;      // 0=select op, 1=ADSR, 2=params
    bool fm_edit_dirty = true;
    uint8_t fm_param_index = 0; // 0=ratio, 1=feedback, 2=fm_depth, 3=wave_type
    bool karplus_edit_dirty = true;
    uint8_t karplus_last_note = 60;
    uint16_t karplus_last_delay_samples = 0;

    // choose state~
    bool chosen_dirty = true;
    int chosen_index = 1;

    // engine select state
    bool engine_select_dirty = true;
    int engine_select_index = 0;

    // sampler state
    bool sampler_dirty = true;
    int sample_index = 0;
    int sample_channel = 6;
    // WAV file list for sampler
    WavFileList wav_files;

    struct FxParams {
        int p1;
        int p2;
        int mix;
    };

    enum FXType {
        FX_DELAY = 0,
        FX_DISTORTION = 1,
        FX_REVERB = 2,
        FX_CHORUS = 3,
        FX_REVERB_SC = 4,
        FX_COUNT = Synth::FX_SLOT_COUNT
    };
    std::array<FxParams, FX_COUNT> fx_params = {{
        {250, 10000, 10000},  // Delay: time, feedback, mix
        {500, 200, 30000},  // Distortion: drive, threshold, mix
        {300, 500, 30000},   // Reverb: size, damp, mix
        {450, 32000, 32000},   // Chorus: rate, depth, mix
        {1000, 32000, 32000},  // RevSC: time, tone, mix
    }};
    bool fx_dirty = true;
    int current_fx = FX_DELAY;
    bool fx_enabled[FX_COUNT] = { true, false, false, false, false };

    struct AnalogSettings {
        bool enabled = false;
        uint8_t frequency_tenths_hz = 4;
        uint8_t dispersion_percent = 2;
    } analog_settings;

    struct AnalogOperatorOffsets {
        int wave_type = 0;
        int attack = 0;
        int decay = 0;
        int sustain = 0;
        int release = 0;
        int ratio = 0;
        int feedback = 0;
        int fm_depth = 0;
    };

    struct AnalogFmOffsets {
        std::array<AnalogOperatorOffsets, OP_PER_VOICE> ops;
    };

    struct AnalogKarplusOffsets {
        int impulse_type = 0;
        int filter_gain = 0;
        int decay = 0;
        int impulse_length = 0;
        int pick_position = 0;
        int dispersion = 0;
        int body_resonance = 0;
    };

    struct AnalogFxOffsets {
        int p1 = 0;
        int p2 = 0;
        int mix = 0;
    };

    bool analog_dirty = true;
    bool analog_reapply_pending = false;
    bool analog_offsets_initialized = false;
    bool analog_was_active = false;
    uint32_t analog_transition_start_ms = 0;
    std::array<Patch, 16> analog_patch_storage;
    std::array<KarplusPatch, 16> analog_karplus_patch_storage;
    std::array<FxParams, FX_COUNT> analog_fx_params{};
    std::array<AnalogFmOffsets, 16> analog_fm_source_offsets{};
    std::array<AnalogFmOffsets, 16> analog_fm_target_offsets{};
    std::array<AnalogKarplusOffsets, 16> analog_karplus_source_offsets{};
    std::array<AnalogKarplusOffsets, 16> analog_karplus_target_offsets{};
    std::array<AnalogFxOffsets, FX_COUNT> analog_fx_source_offsets{};
    std::array<AnalogFxOffsets, FX_COUNT> analog_fx_target_offsets{};

    // Generic FX control helper - forwards UI params to synth effects
    void set_fx_param(int p1, int p2, int mix);
    void set_fx_enabled(int fx_id, bool enabled);
    void mark_fm_patch_updated(uint8_t channel);
    void mark_karplus_patch_updated(uint8_t channel);
    void mark_fx_params_updated(int fx_id);
    void mark_all_fm_patches_updated();
    void mark_all_karplus_patches_updated();
    void mark_all_fx_params_updated();
    uint32_t analog_update_interval_ms() const;
    void randomize_analog_targets();
    void capture_current_analog_offsets(float progress);
    void apply_analog_variation(float progress);
    void restore_base_parameters();
    void update_analog_variation();
};

#endif // !UI_STATE_HPP

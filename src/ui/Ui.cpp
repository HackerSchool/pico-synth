#include "Ui.hpp"
#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sequencer.hpp"
#include "pico/time.h"

namespace {
constexpr int kEncoderDebounceThreshold = 1;
constexpr int kEncoderMediumThreshold = 3;
constexpr int kEncoderFastThreshold = 5;
constexpr int kPresetDetentThreshold = 4;
constexpr uint32_t kUiRandomSeed = 0xC0DE5EEDu;
constexpr int16_t kInvalidTrackedNote = -1;
constexpr int8_t kInvalidTrackedChannel = -1;
constexpr uint64_t kLfoPhaseScale = (1ull << 32);

constexpr std::array<LfoTarget, 17> kFmLfoTargets = {
    LfoTarget::Off,          LfoTarget::FmOp1Wave,
    LfoTarget::FmOp1Attack,  LfoTarget::FmOp1Decay,
    LfoTarget::FmOp1Sustain, LfoTarget::FmOp1Release,
    LfoTarget::FmOp1Ratio,   LfoTarget::FmOp1Feedback,
    LfoTarget::FmOp1FmDepth, LfoTarget::FmOp2Wave,
    LfoTarget::FmOp2Attack,  LfoTarget::FmOp2Decay,
    LfoTarget::FmOp2Sustain, LfoTarget::FmOp2Release,
    LfoTarget::FmOp2Ratio,   LfoTarget::FmOp2Feedback,
    LfoTarget::FmOp2FmDepth};
constexpr std::array<LfoTarget, 8> kKarplusLfoTargets = {
    LfoTarget::Off,                LfoTarget::KarplusImpulseType,
    LfoTarget::KarplusFilterGain,  LfoTarget::KarplusDecay,
    LfoTarget::KarplusImpulseLength, LfoTarget::KarplusPickPosition,
    LfoTarget::KarplusDispersion,  LfoTarget::KarplusBodyResonance};
constexpr std::array<LfoTarget, 6> kModalLfoTargets = {
    LfoTarget::Off,           LfoTarget::ModalStructure,
    LfoTarget::ModalBrightness, LfoTarget::ModalDamping,
    LfoTarget::ModalPosition, LfoTarget::ModalExciterType};

uint32_t ui_random_state = kUiRandomSeed;

inline int abs_int(int value) {
    return value < 0 ? -value : value;
}

uint32_t next_ui_random() {
    ui_random_state ^= ui_random_state << 13;
    ui_random_state ^= ui_random_state >> 17;
    ui_random_state ^= ui_random_state << 5;
    if (ui_random_state == 0) {
        ui_random_state = kUiRandomSeed;
    }
    return ui_random_state;
}

void stir_ui_random(uint32_t salt) {
    ui_random_state ^= salt + 0x9E3779B9u + (ui_random_state << 6) +
                       (ui_random_state >> 2);
    if (ui_random_state == 0) {
        ui_random_state = kUiRandomSeed;
    }
}

int random_range_inclusive(int min_value, int max_value) {
    if (max_value <= min_value) {
        return min_value;
    }

    const uint32_t span =
        static_cast<uint32_t>(max_value - min_value + 1);
    return min_value + static_cast<int>(next_ui_random() % span);
}

int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

uint8_t clamp_u8(int value, int min_value, int max_value) {
    return static_cast<uint8_t>(clamp_int(value, min_value, max_value));
}

uint16_t clamp_u16(int value, int min_value, int max_value) {
    return static_cast<uint16_t>(clamp_int(value, min_value, max_value));
}

float clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

uint8_t synth_filter_value_from_ui(int8_t filter_type) {
    switch (filter_type) {
    case 1:
        return 64;
    case 2:
        return 106;
    default:
        return 21;
    }
}

float filter_cutoff_from_14bit(std::uint16_t value_14bit) {
    const float normalized =
        static_cast<float>(value_14bit) / 16383.0f;
    return 40.0f + (normalized * 11960.0f);
}

float filter_q_from_14bit(std::uint16_t value_14bit) {
    const float normalized =
        static_cast<float>(value_14bit) / 16383.0f;
    return 0.5f + (normalized * 7.5f);
}

int lerp_int(int start_value, int end_value, float progress) {
    return start_value + static_cast<int>(
                             (end_value - start_value) * progress);
}

int round_to_int(float value) {
    return static_cast<int>(value + (value >= 0.0f ? 0.5f : -0.5f));
}

const std::array<int16_t, WAVE_TABLE_LEN> &lfo_wave_table(WaveType type) {
    switch (type) {
    case Square:
        return square_wave_table;
    case Triangle:
        return triangle_wave_table;
    case Sawtooth:
        return sawtooth_wave_table;
    case Sinc:
        return sinc_table;
    case Sine:
    default:
        return sine_wave_table;
    }
}

float lfo_wave_sample(WaveType type, uint32_t phase_q32) {
    const auto &table = lfo_wave_table(type);
    const uint32_t index =
        (phase_q32 >> (32 - WAVE_SHIFT)) & (WAVE_TABLE_LEN - 1);
    return static_cast<float>(table[index]) / 32767.0f;
}

int lfo_target_range(LfoTarget target) {
    switch (target) {
    case LfoTarget::FmOp1Wave:
    case LfoTarget::FmOp2Wave:
        return static_cast<int>(WaveType::Sinc);
    case LfoTarget::FmOp1Ratio:
    case LfoTarget::FmOp2Ratio:
        return 15;
    case LfoTarget::KarplusImpulseType:
        return 6;
    case LfoTarget::ModalExciterType:
        return 3;
    case LfoTarget::Off:
        return 0;
    default:
        return 127;
    }
}

void apply_fm_lfo_target(Patch &patch, LfoTarget target, int amount) {
    switch (target) {
    case LfoTarget::FmOp1Wave:
        patch.ops[0].wave_type = static_cast<WaveType>(clamp_int(
            static_cast<int>(patch.ops[0].wave_type) + amount, 0,
            static_cast<int>(WaveType::Sinc)));
        break;
    case LfoTarget::FmOp1Attack:
        patch.ops[0].attack =
            clamp_u8(static_cast<int>(patch.ops[0].attack) + amount, 0, 127);
        break;
    case LfoTarget::FmOp1Decay:
        patch.ops[0].decay =
            clamp_u8(static_cast<int>(patch.ops[0].decay) + amount, 0, 127);
        break;
    case LfoTarget::FmOp1Sustain:
        patch.ops[0].sustain =
            clamp_u8(static_cast<int>(patch.ops[0].sustain) + amount, 0, 127);
        break;
    case LfoTarget::FmOp1Release:
        patch.ops[0].release =
            clamp_u8(static_cast<int>(patch.ops[0].release) + amount, 0, 127);
        break;
    case LfoTarget::FmOp1Ratio:
        patch.ops[0].ratio =
            clamp_u16(static_cast<int>(patch.ops[0].ratio) + amount, 1, 16);
        break;
    case LfoTarget::FmOp1Feedback:
        patch.ops[0].feedback =
            clamp_u16(static_cast<int>(patch.ops[0].feedback) + amount, 0, 127);
        break;
    case LfoTarget::FmOp1FmDepth:
        patch.ops[0].fm_depth =
            clamp_u16(static_cast<int>(patch.ops[0].fm_depth) + amount, 0, 127);
        break;
    case LfoTarget::FmOp2Wave:
        patch.ops[1].wave_type = static_cast<WaveType>(clamp_int(
            static_cast<int>(patch.ops[1].wave_type) + amount, 0,
            static_cast<int>(WaveType::Sinc)));
        break;
    case LfoTarget::FmOp2Attack:
        patch.ops[1].attack =
            clamp_u8(static_cast<int>(patch.ops[1].attack) + amount, 0, 127);
        break;
    case LfoTarget::FmOp2Decay:
        patch.ops[1].decay =
            clamp_u8(static_cast<int>(patch.ops[1].decay) + amount, 0, 127);
        break;
    case LfoTarget::FmOp2Sustain:
        patch.ops[1].sustain =
            clamp_u8(static_cast<int>(patch.ops[1].sustain) + amount, 0, 127);
        break;
    case LfoTarget::FmOp2Release:
        patch.ops[1].release =
            clamp_u8(static_cast<int>(patch.ops[1].release) + amount, 0, 127);
        break;
    case LfoTarget::FmOp2Ratio:
        patch.ops[1].ratio =
            clamp_u16(static_cast<int>(patch.ops[1].ratio) + amount, 1, 16);
        break;
    case LfoTarget::FmOp2Feedback:
        patch.ops[1].feedback =
            clamp_u16(static_cast<int>(patch.ops[1].feedback) + amount, 0, 127);
        break;
    case LfoTarget::FmOp2FmDepth:
        patch.ops[1].fm_depth =
            clamp_u16(static_cast<int>(patch.ops[1].fm_depth) + amount, 0, 127);
        break;
    default:
        break;
    }
}

void apply_karplus_lfo_target(KarplusPatch &patch, LfoTarget target,
                              int amount) {
    switch (target) {
    case LfoTarget::KarplusImpulseType:
        patch.impulse_type = static_cast<KarplusImpulseType>(clamp_int(
            static_cast<int>(patch.impulse_type) + amount, 0, 6));
        break;
    case LfoTarget::KarplusFilterGain:
        patch.filter_gain =
            clamp_u8(static_cast<int>(patch.filter_gain) + amount, 0, 127);
        break;
    case LfoTarget::KarplusDecay:
        patch.decay = clamp_u8(static_cast<int>(patch.decay) + amount, 0, 127);
        break;
    case LfoTarget::KarplusImpulseLength:
        patch.impulse_length =
            clamp_u8(static_cast<int>(patch.impulse_length) + amount, 0, 127);
        break;
    case LfoTarget::KarplusPickPosition:
        patch.pick_position =
            clamp_u8(static_cast<int>(patch.pick_position) + amount, 0, 127);
        break;
    case LfoTarget::KarplusDispersion:
        patch.dispersion =
            clamp_u8(static_cast<int>(patch.dispersion) + amount, 0, 127);
        break;
    case LfoTarget::KarplusBodyResonance:
        patch.body_resonance =
            clamp_u8(static_cast<int>(patch.body_resonance) + amount, 0, 127);
        break;
    default:
        break;
    }
}

void apply_modal_lfo_target(ModalPatch &patch, LfoTarget target, int amount) {
    switch (target) {
    case LfoTarget::ModalStructure:
        patch.structure =
            clamp_u8(static_cast<int>(patch.structure) + amount, 0, 127);
        break;
    case LfoTarget::ModalBrightness:
        patch.brightness =
            clamp_u8(static_cast<int>(patch.brightness) + amount, 0, 127);
        break;
    case LfoTarget::ModalDamping:
        patch.damping =
            clamp_u8(static_cast<int>(patch.damping) + amount, 0, 127);
        break;
    case LfoTarget::ModalPosition:
        patch.position =
            clamp_u8(static_cast<int>(patch.position) + amount, 0, 127);
        break;
    case LfoTarget::ModalExciterType:
        patch.exciter_type = static_cast<ModalExciterType>(clamp_int(
            static_cast<int>(patch.exciter_type) + amount, 0, 3));
        break;
    default:
        break;
    }
}

int dispersion_span(int total_range, uint16_t dispersion_hundredths_percent,
                    bool allow_minimum_step = true) {
    (void)allow_minimum_step;
    if (dispersion_hundredths_percent == 0 || total_range <= 0) return 0;

    int span = (total_range * static_cast<int>(dispersion_hundredths_percent)) / 10000;
    if (span > total_range) span = total_range;
    return span;
}
} // namespace

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

UiHandler::UiHandler(HardwareManager &hw, MidiHandler &midi_handler,
                     Sequencer &seq, Sampler &sampler, Synth &synth)
    : hw(hw), midi(midi_handler), seq(seq), sampler(sampler), synth(synth) {
    const auto register_state = [this](UiState state,
                                       EncoderHandler handle_encoders,
                                       SwitchHandler handle_switches,
                                       DisplayHandler handle_display) {
        ui_dispatch_table[state] = {handle_encoders, handle_switches,
                                    handle_display};
    };

    karplus_last_delay_samples =
        KarplusVoice::tuned_delay_samples_for_note(karplus_last_note);
    tracked_switch_notes.fill(kInvalidTrackedNote);
    tracked_switch_channels.fill(kInvalidTrackedChannel);

    wav_files = sampler.list_wav_files(true); // recursive
    wav_files.print_files();

    register_state(UI_STATE_MAIN, main_handle_encoders, main_handle_switches,
                   main_update_display);
    register_state(UI_STATE_FM_EDIT, fm_edit_handle_encoders,
                   fm_edit_handle_switches, fm_edit_update_display);
    register_state(UI_STATE_KARPLUS_EDIT, karplus_edit_handle_encoders,
                   karplus_edit_handle_switches,
                   karplus_edit_update_display);
    register_state(UI_STATE_MODAL_EDIT, modal_edit_handle_encoders,
                   modal_edit_handle_switches, modal_edit_update_display);
    register_state(UI_STATE_FX_EDIT, fx_handle_encoders, main_handle_switches,
                   fx_update_display);
    register_state(UI_STATE_ANALOG, analog_handle_encoders,
                   main_handle_switches, analog_update_display);
    register_state(UI_STATE_LFO, lfo_handle_encoders, lfo_handle_switches,
                   lfo_update_display);
    register_state(UI_STATE_MIDI_SETTINGS, midi_handle_encoders,
                   main_handle_switches, midi_update_display);
    register_state(UI_STATE_SEQUENCER, sequencer_handle_encoders,
                   main_handle_switches, sequencer_update_display);
    register_state(UI_STATE_SEQUENCER_EDIT,
                   sequencer_note_edit_handle_encoders,
                   sequencer_note_edit_handle_switches,
                   sequencer_note_edit_update_display);
    register_state(UI_STATE_CHOOSE, choose_handle_encoders,
                   main_handle_switches, choose_update_display);
    register_state(UI_STATE_ENGINE_SELECT, engine_select_handle_encoders,
                   main_handle_switches, engine_select_update_display);
    register_state(UI_STATE_SAMPLER, sampler_handle_encoders,
                   sampler_handle_switches, sampler_update_display);

    for (int fx_id = 0; fx_id < FX_COUNT; ++fx_id) {
        synth.set_fx_params(fx_id,
                            fx_params[fx_id].p1,
                            fx_params[fx_id].p2,
                            fx_params[fx_id].mix);
        synth.enable_fx(fx_id, fx_enabled[fx_id]);
    }

    preset_manager.load_factory_presets(sampler);
    prev_switches = hw.curr_switches;
}

void UiHandler::update() {
    preprocess_preset_browse();
    UiDispatchEntry ui_dispatch_entry = ui_dispatch_table[ui_state];
    ui_dispatch_entry.handle_encoders(*this);
    tud_task(); // Service USB
    ui_dispatch_entry.handle_switches(*this);
    tud_task(); // Service USB
    update_analog_variation();
    update_lfo_modulation();
    tud_task(); // Service USB
    ui_dispatch_entry.handle_display(*this);
    update_preset_browse_display();
    tud_task(); // Service USB
}

bool UiHandler::encoder_moved(int32_t delta) {
    return abs_int(static_cast<int>(delta)) > kEncoderDebounceThreshold;
}

void UiHandler::send_note_message(bool note_on, uint8_t channel, uint8_t note,
                                  uint8_t velocity) {
    uint8_t packet[4];
    packet[0] = note_on ? 0x09 : 0x08;
    packet[1] =
        static_cast<uint8_t>((note_on ? 0x90 : 0x80) | (channel & 0x0F));
    packet[2] = note;
    packet[3] = velocity;
    midi.midi_receive_note(packet);
}

void UiHandler::track_switch_note_on(int key_index, uint8_t channel, uint8_t note,
                                     uint8_t velocity) {
    if (key_index < 0 ||
        key_index >= static_cast<int>(tracked_switch_notes.size())) {
        return;
    }

    release_tracked_switch_note(key_index, velocity);
    tracked_switch_notes[key_index] = note;
    tracked_switch_channels[key_index] = static_cast<int8_t>(channel);
    send_note_message(true, channel, note, velocity);
}

void UiHandler::release_tracked_switch_note(int key_index, uint8_t velocity) {
    if (key_index < 0 ||
        key_index >= static_cast<int>(tracked_switch_notes.size())) {
        return;
    }

    const int16_t note = tracked_switch_notes[key_index];
    const int8_t channel = tracked_switch_channels[key_index];
    if (note == kInvalidTrackedNote || channel == kInvalidTrackedChannel) {
        return;
    }

    send_note_message(false, static_cast<uint8_t>(channel),
                      static_cast<uint8_t>(note), velocity);
    tracked_switch_notes[key_index] = kInvalidTrackedNote;
    tracked_switch_channels[key_index] = kInvalidTrackedChannel;
}

void UiHandler::release_all_tracked_switch_notes(uint8_t velocity) {
    for (int i = 0; i < static_cast<int>(tracked_switch_notes.size()); ++i) {
        release_tracked_switch_note(i, velocity);
    }
    prev_switches = hw.curr_switches;
}

int UiHandler::encoder_velocity_step(int32_t delta, int slow_step, int medium_step,
                                     int fast_step) {
    const int magnitude = abs_int(static_cast<int>(delta));

    if (magnitude >= kEncoderFastThreshold) return fast_step;
    if (magnitude >= kEncoderMediumThreshold) return medium_step;
    return slow_step;
}

int UiHandler::encoder_velocity_delta(int32_t delta, int slow_step, int medium_step,
                                      int fast_step) {
    if (delta > 0) return encoder_velocity_step(delta, slow_step, medium_step, fast_step);
    if (delta < 0) return -encoder_velocity_step(delta, slow_step, medium_step, fast_step);
    return 0;
}

int UiHandler::analog_value_step(int current_value_hundredths) {
    if (current_value_hundredths < 10) return 1;
    if (current_value_hundredths < 100) return 10;
    if (current_value_hundredths < 1000) return 100;
    if (current_value_hundredths < 10000) return 1000;
    return 10000;
}

int UiHandler::analog_encoder_delta(int32_t delta, int current_value_hundredths) {
    if (delta == 0) return 0;

    if (delta > 0) {
        return analog_value_step(current_value_hundredths);
    }

    const int previous_value =
        current_value_hundredths > 0 ? current_value_hundredths - 1 : 0;
    return -analog_value_step(previous_value);
}

void UiHandler::invalidate_all_displays() {
    main_dirty = true;
    channel_dirty = true;
    adsr_dirty = true;
    filter_dirty = true;
    midi_settings_dirty = true;
    sequencer_settings_dirty = true;
    sequencer_dirty = true;
    chosen_dirty = true;
    engine_select_dirty = true;
    sampler_dirty = true;
    fm_edit_dirty = true;
    karplus_edit_dirty = true;
    modal_edit_dirty = true;
    lfo_dirty = true;
    fx_dirty = true;
    analog_dirty = true;
}

void UiHandler::begin_randomizer_hold() {
    if (randomizer_hold_active) {
        return;
    }

    randomizer_hold_active = true;
    preset_browse_engaged = false;
    preset_browse_accumulator = 0;
    preset_browse_index = preset_loaded_index >= 0 ? preset_loaded_index : 0;
}

bool UiHandler::apply_factory_preset(int preset_index) {
    PresetState state{};
    if (!preset_manager.load_factory_preset(preset_index, sampler, state)) {
        return false;
    }

    apply_preset_state(state, preset_index);
    return true;
}

void UiHandler::apply_preset_state(const PresetState &state, int preset_index) {
    release_all_tracked_switch_notes();
    sampler.stop_all();
    synth.reset_runtime_state();
    synth.set_engine(state.engine);

    switch (state.engine) {
    case SynthEngine::KarplusStrong:
        for (std::size_t channel = 0; channel < synth.karplus_patch_storage.size();
             ++channel) {
            synth.karplus_patch_storage[channel] = state.karplus_patch;
            synth.active_karplus_patch[channel].store(
                &synth.karplus_patch_storage[channel], std::memory_order_release);
            synth.karplus_patch_dirty_flags.set(channel);
        }
        break;
    case SynthEngine::Modal:
        for (std::size_t channel = 0; channel < synth.modal_patch_storage.size();
             ++channel) {
            synth.modal_patch_storage[channel] = state.modal_patch;
            synth.active_modal_patch[channel].store(
                &synth.modal_patch_storage[channel], std::memory_order_release);
            synth.modal_patch_dirty_flags.set(channel);
        }
        break;
    case SynthEngine::FM:
    default:
        for (std::size_t channel = 0; channel < synth.patch_storage.size(); ++channel) {
            synth.patch_storage[channel] = state.fm_patch;
            synth.active_patch[channel].store(
                &synth.patch_storage[channel], std::memory_order_release);
            synth.patch_dirty_flags.set(channel);
        }
        break;
    }

    for (int fx_id = 0; fx_id < FX_COUNT; ++fx_id) {
        fx_params[fx_id].p1 = state.fx[fx_id].p1;
        fx_params[fx_id].p2 = state.fx[fx_id].p2;
        fx_params[fx_id].mix = state.fx[fx_id].mix;
        fx_enabled[fx_id] = state.fx[fx_id].enabled;
        synth.set_fx_params(fx_id, fx_params[fx_id].p1, fx_params[fx_id].p2,
                            fx_params[fx_id].mix);
        synth.enable_fx(fx_id, fx_enabled[fx_id]);
    }

    analog_settings.enabled = state.analog_enabled;
    analog_settings.frequency_hundredths_hz =
        state.analog_frequency_hundredths_hz;
    analog_settings.dispersion_hundredths_percent =
        state.analog_dispersion_hundredths_percent;
    analog_offsets_initialized = false;
    analog_reapply_pending = true;
    analog_was_active = false;
    analog_transition_start_ms = to_ms_since_boot(get_absolute_time());

    filter_type = static_cast<int8_t>(state.filter_type);
    filter_cutoff_msb = state.filter_cutoff_msb;
    filter_cutoff_lsb = state.filter_cutoff_lsb;
    filter_q_msb = state.filter_q_msb;
    filter_q_lsb = state.filter_q_lsb;

    synth.set_filter_type(synth_filter_value_from_ui(filter_type));
    const std::uint16_t cutoff_14bit =
        static_cast<std::uint16_t>((filter_cutoff_msb << 7) | filter_cutoff_lsb);
    const std::uint16_t q_14bit =
        static_cast<std::uint16_t>((filter_q_msb << 7) | filter_q_lsb);
    synth.set_filter_cutoff(filter_cutoff_from_14bit(cutoff_14bit),
                            filter_q_from_14bit(q_14bit));

    engine_select_index = static_cast<int>(state.engine);
    preset_loaded_index = preset_index;
    preset_browse_index = preset_index;

    if (ui_state == UI_STATE_FM_EDIT || ui_state == UI_STATE_KARPLUS_EDIT ||
        ui_state == UI_STATE_MODAL_EDIT || ui_state == UI_STATE_ENGINE_SELECT) {
        ui_state = UI_STATE_MAIN;
    }

    invalidate_all_displays();
}

void UiHandler::end_randomizer_hold() {
    if (!randomizer_hold_active) {
        return;
    }

    randomizer_hold_active = false;
    preset_browse_accumulator = 0;

    const bool should_apply =
        preset_browse_engaged && preset_manager.get_factory_preset_count() > 0;
    preset_browse_engaged = false;

    if (should_apply) {
        if (!apply_factory_preset(preset_browse_index)) {
            invalidate_all_displays();
        }
        return;
    }

    randomize_current_engine_patch(*this);
}

void UiHandler::preprocess_preset_browse() {
    const bool randomizer_held = (hw.curr_switches & (1u << 3)) != 0;
    if (!randomizer_held) {
        return;
    }

    begin_randomizer_hold();

    Encoder &preset_encoder = hw.encoders[3];
    const int32_t delta = preset_encoder.delta;
    preset_encoder.delta = 0;

    if (!encoder_moved(delta) || preset_manager.get_factory_preset_count() <= 0) {
        return;
    }

    preset_browse_accumulator += delta;
    if (abs_int(static_cast<int>(preset_browse_accumulator)) <
        kPresetDetentThreshold) {
        return;
    }

    const int dir =
        encoder_velocity_delta(preset_browse_accumulator, 1, 1, 1);
    preset_browse_accumulator = 0;
    if (dir == 0) {
        return;
    }

    preset_browse_index =
        preset_manager.wrap_factory_preset_index(preset_browse_index + dir);
    preset_browse_engaged = true;
}

void UiHandler::update_preset_browse_display() {
    if (!randomizer_hold_active || !preset_browse_engaged) {
        return;
    }

    const PresetManager::Metadata *metadata =
        preset_manager.get_factory_preset(preset_browse_index);
    const engine_bitmaps::Asset *asset =
        preset_manager.get_factory_preset_asset(preset_browse_index);
    if (metadata == nullptr || asset == nullptr) {
        return;
    }

    hw.draw_bitmap_select_menu(asset->data, asset->size, metadata->title);
    hw.display_show();
}

bool UiHandler::preset_browse_overlay_active() const {
    return randomizer_hold_active && preset_browse_engaged;
}

void UiHandler::randomize_current_engine_patch(UiHandler &self) {
    stir_ui_random((static_cast<uint32_t>(self.hw.curr_switches) << 16) ^
                   static_cast<uint32_t>(self.midi_channel) ^
                   (static_cast<uint32_t>(self.octave & 0xFF) << 8) ^
                   (static_cast<uint32_t>(self.karplus_last_note) << 8) ^
                   static_cast<uint32_t>(self.modal_last_note));

    if (self.synth.get_engine() == SynthEngine::KarplusStrong) {
        randomize_karplus_patch(self);
        return;
    }

    if (self.synth.get_engine() == SynthEngine::Modal) {
        randomize_modal_patch(self);
        return;
    }

    randomize_fm_patch(self);
}

void UiHandler::randomize_fm_patch(UiHandler &self) {
    Patch &patch = self.synth.patch_storage[self.midi_channel];

    patch.algorithm = 0;
    patch.volume = static_cast<uint8_t>(random_range_inclusive(92, 127));
    patch.pan = 64;

    patch.ops[0].wave_type =
        static_cast<WaveType>(random_range_inclusive(0, 4));
    patch.ops[0].attack = static_cast<uint8_t>(random_range_inclusive(0, 32));
    patch.ops[0].decay = static_cast<uint8_t>(random_range_inclusive(8, 110));
    patch.ops[0].sustain =
        static_cast<uint8_t>(random_range_inclusive(28, 127));
    patch.ops[0].release =
        static_cast<uint8_t>(random_range_inclusive(8, 100));
    patch.ops[0].ratio = static_cast<uint16_t>(random_range_inclusive(1, 8));
    patch.ops[0].feedback =
        static_cast<uint16_t>(random_range_inclusive(0, 96));
    patch.ops[0].fm_depth = 0;

    patch.ops[1].wave_type =
        static_cast<WaveType>(random_range_inclusive(0, 4));
    patch.ops[1].attack = static_cast<uint8_t>(random_range_inclusive(0, 24));
    patch.ops[1].decay = static_cast<uint8_t>(random_range_inclusive(6, 110));
    patch.ops[1].sustain =
        static_cast<uint8_t>(random_range_inclusive(0, 110));
    patch.ops[1].release =
        static_cast<uint8_t>(random_range_inclusive(8, 100));
    patch.ops[1].ratio = static_cast<uint16_t>(random_range_inclusive(1, 16));
    patch.ops[1].feedback =
        static_cast<uint16_t>(random_range_inclusive(0, 127));
    patch.ops[1].fm_depth =
        static_cast<uint16_t>(random_range_inclusive(8, 127));

    self.mark_fm_patch_updated(self.midi_channel);

    self.main_dirty = true;
    self.channel_dirty = true;
    self.adsr_dirty = true;
    self.filter_dirty = true;
    self.fm_edit_dirty = true;

}

void UiHandler::randomize_karplus_patch(UiHandler &self) {
    KarplusPatch &patch = self.synth.karplus_patch_storage[self.midi_channel];

    patch.impulse_type = static_cast<KarplusImpulseType>(
        random_range_inclusive(0, 6));
    patch.filter_gain = static_cast<uint8_t>(random_range_inclusive(28, 127));
    patch.decay = static_cast<uint8_t>(random_range_inclusive(88, 127));
    patch.impulse_length =
        static_cast<uint8_t>(random_range_inclusive(6, 127));
    patch.pick_position =
        static_cast<uint8_t>(random_range_inclusive(0, 127));
    patch.dispersion = static_cast<uint8_t>(random_range_inclusive(0, 127));
    patch.body_resonance =
        static_cast<uint8_t>(random_range_inclusive(0, 127));

    self.mark_karplus_patch_updated(self.midi_channel);

    self.main_dirty = true;
    self.channel_dirty = true;
    self.karplus_edit_dirty = true;

}

void UiHandler::randomize_modal_patch(UiHandler &self) {
    ModalPatch &patch = self.synth.modal_patch_storage[self.midi_channel];

    patch.structure = static_cast<uint8_t>(random_range_inclusive(0, 127));
    patch.brightness = static_cast<uint8_t>(random_range_inclusive(28, 127));
    patch.damping = static_cast<uint8_t>(random_range_inclusive(48, 127));
    patch.position = static_cast<uint8_t>(random_range_inclusive(8, 118));
    patch.exciter_type = static_cast<ModalExciterType>(
        random_range_inclusive(0, 3));

    self.mark_modal_patch_updated(self.midi_channel);

    self.main_dirty = true;
    self.channel_dirty = true;
    self.modal_edit_dirty = true;

}

void UiHandler::set_delay_param(int delay_ms, int feedback, int mix){
    synth.set_fx_params(FX_DELAY, delay_ms, feedback, mix);
}

void UiHandler::set_fx_param(int p1, int p2, int mix){
    // Forward generic FX parameters to synth based on currently selected FX
    synth.set_fx_params(current_fx, p1, p2, mix);
}

void UiHandler::set_fx_enabled(int fx_id, bool enabled){
    if (fx_id < 0 || fx_id >= FX_COUNT) return;
    fx_enabled[fx_id] = enabled;
    synth.enable_fx(fx_id, enabled);
}

void UiHandler::mark_fm_patch_updated(uint8_t channel) {
    if (channel >= synth.patch_storage.size()) return;

    if (analog_settings.enabled) {
        analog_reapply_pending = true;
    } else {
        synth.active_patch[channel].store(&synth.patch_storage[channel],
                                          std::memory_order_release);
    }

    synth.patch_dirty_flags.set(channel);
}

void UiHandler::mark_karplus_patch_updated(uint8_t channel) {
    if (channel >= synth.karplus_patch_storage.size()) return;

    if (analog_settings.enabled) {
        analog_reapply_pending = true;
    } else {
        synth.active_karplus_patch[channel].store(&synth.karplus_patch_storage[channel],
                                                  std::memory_order_release);
    }

    synth.karplus_patch_dirty_flags.set(channel);
}

void UiHandler::mark_modal_patch_updated(uint8_t channel) {
    if (channel >= synth.modal_patch_storage.size()) return;

    if (analog_settings.enabled) {
        analog_reapply_pending = true;
    } else {
        synth.active_modal_patch[channel].store(&synth.modal_patch_storage[channel],
                                                std::memory_order_release);
    }

    synth.modal_patch_dirty_flags.set(channel);
}

void UiHandler::mark_fx_params_updated(int fx_id) {
    if (fx_id < 0 || fx_id >= FX_COUNT) return;

    if (analog_settings.enabled) {
        analog_reapply_pending = true;
        return;
    }

    synth.set_fx_params(fx_id, fx_params[fx_id].p1, fx_params[fx_id].p2,
                        fx_params[fx_id].mix);
}

void UiHandler::mark_all_fm_patches_updated() {
    for (uint8_t channel = 0; channel < synth.patch_storage.size(); ++channel) {
        mark_fm_patch_updated(channel);
    }
}

void UiHandler::mark_all_karplus_patches_updated() {
    for (uint8_t channel = 0; channel < synth.karplus_patch_storage.size(); ++channel) {
        mark_karplus_patch_updated(channel);
    }
}

void UiHandler::mark_all_modal_patches_updated() {
    for (uint8_t channel = 0; channel < synth.modal_patch_storage.size(); ++channel) {
        mark_modal_patch_updated(channel);
    }
}

void UiHandler::mark_all_fx_params_updated() {
    for (int fx_id = 0; fx_id < FX_COUNT; ++fx_id) {
        mark_fx_params_updated(fx_id);
    }
}

uint32_t UiHandler::analog_update_interval_ms() const {
    if (analog_settings.frequency_hundredths_hz == 0) {
        return 0xFFFFFFFFu;
    }

    return 100000u /
           static_cast<uint32_t>(analog_settings.frequency_hundredths_hz);
}

void UiHandler::randomize_analog_targets() {
    const auto random_offset = [&](int range, bool allow_minimum_step = true) {
        const int span = dispersion_span(
            range, analog_settings.dispersion_hundredths_percent,
            allow_minimum_step);
        return random_range_inclusive(-span, span);
    };

    for (size_t ch = 0; ch < analog_fm_target_offsets.size(); ++ch) {
        AnalogFmOffsets &fm_offsets = analog_fm_target_offsets[ch];
        for (size_t op_index = 0; op_index < fm_offsets.ops.size(); ++op_index) {
            AnalogOperatorOffsets &op_offsets = fm_offsets.ops[op_index];
            op_offsets.wave_type = 0;
            op_offsets.attack = random_offset(127);
            op_offsets.decay = random_offset(127);
            op_offsets.sustain = random_offset(127);
            op_offsets.release = random_offset(127);
            op_offsets.ratio = 0;
            op_offsets.feedback = random_offset(127);
            op_offsets.fm_depth = random_offset(127);
        }

        AnalogKarplusOffsets &karplus_offsets = analog_karplus_target_offsets[ch];
        karplus_offsets.impulse_type = 0;
        karplus_offsets.filter_gain = random_offset(127);
        karplus_offsets.decay = random_offset(127);
        karplus_offsets.impulse_length = random_offset(127);
        karplus_offsets.pick_position = random_offset(127);
        karplus_offsets.dispersion = random_offset(127);
        karplus_offsets.body_resonance = random_offset(127);

        AnalogModalOffsets &modal_offsets = analog_modal_target_offsets[ch];
        modal_offsets.structure = random_offset(127);
        modal_offsets.brightness = random_offset(127);
        modal_offsets.damping = random_offset(127);
        modal_offsets.position = random_offset(127);
        modal_offsets.exciter_type = 0;
    }

    for (size_t fx_id = 0; fx_id < analog_fx_target_offsets.size(); ++fx_id) {
        AnalogFxOffsets &fx_offsets = analog_fx_target_offsets[fx_id];
        fx_offsets.p1 = 0;
        fx_offsets.p2 = 0;
        fx_offsets.mix = 0;
    }
}

void UiHandler::capture_current_analog_offsets(float progress) {
    const float clamped_progress = clamp_float(progress, 0.0f, 1.0f);
    const auto interpolate = [clamped_progress](int current, int target) {
        return lerp_int(current, target, clamped_progress);
    };
    const auto capture_operator_offsets =
        [&](AnalogOperatorOffsets &source,
            const AnalogOperatorOffsets &target) {
            source.wave_type = interpolate(source.wave_type, target.wave_type);
            source.attack = interpolate(source.attack, target.attack);
            source.decay = interpolate(source.decay, target.decay);
            source.sustain = interpolate(source.sustain, target.sustain);
            source.release = interpolate(source.release, target.release);
            source.ratio = interpolate(source.ratio, target.ratio);
            source.feedback = interpolate(source.feedback, target.feedback);
            source.fm_depth = interpolate(source.fm_depth, target.fm_depth);
        };
    const auto capture_karplus_offsets =
        [&](AnalogKarplusOffsets &source,
            const AnalogKarplusOffsets &target) {
            source.impulse_type =
                interpolate(source.impulse_type, target.impulse_type);
            source.filter_gain =
                interpolate(source.filter_gain, target.filter_gain);
            source.decay = interpolate(source.decay, target.decay);
            source.impulse_length =
                interpolate(source.impulse_length, target.impulse_length);
            source.pick_position =
                interpolate(source.pick_position, target.pick_position);
            source.dispersion =
                interpolate(source.dispersion, target.dispersion);
            source.body_resonance =
                interpolate(source.body_resonance, target.body_resonance);
        };
    const auto capture_modal_offsets =
        [&](AnalogModalOffsets &source, const AnalogModalOffsets &target) {
            source.structure = interpolate(source.structure, target.structure);
            source.brightness =
                interpolate(source.brightness, target.brightness);
            source.damping = interpolate(source.damping, target.damping);
            source.position = interpolate(source.position, target.position);
            source.exciter_type =
                interpolate(source.exciter_type, target.exciter_type);
        };
    const auto capture_fx_offsets = [&](AnalogFxOffsets &source,
                                        const AnalogFxOffsets &target) {
        source.p1 = interpolate(source.p1, target.p1);
        source.p2 = interpolate(source.p2, target.p2);
        source.mix = interpolate(source.mix, target.mix);
    };

    for (size_t ch = 0; ch < analog_fm_source_offsets.size(); ++ch) {
        AnalogFmOffsets &source = analog_fm_source_offsets[ch];
        const AnalogFmOffsets &target = analog_fm_target_offsets[ch];

        for (size_t op_index = 0; op_index < source.ops.size(); ++op_index) {
            capture_operator_offsets(source.ops[op_index], target.ops[op_index]);
        }
    }

    for (size_t ch = 0; ch < analog_karplus_source_offsets.size(); ++ch) {
        capture_karplus_offsets(analog_karplus_source_offsets[ch],
                                analog_karplus_target_offsets[ch]);
    }

    for (size_t ch = 0; ch < analog_modal_source_offsets.size(); ++ch) {
        capture_modal_offsets(analog_modal_source_offsets[ch],
                              analog_modal_target_offsets[ch]);
    }

    for (size_t fx_id = 0; fx_id < analog_fx_source_offsets.size(); ++fx_id) {
        capture_fx_offsets(analog_fx_source_offsets[fx_id],
                           analog_fx_target_offsets[fx_id]);
    }
}

void UiHandler::apply_analog_variation(float progress) {
    const int max_wave_type = static_cast<int>(WaveType::Sinc);
    const float clamped_progress = clamp_float(progress, 0.0f, 1.0f);
    const auto interpolate = [clamped_progress](int source, int target) {
        return lerp_int(source, target, clamped_progress);
    };
    const auto apply_operator_offsets =
        [&](OperatorParams &effective_op, const AnalogOperatorOffsets &source,
            const AnalogOperatorOffsets &target) {
            effective_op.wave_type = static_cast<WaveType>(clamp_int(
                static_cast<int>(effective_op.wave_type) +
                    interpolate(source.wave_type, target.wave_type),
                0, max_wave_type));
            effective_op.attack = clamp_u8(
                static_cast<int>(effective_op.attack) +
                    interpolate(source.attack, target.attack),
                0, 127);
            effective_op.decay = clamp_u8(
                static_cast<int>(effective_op.decay) +
                    interpolate(source.decay, target.decay),
                0, 127);
            effective_op.sustain = clamp_u8(
                static_cast<int>(effective_op.sustain) +
                    interpolate(source.sustain, target.sustain),
                0, 127);
            effective_op.release = clamp_u8(
                static_cast<int>(effective_op.release) +
                    interpolate(source.release, target.release),
                0, 127);
            effective_op.ratio = clamp_u16(
                static_cast<int>(effective_op.ratio) +
                    interpolate(source.ratio, target.ratio),
                1, 16);
            effective_op.feedback = clamp_u16(
                static_cast<int>(effective_op.feedback) +
                    interpolate(source.feedback, target.feedback),
                0, 127);
            effective_op.fm_depth = clamp_u16(
                static_cast<int>(effective_op.fm_depth) +
                    interpolate(source.fm_depth, target.fm_depth),
                0, 127);
        };
    const auto apply_karplus_offsets =
        [&](KarplusPatch &effective_patch, const AnalogKarplusOffsets &source,
            const AnalogKarplusOffsets &target) {
            effective_patch.impulse_type =
                static_cast<KarplusImpulseType>(clamp_int(
                    static_cast<int>(effective_patch.impulse_type) +
                        interpolate(source.impulse_type, target.impulse_type),
                    0, 6));
            effective_patch.filter_gain = clamp_u8(
                static_cast<int>(effective_patch.filter_gain) +
                    interpolate(source.filter_gain, target.filter_gain),
                0, 127);
            effective_patch.decay = clamp_u8(
                static_cast<int>(effective_patch.decay) +
                    interpolate(source.decay, target.decay),
                0, 127);
            effective_patch.impulse_length = clamp_u8(
                static_cast<int>(effective_patch.impulse_length) +
                    interpolate(source.impulse_length, target.impulse_length),
                0, 127);
            effective_patch.pick_position = clamp_u8(
                static_cast<int>(effective_patch.pick_position) +
                    interpolate(source.pick_position, target.pick_position),
                0, 127);
            effective_patch.dispersion = clamp_u8(
                static_cast<int>(effective_patch.dispersion) +
                    interpolate(source.dispersion, target.dispersion),
                0, 127);
            effective_patch.body_resonance = clamp_u8(
                static_cast<int>(effective_patch.body_resonance) +
                    interpolate(source.body_resonance, target.body_resonance),
                0, 127);
        };
    const auto apply_modal_offsets =
        [&](ModalPatch &effective_patch, const AnalogModalOffsets &source,
            const AnalogModalOffsets &target) {
            effective_patch.structure = clamp_u8(
                static_cast<int>(effective_patch.structure) +
                    interpolate(source.structure, target.structure),
                0, 127);
            effective_patch.brightness = clamp_u8(
                static_cast<int>(effective_patch.brightness) +
                    interpolate(source.brightness, target.brightness),
                0, 127);
            effective_patch.damping = clamp_u8(
                static_cast<int>(effective_patch.damping) +
                    interpolate(source.damping, target.damping),
                0, 127);
            effective_patch.position = clamp_u8(
                static_cast<int>(effective_patch.position) +
                    interpolate(source.position, target.position),
                0, 127);
            effective_patch.exciter_type =
                static_cast<ModalExciterType>(clamp_int(
                    static_cast<int>(effective_patch.exciter_type) +
                        interpolate(source.exciter_type, target.exciter_type),
                    0, 3));
        };

    for (size_t ch = 0; ch < analog_patch_storage.size(); ++ch) {
        analog_patch_storage[ch] = synth.patch_storage[ch];
        Patch &effective_patch = analog_patch_storage[ch];
        const AnalogFmOffsets &source_offsets = analog_fm_source_offsets[ch];
        const AnalogFmOffsets &target_offsets = analog_fm_target_offsets[ch];

        for (size_t op_index = 0; op_index < source_offsets.ops.size(); ++op_index) {
            apply_operator_offsets(effective_patch.ops[op_index],
                                   source_offsets.ops[op_index],
                                   target_offsets.ops[op_index]);
        }

        synth.active_patch[ch].store(&analog_patch_storage[ch],
                                     std::memory_order_release);
        synth.patch_dirty_flags.set(ch);
    }

    for (size_t ch = 0; ch < analog_karplus_patch_storage.size(); ++ch) {
        analog_karplus_patch_storage[ch] = synth.karplus_patch_storage[ch];
        KarplusPatch &effective_patch = analog_karplus_patch_storage[ch];
        const AnalogKarplusOffsets &source = analog_karplus_source_offsets[ch];
        const AnalogKarplusOffsets &target = analog_karplus_target_offsets[ch];

        apply_karplus_offsets(effective_patch, source, target);

        synth.active_karplus_patch[ch].store(&analog_karplus_patch_storage[ch],
                                             std::memory_order_release);
        synth.karplus_patch_dirty_flags.set(ch);
    }

    for (size_t ch = 0; ch < analog_modal_patch_storage.size(); ++ch) {
        analog_modal_patch_storage[ch] = synth.modal_patch_storage[ch];
        ModalPatch &effective_patch = analog_modal_patch_storage[ch];
        const AnalogModalOffsets &source = analog_modal_source_offsets[ch];
        const AnalogModalOffsets &target = analog_modal_target_offsets[ch];

        apply_modal_offsets(effective_patch, source, target);

        synth.active_modal_patch[ch].store(&analog_modal_patch_storage[ch],
                                           std::memory_order_release);
        synth.modal_patch_dirty_flags.set(ch);
    }

    for (int fx_id = 0; fx_id < FX_COUNT; ++fx_id) {
        analog_fx_params[fx_id] = fx_params[fx_id];
        const AnalogFxOffsets &source = analog_fx_source_offsets[fx_id];
        const AnalogFxOffsets &target = analog_fx_target_offsets[fx_id];
        analog_fx_params[fx_id].p1 = clamp_int(
            analog_fx_params[fx_id].p1 +
                lerp_int(source.p1, target.p1, clamped_progress),
            0, 1000);
        analog_fx_params[fx_id].p2 = clamp_int(
            analog_fx_params[fx_id].p2 +
                lerp_int(source.p2, target.p2, clamped_progress),
            0, 32000);
        analog_fx_params[fx_id].mix = clamp_int(
            analog_fx_params[fx_id].mix +
                lerp_int(source.mix, target.mix, clamped_progress),
            0, 32000);

        synth.set_fx_params(fx_id, analog_fx_params[fx_id].p1,
                            analog_fx_params[fx_id].p2,
                            analog_fx_params[fx_id].mix);
    }
}

void UiHandler::restore_base_parameters() {
    for (size_t ch = 0; ch < synth.patch_storage.size(); ++ch) {
        synth.active_patch[ch].store(&synth.patch_storage[ch],
                                     std::memory_order_release);
        synth.patch_dirty_flags.set(ch);
    }

    for (size_t ch = 0; ch < synth.karplus_patch_storage.size(); ++ch) {
        synth.active_karplus_patch[ch].store(&synth.karplus_patch_storage[ch],
                                             std::memory_order_release);
        synth.karplus_patch_dirty_flags.set(ch);
    }

    for (size_t ch = 0; ch < synth.modal_patch_storage.size(); ++ch) {
        synth.active_modal_patch[ch].store(&synth.modal_patch_storage[ch],
                                           std::memory_order_release);
        synth.modal_patch_dirty_flags.set(ch);
    }

    for (int fx_id = 0; fx_id < FX_COUNT; ++fx_id) {
        analog_fx_params[fx_id] = fx_params[fx_id];
        synth.set_fx_params(fx_id, fx_params[fx_id].p1, fx_params[fx_id].p2,
                            fx_params[fx_id].mix);
    }
}

void UiHandler::update_analog_variation() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const uint32_t interval_ms = analog_update_interval_ms();

    if (!analog_settings.enabled) {
        if (analog_was_active) {
            restore_base_parameters();
        }

        analog_offsets_initialized = false;
        analog_reapply_pending = false;
        analog_was_active = false;
        analog_transition_start_ms = now;
        return;
    }

    if (!analog_offsets_initialized) {
        if (analog_was_active) {
            const float restart_progress =
                interval_ms == 0
                    ? 1.0f
                    : clamp_float(static_cast<float>(now - analog_transition_start_ms) /
                                      static_cast<float>(interval_ms),
                                  0.0f, 1.0f);
            capture_current_analog_offsets(restart_progress);
        }
        analog_transition_start_ms = now;
        randomize_analog_targets();
        analog_offsets_initialized = true;
        analog_reapply_pending = true;
    } else if ((now - analog_transition_start_ms) >= interval_ms) {
        capture_current_analog_offsets(1.0f);
        analog_transition_start_ms = now;
        randomize_analog_targets();
        analog_reapply_pending = true;
    }

    const float progress =
        interval_ms == 0
            ? 1.0f
            : clamp_float(static_cast<float>(now - analog_transition_start_ms) /
                              static_cast<float>(interval_ms),
                          0.0f, 1.0f);

    apply_analog_variation(progress);
    analog_reapply_pending = false;
    analog_was_active = true;
}

bool UiHandler::lfo_target_matches_engine(LfoTarget target,
                                          SynthEngine engine) {
    switch (engine) {
    case SynthEngine::KarplusStrong:
        return target == LfoTarget::KarplusImpulseType ||
               target == LfoTarget::KarplusFilterGain ||
               target == LfoTarget::KarplusDecay ||
               target == LfoTarget::KarplusImpulseLength ||
               target == LfoTarget::KarplusPickPosition ||
               target == LfoTarget::KarplusDispersion ||
               target == LfoTarget::KarplusBodyResonance;
    case SynthEngine::Modal:
        return target == LfoTarget::ModalStructure ||
               target == LfoTarget::ModalBrightness ||
               target == LfoTarget::ModalDamping ||
               target == LfoTarget::ModalPosition ||
               target == LfoTarget::ModalExciterType;
    case SynthEngine::FM:
    default:
        return target == LfoTarget::FmOp1Wave ||
               target == LfoTarget::FmOp1Attack ||
               target == LfoTarget::FmOp1Decay ||
               target == LfoTarget::FmOp1Sustain ||
               target == LfoTarget::FmOp1Release ||
               target == LfoTarget::FmOp1Ratio ||
               target == LfoTarget::FmOp1Feedback ||
               target == LfoTarget::FmOp1FmDepth ||
               target == LfoTarget::FmOp2Wave ||
               target == LfoTarget::FmOp2Attack ||
               target == LfoTarget::FmOp2Decay ||
               target == LfoTarget::FmOp2Sustain ||
               target == LfoTarget::FmOp2Release ||
               target == LfoTarget::FmOp2Ratio ||
               target == LfoTarget::FmOp2Feedback ||
               target == LfoTarget::FmOp2FmDepth;
    }
}

const char *UiHandler::lfo_target_to_string(LfoTarget target) {
    switch (target) {
    case LfoTarget::Off:
        return "OFF";
    case LfoTarget::FmOp1Wave:
        return "Op1 Wave";
    case LfoTarget::FmOp1Attack:
        return "Op1 Attack";
    case LfoTarget::FmOp1Decay:
        return "Op1 Decay";
    case LfoTarget::FmOp1Sustain:
        return "Op1 Sustain";
    case LfoTarget::FmOp1Release:
        return "Op1 Release";
    case LfoTarget::FmOp1Ratio:
        return "Op1 Ratio";
    case LfoTarget::FmOp1Feedback:
        return "Op1 Feedback";
    case LfoTarget::FmOp1FmDepth:
        return "Op1 Depth";
    case LfoTarget::FmOp2Wave:
        return "Op2 Wave";
    case LfoTarget::FmOp2Attack:
        return "Op2 Attack";
    case LfoTarget::FmOp2Decay:
        return "Op2 Decay";
    case LfoTarget::FmOp2Sustain:
        return "Op2 Sustain";
    case LfoTarget::FmOp2Release:
        return "Op2 Release";
    case LfoTarget::FmOp2Ratio:
        return "Op2 Ratio";
    case LfoTarget::FmOp2Feedback:
        return "Op2 Feedback";
    case LfoTarget::FmOp2FmDepth:
        return "Op2 Depth";
    case LfoTarget::KarplusImpulseType:
        return "Impulse";
    case LfoTarget::KarplusFilterGain:
        return "Filter Gain";
    case LfoTarget::KarplusDecay:
        return "Decay";
    case LfoTarget::KarplusImpulseLength:
        return "Imp Length";
    case LfoTarget::KarplusPickPosition:
        return "Pick Pos";
    case LfoTarget::KarplusDispersion:
        return "Dispersion";
    case LfoTarget::KarplusBodyResonance:
        return "Body Res";
    case LfoTarget::ModalStructure:
        return "Structure";
    case LfoTarget::ModalBrightness:
        return "Brightness";
    case LfoTarget::ModalDamping:
        return "Damping";
    case LfoTarget::ModalPosition:
        return "Position";
    case LfoTarget::ModalExciterType:
        return "Exciter";
    default:
        return "OFF";
    }
}

int UiHandler::lfo_route_count(SynthEngine engine) {
    switch (engine) {
    case SynthEngine::KarplusStrong:
        return static_cast<int>(kKarplusLfoTargets.size());
    case SynthEngine::Modal:
        return static_cast<int>(kModalLfoTargets.size());
    case SynthEngine::FM:
    default:
        return static_cast<int>(kFmLfoTargets.size());
    }
}

LfoTarget UiHandler::lfo_target_from_engine_index(SynthEngine engine,
                                                  int index) {
    if (index < 0) {
        index = 0;
    }

    switch (engine) {
    case SynthEngine::KarplusStrong:
        if (index >= static_cast<int>(kKarplusLfoTargets.size())) {
            index = static_cast<int>(kKarplusLfoTargets.size()) - 1;
        }
        return kKarplusLfoTargets[static_cast<std::size_t>(index)];
    case SynthEngine::Modal:
        if (index >= static_cast<int>(kModalLfoTargets.size())) {
            index = static_cast<int>(kModalLfoTargets.size()) - 1;
        }
        return kModalLfoTargets[static_cast<std::size_t>(index)];
    case SynthEngine::FM:
    default:
        if (index >= static_cast<int>(kFmLfoTargets.size())) {
            index = static_cast<int>(kFmLfoTargets.size()) - 1;
        }
        return kFmLfoTargets[static_cast<std::size_t>(index)];
    }
}

int UiHandler::lfo_target_index_for_engine(SynthEngine engine,
                                           LfoTarget target) {
    switch (engine) {
    case SynthEngine::KarplusStrong:
        for (std::size_t i = 0; i < kKarplusLfoTargets.size(); ++i) {
            if (kKarplusLfoTargets[i] == target) return static_cast<int>(i);
        }
        return 0;
    case SynthEngine::Modal:
        for (std::size_t i = 0; i < kModalLfoTargets.size(); ++i) {
            if (kModalLfoTargets[i] == target) return static_cast<int>(i);
        }
        return 0;
    case SynthEngine::FM:
    default:
        for (std::size_t i = 0; i < kFmLfoTargets.size(); ++i) {
            if (kFmLfoTargets[i] == target) return static_cast<int>(i);
        }
        return 0;
    }
}

bool UiHandler::lfo_engine_active(SynthEngine engine) const {
    for (const LfoSettings &lfo : lfo_settings) {
        if (lfo.target != LfoTarget::Off && lfo.depth_hundredths_percent > 0 &&
            lfo_target_matches_engine(lfo.target, engine)) {
            return true;
        }
    }
    return false;
}

void UiHandler::update_lfo_modulation() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (lfo_last_update_ms == 0) {
        lfo_last_update_ms = now;
    }

    const uint32_t elapsed_ms = now - lfo_last_update_ms;
    lfo_last_update_ms = now;

    const SynthEngine engine = synth.get_engine();

    for (LfoSettings &lfo : lfo_settings) {
        if (lfo.target == LfoTarget::Off ||
            !lfo_target_matches_engine(lfo.target, engine) ||
            lfo.frequency_hundredths_hz == 0 || elapsed_ms == 0) {
            continue;
        }

        const uint64_t phase_advance =
            (static_cast<uint64_t>(lfo.frequency_hundredths_hz) * elapsed_ms *
             kLfoPhaseScale) /
            100000ull;
        lfo.phase_q32 += static_cast<uint32_t>(phase_advance);
    }

    if (!lfo_engine_active(engine)) {
        if (!analog_settings.enabled && lfo_was_active) {
            restore_base_parameters();
        }
        lfo_was_active = false;
        return;
    }

    switch (engine) {
    case SynthEngine::KarplusStrong:
        for (std::size_t ch = 0; ch < lfo_karplus_patch_storage.size(); ++ch) {
            const KarplusPatch &source =
                analog_settings.enabled ? analog_karplus_patch_storage[ch]
                                        : synth.karplus_patch_storage[ch];
            lfo_karplus_patch_storage[ch] = source;

            for (const LfoSettings &lfo : lfo_settings) {
                if (lfo.target == LfoTarget::Off ||
                    lfo.depth_hundredths_percent == 0 ||
                    !lfo_target_matches_engine(lfo.target, engine)) {
                    continue;
                }

                const float sample =
                    lfo.frequency_hundredths_hz == 0
                        ? 0.0f
                        : lfo_wave_sample(lfo.wave_type, lfo.phase_q32);
                const int amount = round_to_int(
                    sample * static_cast<float>(lfo_target_range(lfo.target)) *
                    (static_cast<float>(lfo.depth_hundredths_percent) / 10000.0f));
                apply_karplus_lfo_target(lfo_karplus_patch_storage[ch],
                                         lfo.target, amount);
            }

            synth.active_karplus_patch[ch].store(&lfo_karplus_patch_storage[ch],
                                                 std::memory_order_release);
            synth.karplus_patch_dirty_flags.set(ch);
        }
        break;
    case SynthEngine::Modal:
        for (std::size_t ch = 0; ch < lfo_modal_patch_storage.size(); ++ch) {
            const ModalPatch &source =
                analog_settings.enabled ? analog_modal_patch_storage[ch]
                                        : synth.modal_patch_storage[ch];
            lfo_modal_patch_storage[ch] = source;

            for (const LfoSettings &lfo : lfo_settings) {
                if (lfo.target == LfoTarget::Off ||
                    lfo.depth_hundredths_percent == 0 ||
                    !lfo_target_matches_engine(lfo.target, engine)) {
                    continue;
                }

                const float sample =
                    lfo.frequency_hundredths_hz == 0
                        ? 0.0f
                        : lfo_wave_sample(lfo.wave_type, lfo.phase_q32);
                const int amount = round_to_int(
                    sample * static_cast<float>(lfo_target_range(lfo.target)) *
                    (static_cast<float>(lfo.depth_hundredths_percent) / 10000.0f));
                apply_modal_lfo_target(lfo_modal_patch_storage[ch], lfo.target,
                                       amount);
            }

            synth.active_modal_patch[ch].store(&lfo_modal_patch_storage[ch],
                                               std::memory_order_release);
            synth.modal_patch_dirty_flags.set(ch);
        }
        break;
    case SynthEngine::FM:
    default:
        for (std::size_t ch = 0; ch < lfo_patch_storage.size(); ++ch) {
            const Patch &source =
                analog_settings.enabled ? analog_patch_storage[ch]
                                        : synth.patch_storage[ch];
            lfo_patch_storage[ch] = source;

            for (const LfoSettings &lfo : lfo_settings) {
                if (lfo.target == LfoTarget::Off ||
                    lfo.depth_hundredths_percent == 0 ||
                    !lfo_target_matches_engine(lfo.target, engine)) {
                    continue;
                }

                const float sample =
                    lfo.frequency_hundredths_hz == 0
                        ? 0.0f
                        : lfo_wave_sample(lfo.wave_type, lfo.phase_q32);
                const int amount = round_to_int(
                    sample * static_cast<float>(lfo_target_range(lfo.target)) *
                    (static_cast<float>(lfo.depth_hundredths_percent) / 10000.0f));
                apply_fm_lfo_target(lfo_patch_storage[ch], lfo.target, amount);
            }

            synth.active_patch[ch].store(&lfo_patch_storage[ch],
                                         std::memory_order_release);
            synth.patch_dirty_flags.set(ch);
        }
        break;
    }

    lfo_was_active = true;
}

uint8_t UiHandler::get_adsr_param(int param) {
    (void)param;
    return 0;
}

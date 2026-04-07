#include "Ui.hpp"
#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sequencer.hpp"
#include "pico/time.h"

namespace {
constexpr int kEncoderDebounceThreshold = 1;
constexpr int kEncoderMediumThreshold = 3;
constexpr int kEncoderFastThreshold = 5;
constexpr uint32_t kUiRandomSeed = 0xC0DE5EEDu;

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

int lerp_int(int start_value, int end_value, float progress) {
    return start_value + static_cast<int>(
                             (end_value - start_value) * progress);
}

int dispersion_span(int total_range, uint8_t dispersion_percent,
                    bool allow_minimum_step = true) {
    if (dispersion_percent == 0 || total_range <= 0) return 0;

    int span = (total_range * dispersion_percent + 99) / 100;
    if (allow_minimum_step && span == 0) span = 1;
    if (span > total_range) span = total_range;
    return span;
}
} // namespace

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

UiHandler::UiHandler(HardwareManager &hw, MidiHandler &midi_handler,
                     Sequencer &seq, Sampler &sampler, Synth &synth)
    : hw(hw), midi(midi_handler), seq(seq), sampler(sampler), synth(synth) {
    karplus_last_delay_samples =
        KarplusVoice::tuned_delay_samples_for_note(karplus_last_note);

    // List all WAV files and store them
    wav_files = sampler.list_wav_files(true); // recursive
    wav_files.print_files();                  // Optional: print on startup

    // Optional: Load first file by default
    // if (wav_files.get_count() > 0) {
    //    sampler.load_sample_by_index(0, 0); // Load first file into player 0
    //}

    ui_dispatch_table[UI_STATE_MAIN] = {
        main_handle_encoders, main_handle_switches, main_update_display};
    ui_dispatch_table[UI_STATE_FM_EDIT] = {fm_edit_handle_encoders,
                                           fm_edit_handle_switches,
                                           fm_edit_update_display};
    ui_dispatch_table[UI_STATE_KARPLUS_EDIT] = {
        karplus_edit_handle_encoders,
        karplus_edit_handle_switches,
        karplus_edit_update_display};
    ui_dispatch_table[UI_STATE_MODAL_EDIT] = {
        modal_edit_handle_encoders,
        modal_edit_handle_switches,
        modal_edit_update_display};
    ui_dispatch_table[UI_STATE_FX_EDIT] = {
        fx_handle_encoders, main_handle_switches, fx_update_display};
    ui_dispatch_table[UI_STATE_ANALOG] = {
        analog_handle_encoders, main_handle_switches, analog_update_display};
    ui_dispatch_table[UI_STATE_MIDI_SETTINGS] = {
        midi_handle_encoders, main_handle_switches, midi_update_display};
    ui_dispatch_table[UI_STATE_SEQUENCER] = {sequencer_handle_encoders,
                                             main_handle_switches,
                                             sequencer_update_display};
    ui_dispatch_table[UI_STATE_SEQUENCER_EDIT] = {
        sequencer_note_edit_handle_encoders,
        sequencer_note_edit_handle_switches,
        sequencer_note_edit_update_display};
    ui_dispatch_table[UI_STATE_CHOOSE] = {
        choose_handle_encoders, main_handle_switches, choose_update_display};
    ui_dispatch_table[UI_STATE_ENGINE_SELECT] = {
        engine_select_handle_encoders,
        main_handle_switches,
        engine_select_update_display};
    ui_dispatch_table[UI_STATE_SAMPLER] = {
        sampler_handle_encoders, main_handle_switches, sampler_update_display};

    for (int fx_id = 0; fx_id < FX_COUNT; ++fx_id) {
        synth.set_fx_params(fx_id,
                            fx_params[fx_id].p1,
                            fx_params[fx_id].p2,
                            fx_params[fx_id].mix);
        synth.enable_fx(fx_id, fx_enabled[fx_id]);
    }
}

void UiHandler::update() {
    UiDispatchEntry ui_dispatch_entry = ui_dispatch_table[ui_state];
    ui_dispatch_entry.handle_encoders(*this);
    tud_task(); // Service USB
    ui_dispatch_entry.handle_switches(*this);
    tud_task(); // Service USB
    update_analog_variation();
    tud_task(); // Service USB
    ui_dispatch_entry.handle_display(*this);
    tud_task(); // Service USB
}

bool UiHandler::encoder_moved(int32_t delta) {
    return abs_int(static_cast<int>(delta)) > kEncoderDebounceThreshold;
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

    printf("Randomized FM patch on channel %d\n", self.midi_channel + 1);
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

    printf("Randomized Karplus patch on channel %d\n",
           self.midi_channel + 1);
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

    printf("Randomized Modal patch on channel %d\n",
           self.midi_channel + 1);
}

// // Helper functions
// void UiHandler::set_adsr_param(int param, uint8_t value) {
//     switch (param) {
//     case 0:
//         channel_params[midi_channel].attack = value;
//         break;
//     case 1:
//         channel_params[midi_channel].decay = value;
//         break;
//     case 2:
//         channel_params[midi_channel].sustain = value << 8;
//         break;
//     case 3:
//         channel_params[midi_channel].release = value;
//         break;
//     }
// }
//

void UiHandler::set_delay_param(int delay_ms, int feedback, int mix){
    synth.delay_effect.set_delay_ms(delay_ms);
    synth.delay_effect.set_feedback(feedback);
    synth.delay_effect.set_mix(mix);
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
    const uint32_t tenths_hz =
        analog_settings.frequency_tenths_hz == 0
            ? 1u
            : static_cast<uint32_t>(analog_settings.frequency_tenths_hz);
    return 10000u / tenths_hz;
}

void UiHandler::randomize_analog_targets() {
    const uint8_t dispersion = analog_settings.dispersion_percent;
    const int max_wave_type = static_cast<int>(WaveType::Sinc);
    const int max_impulse_type = 6;

    for (size_t ch = 0; ch < analog_fm_target_offsets.size(); ++ch) {
        AnalogFmOffsets &fm_offsets = analog_fm_target_offsets[ch];
        for (size_t op_index = 0; op_index < fm_offsets.ops.size(); ++op_index) {
            AnalogOperatorOffsets &op_offsets = fm_offsets.ops[op_index];
            op_offsets.wave_type = random_range_inclusive(
                -dispersion_span(max_wave_type, dispersion, false),
                dispersion_span(max_wave_type, dispersion, false));
            op_offsets.attack = random_range_inclusive(
                -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
            op_offsets.decay = random_range_inclusive(
                -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
            op_offsets.sustain = random_range_inclusive(
                -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
            op_offsets.release = random_range_inclusive(
                -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
            op_offsets.ratio = random_range_inclusive(
                -dispersion_span(15, dispersion), dispersion_span(15, dispersion));
            op_offsets.feedback = random_range_inclusive(
                -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
            op_offsets.fm_depth = random_range_inclusive(
                -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        }

        AnalogKarplusOffsets &karplus_offsets = analog_karplus_target_offsets[ch];
        karplus_offsets.impulse_type = random_range_inclusive(
            -dispersion_span(max_impulse_type, dispersion, false),
            dispersion_span(max_impulse_type, dispersion, false));
        karplus_offsets.filter_gain = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        karplus_offsets.decay = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        karplus_offsets.impulse_length = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        karplus_offsets.pick_position = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        karplus_offsets.dispersion = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        karplus_offsets.body_resonance = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));

        AnalogModalOffsets &modal_offsets = analog_modal_target_offsets[ch];
        modal_offsets.structure = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        modal_offsets.brightness = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        modal_offsets.damping = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        modal_offsets.position = random_range_inclusive(
            -dispersion_span(127, dispersion), dispersion_span(127, dispersion));
        modal_offsets.exciter_type = random_range_inclusive(
            -dispersion_span(3, dispersion, false),
            dispersion_span(3, dispersion, false));
    }

    for (size_t fx_id = 0; fx_id < analog_fx_target_offsets.size(); ++fx_id) {
        AnalogFxOffsets &fx_offsets = analog_fx_target_offsets[fx_id];
        const int p1_range = 1000;
        const int p2_range = 32000;
        const int mix_range = 32000;
        fx_offsets.p1 = random_range_inclusive(
            -dispersion_span(p1_range, dispersion), dispersion_span(p1_range, dispersion));
        fx_offsets.p2 = random_range_inclusive(
            -dispersion_span(p2_range, dispersion), dispersion_span(p2_range, dispersion));
        fx_offsets.mix = random_range_inclusive(
            -dispersion_span(mix_range, dispersion), dispersion_span(mix_range, dispersion));
    }
}

void UiHandler::capture_current_analog_offsets(float progress) {
    const float clamped_progress = clamp_float(progress, 0.0f, 1.0f);

    for (size_t ch = 0; ch < analog_fm_source_offsets.size(); ++ch) {
        AnalogFmOffsets &source = analog_fm_source_offsets[ch];
        const AnalogFmOffsets &target = analog_fm_target_offsets[ch];

        for (size_t op_index = 0; op_index < source.ops.size(); ++op_index) {
            AnalogOperatorOffsets &src_op = source.ops[op_index];
            const AnalogOperatorOffsets &target_op = target.ops[op_index];

            src_op.wave_type = lerp_int(src_op.wave_type, target_op.wave_type,
                                        clamped_progress);
            src_op.attack =
                lerp_int(src_op.attack, target_op.attack, clamped_progress);
            src_op.decay = lerp_int(src_op.decay, target_op.decay, clamped_progress);
            src_op.sustain =
                lerp_int(src_op.sustain, target_op.sustain, clamped_progress);
            src_op.release =
                lerp_int(src_op.release, target_op.release, clamped_progress);
            src_op.ratio = lerp_int(src_op.ratio, target_op.ratio, clamped_progress);
            src_op.feedback =
                lerp_int(src_op.feedback, target_op.feedback, clamped_progress);
            src_op.fm_depth =
                lerp_int(src_op.fm_depth, target_op.fm_depth, clamped_progress);
        }
    }

    for (size_t ch = 0; ch < analog_karplus_source_offsets.size(); ++ch) {
        AnalogKarplusOffsets &source = analog_karplus_source_offsets[ch];
        const AnalogKarplusOffsets &target = analog_karplus_target_offsets[ch];

        source.impulse_type =
            lerp_int(source.impulse_type, target.impulse_type, clamped_progress);
        source.filter_gain =
            lerp_int(source.filter_gain, target.filter_gain, clamped_progress);
        source.decay = lerp_int(source.decay, target.decay, clamped_progress);
        source.impulse_length = lerp_int(source.impulse_length, target.impulse_length,
                                         clamped_progress);
        source.pick_position = lerp_int(source.pick_position, target.pick_position,
                                        clamped_progress);
        source.dispersion =
            lerp_int(source.dispersion, target.dispersion, clamped_progress);
        source.body_resonance = lerp_int(source.body_resonance,
                                         target.body_resonance, clamped_progress);
    }

    for (size_t ch = 0; ch < analog_modal_source_offsets.size(); ++ch) {
        AnalogModalOffsets &source = analog_modal_source_offsets[ch];
        const AnalogModalOffsets &target = analog_modal_target_offsets[ch];

        source.structure =
            lerp_int(source.structure, target.structure, clamped_progress);
        source.brightness =
            lerp_int(source.brightness, target.brightness, clamped_progress);
        source.damping =
            lerp_int(source.damping, target.damping, clamped_progress);
        source.position =
            lerp_int(source.position, target.position, clamped_progress);
        source.exciter_type =
            lerp_int(source.exciter_type, target.exciter_type, clamped_progress);
    }

    for (size_t fx_id = 0; fx_id < analog_fx_source_offsets.size(); ++fx_id) {
        AnalogFxOffsets &source = analog_fx_source_offsets[fx_id];
        const AnalogFxOffsets &target = analog_fx_target_offsets[fx_id];

        source.p1 = lerp_int(source.p1, target.p1, clamped_progress);
        source.p2 = lerp_int(source.p2, target.p2, clamped_progress);
        source.mix = lerp_int(source.mix, target.mix, clamped_progress);
    }
}

void UiHandler::apply_analog_variation(float progress) {
    const int max_wave_type = static_cast<int>(WaveType::Sinc);
    const float clamped_progress = clamp_float(progress, 0.0f, 1.0f);

    for (size_t ch = 0; ch < analog_patch_storage.size(); ++ch) {
        analog_patch_storage[ch] = synth.patch_storage[ch];
        Patch &effective_patch = analog_patch_storage[ch];
        const AnalogFmOffsets &source_offsets = analog_fm_source_offsets[ch];
        const AnalogFmOffsets &target_offsets = analog_fm_target_offsets[ch];

        for (size_t op_index = 0; op_index < source_offsets.ops.size(); ++op_index) {
            OperatorParams &effective_op = effective_patch.ops[op_index];
            const AnalogOperatorOffsets &source = source_offsets.ops[op_index];
            const AnalogOperatorOffsets &target = target_offsets.ops[op_index];

            effective_op.wave_type = static_cast<WaveType>(clamp_int(
                static_cast<int>(effective_op.wave_type) +
                    lerp_int(source.wave_type, target.wave_type, clamped_progress),
                0, max_wave_type));
            effective_op.attack = clamp_u8(
                static_cast<int>(effective_op.attack) +
                    lerp_int(source.attack, target.attack, clamped_progress),
                0, 127);
            effective_op.decay = clamp_u8(
                static_cast<int>(effective_op.decay) +
                    lerp_int(source.decay, target.decay, clamped_progress),
                0, 127);
            effective_op.sustain = clamp_u8(
                static_cast<int>(effective_op.sustain) +
                    lerp_int(source.sustain, target.sustain, clamped_progress),
                0, 127);
            effective_op.release = clamp_u8(
                static_cast<int>(effective_op.release) +
                    lerp_int(source.release, target.release, clamped_progress),
                0, 127);
            effective_op.ratio = clamp_u16(
                static_cast<int>(effective_op.ratio) +
                    lerp_int(source.ratio, target.ratio, clamped_progress),
                1, 16);
            effective_op.feedback = clamp_u16(
                static_cast<int>(effective_op.feedback) +
                    lerp_int(source.feedback, target.feedback, clamped_progress),
                0, 127);
            effective_op.fm_depth = clamp_u16(
                static_cast<int>(effective_op.fm_depth) +
                    lerp_int(source.fm_depth, target.fm_depth, clamped_progress),
                0, 127);
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

        effective_patch.impulse_type = static_cast<KarplusImpulseType>(clamp_int(
            static_cast<int>(effective_patch.impulse_type) +
                lerp_int(source.impulse_type, target.impulse_type, clamped_progress),
            0, 6));
        effective_patch.filter_gain = clamp_u8(
            static_cast<int>(effective_patch.filter_gain) +
                lerp_int(source.filter_gain, target.filter_gain, clamped_progress),
            0, 127);
        effective_patch.decay = clamp_u8(
            static_cast<int>(effective_patch.decay) +
                lerp_int(source.decay, target.decay, clamped_progress),
            0, 127);
        effective_patch.impulse_length = clamp_u8(
            static_cast<int>(effective_patch.impulse_length) +
                lerp_int(source.impulse_length, target.impulse_length, clamped_progress),
            0, 127);
        effective_patch.pick_position = clamp_u8(
            static_cast<int>(effective_patch.pick_position) +
                lerp_int(source.pick_position, target.pick_position, clamped_progress),
            0, 127);
        effective_patch.dispersion = clamp_u8(
            static_cast<int>(effective_patch.dispersion) +
                lerp_int(source.dispersion, target.dispersion, clamped_progress),
            0, 127);
        effective_patch.body_resonance = clamp_u8(
            static_cast<int>(effective_patch.body_resonance) +
                lerp_int(source.body_resonance, target.body_resonance, clamped_progress),
            0, 127);

        synth.active_karplus_patch[ch].store(&analog_karplus_patch_storage[ch],
                                             std::memory_order_release);
        synth.karplus_patch_dirty_flags.set(ch);
    }

    for (size_t ch = 0; ch < analog_modal_patch_storage.size(); ++ch) {
        analog_modal_patch_storage[ch] = synth.modal_patch_storage[ch];
        ModalPatch &effective_patch = analog_modal_patch_storage[ch];
        const AnalogModalOffsets &source = analog_modal_source_offsets[ch];
        const AnalogModalOffsets &target = analog_modal_target_offsets[ch];

        effective_patch.structure = clamp_u8(
            static_cast<int>(effective_patch.structure) +
                lerp_int(source.structure, target.structure, clamped_progress),
            0, 127);
        effective_patch.brightness = clamp_u8(
            static_cast<int>(effective_patch.brightness) +
                lerp_int(source.brightness, target.brightness, clamped_progress),
            0, 127);
        effective_patch.damping = clamp_u8(
            static_cast<int>(effective_patch.damping) +
                lerp_int(source.damping, target.damping, clamped_progress),
            0, 127);
        effective_patch.position = clamp_u8(
            static_cast<int>(effective_patch.position) +
                lerp_int(source.position, target.position, clamped_progress),
            0, 127);
        effective_patch.exciter_type = static_cast<ModalExciterType>(clamp_int(
            static_cast<int>(effective_patch.exciter_type) +
                lerp_int(source.exciter_type, target.exciter_type, clamped_progress),
            0, 3));

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

uint8_t UiHandler::get_adsr_param(int param) {
    (void)param;
    //     switch (param) {
    //     case 0:
    //         return channel_params[midi_channel].attack;
    //     case 1:
    //         return channel_params[midi_channel].decay;
    //     case 2:
    //         return channel_params[midi_channel].sustain >> 8;
    //     case 3:
    //         return channel_params[midi_channel].release;
    //     default:
    //         return 0;
    //     }
    return 0;
}

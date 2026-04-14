#include "Synth.hpp"

#include "Chorus.hpp"
#include "Compressor.hpp"
#include "Delay.hpp"
#include "Distortion.hpp"
#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Reverb.hpp"
#include "ReverbSc.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include "hardware/interp.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {
constexpr std::array<WaveType, 16> kChannelBaseWaves = {
    Sine, Square, Triangle, Sawtooth, Sinc, Sine, Square, Triangle,
    Sawtooth, Sinc, Sine, Square, Triangle, Sawtooth, Sinc, Sine};

void configure_interp_lane(interp_hw_t *interp) {
    interp_config config = interp_default_config();
    interp_config_set_shift(&config, 15);
    interp_config_set_mask(&config, 1, WAVE_SHIFT);
    interp_config_set_add_raw(&config, true);
    interp_set_config(interp, 0, &config);
}

void configure_interpolators() {
    configure_interp_lane(interp0);
    configure_interp_lane(interp1);
}

Patch make_default_patch(WaveType carrier_wave) {
    Patch patch{};
    patch.algorithm = 0;
    patch.volume = 100;
    patch.pan = 64;

    patch.ops[0].wave_type = carrier_wave;
    patch.ops[0].attack = 5;
    patch.ops[0].decay = 20;
    patch.ops[0].sustain = 100;
    patch.ops[0].release = 30;
    patch.ops[0].ratio = 1;
    patch.ops[0].feedback = 0;
    patch.ops[0].fm_depth = 0;

    patch.ops[1].wave_type = Sine;
    patch.ops[1].attack = 5;
    patch.ops[1].decay = 15;
    patch.ops[1].sustain = 80;
    patch.ops[1].release = 25;
    patch.ops[1].ratio = 2;
    patch.ops[1].feedback = 0;
    patch.ops[1].fm_depth = 50;

    return patch;
}

void cleanup_idle_fm_voices(std::array<Voice, NUM_VOICES> &voices) {
    for (auto &voice : voices) {
        if (!voice.playing || !voice.is_idle()) {
            continue;
        }

        voice.playing = false;
        voice.steal = false;
    }
}

template <typename VoiceArray, typename PatchType, typename MatchFn>
void apply_patch_updates(VoiceArray &voices, std::bitset<16> &dirty_flags,
                         std::array<std::atomic<PatchType *>, 16> &active_patch,
                         MatchFn matches_channel) {
    for (uint8_t channel = 0; channel < 16; ++channel) {
        if (!dirty_flags.test(channel)) {
            continue;
        }

        const PatchType *patch =
            active_patch[channel].load(std::memory_order_acquire);
        if (patch) {
            for (auto &voice : voices) {
                if (matches_channel(voice, channel)) {
                    voice.apply_patch(*patch);
                }
            }
        }

        dirty_flags.reset(channel);
    }
}

template <typename VoiceArray, typename ActiveFn, typename RenderFn>
void mix_active_voices(VoiceArray &voices,
                       std::array<int16_t, SAMPLES_PER_BUFFER> &flow_buffer,
                       std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                       int mix_shift, ActiveFn is_active, RenderFn render) {
    for (auto &voice : voices) {
        if (!is_active(voice)) {
            continue;
        }

        render(voice, flow_buffer);
        for (int sample = 0; sample < SAMPLES_PER_BUFFER; ++sample) {
            buffer[sample] += flow_buffer[sample] >> mix_shift;
        }
    }
}

template <typename VoiceArray, typename PatchType>
bool restart_or_allocate_voice(VoiceArray &voices, uint8_t channel,
                               uint8_t note, uint8_t velocity,
                               const PatchType *patch) {
    for (auto &voice : voices) {
        if (voice.matches(channel, note)) {
            voice.start(note, channel, velocity, *patch);
            return true;
        }
    }

    for (auto &voice : voices) {
        if (!voice.is_active()) {
            voice.start(note, channel, velocity, *patch);
            return true;
        }
    }

    return false;
}

template <typename VoiceArray>
void note_off_voice(VoiceArray &voices, uint8_t channel, uint8_t note) {
    for (auto &voice : voices) {
        if (voice.matches(channel, note)) {
            voice.note_off();
        }
    }
}

float unit_interval(int value, float scale) {
    float normalized = static_cast<float>(value) / scale;
    if (normalized < 0.0f) return 0.0f;
    if (normalized > 1.0f) return 1.0f;
    return normalized;
}

std::size_t fx_slot_bytes(int fx_id) {
    switch (fx_id) {
    case 0:
        return sizeof(Delay);
    case 1:
        return sizeof(Distortion);
    case 2:
        return sizeof(Reverb);
    case 3:
        return sizeof(Chorus);
    case 4:
        return sizeof(ReverbScFx);
    case 5:
        return sizeof(Compressor);
    default:
        return 0;
    }
}
} // namespace

Synth::Synth() {
    initialize_patches();
    initialize_karplus_patches();
    initialize_modal_patches();

    for (int i = 0; i < 16; ++i) {
        active_patch[i].store(&patch_storage[i], std::memory_order_release);
        active_karplus_patch[i].store(&karplus_patch_storage[i],
                                      std::memory_order_release);
        active_modal_patch[i].store(&modal_patch_storage[i],
                                    std::memory_order_release);
    }

    fx_param_cache = {{
        {250, 10000, 10000},
        {500, 200, 30000},
        {300, 500, 30000},
        {450, 32000, 32000},
        {1000, 32000, 32000},
        {420, 6000, 32000},
    }};

    ensure_engine_runtime(current_engine);
    ensure_compressor_effect();
    set_fx_params(5, fx_param_cache[5].p1, fx_param_cache[5].p2,
                  fx_param_cache[5].mix);
}

Synth::~Synth() {
    release_engine_runtimes();
    destroy_fx_slot(0);
    destroy_fx_slot(1);
    destroy_fx_slot(2);
    destroy_fx_slot(3);
    destroy_fx_slot(4);
    destroy_fx_slot(5);
}

void Synth::initialize_patches() {
    for (int ch = 0; ch < 16; ++ch) {
        patch_storage[ch] = make_default_patch(kChannelBaseWaves[ch]);
    }
}

void Synth::initialize_karplus_patches() {
    for (int ch = 0; ch < 16; ++ch) {
        KarplusPatch &patch = karplus_patch_storage[ch];
        patch.impulse_type = KarplusImpulseType::WhiteNoise;
        patch.filter_gain = 92;
        patch.decay = 110;
        patch.impulse_length = 72;
        patch.pick_position = 32;
        patch.dispersion = 24;
        patch.body_resonance = 40;
    }
}

void Synth::initialize_modal_patches() {
    for (int ch = 0; ch < 16; ++ch) {
        ModalPatch &patch = modal_patch_storage[ch];
        patch.structure = 14;
        patch.brightness = 92;
        patch.damping = 100;
        patch.position = 34;
        patch.exciter_type = ModalExciterType::SoftStrike;
    }
}

void Synth::ensure_engine_runtime(SynthEngine engine) {
    switch (engine) {
    case SynthEngine::FM:
        if (!fm_voices) {
            fm_voices = std::make_unique<std::array<Voice, NUM_VOICES>>();
        }
        break;
    case SynthEngine::KarplusStrong:
        if (!karplus_voices) {
            karplus_voices =
                std::make_unique<std::array<KarplusVoice, KARPLUS_VOICE_COUNT>>();
        }
        break;
    case SynthEngine::Modal:
        if (!modal_voices) {
            modal_voices =
                std::make_unique<std::array<ModalVoice, MODAL_VOICE_COUNT>>();
        }
        break;
    }
}

void Synth::release_engine_runtimes() {
    fm_voices.reset();
    karplus_voices.reset();
    modal_voices.reset();
}

Delay *Synth::ensure_delay_effect() {
    if (!delay_effect) {
        delay_effect = new Delay();
    }
    return delay_effect;
}

Distortion *Synth::ensure_distortion_effect() {
    if (!distortion_effect) {
        distortion_effect = new Distortion();
    }
    return distortion_effect;
}

Reverb *Synth::ensure_reverb_effect() {
    if (!reverb_effect) {
        reverb_effect = new Reverb();
    }
    return reverb_effect;
}

Chorus *Synth::ensure_chorus_effect() {
    if (!chorus_effect) {
        chorus_effect = new Chorus();
    }
    return chorus_effect;
}

ReverbScFx *Synth::ensure_reverb_sc_effect() {
    if (!reverb_sc_effect) {
        reverb_sc_effect = new ReverbScFx();
    }
    return reverb_sc_effect;
}

Compressor *Synth::ensure_compressor_effect() {
    if (!compressor_effect) {
        compressor_effect = new Compressor();
    }
    return compressor_effect;
}

void Synth::reset_fx_slot(int fx_id) {
    switch (fx_id) {
    case 0:
        if (delay_effect) delay_effect->reset();
        break;
    case 1:
        if (distortion_effect) distortion_effect->reset();
        break;
    case 2:
        if (reverb_effect) reverb_effect->reset();
        break;
    case 3:
        if (chorus_effect) chorus_effect->reset();
        break;
    case 4:
        if (reverb_sc_effect) reverb_sc_effect->reset();
        break;
    case 5:
        if (compressor_effect) compressor_effect->reset();
        break;
    default:
        break;
    }
}

void Synth::destroy_fx_slot(int fx_id) {
    switch (fx_id) {
    case 0:
        delete delay_effect;
        delay_effect = nullptr;
        break;
    case 1:
        delete distortion_effect;
        distortion_effect = nullptr;
        break;
    case 2:
        delete reverb_effect;
        reverb_effect = nullptr;
        break;
    case 3:
        delete chorus_effect;
        chorus_effect = nullptr;
        break;
    case 4:
        delete reverb_sc_effect;
        reverb_sc_effect = nullptr;
        break;
    case 5:
        if (compressor_effect) {
            compressor_effect->reset();
        }
        break;
    default:
        break;
    }
}

void Synth::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {
    configure_interpolators();

    switch (current_engine) {
    case SynthEngine::FM:
        ensure_engine_runtime(SynthEngine::FM);
        cleanup_idle_fm_voices(*fm_voices);
        apply_patch_updates(
            *fm_voices, patch_dirty_flags, active_patch,
            [](const Voice &voice, uint8_t channel) {
                return voice.playing && voice.midi_channel == channel;
            });
        mix_active_voices(
            *fm_voices, flow_buffer, buffer, 4,
            [](const Voice &voice) { return voice.playing; },
            [](Voice &voice, std::array<int16_t, SAMPLES_PER_BUFFER> &mix_buffer) {
                voice.out(mix_buffer);
            });
        break;
    case SynthEngine::KarplusStrong:
        ensure_engine_runtime(SynthEngine::KarplusStrong);
        apply_patch_updates(
            *karplus_voices, karplus_patch_dirty_flags, active_karplus_patch,
            [](const KarplusVoice &voice, uint8_t channel) {
                return voice.is_active() && voice.midi_channel == channel;
            });
        mix_active_voices(
            *karplus_voices, flow_buffer, buffer, 3,
            [](const KarplusVoice &voice) { return voice.is_active(); },
            [](KarplusVoice &voice,
               std::array<int16_t, SAMPLES_PER_BUFFER> &mix_buffer) {
                voice.render(mix_buffer);
            });
        break;
    case SynthEngine::Modal:
        ensure_engine_runtime(SynthEngine::Modal);
        apply_patch_updates(
            *modal_voices, modal_patch_dirty_flags, active_modal_patch,
            [](const ModalVoice &voice, uint8_t channel) {
                return voice.is_active() && voice.midi_channel == channel;
            });
        mix_active_voices(
            *modal_voices, flow_buffer, buffer, 3,
            [](const ModalVoice &voice) { return voice.is_active(); },
            [](ModalVoice &voice,
               std::array<int16_t, SAMPLES_PER_BUFFER> &mix_buffer) {
                voice.render(mix_buffer);
            });
        break;
    }
}

void Synth::process_fx(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {
    if (fx_enabled[5] && compressor_effect) {
        compressor_effect->process(buffer.data(), SAMPLES_PER_BUFFER);
    }
    if (fx_enabled[1] && distortion_effect) {
        distortion_effect->process(buffer.data(), SAMPLES_PER_BUFFER);
    }
    if (fx_enabled[3] && chorus_effect) {
        chorus_effect->process(buffer.data(), SAMPLES_PER_BUFFER);
    }
    if (fx_enabled[0] && delay_effect) {
        delay_effect->process(buffer.data(), SAMPLES_PER_BUFFER);
    }
    if (fx_enabled[2] && reverb_effect) {
        reverb_effect->process(buffer.data(), SAMPLES_PER_BUFFER);
    }
    if (fx_enabled[4] && reverb_sc_effect) {
        reverb_sc_effect->process(buffer.data(), SAMPLES_PER_BUFFER);
    }
}

void Synth::process_midi_packet(uint8_t packet[4]) {
    uint8_t msg_type = packet[1] & 0xF0;
    uint8_t channel = packet[1] & 0x0F;
    uint8_t note = packet[2];
    uint8_t velocity = packet[3];

    switch (msg_type) {
    case 0x90:
        if (velocity > 0) {
            note_on(channel, note, velocity);
        } else {
            note_off(channel, note, velocity);
        }
        break;
    case 0x80:
        note_off(channel, note, velocity);
        break;
    case 0xB0:
        break;
    }
}

void Synth::update_filter_cutoff(uint8_t channel) { (void)channel; }

void Synth::update_filter_q(uint8_t channel) { update_filter_cutoff(channel); }

void Synth::set_filter_type(uint8_t type_value) {
    if (type_value < 43) {
        current_filter_type = FILTER_OFF;
    } else if (type_value < 85) {
        current_filter_type = FILTER_LOW_PASS;
    } else {
        current_filter_type = FILTER_CHEBYSHEV;
    }
}

const WaveType channel_wave_map[16] = {
    Sine,     Square, Triangle, Sawtooth, Sinc,     Sine,     Square, Triangle,
    Sawtooth, Sinc,   Sine,     Square,   Triangle, Sawtooth, Sinc,   Sine};

void Synth::note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (current_engine == SynthEngine::KarplusStrong) {
        ensure_engine_runtime(SynthEngine::KarplusStrong);
        notes_playing_bitset.set(note);

        const KarplusPatch *patch_ptr =
            active_karplus_patch[channel].load(std::memory_order_acquire);
        if (!patch_ptr) {
            return;
        }

        if (!restart_or_allocate_voice(*karplus_voices, channel, note, velocity,
                                       patch_ptr)) {
            (*karplus_voices)[0].start(note, channel, velocity, *patch_ptr);
        }
        return;
    }

    if (current_engine == SynthEngine::Modal) {
        ensure_engine_runtime(SynthEngine::Modal);
        notes_playing_bitset.set(note);

        const ModalPatch *patch_ptr =
            active_modal_patch[channel].load(std::memory_order_acquire);
        if (!patch_ptr) {
            return;
        }

        if (!restart_or_allocate_voice(*modal_voices, channel, note, velocity,
                                       patch_ptr)) {
            (*modal_voices)[0].start(note, channel, velocity, *patch_ptr);
        }
        return;
    }

    ensure_engine_runtime(SynthEngine::FM);

    for (int i = 0; i < NUM_VOICES; ++i) {
        if ((*fm_voices)[i].midi_note == note && (*fm_voices)[i].playing &&
            !(*fm_voices)[i].steal && (*fm_voices)[i].midi_channel == channel) {
            return;
        }
    }

    for (int i = 0; i < NUM_VOICES; ++i) {
        if (!(*fm_voices)[i].playing) {
            (*fm_voices)[i].midi_channel = channel;
            (*fm_voices)[i].midi_note = note;
            __sync_synchronize();
            (*fm_voices)[i].playing = true;
            notes_playing_bitset.set(note);

            const Patch *patch_ptr =
                active_patch[channel].load(std::memory_order_acquire);
            if (patch_ptr) {
                (*fm_voices)[i].apply_patch(*patch_ptr);
            }

            (*fm_voices)[i].gate_on();
            break;
        }
    }
}

void Synth::note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
    (void)velocity;

    if (current_engine == SynthEngine::KarplusStrong) {
        notes_playing_bitset.reset(note);
        if (karplus_voices) {
            note_off_voice(*karplus_voices, channel, note);
        }
        return;
    }

    if (current_engine == SynthEngine::Modal) {
        notes_playing_bitset.reset(note);
        if (modal_voices) {
            note_off_voice(*modal_voices, channel, note);
        }
        return;
    }

    if (!fm_voices) {
        return;
    }

    for (int i = 0; i < NUM_VOICES; ++i) {
        if ((*fm_voices)[i].midi_note == note && (*fm_voices)[i].playing &&
            !(*fm_voices)[i].steal && (*fm_voices)[i].midi_channel == channel) {
            (*fm_voices)[i].gate_off();
            (*fm_voices)[i].steal = true;
            notes_playing_bitset.reset(note);
            break;
        }
    }
}

void Synth::clear_fm_voices() {
    if (!fm_voices) return;
    for (auto &voice : *fm_voices) {
        voice.playing = false;
        voice.steal = false;
        voice.op[0].env.set_idle();
        voice.op[1].env.set_idle();
        voice.op[0].osc.reset_dco_pos();
        voice.op[1].osc.reset_dco_pos();
    }
}

void Synth::clear_karplus_voices() {
    if (!karplus_voices) return;
    for (auto &voice : *karplus_voices) {
        voice.reset();
    }
}

void Synth::clear_modal_voices() {
    if (!modal_voices) return;
    for (auto &voice : *modal_voices) {
        voice.reset();
    }
}

void Synth::reset_runtime_state() {
    clear_fm_voices();
    clear_karplus_voices();
    clear_modal_voices();
    notes_playing_bitset.reset();

    for (int fx_id = 0; fx_id < FX_SLOT_COUNT; ++fx_id) {
        reset_fx_slot(fx_id);
    }
}

void Synth::set_engine(SynthEngine engine) {
    if (current_engine == engine) {
        return;
    }

    reset_runtime_state();
    release_engine_runtimes();
    current_engine = engine;
    ensure_engine_runtime(current_engine);
}

void Synth::enable_fx(int fx_id, bool enabled) {
    if (fx_id < 0 || fx_id >= FX_SLOT_COUNT) return;
    if (fx_enabled[fx_id] == enabled) {
        if (enabled && fx_id != 5) {
            set_fx_params(fx_id, fx_param_cache[fx_id].p1, fx_param_cache[fx_id].p2,
                          fx_param_cache[fx_id].mix);
        }
        return;
    }

    fx_enabled[fx_id] = enabled;
    if (enabled) {
        set_fx_params(fx_id, fx_param_cache[fx_id].p1, fx_param_cache[fx_id].p2,
                      fx_param_cache[fx_id].mix);
        return;
    }

    reset_fx_slot(fx_id);
    destroy_fx_slot(fx_id);
}

void Synth::set_fx_params(int fx_id, int p1, int p2, int mix) {
    if (fx_id < 0 || fx_id >= FX_SLOT_COUNT) return;

    fx_param_cache[fx_id] = {p1, p2, mix};

    switch (fx_id) {
    case 0: {
        Delay *effect = delay_effect;
        if (!effect && fx_enabled[fx_id]) {
            effect = ensure_delay_effect();
        }
        if (effect) {
            effect->set_delay_ms(p1);
            effect->set_feedback(static_cast<int16_t>(p2));
            effect->set_mix(static_cast<int16_t>(mix));
        }
        break;
    }
    case 1:
        if (distortion_effect || fx_enabled[fx_id]) {
            ensure_distortion_effect()->set_params(p1, p2, mix);
        }
        break;
    case 2:
        if (reverb_effect || fx_enabled[fx_id]) {
            ensure_reverb_effect()->set_params(unit_interval(p1, 1000.0f),
                                               unit_interval(p2, 32767.0f),
                                               unit_interval(mix, 32767.0f));
        }
        break;
    case 3:
        if (chorus_effect || fx_enabled[fx_id]) {
            ensure_chorus_effect()->set_params(p1, p2, mix);
        }
        break;
    case 4:
        if (reverb_sc_effect || fx_enabled[fx_id]) {
            ensure_reverb_sc_effect()->set_params(unit_interval(p1, 1000.0f),
                                                  unit_interval(p2, 32767.0f),
                                                  unit_interval(mix, 32767.0f));
        }
        break;
    case 5:
        ensure_compressor_effect()->set_params(p1, p2, mix);
        break;
    default:
        break;
    }
}

const char *Synth::get_notes_playing_names() {
    static char buffer[64];
    int pos = 0;

    for (int note = 0; note < 128; ++note) {
        if (notes_playing_bitset.test(note)) {
            const char *name = midi_note_names[note];
            const int written =
                std::snprintf(buffer + pos, sizeof(buffer) - pos, "%s,", name);
            if (written < 0 || written >= static_cast<int>(sizeof(buffer) - pos)) {
                break;
            }
            pos += written;
        }
    }

    if (pos > 0 && buffer[pos - 1] == ',') {
        buffer[pos - 1] = '\0';
    } else {
        buffer[pos] = '\0';
    }

    return buffer;
}

void Synth::cycle_filter_type() {
    current_filter_type =
        static_cast<FilterType>((current_filter_type + 1) % NUM_FILTER_TYPES);
}

void Synth::set_filter_cutoff(float cutoff, float q) {
    switch (current_filter_type) {
    case FILTER_LOW_PASS:
        low_pass.set_cutoff_freq(cutoff);
        break;
    case FILTER_CHEBYSHEV:
        low_pass_cheb.set_cutoff_freq(cutoff, q);
        break;
    default:
        break;
    }
}

float Synth::get_filter_cutoff() {
    switch (current_filter_type) {
    case FILTER_LOW_PASS:
        return low_pass.get_cutoff();
    case FILTER_CHEBYSHEV:
        return low_pass_cheb.get_cutoff();
    default:
        return 0.0f;
    }
}

std::size_t Synth::active_runtime_bytes() const {
    switch (current_engine) {
    case SynthEngine::FM:
        return fm_voices ? sizeof(*fm_voices) : 0;
    case SynthEngine::KarplusStrong:
        return karplus_voices ? sizeof(*karplus_voices) : 0;
    case SynthEngine::Modal:
        return modal_voices ? sizeof(*modal_voices) : 0;
    default:
        return 0;
    }
}

std::size_t Synth::active_fx_bytes() const {
    std::size_t total = 0;
    if (delay_effect) total += fx_slot_bytes(0);
    if (distortion_effect) total += fx_slot_bytes(1);
    if (reverb_effect) total += fx_slot_bytes(2);
    if (chorus_effect) total += fx_slot_bytes(3);
    if (reverb_sc_effect) total += fx_slot_bytes(4);
    if (compressor_effect) total += fx_slot_bytes(5);
    return total;
}

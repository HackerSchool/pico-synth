// Optimized Oscillator.hpp
#include "config.hpp"
class Oscillator {
private:
    const std::vector<int16_t>* wavetable_;
    uint32_t pos_fixed;           // Fixed-point position (16.16 format)
    uint32_t step_fixed;          // Fixed-point step size (16.16 format)
    std::array<int16_t, SAMPLES_PER_BUFFER> output;

public:
    Oscillator() : wavetable_(&sine_wave_table), pos_fixed(0), step_fixed(0) {}
    
    Oscillator(WaveType wave_type, float freq) : pos_fixed(0) {
        // Choose the wavetable based on wave type
        switch (wave_type) {
        case Sine:
            wavetable_ = &sine_wave_table;
            break;
        case Square:
            wavetable_ = &square_wave_table;
            break;
        case Triangle:
            wavetable_ = &triangle_wave_table;
            break;
        case Sawtooth:
            wavetable_ = &sawtooth_wave_table;
            break;
        default:
            wavetable_ = &sine_wave_table;
            break;
        }
        set_freq(freq);
    }
    
    void out() {
        const uint32_t pos_mask = (WAVE_TABLE_LEN << 16) - 1;
        const uint32_t table_len = WAVE_TABLE_LEN;
        
        for (int i = 0; i < SAMPLES_PER_BUFFER; i++) {
            // Extract the integer part of the position (top 16 bits)
            uint32_t pos_int = pos_fixed >> 16;
            output[i] = (*wavetable_)[pos_int];
            
            // Increment position and wrap around using bitwise operations
            pos_fixed = (pos_fixed + step_fixed) & pos_mask;
        }
    }
    
    std::array<int16_t, SAMPLES_PER_BUFFER>& get_output() {
        return output;
    }
    
    void set_freq(float new_freq) {
        // Convert to fixed-point (16.16 format)
        step_fixed = static_cast<uint32_t>((WAVE_TABLE_LEN * new_freq / 44100.0f) * 65536.0f);
    }
};

// Optimized Envelope.hpp
class ADSREnvelope {
private:
    enum EnvelopeState {
        ENV_IDLE,
        ENV_ATTACK,
        ENV_DECAY,
        ENV_SUSTAIN,
        ENV_RELEASE
    };

    // Use fixed-point arithmetic for envelope (8.24 format)
    uint32_t a_fixed;
    uint32_t d_fixed;
    uint32_t s_fixed;
    uint32_t r_fixed;
    uint32_t t_fixed;
    uint32_t release_start_fixed;
    uint32_t current_scale_fixed;
    
    EnvelopeState state;
    std::array<int16_t, SAMPLES_PER_BUFFER> output;
    std::array<int16_t, SAMPLES_PER_BUFFER>* in_signal;
    bool triggered;

    // Precompute constants
    static constexpr uint32_t FIXED_ONE = 16777216; // 1.0 in 8.24 format

public:
    ADSREnvelope() : a_fixed(8388608), d_fixed(1677722), s_fixed(6710886), 
                     r_fixed(16777216), in_signal(nullptr), triggered(false), 
                     t_fixed(0), current_scale_fixed(0), state(ENV_IDLE) {}
    
    ADSREnvelope(float a, float d, float s, float r, std::array<int16_t, SAMPLES_PER_BUFFER>& in_signal, float trigger)
        : in_signal(&in_signal), triggered(trigger > 4.5f) {
        // Convert to fixed-point (8.24 format)
        a_fixed = static_cast<uint32_t>(a * FIXED_ONE);
        d_fixed = static_cast<uint32_t>(d * FIXED_ONE);
        s_fixed = static_cast<uint32_t>(s * FIXED_ONE);
        r_fixed = static_cast<uint32_t>(r * FIXED_ONE);
        t_fixed = 0;
        current_scale_fixed = 0;
        state = ENV_IDLE;
    }
    
    void out() {
        uint32_t scale_fixed = 0;
        
        // Note released - only change state if needed
        if (!triggered && state != ENV_RELEASE && state != ENV_IDLE) {
            release_start_fixed = current_scale_fixed;
            t_fixed = 0;
            state = ENV_RELEASE;
        }
        
        // Process the entire buffer with the same envelope state when possible
        switch (state) {
            case ENV_ATTACK: {
                if (t_fixed >= a_fixed) {
                    // Entire buffer in decay
                    state = ENV_DECAY;
                    t_fixed = 0;
                    process_buffer_decay();
                } else if (t_fixed + SAMPLES_PER_BUFFER * SAMPLE_DELTA_FIXED >= a_fixed) {
                    // Transition from attack to decay during this buffer
                    uint32_t samples_in_attack = (a_fixed - t_fixed) / SAMPLE_DELTA_FIXED;
                    process_buffer_attack_decay(samples_in_attack);
                } else {
                    // Entire buffer in attack
                    process_buffer_attack();
                }
                break;
            }
            case ENV_DECAY: {
                if (t_fixed >= d_fixed) {
                    // Entire buffer in sustain
                    state = ENV_SUSTAIN;
                    t_fixed = 0;
                    process_buffer_sustain();
                } else if (t_fixed + SAMPLES_PER_BUFFER * SAMPLE_DELTA_FIXED >= d_fixed) {
                    // Transition from decay to sustain
                    uint32_t samples_in_decay = (d_fixed - t_fixed) / SAMPLE_DELTA_FIXED;
                    process_buffer_decay_sustain(samples_in_decay);
                } else {
                    // Entire buffer in decay
                    process_buffer_decay();
                }
                break;
            }
            case ENV_SUSTAIN: {
                // No change in envelope during sustain
                process_buffer_sustain();
                break;
            }
            case ENV_RELEASE: {
                if (triggered) {
                    // Note retriggered
                    state = ENV_ATTACK;
                    t_fixed = 0;
                    process_buffer_attack();
                } else if (t_fixed >= r_fixed) {
                    // Envelope complete
                    state = ENV_IDLE;
                    process_buffer_idle();
                } else if (t_fixed + SAMPLES_PER_BUFFER * SAMPLE_DELTA_FIXED >= r_fixed) {
                    // Transition from release to idle
                    uint32_t samples_in_release = (r_fixed - t_fixed) / SAMPLE_DELTA_FIXED;
                    process_buffer_release_idle(samples_in_release);
                } else {
                    // Entire buffer in release
                    process_buffer_release();
                }
                break;
            }
            case ENV_IDLE: {
                if (triggered) {
                    // Note triggered
                    state = ENV_ATTACK;
                    t_fixed = 0;
                    process_buffer_attack();
                } else {
                    // Stay idle
                    process_buffer_idle();
                }
                break;
            }
        }
    }
    
    void set_trigger(float trig) {
        triggered = trig > 4.5f;
    }
    
    std::array<int16_t, SAMPLES_PER_BUFFER>& get_output() {
        return output;
    }

private:
    // Specialized processing functions for each envelope state
    void process_buffer_attack() {
        for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
            current_scale_fixed = (t_fixed * FIXED_ONE) / a_fixed;
            output[i] = static_cast<int16_t>(((*in_signal)[i] * current_scale_fixed) >> 24);
            t_fixed += SAMPLE_DELTA_FIXED;
        }
    }
    
    void process_buffer_decay() {
        uint32_t one_minus_s = FIXED_ONE - s_fixed;
        for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
            current_scale_fixed = FIXED_ONE - ((one_minus_s * t_fixed) / d_fixed);
            output[i] = static_cast<int16_t>(((*in_signal)[i] * current_scale_fixed) >> 24);
            t_fixed += SAMPLE_DELTA_FIXED;
        }
    }
    
    void process_buffer_sustain() {
        // Apply the same scale to all samples in sustain
        for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
            current_scale_fixed = s_fixed;
            output[i] = static_cast<int16_t>(((*in_signal)[i] * s_fixed) >> 24);
        }
    }
    
    void process_buffer_release() {
        for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
            current_scale_fixed = release_start_fixed * (FIXED_ONE - (t_fixed / r_fixed));
            output[i] = static_cast<int16_t>(((*in_signal)[i] * current_scale_fixed) >> 24);
            t_fixed += SAMPLE_DELTA_FIXED;
        }
    }
    
    void process_buffer_idle() {
        // Clear the output buffer - all zeros
        memset(output.data(), 0, output.size() * sizeof(int16_t));
    }
    
    // Specialized functions for state transitions
    void process_buffer_attack_decay(uint32_t samples_in_attack) {
        uint i = 0;
        // Process attack phase
        for (; i < samples_in_attack; i++) {
            current_scale_fixed = (t_fixed * FIXED_ONE) / a_fixed;
            output[i] = static_cast<int16_t>(((*in_signal)[i] * current_scale_fixed) >> 24);
            t_fixed += SAMPLE_DELTA_FIXED;
        }
        
        // Transition to decay
        state = ENV_DECAY;
        t_fixed = 0;
        
        // Process decay phase
        uint32_t one_minus_s = FIXED_ONE - s_fixed;
        for (; i < SAMPLES_PER_BUFFER; i++) {
            current_scale_fixed = FIXED_ONE - ((one_minus_s * t_fixed) / d_fixed);
            output[i] = static_cast<int16_t>(((*in_signal)[i] * current_scale_fixed) >> 24);
            t_fixed += SAMPLE_DELTA_FIXED;
        }
    }
    
    void process_buffer_decay_sustain(uint32_t samples_in_decay) {
        uint i = 0;
        // Process decay phase
        uint32_t one_minus_s = FIXED_ONE - s_fixed;
        for (; i < samples_in_decay; i++) {
            current_scale_fixed = FIXED_ONE - ((one_minus_s * t_fixed) / d_fixed);
            output[i] = static_cast<int16_t>(((*in_signal)[i] * current_scale_fixed) >> 24);
            t_fixed += SAMPLE_DELTA_FIXED;
        }
        
        // Transition to sustain
        state = ENV_SUSTAIN;
        
        // Process sustain phase
        for (; i < SAMPLES_PER_BUFFER; i++) {
            output[i] = static_cast<int16_t>(((*in_signal)[i] * s_fixed) >> 24);
        }
    }
    
    void process_buffer_release_idle(uint32_t samples_in_release) {
        uint i = 0;
        // Process release phase
        for (; i < samples_in_release; i++) {
            current_scale_fixed = release_start_fixed * (FIXED_ONE - (t_fixed / r_fixed));
            output[i] = static_cast<int16_t>(((*in_signal)[i] * current_scale_fixed) >> 24);
            t_fixed += SAMPLE_DELTA_FIXED;
        }
        
        // Transition to idle
        state = ENV_IDLE;
        
        // Clear the rest of the buffer
        memset(&output[i], 0, (SAMPLES_PER_BUFFER - i) * sizeof(int16_t));
    }
};

// Optimized Synth.hpp
class Synth {
private:
    std::array<Oscillator, NUM_OSC> oscillators;
    std::array<ADSREnvelope, NUM_OSC> envelopes;
    std::array<int16_t, SAMPLES_PER_BUFFER> output;
    std::array<uint8_t, NUM_OSC> osc_midi_note;
    std::array<bool, NUM_OSC> osc_playing;
    std::array<uint8_t, NUM_OSC> osc_age;  // Age counter for voice stealing
    uint8_t voice_counter;                 // Counter for tracking voice allocation
    
    // Temp buffer for mixing
    std::array<int32_t, SAMPLES_PER_BUFFER> mix_buffer;

public:
    Synth() : voice_counter(0) {
        // Initialize oscillators and envelopes
        for (int i = 0; i < NUM_OSC; i++) {
            oscillators[i] = Oscillator(Square, 440.f);
            envelopes[i] = ADSREnvelope(0.5f, 0.1f, 0.4f, 1.f,
                                      oscillators[i].get_output(), 0.f);
            osc_playing[i] = false;
            osc_midi_note[i] = 0;
            osc_age[i] = 0;
        }
    }
    
    void out() {
        // Clear the mix buffer
        memset(mix_buffer.data(), 0, mix_buffer.size() * sizeof(int32_t));
        
        // Count active voices
        int active_voices = 0;
        
        // Process active voices
        for (int i = 0; i < NUM_OSC; i++) {
            if (osc_playing[i]) {
                active_voices++;
                
                // Process oscillator and envelope
                oscillators[i].out();
                envelopes[i].out();
                
                // Mix into buffer - directly accumulate
                std::array<int16_t, SAMPLES_PER_BUFFER>& env_out = envelopes[i].get_output();
                for (int k = 0; k < SAMPLES_PER_BUFFER; k++) {
                    mix_buffer[k] += env_out[k];
                }
            }
        }
        
        // Scale and clip the final output
        int divisor = (active_voices > 0) ? active_voices : 1;
        for (int k = 0; k < SAMPLES_PER_BUFFER; k++) {
            // Scale by number of active voices and clip
            int32_t sample = mix_buffer[k] / divisor;
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            output[k] = static_cast<int16_t>(sample);
        }
    }
    
    std::array<int16_t, SAMPLES_PER_BUFFER>& get_output() { 
        return output; 
    }
    
    void process_midi_packet(uint8_t packet[4]) {
        uint8_t msg_type = packet[1] & 0xF0;
        uint8_t note = packet[2];
        uint8_t velocity = packet[3];
        
        switch (msg_type) {
        case 0x90: // Note On
            if (velocity > 0) {
                note_on(note, velocity);
            } else {
                note_off(note, velocity);
            }
            break;
        case 0x80: // Note Off
            note_off(note, velocity);
            break;
        case 0xB0: // Control Change
            // Handle control change
            break;
        }
    }
    
    void note_on(uint8_t note, uint8_t velocity) {
        // Check if note is already playing
        for (int i = 0; i < NUM_OSC; i++) {
            if (osc_midi_note[i] == note && osc_playing[i]) {
                // Note already playing - retrigger
                envelopes[i].set_trigger(5.f);
                osc_age[i] = voice_counter++; // Update age
                return;
            }
        }
        
        // Find a free voice
        int free_voice = -1;
        for (int i = 0; i < NUM_OSC; i++) {
            if (!osc_playing[i]) {
                free_voice = i;
                break;
            }
        }
        
        // If no free voice, use voice stealing
        if (free_voice == -1) {
            free_voice = find_voice_to_steal();
        }
        
        // Assign note to voice
        osc_playing[free_voice] = true;
        osc_midi_note[free_voice] = note;
        osc_age[free_voice] = voice_counter++; // Update age
        
        // Set oscillator and envelope
        float freq = midi_to_freq(note);
        oscillators[free_voice].set_freq(freq);
        envelopes[free_voice].set_trigger(5.f);
    }
    
    void note_off(uint8_t note, uint8_t velocity) {
        for (int i = 0; i < NUM_OSC; i++) {
            if (osc_midi_note[i] == note && osc_playing[i]) {
                envelopes[i].set_trigger(0.f);
                // Note: We don't set osc_playing to false here
                // The voice will be marked as free after the release phase
                break;
            }
        }
    }
    
private:
    int find_voice_to_steal() {
        // Simple algorithm: steal the oldest voice
        uint8_t oldest = 255;
        int oldest_index = 0;
        
        for (int i = 0; i < NUM_OSC; i++) {
            uint8_t age_diff = voice_counter - osc_age[i];
            if (age_diff > oldest) {
                oldest = age_diff;
                oldest_index = i;
            }
        }
        
        return oldest_index;
    }
};

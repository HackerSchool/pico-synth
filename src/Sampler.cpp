// Sampler.cpp implementation

#include "Sampler.hpp"
#include <cstring>
#include <algorithm>
#include <stdio.h>

// Static members
FATFS Sampler::fs;
bool Sampler::fs_mounted = false;

// SamplePlayer Implementation
SamplePlayer::SamplePlayer() 
    : file_open(false), playing(false), buffer_pos(0), buffer_valid(0), 
      eof_reached(false), sample_rate(44100), channels(1), bits_per_sample(16),
      data_start_pos(44), data_size(0), volume(1.0f), loop_enabled(false) {
}

SamplePlayer::~SamplePlayer() {
    unload_sample();
}

bool SamplePlayer::parse_wav_header() {
    uint8_t header[44];
    UINT bytes_read;
    
    // Read header
    if (f_read(&file, header, 44, &bytes_read) != FR_OK || bytes_read != 44) {
        return false;
    }
    
    // Check "RIFF" and "WAVE"
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }
    
    // Extract info (little-endian)
    channels = header[22] | (header[23] << 8);
    sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    bits_per_sample = header[34] | (header[35] << 8);
    
    // Get data chunk size
    data_size = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);
    
    // Only support 16-bit mono/stereo for now
    if (bits_per_sample != 16 || channels > 2) {
        return false;
    }
    
    data_start_pos = 44;
    return true;
}

bool SamplePlayer::load_sample(const std::string& filename) {
    // Unload any existing sample
    unload_sample();
    
    // Open file
    if (f_open(&file, filename.c_str(), FA_READ) != FR_OK) {
        printf("Failed to open: %s\n", filename.c_str());
        return false;
    }
    
    // Parse header
    if (!parse_wav_header()) {
        printf("Invalid WAV file: %s\n", filename.c_str());
        f_close(&file);
        return false;
    }
    
    // Seek to data start
    f_lseek(&file, data_start_pos);
    
    file_open = true;
    this->filename = filename;
    buffer_pos = 0;
    buffer_valid = 0;
    eof_reached = false;
    playing = false;
    
    printf("Loaded: %s (%ldHz, %dch)\n", filename.c_str(), sample_rate, channels);
    return true;
}

void SamplePlayer::unload_sample() {
    if (file_open) {
        f_close(&file);
        file_open = false;
        playing = false;
        filename.clear();
    }
}

void SamplePlayer::trigger() {
    if (!file_open) return;
    
    // Reset to beginning
    f_lseek(&file, data_start_pos);
    playing = true;
    buffer_pos = 0;
    buffer_valid = 0;
    eof_reached = false;
}

void SamplePlayer::stop() {
    playing = false;
}

void SamplePlayer::render_buffer(std::array<int16_t, SAMPLES_PER_BUFFER>& buffer) {
    buffer.fill(0);  // Clear buffer first
    
    if (!playing || !file_open || eof_reached) {
        return;  // Return silence
    }
    
    size_t samples_filled = 0;
    
    while (samples_filled < SAMPLES_PER_BUFFER && !eof_reached) {
        // Refill read buffer if needed
        if (buffer_pos >= buffer_valid) {
            UINT bytes_read;
            FRESULT result = f_read(&file, read_buffer, SD_READ_BUFFER_SIZE, &bytes_read);
            
            if (result != FR_OK || bytes_read == 0) {
                // End of file reached
                if (loop_enabled && file_open) {
                    // Loop back to beginning
                    f_lseek(&file, data_start_pos);
                    buffer_pos = 0;
                    buffer_valid = 0;
                    continue;  // Try reading again
                } else {
                    // Stop playing
                    eof_reached = true;
                    playing = false;
                    break;
                }
            }
            
            buffer_valid = bytes_read;
            buffer_pos = 0;
        }
        
        // Calculate samples to copy
        size_t bytes_available = buffer_valid - buffer_pos;
        size_t samples_available = bytes_available / (2 * channels);  // 16-bit samples
        size_t samples_to_copy = std::min(samples_available, SAMPLES_PER_BUFFER - samples_filled);
        
        // Copy samples with volume scaling
        int16_t* src = (int16_t*)&read_buffer[buffer_pos];
        
        if (channels == 1) {
            // Mono
            for (size_t i = 0; i < samples_to_copy; i++) {
                // int32_t sample = (int32_t)(src[i] * volume);
                int32_t sample = (int32_t)(src[i]); 
                // Clamp
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                buffer[samples_filled + i] = (int16_t)sample;
            }
            buffer_pos += samples_to_copy * 2;
        } else {
            // Stereo - mix down to mono
            for (size_t i = 0; i < samples_to_copy; i++) {
                int32_t left = src[i * 2];
                int32_t right = src[i * 2 + 1];
                // int32_t mono = ((left + right) / 2) * volume;
                int32_t mono = ((left + right) / 2); 
                // Clamp
                if (mono > 32767) mono = 32767;
                if (mono < -32768) mono = -32768;
                buffer[samples_filled + i] = (int16_t)mono;
            }
            buffer_pos += samples_to_copy * 4;  // 2 channels * 2 bytes
        }
        
        samples_filled += samples_to_copy;
    }
}

// Sampler Implementation
Sampler::Sampler() {
}

Sampler::~Sampler() {
    stop_all();
}

bool Sampler::init() {
    if (!fs_mounted) {
        if (f_mount(&fs, "", 1) != FR_OK) {
            printf("Failed to mount SD card for sampler\n");
            return false;
        }
        fs_mounted = true;
        printf("Sampler: SD card mounted\n");
    }
    return true;
}

bool Sampler::load_sample(uint8_t player_id, const std::string& filename) {
    if (player_id >= SAMPLE_PLAYER_NUM) return false;
    return players[player_id].load_sample(filename);
}

void Sampler::unload_sample(uint8_t player_id) {
    if (player_id >= SAMPLE_PLAYER_NUM) return;
    players[player_id].unload_sample();
}

void Sampler::trigger_player(uint8_t player_id) {
    if (player_id >= SAMPLE_PLAYER_NUM) return;
    players[player_id].trigger();
}

void Sampler::stop_player(uint8_t player_id) {
    if (player_id >= SAMPLE_PLAYER_NUM) return;
    players[player_id].stop();
}

void Sampler::stop_all() {
    for (auto& player : players) {
        player.stop();
    }
}

void Sampler::set_player_volume(uint8_t player_id, float volume) {
    if (player_id >= SAMPLE_PLAYER_NUM) return;
    players[player_id].set_volume(volume);
}

void Sampler::set_player_loop(uint8_t player_id, bool enable) {
    if (player_id >= SAMPLE_PLAYER_NUM) return;
    players[player_id].set_loop(enable);
}

SamplePlayer* Sampler::get_player(uint8_t player_id) {
    if (player_id >= SAMPLE_PLAYER_NUM) return nullptr;
    return &players[player_id];
}

void Sampler::out(std::array<int16_t, SAMPLES_PER_BUFFER>& buffer) {
    buffer.fill(0);  // Clear output buffer
    
    std::array<int16_t, SAMPLES_PER_BUFFER> player_buffer;
    
    // Mix all active players
    for (auto& player : players) {
        if (player.is_playing()) {
            player.render_buffer(player_buffer);
            
            // Mix into main buffer
            for (size_t i = 0; i < SAMPLES_PER_BUFFER; i++) {
                int32_t mixed = (int32_t)buffer[i] + (int32_t)player_buffer[i];
                // Clamp to prevent overflow
                if (mixed > 32767) mixed = 32767;
                if (mixed < -32768) mixed = -32768;
                buffer[i] = (int16_t)mixed;
            }
        }
    }
}

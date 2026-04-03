// Sampler.cpp implementation

#include "Sampler.hpp"
#include <cstring>
#include <algorithm>
#include <stdio.h>
#include <cctype>

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
    uint8_t buffer[12];
    UINT bytes_read;
    
    // Read RIFF header
    if (f_read(&file, buffer, 12, &bytes_read) != FR_OK || bytes_read != 12) {
        return false;
    }
    
    // Check "RIFF" and "WAVE"
    if (memcmp(buffer, "RIFF", 4) != 0 || memcmp(buffer + 8, "WAVE", 4) != 0) {
        return false;
    }
    
    // Initialize defaults
    channels = 0;
    sample_rate = 0;
    bits_per_sample = 0;
    data_size = 0;
    data_start_pos = 0;
    
    // Parse chunks
    while (true) {
        uint8_t chunk_header[8];
        
        // Read chunk ID and size
        if (f_read(&file, chunk_header, 8, &bytes_read) != FR_OK || bytes_read != 8) {
            break;  // End of file or error
        }
        
        // Get chunk size (little-endian)
        uint32_t chunk_size = chunk_header[4] | (chunk_header[5] << 8) | 
                             (chunk_header[6] << 16) | (chunk_header[7] << 24);
        
        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            // Format chunk
            if (chunk_size < 16) {
                return false;  // Invalid format chunk
            }
            
            uint8_t fmt_data[16];
            if (f_read(&file, fmt_data, 16, &bytes_read) != FR_OK || bytes_read != 16) {
                return false;
            }
            
            // Extract format info (little-endian)
            uint16_t audio_format = fmt_data[0] | (fmt_data[1] << 8);
            channels = fmt_data[2] | (fmt_data[3] << 8);
            sample_rate = fmt_data[4] | (fmt_data[5] << 8) | 
                         (fmt_data[6] << 16) | (fmt_data[7] << 24);
            bits_per_sample = fmt_data[14] | (fmt_data[15] << 8);
            
            // Only support PCM (format 1) and 16-bit
            if (audio_format != 1 || bits_per_sample != 16 || channels > 2) {
                return false;
            }
            
            // Skip any remaining bytes in format chunk
            if (chunk_size > 16) {
                f_lseek(&file, f_tell(&file) + (chunk_size - 16));
            }
            
        } else if (memcmp(chunk_header, "data", 4) == 0) {
            // Data chunk - this is where audio starts
            data_size = chunk_size;
            data_start_pos = f_tell(&file);
            
            // We found the data chunk, we're done parsing
            break;
            
        } else {
            // Unknown chunk - skip it
            f_lseek(&file, f_tell(&file) + chunk_size);
        }
        
        // Ensure we're on even byte boundary (WAV chunks are word-aligned)
        if (chunk_size & 1) {
            f_lseek(&file, f_tell(&file) + 1);
        }
    }
    
    // Verify we found all required chunks
    if (channels == 0 || sample_rate == 0 || data_size == 0 || data_start_pos == 0) {
        return false;
    }
    
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
            // Calculate how many bytes we can still read from data chunk
            uint32_t current_pos = f_tell(&file);
            uint32_t bytes_remaining_in_data = (data_start_pos + data_size) - current_pos;

            if (bytes_remaining_in_data == 0) {
                // We've reached the end of the actual audio data
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

            // Don't read beyond the data chunk
            UINT bytes_to_read = (SD_READ_BUFFER_SIZE < bytes_remaining_in_data) ?
                                SD_READ_BUFFER_SIZE : (UINT)bytes_remaining_in_data;
            UINT bytes_read;
            FRESULT result = f_read(&file, read_buffer, bytes_to_read, &bytes_read);

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
        size_t samples_available = (channels == 1) ?
                                  (bytes_available >> 1) :      // Mono: divide by 2
                                  (bytes_available >> 2);       // Stereo: divide by 4
        size_t samples_to_copy = (samples_available < (SAMPLES_PER_BUFFER - samples_filled)) ?
                                samples_available : (SAMPLES_PER_BUFFER - samples_filled);

        // Fast copy for mono: memcpy the raw sample bytes directly into buffer
        int16_t* src = (int16_t*)&read_buffer[buffer_pos];
        if (channels == 1) {
            // Mono: raw copy (samples are already 16-bit signed)
            memcpy(&buffer[samples_filled], src, samples_to_copy * sizeof(int16_t));
            buffer_pos += samples_to_copy * 2;
        } else {
            // Stereo - mix down to mono (simple average). This range fits in int16.
            for (size_t i = 0; i < samples_to_copy; i++) {
                int32_t left = src[i * 2];
                int32_t right = src[i * 2 + 1];
                int16_t mono = (int16_t)((left + right) >> 1);
                buffer[samples_filled + i] = mono;
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
    // Use a 32-bit accumulation buffer to avoid clamping on every player add
    int32_t accum[SAMPLES_PER_BUFFER];
    for (size_t i = 0; i < SAMPLES_PER_BUFFER; ++i) accum[i] = 0;

    std::array<int16_t, SAMPLES_PER_BUFFER> player_buffer;

    // Accumulate samples from all active players into accum[]
    for (auto& player : players) {
        if (!player.is_playing()) continue;
        player.render_buffer(player_buffer);
        for (size_t i = 0; i < SAMPLES_PER_BUFFER; ++i) {
            accum[i] += (int32_t)player_buffer[i];
        }
    }

    // Convert back to 16-bit with a single clamp pass
    for (size_t i = 0; i < SAMPLES_PER_BUFFER; ++i) {
        int32_t v = accum[i];
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        buffer[i] = (int16_t)v;
    }
}

// WAV File List Implementation
WavFileList::WavFileList() : count(0), capacity(MAX_WAV_FILES) {
    filenames = new std::string[capacity];
}

WavFileList::~WavFileList() {
    delete[] filenames;
}

bool WavFileList::add_file(const std::string& filename) {
    if (count >= capacity) return false;
    filenames[count] = filename;
    ++count;
    return true;
}

const std::string& WavFileList::get_filename(int index) const {
    static const std::string empty_string;
    if (index < 0 || index >= count) return empty_string;
    return filenames[index];
}

void WavFileList::clear() {
    count = 0;
}

void WavFileList::print_files() const {
    if (count == 0) {
        printf("No WAV files found.\n");
        return;
    }
    
    printf("Found %d WAV files:\n", count);
    for (int i = 0; i < count; i++) {
        printf("  %d: %s\n", i + 1, filenames[i].c_str());
    }
}

// Helper function to check if filename has .wav extension (case insensitive)
static bool is_wav_file(const std::string& filename) {
    if (filename.length() < 4) return false;
    
    std::string ext = filename.substr(filename.length() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".wav";
}

// Helper function to recursively scan directory for WAV files
FRESULT Sampler::scan_directory_for_wav(WavFileList& list, const std::string& path, bool recursive) {
    FRESULT fr;
    DIR dir;
    FILINFO fno;
    
    fr = f_opendir(&dir, path.c_str());
    if (fr != FR_OK) return fr;
    
    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;  // Break on error or end of dir
        
        // Skip hidden files and directories starting with '.'
        if (fno.fname[0] == '.') continue;
        
        // Construct full path
        std::string current_path;
        if (path == "/") {
            current_path = "/" + std::string(fno.fname);
        } else {
            current_path = path + "/" + std::string(fno.fname);
        }
        
        if (fno.fattrib & AM_DIR) {
            // It's a directory - recurse if requested
            if (recursive && list.get_count() < list.get_capacity()) {
                scan_directory_for_wav(list, current_path, recursive);
            }
        } else {
            // It's a file - check if it's a WAV file
            if (is_wav_file(fno.fname)) {
                if (!list.add_file(current_path)) {
                    // List is full
                    break;
                }
            }
        }
    }
    
    f_closedir(&dir);
    return fr;
}

WavFileList Sampler::list_wav_files(bool recursive) {
    WavFileList list;
    
    if (!fs_mounted) {
        printf("SD card not mounted\n");
        return list;
    }
    
    // Scan the root directory
    FRESULT fr = scan_directory_for_wav(list, "/", recursive);
    if (fr != FR_OK) {
        printf("Error scanning directory: %d\n", fr);
        list.clear();
    }
    
    return list;
}

bool Sampler::load_sample_by_index(uint8_t player_id, int file_index, bool recursive) {
    WavFileList wav_files = list_wav_files(recursive);
    
    if (file_index < 0 || file_index >= wav_files.get_count()) {
        printf("Invalid file index: %d (available: 0-%d)\n", file_index, wav_files.get_count() - 1);
        return false;
    }
    
    const std::string& filename = wav_files.get_filename(file_index);
    printf("Loading file %d: %s\n", file_index, filename.c_str());
    
    return load_sample(player_id, filename);
}
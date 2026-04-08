// Sampler.hpp
#pragma once

#include <array>
#include <string>
#include <cstdint>
#include "ff.h"
#include "config.hpp"  // For SAMPLES_PER_BUFFER

#define SAMPLE_PLAYER_NUM 8  // Number of concurrent sample players
#define SD_READ_BUFFER_SIZE 2048
#define MAX_WAV_FILES 100

class SamplePlayer {
private:
    FIL file;
    bool file_open;
    bool playing;
    uint8_t read_buffer[SD_READ_BUFFER_SIZE];
    size_t buffer_pos;
    size_t buffer_valid;
    bool eof_reached;
    
    // WAV file info
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_start_pos;
    uint32_t data_size;

    std::string filename;
    
    // Playback control
    float volume;
    bool loop_enabled;
    
    bool parse_wav_header();
    
public:
    SamplePlayer();
    ~SamplePlayer();
    
    // Sample management
    bool load_sample(const std::string& filename);
    void unload_sample();
    bool is_loaded() const { return file_open; }
    
    // Playback control
    void trigger();
    void stop();
    bool is_playing() const { return playing; }
    
    // Parameters
    void set_volume(float vol) { volume = vol; }
    float get_volume() const { return volume; }
    void set_loop(bool enable) { loop_enabled = enable; }
    bool get_loop() const { return loop_enabled; }
    
    // Audio rendering
    void render_buffer(std::array<int16_t, SAMPLES_PER_BUFFER>& buffer);
    
    // Info
    uint32_t get_sample_rate() const { return sample_rate; }
    uint16_t get_channels() const { return channels; }
    const std::string& get_filename() const { return filename; }
    
};

class WavFileList {
private:
    std::array<std::string, MAX_WAV_FILES> filenames{};
    int count;

public:
    WavFileList();
    
    bool add_file(const std::string& filename);
    const std::string& get_filename(int index) const;
    int get_count() const { return count; }
    int get_capacity() const { return MAX_WAV_FILES; }
    void clear();
    void print_files() const;
        
};

class Sampler {
private:
    std::array<SamplePlayer, SAMPLE_PLAYER_NUM> players;
    static FATFS fs;
    static bool fs_mounted;
    FRESULT scan_directory_for_wav(WavFileList& list, const std::string& path, bool recursive);
    
public:
    Sampler();
    ~Sampler();
    
    // Initialization
    bool init();
    
    // Sample management
    bool load_sample(uint8_t player_id, const std::string& filename);
    void unload_sample(uint8_t player_id);
    
    // Playback control
    void trigger_player(uint8_t player_id);
    void stop_player(uint8_t player_id);
    void stop_all();
    
    // Parameters
    void set_player_volume(uint8_t player_id, float volume);
    void set_player_loop(uint8_t player_id, bool enable);
    
    // Audio rendering - main output function
    void out(std::array<int16_t, SAMPLES_PER_BUFFER>& buffer);
    
    // Info
    SamplePlayer* get_player(uint8_t player_id);
    uint8_t get_num_players() const { return SAMPLE_PLAYER_NUM; }

    // WAV file listing functions
    WavFileList list_wav_files(bool recursive = true);
    bool load_sample_by_index(uint8_t player_id, int file_index, bool recursive = true);
};

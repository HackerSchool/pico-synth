// Sampler.hpp
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "config.hpp"
#include "ff.h"

#define SAMPLE_PLAYER_NUM 8
#define SD_READ_BUFFER_SIZE 2048
#define MAX_WAV_FILES 100

class SamplePlayer {
  public:
    static constexpr std::size_t HEAD_CACHE_SAMPLES = 1024;
    static constexpr std::size_t STREAM_BUFFER_SAMPLES = 2048;
    static constexpr std::size_t STREAM_REFILL_LOW_WATER =
        STREAM_BUFFER_SAMPLES / 2;
    static constexpr std::size_t RAM_RESIDENT_MAX_SAMPLES = 32768;

    enum class StorageMode : uint8_t {
        Unloaded = 0,
        RamResident,
        Streaming
    };

    SamplePlayer();
    ~SamplePlayer();

    bool load_sample(const std::string &filename, uint8_t *shared_read_buffer,
                     std::size_t shared_read_buffer_size,
                     std::size_t max_ram_resident_bytes);
    void unload_sample();
    bool is_loaded() const { return file_open; }

    void trigger(uint8_t *shared_read_buffer,
                 std::size_t shared_read_buffer_size);
    void stop();
    bool is_playing() const { return playing; }

    void set_volume(float vol) { volume = vol; }
    float get_volume() const { return volume; }
    void set_loop(bool enable) { loop_enabled = enable; }
    bool get_loop() const { return loop_enabled; }

    void render_buffer(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                       uint8_t *shared_read_buffer,
                       std::size_t shared_read_buffer_size);

    uint32_t get_sample_rate() const { return sample_rate; }
    uint16_t get_channels() const { return channels; }
    const std::string &get_filename() const { return filename; }
    uint32_t get_total_samples() const { return total_samples; }
    std::size_t get_head_cache_samples() const { return head_cache_count; }
    std::size_t get_stream_buffered_samples() const { return stream_count; }
    uint32_t get_stream_underruns() const { return underrun_count; }
    std::size_t get_resident_sample_bytes() const {
        return resident_samples.size() * sizeof(int16_t);
    }
    StorageMode get_storage_mode() const { return storage_mode; }

  private:
    bool parse_wav_header();
    bool seek_to_sample(uint32_t sample_index);
    std::size_t decode_mono_samples(uint8_t *shared_read_buffer,
                                    std::size_t shared_read_buffer_size,
                                    int16_t *dest, std::size_t max_samples);
    void reset_playback_state();
    void prime_stream_buffer(uint8_t *shared_read_buffer,
                             std::size_t shared_read_buffer_size);
    bool load_ram_resident(uint8_t *shared_read_buffer,
                           std::size_t shared_read_buffer_size);

    FIL file{};
    bool file_handle_open = false;
    bool file_open = false;
    bool playing = false;
    StorageMode storage_mode = StorageMode::Unloaded;

    std::array<int16_t, HEAD_CACHE_SAMPLES> head_cache{};
    std::array<int16_t, STREAM_BUFFER_SAMPLES> stream_buffer{};
    std::vector<int16_t> resident_samples{};
    std::size_t resident_playback_pos = 0;
    std::size_t head_cache_count = 0;
    std::size_t head_playback_pos = 0;
    std::size_t stream_read_pos = 0;
    std::size_t stream_write_pos = 0;
    std::size_t stream_count = 0;
    uint32_t next_stream_sample_index = 0;
    uint32_t underrun_count = 0;

    uint32_t sample_rate = 44100;
    uint16_t channels = 1;
    uint16_t bits_per_sample = 16;
    uint32_t data_start_pos = 44;
    uint32_t data_size = 0;
    uint32_t total_samples = 0;
    uint16_t bytes_per_frame = sizeof(int16_t);

    std::string filename;
    float volume = 1.0f;
    bool loop_enabled = false;
};

class WavFileList {
  private:
    std::array<std::string, MAX_WAV_FILES> filenames{};
    int count;

  public:
    WavFileList();

    bool add_file(const std::string &filename);
    const std::string &get_filename(int index) const;
    int get_count() const { return count; }
    int get_capacity() const { return MAX_WAV_FILES; }
    void clear();
    void print_files() const;
};

class Sampler {
  private:
    static constexpr std::size_t RAM_RESIDENT_BUDGET_BYTES = 128 * 1024;
    std::array<SamplePlayer, SAMPLE_PLAYER_NUM> players;
    std::array<uint8_t, SD_READ_BUFFER_SIZE> shared_read_buffer{};
    static FATFS fs;
    static bool fs_mounted;
    FRESULT scan_directory_for_wav(WavFileList &list, const std::string &path,
                                   bool recursive);
    std::size_t resident_sample_bytes_excluding(uint8_t player_id) const;

  public:
    Sampler();
    ~Sampler();

    bool init();

    bool load_sample(uint8_t player_id, const std::string &filename);
    void unload_sample(uint8_t player_id);

    void trigger_player(uint8_t player_id);
    void stop_player(uint8_t player_id);
    void stop_all();

    void set_player_volume(uint8_t player_id, float volume);
    void set_player_loop(uint8_t player_id, bool enable);

    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);

    SamplePlayer *get_player(uint8_t player_id);
    uint8_t get_num_players() const { return SAMPLE_PLAYER_NUM; }
    void print_memory_stats() const;

    WavFileList list_wav_files(bool recursive = true);
    bool load_sample_by_index(uint8_t player_id, int file_index,
                              bool recursive = true);
};

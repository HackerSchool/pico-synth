// Sampler.cpp implementation

#include "Sampler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {
inline int16_t scale_sample(int16_t sample, float volume) {
    if (volume >= 0.999f) {
        return sample;
    }

    int32_t scaled = static_cast<int32_t>(static_cast<float>(sample) * volume);
    if (scaled > 32767) scaled = 32767;
    if (scaled < -32768) scaled = -32768;
    return static_cast<int16_t>(scaled);
}
} // namespace

FATFS Sampler::fs;
bool Sampler::fs_mounted = false;

SamplePlayer::SamplePlayer() = default;

SamplePlayer::~SamplePlayer() { unload_sample(); }

bool SamplePlayer::parse_wav_header() {
    uint8_t buffer[12];
    UINT bytes_read = 0;

    if (f_read(&file, buffer, sizeof(buffer), &bytes_read) != FR_OK ||
        bytes_read != sizeof(buffer)) {
        return false;
    }

    if (std::memcmp(buffer, "RIFF", 4) != 0 ||
        std::memcmp(buffer + 8, "WAVE", 4) != 0) {
        return false;
    }

    channels = 0;
    sample_rate = 0;
    bits_per_sample = 0;
    data_size = 0;
    data_start_pos = 0;

    while (true) {
        uint8_t chunk_header[8];
        if (f_read(&file, chunk_header, sizeof(chunk_header), &bytes_read) != FR_OK ||
            bytes_read != sizeof(chunk_header)) {
            break;
        }

        const uint32_t chunk_size =
            static_cast<uint32_t>(chunk_header[4]) |
            (static_cast<uint32_t>(chunk_header[5]) << 8) |
            (static_cast<uint32_t>(chunk_header[6]) << 16) |
            (static_cast<uint32_t>(chunk_header[7]) << 24);

        if (std::memcmp(chunk_header, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return false;
            }

            uint8_t fmt_data[16];
            if (f_read(&file, fmt_data, sizeof(fmt_data), &bytes_read) != FR_OK ||
                bytes_read != sizeof(fmt_data)) {
                return false;
            }

            const uint16_t audio_format =
                static_cast<uint16_t>(fmt_data[0] | (fmt_data[1] << 8));
            channels = static_cast<uint16_t>(fmt_data[2] | (fmt_data[3] << 8));
            sample_rate = static_cast<uint32_t>(fmt_data[4]) |
                          (static_cast<uint32_t>(fmt_data[5]) << 8) |
                          (static_cast<uint32_t>(fmt_data[6]) << 16) |
                          (static_cast<uint32_t>(fmt_data[7]) << 24);
            bits_per_sample =
                static_cast<uint16_t>(fmt_data[14] | (fmt_data[15] << 8));

            if (audio_format != 1 || bits_per_sample != 16 || channels == 0 ||
                channels > 2) {
                return false;
            }

            if (chunk_size > sizeof(fmt_data)) {
                f_lseek(&file, f_tell(&file) + (chunk_size - sizeof(fmt_data)));
            }
        } else if (std::memcmp(chunk_header, "data", 4) == 0) {
            data_size = chunk_size;
            data_start_pos = f_tell(&file);
            break;
        } else {
            f_lseek(&file, f_tell(&file) + chunk_size);
        }

        if (chunk_size & 1u) {
            f_lseek(&file, f_tell(&file) + 1);
        }
    }

    if (channels == 0 || sample_rate == 0 || data_size == 0 ||
        data_start_pos == 0) {
        return false;
    }

    bytes_per_frame = static_cast<uint16_t>(channels * sizeof(int16_t));
    if (bytes_per_frame == 0) {
        return false;
    }

    total_samples = data_size / bytes_per_frame;
    return total_samples != 0;
}

bool SamplePlayer::seek_to_sample(uint32_t sample_index) {
    if (!file_handle_open) {
        return false;
    }

    if (sample_index > total_samples) {
        sample_index = total_samples;
    }

    const FSIZE_t offset =
        static_cast<FSIZE_t>(data_start_pos) +
        (static_cast<FSIZE_t>(sample_index) * bytes_per_frame);
    return f_lseek(&file, offset) == FR_OK;
}

std::size_t SamplePlayer::decode_mono_samples(uint8_t *shared_read_buffer,
                                              std::size_t shared_read_buffer_size,
                                              int16_t *dest,
                                              std::size_t max_samples) {
    if (!shared_read_buffer || shared_read_buffer_size < bytes_per_frame ||
        !dest || max_samples == 0) {
        return 0;
    }

    std::size_t samples_written = 0;
    while (samples_written < max_samples &&
           next_stream_sample_index + samples_written < total_samples) {
        const uint32_t remaining_samples =
            total_samples - (next_stream_sample_index +
                             static_cast<uint32_t>(samples_written));
        const std::size_t samples_this_read = std::min<std::size_t>(
            max_samples - samples_written,
            std::min<std::size_t>(remaining_samples,
                                  shared_read_buffer_size / bytes_per_frame));
        if (samples_this_read == 0) {
            break;
        }

        const UINT bytes_to_read =
            static_cast<UINT>(samples_this_read * bytes_per_frame);
        UINT bytes_read = 0;
        if (f_read(&file, shared_read_buffer, bytes_to_read, &bytes_read) != FR_OK ||
            bytes_read == 0) {
            break;
        }

        const std::size_t samples_read = bytes_read / bytes_per_frame;
        if (channels == 1) {
            for (std::size_t i = 0; i < samples_read; ++i) {
                int16_t sample = 0;
                std::memcpy(&sample,
                            &shared_read_buffer[i * sizeof(int16_t)],
                            sizeof(sample));
                dest[samples_written + i] = sample;
            }
        } else {
            for (std::size_t i = 0; i < samples_read; ++i) {
                int16_t left = 0;
                int16_t right = 0;
                const std::size_t byte_index = i * bytes_per_frame;
                std::memcpy(&left, &shared_read_buffer[byte_index], sizeof(left));
                std::memcpy(&right, &shared_read_buffer[byte_index + sizeof(int16_t)],
                            sizeof(right));
                dest[samples_written + i] =
                    static_cast<int16_t>((static_cast<int32_t>(left) +
                                          static_cast<int32_t>(right)) >>
                                         1);
            }
        }

        samples_written += samples_read;
        if (samples_read < samples_this_read) {
            break;
        }
    }

    return samples_written;
}

void SamplePlayer::reset_playback_state() {
    head_playback_pos = 0;
    stream_read_pos = 0;
    stream_write_pos = 0;
    stream_count = 0;
    next_stream_sample_index = static_cast<uint32_t>(head_cache_count);
    if (file_handle_open) {
        seek_to_sample(next_stream_sample_index);
    }
}

void SamplePlayer::prime_stream_buffer(uint8_t *shared_read_buffer,
                                       std::size_t shared_read_buffer_size) {
    if (!file_open || storage_mode != StorageMode::Streaming ||
        next_stream_sample_index >= total_samples) {
        return;
    }

    while (stream_count < STREAM_BUFFER_SAMPLES &&
           next_stream_sample_index < total_samples) {
        const std::size_t contiguous_space = std::min<std::size_t>(
            STREAM_BUFFER_SAMPLES - stream_count,
            STREAM_BUFFER_SAMPLES - stream_write_pos);
        if (contiguous_space == 0) {
            break;
        }

        const std::size_t decoded = decode_mono_samples(
            shared_read_buffer, shared_read_buffer_size,
            stream_buffer.data() + stream_write_pos, contiguous_space);
        if (decoded == 0) {
            break;
        }

        stream_write_pos = (stream_write_pos + decoded) % STREAM_BUFFER_SAMPLES;
        stream_count += decoded;
        next_stream_sample_index += static_cast<uint32_t>(decoded);
    }
}

bool SamplePlayer::load_ram_resident(uint8_t *shared_read_buffer,
                                     std::size_t shared_read_buffer_size) {
    if (!seek_to_sample(0)) {
        return false;
    }

    resident_samples.clear();
    resident_samples.resize(total_samples);
    next_stream_sample_index = 0;

    std::size_t written = 0;
    while (written < resident_samples.size()) {
        const std::size_t decoded = decode_mono_samples(
            shared_read_buffer, shared_read_buffer_size,
            resident_samples.data() + written, resident_samples.size() - written);
        if (decoded == 0) {
            break;
        }
        written += decoded;
        next_stream_sample_index += static_cast<uint32_t>(decoded);
    }

    if (written == 0) {
        std::vector<int16_t>().swap(resident_samples);
        return false;
    }

    if (written < resident_samples.size()) {
        resident_samples.resize(written);
        total_samples = static_cast<uint32_t>(written);
    }

    storage_mode = StorageMode::RamResident;
    resident_playback_pos = 0;
    head_cache_count = 0;
    head_playback_pos = 0;
    stream_read_pos = 0;
    stream_write_pos = 0;
    stream_count = 0;
    underrun_count = 0;
    next_stream_sample_index = total_samples;

    if (file_handle_open) {
        f_close(&file);
        file_handle_open = false;
    }

    return true;
}

bool SamplePlayer::load_sample(const std::string &filename_in,
                               uint8_t *shared_read_buffer,
                               std::size_t shared_read_buffer_size,
                               std::size_t max_ram_resident_bytes) {
    unload_sample();

    if (f_open(&file, filename_in.c_str(), FA_READ) != FR_OK) {
        std::printf("Failed to open: %s\n", filename_in.c_str());
        return false;
    }
    file_handle_open = true;

    if (!parse_wav_header()) {
        std::printf("Invalid WAV file: %s\n", filename_in.c_str());
        unload_sample();
        return false;
    }

    if (!seek_to_sample(0)) {
        unload_sample();
        return false;
    }

    file_open = true;
    playing = false;
    this->filename = filename_in;

    const std::size_t sample_bytes =
        static_cast<std::size_t>(total_samples) * sizeof(int16_t);
    const bool can_fit_in_ram =
        sample_bytes <= max_ram_resident_bytes &&
        total_samples <= RAM_RESIDENT_MAX_SAMPLES;

    if (can_fit_in_ram) {
        if (load_ram_resident(shared_read_buffer, shared_read_buffer_size)) {
            return true;
        }

        if (!file_handle_open &&
            f_open(&file, filename_in.c_str(), FA_READ) == FR_OK) {
            file_handle_open = true;
            if (!seek_to_sample(0)) {
                unload_sample();
                return false;
            }
        }
    }

    storage_mode = StorageMode::Streaming;
    next_stream_sample_index = 0;
    head_cache_count = decode_mono_samples(shared_read_buffer,
                                           shared_read_buffer_size,
                                           head_cache.data(),
                                           head_cache.size());
    next_stream_sample_index = static_cast<uint32_t>(head_cache_count);
    if (head_cache_count == 0) {
        unload_sample();
        return false;
    }

    reset_playback_state();
    prime_stream_buffer(shared_read_buffer, shared_read_buffer_size);
    return true;
}

void SamplePlayer::unload_sample() {
    if (file_handle_open) {
        f_close(&file);
        file_handle_open = false;
    }

    file_open = false;
    playing = false;
    storage_mode = StorageMode::Unloaded;
    std::vector<int16_t>().swap(resident_samples);
    resident_playback_pos = 0;
    underrun_count = 0;
    head_cache_count = 0;
    head_playback_pos = 0;
    stream_read_pos = 0;
    stream_write_pos = 0;
    stream_count = 0;
    next_stream_sample_index = 0;
    total_samples = 0;
    data_size = 0;
    data_start_pos = 0;
    bytes_per_frame = sizeof(int16_t);
    filename.clear();
}

void SamplePlayer::trigger(uint8_t *shared_read_buffer,
                           std::size_t shared_read_buffer_size) {
    if (!file_open) {
        return;
    }

    playing = true;
    if (storage_mode == StorageMode::RamResident) {
        resident_playback_pos = 0;
        return;
    }

    if (head_cache_count == 0) {
        playing = false;
        return;
    }

    reset_playback_state();
    prime_stream_buffer(shared_read_buffer, shared_read_buffer_size);
}

void SamplePlayer::stop() { playing = false; }

void SamplePlayer::render_buffer(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                                 uint8_t *shared_read_buffer,
                                 std::size_t shared_read_buffer_size) {
    buffer.fill(0);

    if (!playing || !file_open) {
        return;
    }

    if (storage_mode == StorageMode::RamResident) {
        for (std::size_t i = 0; i < buffer.size() && playing; ++i) {
            if (resident_playback_pos >= resident_samples.size()) {
                if (loop_enabled) {
                    resident_playback_pos = 0;
                } else {
                    playing = false;
                    break;
                }
            }

            buffer[i] = scale_sample(resident_samples[resident_playback_pos++],
                                     volume);
        }
        return;
    }

    if (stream_count < STREAM_REFILL_LOW_WATER) {
        prime_stream_buffer(shared_read_buffer, shared_read_buffer_size);
    }

    for (std::size_t i = 0; i < buffer.size(); ++i) {
        int16_t sample = 0;

        while (true) {
            if (head_playback_pos < head_cache_count) {
                sample = head_cache[head_playback_pos++];
                break;
            }

            if (stream_count > 0) {
                sample = stream_buffer[stream_read_pos];
                stream_read_pos = (stream_read_pos + 1) % STREAM_BUFFER_SAMPLES;
                --stream_count;
                if (stream_count < STREAM_REFILL_LOW_WATER) {
                    prime_stream_buffer(shared_read_buffer, shared_read_buffer_size);
                }
                break;
            }

            if (next_stream_sample_index < total_samples) {
                prime_stream_buffer(shared_read_buffer, shared_read_buffer_size);
                if (stream_count > 0) {
                    continue;
                }

                ++underrun_count;
                playing = false;
                break;
            }

            if (!loop_enabled) {
                playing = false;
                break;
            }

            reset_playback_state();
            prime_stream_buffer(shared_read_buffer, shared_read_buffer_size);
        }

        if (!playing && sample == 0) {
            break;
        }

        buffer[i] = scale_sample(sample, volume);
    }
}

WavFileList::WavFileList() : count(0) {}

bool WavFileList::add_file(const std::string &filename) {
    if (count >= static_cast<int>(filenames.size())) return false;
    filenames[count] = filename;
    ++count;
    return true;
}

const std::string &WavFileList::get_filename(int index) const {
    static const std::string empty_string;
    if (index < 0 || index >= count) return empty_string;
    return filenames[index];
}

void WavFileList::clear() { count = 0; }

void WavFileList::print_files() const {
    if (count == 0) {
        std::printf("No WAV files found.\n");
        return;
    }

    std::printf("Found %d WAV files:\n", count);
    for (int i = 0; i < count; ++i) {
        std::printf("  %d: %s\n", i + 1, filenames[i].c_str());
    }
}

Sampler::Sampler() = default;

Sampler::~Sampler() {
    for (auto &player : players) {
        player.unload_sample();
    }
}

bool Sampler::init() {
    if (!fs_mounted) {
        if (f_mount(&fs, "", 1) != FR_OK) {
            std::printf("Failed to mount SD card for sampler\n");
            return false;
        }
        fs_mounted = true;
        std::printf("Sampler: SD card mounted\n");
    }
    return true;
}

bool Sampler::load_sample(uint8_t player_id, const std::string &filename) {
    if (player_id >= SAMPLE_PLAYER_NUM) return false;
    const std::size_t used_resident_bytes =
        resident_sample_bytes_excluding(player_id);
    const std::size_t available_resident_bytes =
        used_resident_bytes >= RAM_RESIDENT_BUDGET_BYTES
            ? 0
            : (RAM_RESIDENT_BUDGET_BYTES - used_resident_bytes);

    return players[player_id].load_sample(filename, shared_read_buffer.data(),
                                          shared_read_buffer.size(),
                                          available_resident_bytes);
}

void Sampler::unload_sample(uint8_t player_id) {
    if (player_id >= SAMPLE_PLAYER_NUM) return;
    players[player_id].unload_sample();
}

void Sampler::trigger_player(uint8_t player_id) {
    if (player_id >= SAMPLE_PLAYER_NUM) return;
    players[player_id].trigger(shared_read_buffer.data(),
                               shared_read_buffer.size());
}

void Sampler::stop_player(uint8_t player_id) {
    if (player_id >= SAMPLE_PLAYER_NUM) return;
    players[player_id].stop();
}

void Sampler::stop_all() {
    for (auto &player : players) {
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

SamplePlayer *Sampler::get_player(uint8_t player_id) {
    if (player_id >= SAMPLE_PLAYER_NUM) return nullptr;
    return &players[player_id];
}

std::size_t Sampler::resident_sample_bytes_excluding(uint8_t player_id) const {
    std::size_t total = 0;
    for (std::size_t i = 0; i < players.size(); ++i) {
        if (i == player_id) continue;
        total += players[i].get_resident_sample_bytes();
    }
    return total;
}

void Sampler::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {
    int32_t accum[SAMPLES_PER_BUFFER];
    for (std::size_t i = 0; i < SAMPLES_PER_BUFFER; ++i) {
        accum[i] = 0;
    }

    std::array<int16_t, SAMPLES_PER_BUFFER> player_buffer{};

    for (auto &player : players) {
        if (!player.is_playing()) continue;
        player.render_buffer(player_buffer, shared_read_buffer.data(),
                             shared_read_buffer.size());
        for (std::size_t i = 0; i < SAMPLES_PER_BUFFER; ++i) {
            accum[i] += static_cast<int32_t>(player_buffer[i]);
        }
    }

    for (std::size_t i = 0; i < SAMPLES_PER_BUFFER; ++i) {
        int32_t value = accum[i];
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        buffer[i] = static_cast<int16_t>(value);
    }
}

void Sampler::print_memory_stats() const {
    std::printf("Sampler: shared_read_buffer=%u bytes\n",
                static_cast<unsigned>(shared_read_buffer.size()));
    for (std::size_t i = 0; i < players.size(); ++i) {
        const SamplePlayer &player = players[i];
        if (!player.is_loaded()) {
            std::printf("Sampler[%u]: unloaded\n", static_cast<unsigned>(i));
            continue;
        }

        const char *mode = "unknown";
        switch (player.get_storage_mode()) {
        case SamplePlayer::StorageMode::RamResident:
            mode = "ram";
            break;
        case SamplePlayer::StorageMode::Streaming:
            mode = "stream";
            break;
        case SamplePlayer::StorageMode::Unloaded:
            mode = "unloaded";
            break;
        }
        const std::size_t head_bytes =
            player.get_head_cache_samples() * sizeof(int16_t);
        const std::size_t stream_bytes =
            player.get_stream_buffered_samples() * sizeof(int16_t);
        std::printf(
            "Sampler[%u]: mode=%s file=%s total_samples=%lu ram=%uB head=%uB buffered=%uB underruns=%lu playing=%d\n",
            static_cast<unsigned>(i), mode, player.get_filename().c_str(),
            static_cast<unsigned long>(player.get_total_samples()),
            static_cast<unsigned>(player.get_resident_sample_bytes()),
            static_cast<unsigned>(head_bytes),
            static_cast<unsigned>(stream_bytes),
            static_cast<unsigned long>(player.get_stream_underruns()),
            player.is_playing() ? 1 : 0);
    }
}

static bool is_wav_file(const std::string &filename) {
    if (filename.length() < 4) return false;

    std::string ext = filename.substr(filename.length() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".wav";
}

FRESULT Sampler::scan_directory_for_wav(WavFileList &list,
                                        const std::string &path,
                                        bool recursive) {
    FRESULT fr;
    DIR dir;
    FILINFO fno;

    fr = f_opendir(&dir, path.c_str());
    if (fr != FR_OK) return fr;

    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        if (fno.fname[0] == '.') continue;

        std::string current_path;
        if (path == "/") {
            current_path = "/" + std::string(fno.fname);
        } else {
            current_path = path + "/" + std::string(fno.fname);
        }

        if (fno.fattrib & AM_DIR) {
            if (recursive && list.get_count() < list.get_capacity()) {
                scan_directory_for_wav(list, current_path, recursive);
            }
        } else if (is_wav_file(fno.fname)) {
            if (!list.add_file(current_path)) {
                break;
            }
        }
    }

    f_closedir(&dir);
    return fr;
}

WavFileList Sampler::list_wav_files(bool recursive) {
    WavFileList list;

    if (!fs_mounted) {
        std::printf("SD card not mounted\n");
        return list;
    }

    const FRESULT fr = scan_directory_for_wav(list, "/", recursive);
    if (fr != FR_OK) {
        std::printf("Error scanning directory: %d\n", fr);
        list.clear();
    }

    return list;
}

bool Sampler::load_sample_by_index(uint8_t player_id, int file_index,
                                   bool recursive) {
    WavFileList wav_files = list_wav_files(recursive);

    if (file_index < 0 || file_index >= wav_files.get_count()) {
        std::printf("Invalid file index: %d (available: 0-%d)\n", file_index,
                    wav_files.get_count() - 1);
        return false;
    }

    const std::string &filename = wav_files.get_filename(file_index);
    return load_sample(player_id, filename);
}

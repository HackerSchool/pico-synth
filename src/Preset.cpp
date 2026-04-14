#include "Preset.hpp"

#include "ff.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char *kPresetDirectory = "/presets";
constexpr const char *kPresetHeader = "PICOSYNTH_PRESET_V1";

int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

void copy_string(char *dst, std::size_t dst_size, const char *src) {
    if (dst_size == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }

    std::snprintf(dst, dst_size, "%s", src);
}

char *trim(char *text) {
    while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text))) {
        ++text;
    }

    char *end = text + std::strlen(text);
    while (end > text &&
           std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    *end = '\0';
    return text;
}

bool equals_ignore_case(const char *lhs, const char *rhs) {
    if (lhs == nullptr || rhs == nullptr) return false;

    while (*lhs != '\0' && *rhs != '\0') {
        if (std::tolower(static_cast<unsigned char>(*lhs)) !=
            std::tolower(static_cast<unsigned char>(*rhs))) {
            return false;
        }
        ++lhs;
        ++rhs;
    }

    return *lhs == '\0' && *rhs == '\0';
}

bool parse_int_value(const char *text, int &value) {
    if (text == nullptr || *text == '\0') return false;

    char *end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *trim(end) != '\0') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

bool parse_bool_value(const char *text, bool &value) {
    int numeric_value = 0;
    if (parse_int_value(text, numeric_value)) {
        value = numeric_value != 0;
        return true;
    }

    if (equals_ignore_case(text, "true") || equals_ignore_case(text, "on") ||
        equals_ignore_case(text, "yes")) {
        value = true;
        return true;
    }

    if (equals_ignore_case(text, "false") || equals_ignore_case(text, "off") ||
        equals_ignore_case(text, "no")) {
        value = false;
        return true;
    }

    return false;
}

bool parse_engine_value(const char *text, SynthEngine &engine) {
    int numeric_value = 0;
    if (parse_int_value(text, numeric_value)) {
        switch (numeric_value) {
        case 0:
            engine = SynthEngine::FM;
            return true;
        case 1:
            engine = SynthEngine::KarplusStrong;
            return true;
        case 2:
            engine = SynthEngine::Modal;
            return true;
        default:
            return false;
        }
    }

    if (equals_ignore_case(text, "fm")) {
        engine = SynthEngine::FM;
        return true;
    }

    if (equals_ignore_case(text, "karplus") ||
        equals_ignore_case(text, "karplusstrong")) {
        engine = SynthEngine::KarplusStrong;
        return true;
    }

    if (equals_ignore_case(text, "modal")) {
        engine = SynthEngine::Modal;
        return true;
    }

    return false;
}

bool parse_asset_value(const char *text, std::uint8_t &asset_index) {
    int numeric_value = 0;
    if (parse_int_value(text, numeric_value)) {
        asset_index = static_cast<std::uint8_t>(clamp_int(
            numeric_value, 0,
            static_cast<int>(engine_bitmaps::kEngineMenuAssetCount) - 1));
        return true;
    }

    SynthEngine engine = SynthEngine::FM;
    if (!parse_engine_value(text, engine)) {
        return false;
    }

    asset_index = static_cast<std::uint8_t>(engine);
    return true;
}

bool has_preset_extension(const char *filename) {
    if (filename == nullptr) return false;

    const char *dot = std::strrchr(filename, '.');
    if (dot == nullptr) return false;
    return equals_ignore_case(dot, ".pst");
}

bool build_preset_path(char *dst, std::size_t dst_size, const char *filename) {
    if (dst_size == 0 || filename == nullptr) {
        return false;
    }

    const int written =
        std::snprintf(dst, dst_size, "%s/%s", kPresetDirectory, filename);
    return written > 0 && static_cast<std::size_t>(written) < dst_size;
}

bool apply_key_value(PresetState &state, const char *key, const char *value) {
    int parsed_int = 0;
    bool parsed_bool = false;

    if (equals_ignore_case(key, "title")) {
        copy_string(state.title, sizeof(state.title), value);
        return true;
    }

    if (equals_ignore_case(key, "engine")) {
        SynthEngine engine = state.engine;
        if (parse_engine_value(value, engine)) {
            state.engine = engine;
            return true;
        }
        return false;
    }

    if (equals_ignore_case(key, "asset")) {
        return parse_asset_value(value, state.asset_index);
    }

    if (equals_ignore_case(key, "fm.algorithm") && parse_int_value(value, parsed_int)) {
        state.fm_patch.algorithm = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.volume") && parse_int_value(value, parsed_int)) {
        state.fm_patch.volume = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.pan") && parse_int_value(value, parsed_int)) {
        state.fm_patch.pan = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }

    if (equals_ignore_case(key, "fm.op0.wave") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[0].wave_type =
            static_cast<WaveType>(clamp_int(parsed_int, 0, static_cast<int>(WaveType::Sinc)));
        return true;
    }
    if (equals_ignore_case(key, "fm.op0.attack") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[0].attack = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op0.decay") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[0].decay = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op0.sustain") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[0].sustain = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op0.release") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[0].release = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op0.ratio") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[0].ratio = static_cast<std::uint16_t>(clamp_int(parsed_int, 1, 16));
        return true;
    }
    if (equals_ignore_case(key, "fm.op0.feedback") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[0].feedback = static_cast<std::uint16_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op0.fm_depth") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[0].fm_depth = static_cast<std::uint16_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }

    if (equals_ignore_case(key, "fm.op1.wave") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[1].wave_type =
            static_cast<WaveType>(clamp_int(parsed_int, 0, static_cast<int>(WaveType::Sinc)));
        return true;
    }
    if (equals_ignore_case(key, "fm.op1.attack") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[1].attack = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op1.decay") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[1].decay = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op1.sustain") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[1].sustain = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op1.release") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[1].release = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op1.ratio") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[1].ratio = static_cast<std::uint16_t>(clamp_int(parsed_int, 1, 16));
        return true;
    }
    if (equals_ignore_case(key, "fm.op1.feedback") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[1].feedback = static_cast<std::uint16_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "fm.op1.fm_depth") && parse_int_value(value, parsed_int)) {
        state.fm_patch.ops[1].fm_depth = static_cast<std::uint16_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }

    if (equals_ignore_case(key, "karplus.impulse") && parse_int_value(value, parsed_int)) {
        state.karplus_patch.impulse_type =
            static_cast<KarplusImpulseType>(clamp_int(parsed_int, 0, 6));
        return true;
    }
    if (equals_ignore_case(key, "karplus.filter_gain") && parse_int_value(value, parsed_int)) {
        state.karplus_patch.filter_gain = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "karplus.decay") && parse_int_value(value, parsed_int)) {
        state.karplus_patch.decay = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "karplus.impulse_length") && parse_int_value(value, parsed_int)) {
        state.karplus_patch.impulse_length =
            static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "karplus.pick_position") && parse_int_value(value, parsed_int)) {
        state.karplus_patch.pick_position =
            static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "karplus.dispersion") && parse_int_value(value, parsed_int)) {
        state.karplus_patch.dispersion =
            static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "karplus.body_resonance") && parse_int_value(value, parsed_int)) {
        state.karplus_patch.body_resonance =
            static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }

    if (equals_ignore_case(key, "modal.structure") && parse_int_value(value, parsed_int)) {
        state.modal_patch.structure = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "modal.brightness") && parse_int_value(value, parsed_int)) {
        state.modal_patch.brightness = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "modal.damping") && parse_int_value(value, parsed_int)) {
        state.modal_patch.damping = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "modal.position") && parse_int_value(value, parsed_int)) {
        state.modal_patch.position = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "modal.exciter") && parse_int_value(value, parsed_int)) {
        state.modal_patch.exciter_type =
            static_cast<ModalExciterType>(clamp_int(parsed_int, 0, 3));
        return true;
    }

    for (int fx_id = 0; fx_id < Synth::FX_SLOT_COUNT; ++fx_id) {
        char key_name[24];

        std::snprintf(key_name, sizeof(key_name), "fx%d.enabled", fx_id);
        if (equals_ignore_case(key, key_name) &&
            parse_bool_value(value, parsed_bool)) {
            state.fx[fx_id].enabled = parsed_bool;
            return true;
        }

        std::snprintf(key_name, sizeof(key_name), "fx%d.p1", fx_id);
        if (equals_ignore_case(key, key_name) && parse_int_value(value, parsed_int)) {
            state.fx[fx_id].p1 = clamp_int(parsed_int, 0, 32000);
            return true;
        }

        std::snprintf(key_name, sizeof(key_name), "fx%d.p2", fx_id);
        if (equals_ignore_case(key, key_name) && parse_int_value(value, parsed_int)) {
            state.fx[fx_id].p2 = clamp_int(parsed_int, 0, 32000);
            return true;
        }

        std::snprintf(key_name, sizeof(key_name), "fx%d.mix", fx_id);
        if (equals_ignore_case(key, key_name) && parse_int_value(value, parsed_int)) {
            state.fx[fx_id].mix = clamp_int(parsed_int, 0, 32000);
            return true;
        }
    }

    if (equals_ignore_case(key, "analog.enabled") &&
        parse_bool_value(value, parsed_bool)) {
        state.analog_enabled = parsed_bool;
        return true;
    }
    if (equals_ignore_case(key, "analog.frequency") &&
        parse_int_value(value, parsed_int)) {
        state.analog_frequency_hundredths_hz =
            static_cast<std::uint16_t>(clamp_int(parsed_int, 0, 40000));
        return true;
    }
    if (equals_ignore_case(key, "analog.dispersion") &&
        parse_int_value(value, parsed_int)) {
        state.analog_dispersion_hundredths_percent =
            static_cast<std::uint16_t>(clamp_int(parsed_int, 0, 10000));
        return true;
    }

    if (equals_ignore_case(key, "filter.type") && parse_int_value(value, parsed_int)) {
        state.filter_type = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 2));
        return true;
    }
    if (equals_ignore_case(key, "filter.cutoff_msb") &&
        parse_int_value(value, parsed_int)) {
        state.filter_cutoff_msb = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "filter.cutoff_lsb") &&
        parse_int_value(value, parsed_int)) {
        state.filter_cutoff_lsb = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "filter.q_msb") && parse_int_value(value, parsed_int)) {
        state.filter_q_msb = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }
    if (equals_ignore_case(key, "filter.q_lsb") && parse_int_value(value, parsed_int)) {
        state.filter_q_lsb = static_cast<std::uint8_t>(clamp_int(parsed_int, 0, 127));
        return true;
    }

    return false;
}

bool parse_preset_file(FIL &file, PresetState &state) {
    char line[96];
    bool header_seen = false;

    while (f_gets(line, static_cast<int>(sizeof(line)), &file) != nullptr) {
        char *trimmed = trim(line);
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        if (!header_seen) {
            if (!equals_ignore_case(trimmed, kPresetHeader)) {
                return false;
            }
            header_seen = true;
            continue;
        }

        char *separator = std::strchr(trimmed, '=');
        if (separator == nullptr) {
            continue;
        }

        *separator = '\0';
        char *key = trim(trimmed);
        char *value = trim(separator + 1);
        apply_key_value(state, key, value);
    }

    return header_seen;
}
} // namespace

PresetState PresetManager::default_state() {
    PresetState state{};
    copy_string(state.title, sizeof(state.title), "INIT");
    state.engine = SynthEngine::FM;
    state.asset_index = 0xFF;

    state.fm_patch.algorithm = 0;
    state.fm_patch.volume = 100;
    state.fm_patch.pan = 64;
    state.fm_patch.ops[0].wave_type = WaveType::Sine;
    state.fm_patch.ops[0].attack = 5;
    state.fm_patch.ops[0].decay = 20;
    state.fm_patch.ops[0].sustain = 100;
    state.fm_patch.ops[0].release = 30;
    state.fm_patch.ops[0].ratio = 1;
    state.fm_patch.ops[0].feedback = 0;
    state.fm_patch.ops[0].fm_depth = 0;
    state.fm_patch.ops[1].wave_type = WaveType::Sine;
    state.fm_patch.ops[1].attack = 5;
    state.fm_patch.ops[1].decay = 15;
    state.fm_patch.ops[1].sustain = 80;
    state.fm_patch.ops[1].release = 25;
    state.fm_patch.ops[1].ratio = 2;
    state.fm_patch.ops[1].feedback = 0;
    state.fm_patch.ops[1].fm_depth = 50;

    state.karplus_patch.impulse_type = KarplusImpulseType::WhiteNoise;
    state.karplus_patch.filter_gain = 92;
    state.karplus_patch.decay = 110;
    state.karplus_patch.impulse_length = 72;
    state.karplus_patch.pick_position = 32;
    state.karplus_patch.dispersion = 24;
    state.karplus_patch.body_resonance = 40;

    state.modal_patch.structure = 14;
    state.modal_patch.brightness = 92;
    state.modal_patch.damping = 100;
    state.modal_patch.position = 34;
    state.modal_patch.exciter_type = ModalExciterType::SoftStrike;

    state.fx[0] = {false, 250, 10000, 10000};
    state.fx[1] = {false, 500, 200, 30000};
    state.fx[2] = {false, 300, 500, 30000};
    state.fx[3] = {false, 450, 32000, 32000};
    state.fx[4] = {false, 1000, 32000, 32000};
    state.fx[5] = {true, 420, 6000, 32000};

    state.analog_enabled = false;
    state.analog_frequency_hundredths_hz = 10000;
    state.analog_dispersion_hundredths_percent = 100;

    state.filter_type = 0;
    state.filter_cutoff_msb = 64;
    state.filter_cutoff_lsb = 0;
    state.filter_q_msb = 16;
    state.filter_q_lsb = 0;

    return state;
}

bool PresetManager::load_state_from_file(const char *path, PresetState &out_state) {
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) {
        printf("PresetManager: failed to open %s\n", path);
        return false;
    }

    PresetState state = default_state();
    const bool parsed_ok = parse_preset_file(file, state);
    f_close(&file);

    if (!parsed_ok) {
        printf("PresetManager: invalid preset file %s\n", path);
        return false;
    }

    if (state.title[0] == '\0') {
        copy_string(state.title, sizeof(state.title), "PRESET");
    }

    if (state.asset_index >= engine_bitmaps::kEngineMenuAssetCount) {
        state.asset_index = static_cast<std::uint8_t>(state.engine);
    }

    out_state = state;
    return true;
}

bool PresetManager::load_factory_presets(Sampler &sampler) {
    factory_preset_count = 0;

    if (!sampler.init()) {
        printf("PresetManager: SD card unavailable\n");
        return false;
    }

    DIR dir;
    FILINFO file_info;
    const FRESULT open_result = f_opendir(&dir, kPresetDirectory);
    if (open_result != FR_OK) {
        printf("PresetManager: preset directory unavailable: %s\n",
               kPresetDirectory);
        return false;
    }

    while (factory_preset_count < kMaxFactoryPresets) {
        const FRESULT read_result = f_readdir(&dir, &file_info);
        if (read_result != FR_OK || file_info.fname[0] == '\0') {
            break;
        }

        if ((file_info.fattrib & AM_DIR) != 0 || file_info.fname[0] == '.' ||
            !has_preset_extension(file_info.fname)) {
            continue;
        }

        Metadata metadata{};
        if (!build_preset_path(metadata.path, sizeof(metadata.path),
                               file_info.fname)) {
            printf("PresetManager: preset path too long, skipping %s\n",
                   file_info.fname);
            continue;
        }

        PresetState state{};
        if (!load_state_from_file(metadata.path, state)) {
            continue;
        }

        copy_string(metadata.title, sizeof(metadata.title), state.title);
        metadata.asset_index = state.asset_index;
        factory_presets[factory_preset_count++] = metadata;
    }

    f_closedir(&dir);
    printf("PresetManager: found %d preset(s)\n", factory_preset_count);
    return factory_preset_count > 0;
}

const PresetManager::Metadata *PresetManager::get_factory_preset(int index) const {
    if (index < 0 || index >= factory_preset_count) {
        return nullptr;
    }

    return &factory_presets[index];
}

const engine_bitmaps::Asset *PresetManager::get_factory_preset_asset(int index) const {
    const Metadata *metadata = get_factory_preset(index);
    if (metadata == nullptr) {
        return nullptr;
    }

    const std::size_t asset_index =
        metadata->asset_index < engine_bitmaps::kEngineMenuAssetCount
            ? metadata->asset_index
            : 0;
    return &engine_bitmaps::kEngineMenuAssets[asset_index];
}

bool PresetManager::load_factory_preset(int index, Sampler &sampler,
                                        PresetState &out_state) const {
    if (index < 0 || index >= factory_preset_count) {
        return false;
    }

    if (!sampler.init()) {
        printf("PresetManager: SD card unavailable during load\n");
        return false;
    }

    return load_state_from_file(factory_presets[index].path, out_state);
}

int PresetManager::wrap_factory_preset_index(int index) const {
    if (factory_preset_count <= 0) {
        return 0;
    }

    int wrapped = index % factory_preset_count;
    if (wrapped < 0) {
        wrapped += factory_preset_count;
    }
    return wrapped;
}

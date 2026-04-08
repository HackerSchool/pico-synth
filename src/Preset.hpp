#pragma once

#include "Sampler.hpp"
#include "Synth.hpp"
#include "generated/engine_menu_bitmaps.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

struct PresetState {
    static constexpr std::size_t kTitleLength = 24;
    static constexpr std::uint8_t kFormatVersion = 1;

    char title[kTitleLength]{};
    SynthEngine engine = SynthEngine::FM;
    std::uint8_t asset_index = 0xFF;

    Patch fm_patch{};
    KarplusPatch karplus_patch{};
    ModalPatch modal_patch{};

    struct FxState {
        bool enabled = false;
        int p1 = 0;
        int p2 = 0;
        int mix = 0;
    };

    std::array<FxState, Synth::FX_SLOT_COUNT> fx{};

    bool analog_enabled = false;
    std::uint16_t analog_frequency_hundredths_hz = 10000;
    std::uint16_t analog_dispersion_hundredths_percent = 100;

    std::uint8_t filter_type = 0;
    std::uint8_t filter_cutoff_msb = 64;
    std::uint8_t filter_cutoff_lsb = 0;
    std::uint8_t filter_q_msb = 16;
    std::uint8_t filter_q_lsb = 0;
};

class PresetManager {
  public:
    static constexpr int kMaxFactoryPresets = 16;

    struct Metadata {
        char path[96]{};
        char title[PresetState::kTitleLength]{};
        std::uint8_t asset_index = 0;
    };

    bool load_factory_presets(Sampler &sampler);
    int get_factory_preset_count() const { return factory_preset_count; }
    const Metadata *get_factory_preset(int index) const;
    const engine_bitmaps::Asset *get_factory_preset_asset(int index) const;
    bool load_factory_preset(int index, Sampler &sampler,
                             PresetState &out_state) const;
    int wrap_factory_preset_index(int index) const;

  private:
    static PresetState default_state();
    static bool load_state_from_file(const char *path, PresetState &out_state);

    std::array<Metadata, kMaxFactoryPresets> factory_presets{};
    int factory_preset_count = 0;
};

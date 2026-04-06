#pragma once

#include "generated/engine_bitmaps/fm_bitmap.hpp"
#include "generated/engine_bitmaps/karplus_bitmap.hpp"

#include <cstddef>
#include <cstdint>

namespace engine_bitmaps {

struct Asset {
    const std::uint8_t *data;
    std::size_t size;
    const char *label;
};

inline constexpr Asset kEngineMenuAssets[] = {
    {fm_bitmap, fm_bitmap_len, "FM"},
    {karplus_bitmap, karplus_bitmap_len, "KARPLUS"},
};

inline constexpr std::size_t kEngineMenuAssetCount =
    sizeof(kEngineMenuAssets) / sizeof(kEngineMenuAssets[0]);

} // namespace engine_bitmaps

#pragma once

#include <cstdint>

namespace netgpu {

// NGPU in ascii
constexpr uint32_t kWireMagic = 0x4E475055;
constexpr uint16_t kWireVersion = 1;

bool hasWireMagic(uint32_t magic);
bool isSupportedWireVersion(uint16_t version);

}  // namespace netgpu

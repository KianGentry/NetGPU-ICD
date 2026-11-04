#include "netgpu/wire.hpp"

constexpr uint32_t kWrongMagic = 0;
constexpr uint16_t kWrongVersion = 2;

// exit code 0 means every check passed
int main() {
    if (!netgpu::hasWireMagic(netgpu::kWireMagic)) return 1;
    if (netgpu::hasWireMagic(kWrongMagic)) return 1;
    if (!netgpu::isSupportedWireVersion(netgpu::kWireVersion)) return 1;
    if (netgpu::isSupportedWireVersion(kWrongVersion)) return 1;

    return 0;
}

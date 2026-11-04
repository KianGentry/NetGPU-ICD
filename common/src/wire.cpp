#include "netgpu/wire.hpp"

namespace netgpu {

bool hasWireMagic(uint32_t magic) {
    return magic == kWireMagic;
}

bool isSupportedWireVersion(uint16_t version) {
    return version == kWireVersion;
}

}  // namespace netgpu

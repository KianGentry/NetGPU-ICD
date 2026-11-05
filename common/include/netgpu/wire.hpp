#pragma once

#include <array>
#include <cstdint>

namespace netgpu {

// NGPU in ascii
constexpr uint32_t kWireMagic = 0x4E475055;
constexpr uint16_t kWireVersion = 1;

// sum of every field in WireHeader
constexpr uint16_t kWireHeaderSize = 56;

// native layout, never copied to the wire directly
struct WireHeader {
    uint32_t magic = kWireMagic;
    uint16_t version = kWireVersion;
    uint16_t headerLength = kWireHeaderSize;
    uint16_t messageType = 0;
    uint16_t flags = 0;
    uint32_t fragmentLength = 0;
    uint64_t deviceId = 0;
    uint64_t sessionId = 0;
    uint64_t sequenceId = 0;
    uint64_t requestId = 0;
    uint32_t fragmentOffset = 0;
    uint32_t totalLength = 0;
};

using WireBytes = std::array<uint8_t, kWireHeaderSize>;

bool hasWireMagic(uint32_t magic);
bool isSupportedWireVersion(uint16_t version);

// big-endian on the wire
WireBytes encodeHeader(const WireHeader& header);
WireHeader decodeHeader(const WireBytes& bytes);

} // namespace netgpu

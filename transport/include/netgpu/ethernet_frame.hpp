#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace netgpu {

constexpr uint16_t kNetGpuEtherType = 0x88B5;
constexpr size_t kEthernetAddressSize = 6;
constexpr size_t kEthernetHeaderSize = 14;
constexpr size_t kMaxEthernetPayloadSize = 9000;

using MacAddress = std::array<uint8_t, kEthernetAddressSize>;

struct EthernetFrameView {
    MacAddress destination{};
    MacAddress source{};
    const uint8_t* payload = nullptr;
    size_t payloadLength = 0;
};

bool encodeEthernetFrame(const MacAddress& destination, const MacAddress& source,
                         const std::vector<uint8_t>& payload, size_t maxPayloadLength,
                         std::vector<uint8_t>& frame);
bool decodeEthernetFrame(const uint8_t* frame, size_t frameLength, size_t maxPayloadLength,
                         EthernetFrameView& view);

} // namespace netgpu

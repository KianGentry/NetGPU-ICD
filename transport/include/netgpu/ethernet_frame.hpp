#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace netgpu {

constexpr uint16_t kNetGpuEtherType = 0x88B5;
constexpr size_t kEthernetAddressSize = 6;
constexpr size_t kMaxEthernetPayloadSize = 9000;

using MacAddress = std::array<uint8_t, kEthernetAddressSize>;

struct EthernetFrameLayout {
    static constexpr size_t destinationOffset = 0;
    static constexpr size_t sourceOffset = kEthernetAddressSize;
    static constexpr size_t etherTypeOffset = kEthernetAddressSize * 2;
    static constexpr size_t etherTypeSize = sizeof(uint16_t);
    static constexpr size_t payloadOffset = etherTypeOffset + etherTypeSize;
    static constexpr size_t destinationSize = kEthernetAddressSize;
    static constexpr size_t sourceSize = kEthernetAddressSize;
    static constexpr size_t headerSize = payloadOffset;
};

constexpr size_t kEthernetHeaderSize = EthernetFrameLayout::headerSize;
static_assert(kEthernetHeaderSize == 14, "Ethernet header layout must be 14 bytes");

struct EthernetFrameView {
    MacAddress destination{};
    MacAddress source{};
    const uint8_t* payload = nullptr;
    size_t payloadLength = 0;
};

bool encodeEthernetFrame(const MacAddress& destination, const MacAddress& source,
    const std::vector<uint8_t>& payload, size_t maxPayloadLength, std::vector<uint8_t>& frame);
std::optional<std::vector<uint8_t>> encodeEthernetFrame(
    const MacAddress& destination, const MacAddress& source,
    const std::vector<uint8_t>& payload, size_t maxPayloadLength);

bool decodeEthernetFrame(const uint8_t* frame, size_t frameLength, 
    size_t maxPayloadLength, EthernetFrameView& view);

bool fitsInterfacePayloadLimit(size_t payloadLength, size_t maxPayloadLength);
bool fitsEthernetPayloadLimit(size_t payloadLength);
bool fitsPayloadLimits(size_t payloadLength, size_t maxPayloadLength);
size_t deriveInterfacePayloadLimit(size_t mtu);

} // namespace netgpu

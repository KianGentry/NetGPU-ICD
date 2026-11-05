#include "netgpu/ethernet_frame.hpp"

#include <algorithm>
#include <cstddef>

namespace netgpu {

namespace {

constexpr size_t kEtherTypeOffset = kEthernetAddressSize * 2;

bool payloadLengthIsAllowed(size_t payloadLength, size_t maxPayloadLength) {
    return payloadLength <= maxPayloadLength && payloadLength <= kMaxEthernetPayloadSize;
}

} // namespace

bool encodeEthernetFrame(const MacAddress& destination, const MacAddress& source,
                         const std::vector<uint8_t>& payload, size_t maxPayloadLength,
                         std::vector<uint8_t>& frame) {
    frame.clear();
    if (!payloadLengthIsAllowed(payload.size(), maxPayloadLength)) return false;

    frame.resize(kEthernetHeaderSize + payload.size());
    std::copy(destination.begin(), destination.end(), frame.begin());
    std::copy(source.begin(), source.end(), frame.begin() + kEthernetAddressSize);
    frame[kEtherTypeOffset] = static_cast<uint8_t>(kNetGpuEtherType >> 8);
    frame[kEtherTypeOffset + 1] = static_cast<uint8_t>(kNetGpuEtherType);
    std::copy(payload.begin(), payload.end(), frame.begin() + kEthernetHeaderSize);
    return true;
}

bool decodeEthernetFrame(const uint8_t* frame, size_t frameLength, size_t maxPayloadLength,
                         EthernetFrameView& view) {
    view = {};
    if (frameLength < kEthernetHeaderSize) return false;

    const uint16_t etherType = static_cast<uint16_t>(frame[kEtherTypeOffset] << 8)
        | frame[kEtherTypeOffset + 1];
    if (etherType != kNetGpuEtherType) return false;

    const size_t payloadLength = frameLength - kEthernetHeaderSize;
    if (!payloadLengthIsAllowed(payloadLength, maxPayloadLength)) return false;

    std::copy_n(frame, kEthernetAddressSize, view.destination.begin());
    std::copy_n(frame + kEthernetAddressSize, kEthernetAddressSize, view.source.begin());
    view.payload = frame + kEthernetHeaderSize;
    view.payloadLength = payloadLength;
    return true;
}

} // namespace netgpu

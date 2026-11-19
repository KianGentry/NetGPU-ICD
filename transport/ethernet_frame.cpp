#include "netgpu/ethernet_frame.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace netgpu {

namespace {

void writeBigEndianUint16(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value >> 8);
    destination[1] = static_cast<uint8_t>(value);
}

uint16_t readBigEndianUint16(const uint8_t* source) {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(source[0]) << 8)
        | static_cast<uint16_t>(source[1]));
}

const uint8_t* destinationBytes(const uint8_t* frame) {
    return frame + EthernetFrameLayout::destinationOffset;
}

const uint8_t* sourceBytes(const uint8_t* frame) {
    return frame + EthernetFrameLayout::sourceOffset;
}

const uint8_t* etherTypeBytes(const uint8_t* frame) {
    return frame + EthernetFrameLayout::etherTypeOffset;
}

const uint8_t* payloadBytes(const uint8_t* frame) {
    return frame + EthernetFrameLayout::payloadOffset;
}

} // namespace

bool fitsInterfacePayloadLimit(size_t payloadLength, size_t maxPayloadLength) {
    return payloadLength <= maxPayloadLength;
}

bool fitsEthernetPayloadLimit(size_t payloadLength) {
    return payloadLength <= kMaxEthernetPayloadSize;
}

bool fitsPayloadLimits(size_t payloadLength, size_t maxPayloadLength) {
    return fitsInterfacePayloadLimit(payloadLength, maxPayloadLength)
        && fitsEthernetPayloadLimit(payloadLength);
}

bool encodeEthernetFrame(const MacAddress& destination, const MacAddress& source,
    const std::vector<uint8_t>& payload, size_t maxPayloadLength, std::vector<uint8_t>& frame) {

    frame.clear();
    if (!fitsPayloadLimits(payload.size(), maxPayloadLength)) return false;

    frame.resize(kEthernetHeaderSize + payload.size());
    std::copy(destination.begin(), destination.end(), frame.begin() + EthernetFrameLayout::destinationOffset);
    std::copy(source.begin(), source.end(), frame.begin() + EthernetFrameLayout::sourceOffset);
    writeBigEndianUint16(frame.data() + EthernetFrameLayout::etherTypeOffset, kNetGpuEtherType);
    std::copy(payload.begin(), payload.end(), frame.begin() + EthernetFrameLayout::payloadOffset);
    return true;
}

std::optional<std::vector<uint8_t>> encodeEthernetFrame(
    const MacAddress& destination, const MacAddress& source,
    const std::vector<uint8_t>& payload, size_t maxPayloadLength) {
    std::vector<uint8_t> frame;
    if (!encodeEthernetFrame(destination, source, payload, maxPayloadLength, frame)) {
        return std::nullopt;
    }
    return frame;
}

bool decodeEthernetFrame(const uint8_t* frame, size_t frameLength, size_t maxPayloadLength,
    EthernetFrameView& view) {
    
    view = {};
    if (frameLength < kEthernetHeaderSize) return false;

    const uint16_t etherType = readBigEndianUint16(etherTypeBytes(frame));
    if (etherType != kNetGpuEtherType) return false;

    const size_t payloadLength = frameLength - kEthernetHeaderSize;
    if (!fitsPayloadLimits(payloadLength, maxPayloadLength)) return false;

    std::copy_n(destinationBytes(frame), EthernetFrameLayout::destinationSize, view.destination.begin());
    std::copy_n(sourceBytes(frame), EthernetFrameLayout::sourceSize, view.source.begin());
    view.payload = payloadBytes(frame);
    view.payloadLength = payloadLength;
    return true;
}

} // namespace netgpu

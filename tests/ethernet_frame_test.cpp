#include "netgpu/ethernet_frame.hpp"

#include <algorithm>

constexpr size_t kInterfacePayloadLimit = 1500;
constexpr size_t kTruncatedFrameLength = netgpu::kEthernetHeaderSize - 1;

const netgpu::MacAddress kDestination = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
const netgpu::MacAddress kSource = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

bool frameRoundTrips() {
    const std::vector<uint8_t> payload = {0x4E, 0x47, 0x50, 0x55};
    std::vector<uint8_t> frame;
    if (!netgpu::encodeEthernetFrame(kDestination, kSource, payload, kInterfacePayloadLimit, frame)) return false;

    netgpu::EthernetFrameView view;
    if (!netgpu::decodeEthernetFrame(frame.data(), frame.size(), kInterfacePayloadLimit, view)) return false;
    if (view.destination != kDestination || view.source != kSource) return false;
    if (view.payloadLength != payload.size()) return false;
    return std::equal(view.payload, view.payload + view.payloadLength, payload.begin());
}

bool oversizedPayloadIsRejected() {
    const std::vector<uint8_t> payload(netgpu::kMaxEthernetPayloadSize + 1);
    std::vector<uint8_t> frame;
    return !netgpu::encodeEthernetFrame(kDestination, kSource, payload, netgpu::kMaxEthernetPayloadSize, frame);
}

bool wrongEtherTypeIsRejected() {
    const std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    if (!netgpu::encodeEthernetFrame(kDestination, kSource, payload, kInterfacePayloadLimit, frame)) return false;
    frame[netgpu::EthernetFrameLayout::etherTypeOffset] = 0;

    netgpu::EthernetFrameView view;
    return !netgpu::decodeEthernetFrame(frame.data(), frame.size(), kInterfacePayloadLimit, view);
}

bool truncatedFrameIsRejected() {
    const std::vector<uint8_t> frame(kTruncatedFrameLength);
    netgpu::EthernetFrameView view;
    return !netgpu::decodeEthernetFrame(frame.data(), frame.size(), kInterfacePayloadLimit, view);
}

bool payloadLimitsAreSplitAndChecked() {
    return netgpu::fitsInterfacePayloadLimit(kInterfacePayloadLimit, kInterfacePayloadLimit)
        && !netgpu::fitsInterfacePayloadLimit(kInterfacePayloadLimit + 1, kInterfacePayloadLimit)
        && netgpu::fitsEthernetPayloadLimit(netgpu::kMaxEthernetPayloadSize)
        && !netgpu::fitsEthernetPayloadLimit(netgpu::kMaxEthernetPayloadSize + 1)
        && netgpu::fitsPayloadLimits(kInterfacePayloadLimit, kInterfacePayloadLimit)
        && !netgpu::fitsPayloadLimits(netgpu::kMaxEthernetPayloadSize + 1, netgpu::kMaxEthernetPayloadSize + 1);
}

bool interfacePayloadLimitsAreDerived() {
    constexpr size_t kStandardMtu = 1500;
    constexpr size_t kJumboMtu = 9000;
    constexpr size_t kOverProjectLimitMtu = kJumboMtu + 1;

    return netgpu::deriveInterfacePayloadLimit(kStandardMtu) == kStandardMtu
        && netgpu::deriveInterfacePayloadLimit(kJumboMtu) == kJumboMtu
        && netgpu::deriveInterfacePayloadLimit(kOverProjectLimitMtu) == kJumboMtu;
}

int main() {
    if (!frameRoundTrips()) return 1;
    if (!oversizedPayloadIsRejected()) return 1;
    if (!wrongEtherTypeIsRejected()) return 1;
    if (!truncatedFrameIsRejected()) return 1;
    if (!payloadLimitsAreSplitAndChecked()) return 1;
    if (!interfacePayloadLimitsAreDerived()) return 1;
    return 0;
}

#include "netgpu/ethernet_frame.hpp"

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
    if (view.destination != kDestination || view.source != kSource || view.payloadLength != payload.size()) return false;
    for (size_t i = 0; i < payload.size(); ++i) {
        if (view.payload[i] != payload[i]) return false;
    }
    return true;
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
    frame[netgpu::kEthernetAddressSize * 2] = 0;

    netgpu::EthernetFrameView view;
    return !netgpu::decodeEthernetFrame(frame.data(), frame.size(), kInterfacePayloadLimit, view);
}

bool truncatedFrameIsRejected() {
    const std::vector<uint8_t> frame(kTruncatedFrameLength);
    netgpu::EthernetFrameView view;
    return !netgpu::decodeEthernetFrame(frame.data(), frame.size(), kInterfacePayloadLimit, view);
}

int main() {
    if (!frameRoundTrips()) return 1;
    if (!oversizedPayloadIsRejected()) return 1;
    if (!wrongEtherTypeIsRejected()) return 1;
    if (!truncatedFrameIsRejected()) return 1;
    return 0;
}

#include "netgpu/wire.hpp"

constexpr uint32_t kWrongMagic = 0;
constexpr uint16_t kWrongVersion = 2;

// distinct values so a swapped field shows up
constexpr uint16_t kSampleMessageType = 0x0102;
constexpr uint16_t kSampleFlags = 0x0304;
constexpr uint32_t kSampleFragmentLength = 0x05060708;
constexpr uint64_t kSampleDeviceId = 0x1112131415161718;
constexpr uint64_t kSampleSessionId = 0x2122232425262728;
constexpr uint64_t kSampleSequenceId = 0x3132333435363738;
constexpr uint64_t kSampleRequestId = 0x4142434445464748;
constexpr uint32_t kSampleFragmentOffset = 0x090A0B0C;
constexpr uint32_t kSampleTotalLength = 0x0D0E0F10;

constexpr uint8_t kFirstMagicByte = 'N';

netgpu::WireHeader makeSampleHeader() {
    netgpu::WireHeader header;
    header.messageType = kSampleMessageType;
    header.flags = kSampleFlags;
    header.fragmentLength = kSampleFragmentLength;
    header.deviceId = kSampleDeviceId;
    header.sessionId = kSampleSessionId;
    header.sequenceId = kSampleSequenceId;
    header.requestId = kSampleRequestId;
    header.fragmentOffset = kSampleFragmentOffset;
    header.totalLength = kSampleTotalLength;
    return header;
}

bool magicAndVersionAreChecked() {
    if (!netgpu::hasWireMagic(netgpu::kWireMagic)) return false;
    if (netgpu::hasWireMagic(kWrongMagic)) return false;
    if (!netgpu::isSupportedWireVersion(netgpu::kWireVersion)) return false;
    if (netgpu::isSupportedWireVersion(kWrongVersion)) return false;
    return true;
}

bool encodedHeaderStartsWithMagicBytes() {
    const netgpu::WireBytes bytes = netgpu::encodeHeader(makeSampleHeader());
    return bytes[0] == kFirstMagicByte;
}

bool headerSurvivesRoundTrip() {
    const netgpu::WireHeader original = makeSampleHeader();
    const netgpu::WireHeader decoded = netgpu::decodeHeader(netgpu::encodeHeader(original));

    return decoded.magic == original.magic
        && decoded.version == original.version
        && decoded.headerLength == original.headerLength
        && decoded.messageType == original.messageType
        && decoded.flags == original.flags
        && decoded.fragmentLength == original.fragmentLength
        && decoded.deviceId == original.deviceId
        && decoded.sessionId == original.sessionId
        && decoded.sequenceId == original.sequenceId
        && decoded.requestId == original.requestId
        && decoded.fragmentOffset == original.fragmentOffset
        && decoded.totalLength == original.totalLength;
}

// exit code 0 means every check passed
int main() {
    if (!magicAndVersionAreChecked()) return 1;
    if (!encodedHeaderStartsWithMagicBytes()) return 1;
    if (!headerSurvivesRoundTrip()) return 1;

    return 0;
}

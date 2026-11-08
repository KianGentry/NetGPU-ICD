#include "netgpu/wire.hpp"

constexpr uint32_t kWrongMagic = 0;
constexpr uint16_t kWrongVersion = 2;

// distinct values so a swapped field shows up
constexpr uint16_t kSampleMessageType = static_cast<uint16_t>(netgpu::MessageType::request);
constexpr uint16_t kUnknownMessageType = 0xFFFF;
constexpr uint16_t kSampleFlags = 0x0304;
// constexpr uint32_t kSampleFragmentLength = 0x05060708;
constexpr uint64_t kSampleDeviceId = 0x1112131415161718;
constexpr uint64_t kSampleSessionId = 0x2122232425262728;
constexpr uint64_t kSampleSequenceId = 0x3132333435363738;
constexpr uint64_t kSampleRequestId = 0x4142434445464748;
// constexpr uint32_t kSampleFragmentOffset = 0x090A0B0C;
constexpr uint32_t kSampleTotalLength = 0x0D0E0F10;

constexpr uint8_t kFirstMagicByte = 'N';
constexpr uint32_t kMaxFragmentLength = 1500;
constexpr uint32_t kOversizedFragmentLength = kMaxFragmentLength + 1;
constexpr uint32_t kOneByte = 1;
constexpr uint32_t kLargestUint32 = 0xFFFFFFFF;
constexpr uint32_t kMessageLargerThanLimit = netgpu::kMaxMessageSize + 1;

netgpu::WireHeader makeSampleHeader() {
    std::vector<uint8_t> fragment = {1, 2, 3, 4};
    netgpu::WireHeader header;
    header.messageType = kSampleMessageType;
    header.flags = kSampleFlags;
    header.fragmentLength = static_cast<uint32_t>(fragment.size());
    header.deviceId = kSampleDeviceId;
    header.sessionId = kSampleSessionId;
    header.sequenceId = kSampleSequenceId;
    header.requestId = kSampleRequestId;
    header.fragmentOffset = 0;
    header.totalLength = static_cast<uint32_t>(fragment.size());
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

bool headerValidationAcceptsValidHeader() {
    netgpu::WireHeader header = makeSampleHeader();
    header.fragmentOffset = 0;
    header.fragmentLength = kMaxFragmentLength;
    header.totalLength = kMaxFragmentLength;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::valid;
}

bool headerValidationRejectsBadMagic() {
    netgpu::WireHeader header = makeSampleHeader();
    header.magic = kWrongMagic;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::badMagic;
}

bool headerValidationRejectsUnsupportedVersion() {
    netgpu::WireHeader header = makeSampleHeader();
    header.version = kWrongVersion;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::unsupportedVersion;
}

bool headerValidationRejectsInvalidLength() {
    netgpu::WireHeader header = makeSampleHeader();
    header.headerLength = 0;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::invalidHeaderLength;
}

bool headerValidationRejectsOversizedFragment() {
    netgpu::WireHeader header = makeSampleHeader();
    header.fragmentLength = kOversizedFragmentLength;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::fragmentTooLarge;
}

bool headerValidationRejectsOversizedMessage() {
    netgpu::WireHeader header = makeSampleHeader();
    header.fragmentOffset = 0;
    header.fragmentLength = kMessageLargerThanLimit;
    header.totalLength = kMessageLargerThanLimit;

    return netgpu::validateHeader(header, kLargestUint32) == netgpu::HeaderValidationResult::messageTooLarge;
}

bool headerValidationAcceptsMaximumMessage() {
    netgpu::WireHeader header = makeSampleHeader();
    header.fragmentOffset = 0;
    header.fragmentLength = netgpu::kMaxMessageSize;
    header.totalLength = netgpu::kMaxMessageSize;

    return netgpu::validateHeader(header, kLargestUint32) == netgpu::HeaderValidationResult::valid;
}

bool headerValidationRejectsFragmentPastMessageEnd() {
    netgpu::WireHeader header = makeSampleHeader();
    header.fragmentOffset = 0;
    header.fragmentLength = kOneByte;
    header.totalLength = 0;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::fragmentOutOfBounds;
}

bool headerValidationRejectsOffsetPastMessageEnd() {
    netgpu::WireHeader header = makeSampleHeader();
    header.fragmentOffset = kOneByte;
    header.fragmentLength = 0;
    header.totalLength = 0;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::fragmentOutOfBounds;
}

bool headerValidationRejectsWrappingFragmentRange() {
    netgpu::WireHeader header = makeSampleHeader();
    header.fragmentOffset = kLargestUint32;
    header.fragmentLength = kOneByte;
    header.totalLength = kOneByte;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::fragmentOutOfBounds;
}

bool headerValidationRejectsUnsupportedMessageType() {
    netgpu::WireHeader header = makeSampleHeader();
    header.messageType = kUnknownMessageType;

    return netgpu::validateHeader(header, kMaxFragmentLength) == netgpu::HeaderValidationResult::unsupportedMessageType;
}

bool wirePayloadRoundTrip() {
    netgpu::WireHeader header = makeSampleHeader();
    std::vector<uint8_t> fragment = {1, 2, 3, 4};
    std::vector<uint8_t> payload;

    if (!netgpu::encodeWirePayload(header, fragment, payload)) return false;

    netgpu::WirePayloadView view;
    if (!netgpu::decodeWirePayload(payload.data(), payload.size(), view, kMaxFragmentLength)) return false;

    return view.header.magic == header.magic &&
        view.header.version == header.version &&
        view.header.headerLength == header.headerLength &&
        view.header.messageType == header.messageType &&
        view.header.flags == header.flags &&
        view.header.fragmentLength == header.fragmentLength &&
        view.header.deviceId == header.deviceId &&
        view.header.sessionId == header.sessionId &&
        view.header.sequenceId == header.sequenceId &&
        view.header.requestId == header.requestId &&
        view.header.fragmentOffset == header.fragmentOffset &&
        view.header.totalLength == header.totalLength &&
        view.fragmentLength == fragment.size() &&
        std::equal(view.fragment, view.fragment + view.fragmentLength, fragment.begin());
}

// exit code 0 means every check passed
int main() {
    if (!magicAndVersionAreChecked()) return 1;
    if (!encodedHeaderStartsWithMagicBytes()) return 1;
    if (!headerSurvivesRoundTrip()) return 1;
    if (!headerValidationAcceptsValidHeader()) return 1;
    if (!headerValidationRejectsBadMagic()) return 1;
    if (!headerValidationRejectsUnsupportedVersion()) return 1;
    if (!headerValidationRejectsInvalidLength()) return 1;
    if (!headerValidationRejectsOversizedFragment()) return 1;
    if (!headerValidationRejectsOversizedMessage()) return 1;
    if (!headerValidationAcceptsMaximumMessage()) return 1;
    if (!headerValidationRejectsFragmentPastMessageEnd()) return 1;
    if (!headerValidationRejectsOffsetPastMessageEnd()) return 1;
    if (!headerValidationRejectsWrappingFragmentRange()) return 1;
    if (!headerValidationRejectsUnsupportedMessageType()) return 1;
    if (!wirePayloadRoundTrip()) return 1;

    return 0;
}

#include "netgpu/wire.hpp"

constexpr uint32_t kWrongMagic = 0;
constexpr uint16_t kWrongVersion = 2;

// distinct values so a swapped field shows up
constexpr uint16_t kSampleMessageType = static_cast<uint16_t>(netgpu::MessageType::request);
constexpr uint16_t kUnknownMessageType = 0xFFFF;
constexpr uint16_t kSampleFlags = 0x0304;
constexpr uint64_t kSampleDeviceId = 0x1112131415161718;
constexpr uint64_t kSampleSessionId = 0x2122232425262728;
constexpr uint64_t kSampleSequenceId = 0x3132333435363738;
constexpr uint64_t kSampleRequestId = 0x4142434445464748;
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

    return decoded == original;
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
    const auto payload = netgpu::encodeWirePayload(header, fragment);
    if (!payload) return false;
    const auto view = netgpu::decodeWirePayload(
        netgpu::ByteView{payload->data(), payload->size()}, kMaxFragmentLength);
    if (!view) return false;
    return view->header == header &&
        view->fragment.size == fragment.size() &&
        std::equal(view->fragment.data, view->fragment.data + view->fragment.size, fragment.begin());
}

struct WireTestCase {
    const char* name;
    bool (*run)();
};

// exit code 0 means every named check passed
int main() {
    const WireTestCase tests[] = {
        {"magicAndVersionAreChecked", magicAndVersionAreChecked},
        {"encodedHeaderStartsWithMagicBytes", encodedHeaderStartsWithMagicBytes},
        {"headerSurvivesRoundTrip", headerSurvivesRoundTrip},
        {"headerValidationAcceptsValidHeader", headerValidationAcceptsValidHeader},
        {"headerValidationRejectsBadMagic", headerValidationRejectsBadMagic},
        {"headerValidationRejectsUnsupportedVersion", headerValidationRejectsUnsupportedVersion},
        {"headerValidationRejectsInvalidLength", headerValidationRejectsInvalidLength},
        {"headerValidationRejectsOversizedFragment", headerValidationRejectsOversizedFragment},
        {"headerValidationRejectsOversizedMessage", headerValidationRejectsOversizedMessage},
        {"headerValidationAcceptsMaximumMessage", headerValidationAcceptsMaximumMessage},
        {"headerValidationRejectsFragmentPastMessageEnd", headerValidationRejectsFragmentPastMessageEnd},
        {"headerValidationRejectsOffsetPastMessageEnd", headerValidationRejectsOffsetPastMessageEnd},
        {"headerValidationRejectsWrappingFragmentRange", headerValidationRejectsWrappingFragmentRange},
        {"headerValidationRejectsUnsupportedMessageType", headerValidationRejectsUnsupportedMessageType},
        {"wirePayloadRoundTrip", wirePayloadRoundTrip}
    };
    for (const WireTestCase& test : tests) {
        if (!test.run()) return 1;
    }
    return 0;
}

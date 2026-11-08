#include "netgpu/wire.hpp"

#include <cstddef>
#include <algorithm>

namespace netgpu {

namespace {

constexpr int kBitsPerByte = 8;

// most significant byte first, cursor moves past the value
void writeBigEndian(uint8_t*& cursor, uint64_t value, size_t byteCount) {
    for (size_t i = byteCount; i > 0; --i) {
        *cursor++ = static_cast<uint8_t>(value >> ((i - 1) * kBitsPerByte));
    }
}

// cursor moves past the value
uint64_t readBigEndian(const uint8_t*& cursor, size_t byteCount) {
    uint64_t value = 0;
    for (size_t i = 0; i < byteCount; ++i) {
        value = (value << kBitsPerByte) | *cursor++;
    }
    return value;
}

} // namespace

bool hasWireMagic(uint32_t magic) {
    return magic == kWireMagic;
}

bool isSupportedWireVersion(uint16_t version) {
    return version == kWireVersion;
}

bool isSupportedMessageType(uint16_t messageType) {
    switch (static_cast<MessageType>(messageType)) {
        case MessageType::discoveryRequest:
        case MessageType::discoveryResponse:
        case MessageType::handshakeRequest:
        case MessageType::handshakeResponse:
        case MessageType::capabilitiesRequest:
        case MessageType::capabilitiesResponse:
        case MessageType::request:
        case MessageType::response:
        case MessageType::error:
            return true;
        default:
            return false;
    }
}

// field order here must match decodeHeader
WireBytes encodeHeader(const WireHeader& header) {
    WireBytes bytes{};
    uint8_t* cursor = bytes.data();

    writeBigEndian(cursor, header.magic, sizeof(header.magic));
    writeBigEndian(cursor, header.version, sizeof(header.version));
    writeBigEndian(cursor, header.headerLength, sizeof(header.headerLength));
    writeBigEndian(cursor, header.messageType, sizeof(header.messageType));
    writeBigEndian(cursor, header.flags, sizeof(header.flags));
    writeBigEndian(cursor, header.fragmentLength, sizeof(header.fragmentLength));
    writeBigEndian(cursor, header.deviceId, sizeof(header.deviceId));
    writeBigEndian(cursor, header.sessionId, sizeof(header.sessionId));
    writeBigEndian(cursor, header.sequenceId, sizeof(header.sequenceId));
    writeBigEndian(cursor, header.requestId, sizeof(header.requestId));
    writeBigEndian(cursor, header.fragmentOffset, sizeof(header.fragmentOffset));
    writeBigEndian(cursor, header.totalLength, sizeof(header.totalLength));

    return bytes;
}

// field order here must match encodeHeader
WireHeader decodeHeader(const WireBytes& bytes) {
    WireHeader header;
    const uint8_t* cursor = bytes.data();

    header.magic = static_cast<uint32_t>(readBigEndian(cursor, sizeof(header.magic)));
    header.version = static_cast<uint16_t>(readBigEndian(cursor, sizeof(header.version)));
    header.headerLength = static_cast<uint16_t>(readBigEndian(cursor, sizeof(header.headerLength)));
    header.messageType = static_cast<uint16_t>(readBigEndian(cursor, sizeof(header.messageType)));
    header.flags = static_cast<uint16_t>(readBigEndian(cursor, sizeof(header.flags)));
    header.fragmentLength = static_cast<uint32_t>(readBigEndian(cursor, sizeof(header.fragmentLength)));
    header.deviceId = readBigEndian(cursor, sizeof(header.deviceId));
    header.sessionId = readBigEndian(cursor, sizeof(header.sessionId));
    header.sequenceId = readBigEndian(cursor, sizeof(header.sequenceId));
    header.requestId = readBigEndian(cursor, sizeof(header.requestId));
    header.fragmentOffset = static_cast<uint32_t>(readBigEndian(cursor, sizeof(header.fragmentOffset)));
    header.totalLength = static_cast<uint32_t>(readBigEndian(cursor, sizeof(header.totalLength)));

    return header;
}

HeaderValidationResult validateHeader(const WireHeader& header, uint32_t maxFragmentLength) {
    if (!hasWireMagic(header.magic)) return HeaderValidationResult::badMagic;
    if (!isSupportedWireVersion(header.version)) return HeaderValidationResult::unsupportedVersion;
    if (!isSupportedMessageType(header.messageType)) return HeaderValidationResult::unsupportedMessageType;
    if (header.headerLength != kWireHeaderSize) return HeaderValidationResult::invalidHeaderLength;
    if (header.fragmentLength > maxFragmentLength) return HeaderValidationResult::fragmentTooLarge;
    if (header.totalLength > kMaxMessageSize) return HeaderValidationResult::messageTooLarge;
    if (header.fragmentOffset > header.totalLength) return HeaderValidationResult::fragmentOutOfBounds;
    if (header.fragmentLength > header.totalLength - header.fragmentOffset) {
        return HeaderValidationResult::fragmentOutOfBounds;
    }

    return HeaderValidationResult::valid;
}

bool encodeWirePayload(const WireHeader& header, const std::vector<uint8_t>& fragment, std::vector<uint8_t>& payload) {
    WireBytes headerBytes = encodeHeader(header);

    if (fragment.size() != header.fragmentLength) {
        payload.clear();
        return false;
    }

    payload.resize(headerBytes.size() + fragment.size());
    std::copy(headerBytes.begin(), headerBytes.end(), payload.begin());
    std::copy(fragment.begin(), fragment.end(), payload.begin() + headerBytes.size());

    return true;
}

bool decodeWirePayload(const uint8_t* payload, size_t payloadLength, WirePayloadView& view, uint32_t maxFragmentLength) {
    view = {};
    if (payload == nullptr || payloadLength < kWireHeaderSize) return false;

    WireBytes headerBytes{};
    std::copy_n(payload, kWireHeaderSize, headerBytes.begin());
    const WireHeader header = decodeHeader(headerBytes);

    const size_t fragmentLength = payloadLength - kWireHeaderSize;
    if (header.fragmentLength != fragmentLength) return false;
    if (validateHeader(header, maxFragmentLength) != HeaderValidationResult::valid) return false;

    view.header = header;
    view.fragment = payload + kWireHeaderSize;
    view.fragmentLength = fragmentLength;

    return true;
}

} // namespace netgpu

#include "netgpu/wire.hpp"

#include <cstddef>
#include <algorithm>

namespace netgpu {

namespace {

constexpr uint32_t kBytesInUint16 = 2;
constexpr uint32_t kBytesInUint32 = 4;
constexpr uint32_t kBytesInUint64 = 8;

} // namespace

void WireWriter::writeUint16(uint16_t value) {
    bytes_[cursor_] = static_cast<uint8_t>(value >> 8);
    bytes_[cursor_ + 1] = static_cast<uint8_t>(value);
    cursor_ += kBytesInUint16;
}

void WireWriter::writeUint32(uint32_t value) {
    for (size_t offset = kBytesInUint32; offset > 0; --offset) {
        bytes_[cursor_ + offset - 1] = static_cast<uint8_t>(value >> ((kBytesInUint32 - offset) * 8));
    }
    cursor_ += kBytesInUint32;
}

void WireWriter::writeUint64(uint64_t value) {
    for (size_t offset = kBytesInUint64; offset > 0; --offset) {
        bytes_[cursor_ + offset - 1] = static_cast<uint8_t>(value >> ((kBytesInUint64 - offset) * 8));
    }
    cursor_ += kBytesInUint64;
}

WireBytes WireWriter::finish() const {
    return bytes_;
}

WireReader::WireReader(const WireBytes& bytes)
    : bytes_{bytes.data(), bytes.size()} {}

WireReader::WireReader(ByteView bytes) : bytes_(bytes) {}

uint16_t WireReader::readUint16() {
    const uint16_t value = static_cast<uint16_t>(
        (static_cast<uint16_t>(bytes_.data[cursor_]) << 8)
        | bytes_.data[cursor_ + 1]);
    cursor_ += kBytesInUint16;
    return value;
}

uint32_t WireReader::readUint32() {
    uint32_t value = 0;
    for (size_t offset = 0; offset < kBytesInUint32; ++offset) {
        value = (value << 8) | bytes_.data[cursor_ + offset];
    }
    cursor_ += kBytesInUint32;
    return value;
}

uint64_t WireReader::readUint64() {
    uint64_t value = 0;
    for (size_t offset = 0; offset < kBytesInUint64; ++offset) {
        value = (value << 8) | bytes_.data[cursor_ + offset];
    }
    cursor_ += kBytesInUint64;
    return value;
}

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

WireBytes encodeHeader(const WireHeader& header) {
    WireWriter writer;
    writer.writeUint32(header.magic);
    writer.writeUint16(header.version);
    writer.writeUint16(header.headerLength);
    writer.writeUint16(header.messageType);
    writer.writeUint16(header.flags);
    writer.writeUint32(header.fragmentLength);
    writer.writeUint64(header.deviceId);
    writer.writeUint64(header.sessionId);
    writer.writeUint64(header.sequenceId);
    writer.writeUint64(header.requestId);
    writer.writeUint32(header.fragmentOffset);
    writer.writeUint32(header.totalLength);
    return writer.finish();
}

WireHeader decodeHeader(const WireBytes& bytes) {
    WireHeader header;
    WireReader reader(bytes);
    header.magic = reader.readUint32();
    header.version = reader.readUint16();
    header.headerLength = reader.readUint16();
    header.messageType = reader.readUint16();
    header.flags = reader.readUint16();
    header.fragmentLength = reader.readUint32();
    header.deviceId = reader.readUint64();
    header.sessionId = reader.readUint64();
    header.sequenceId = reader.readUint64();
    header.requestId = reader.readUint64();
    header.fragmentOffset = reader.readUint32();
    header.totalLength = reader.readUint32();
    return header;
}

HeaderValidationResult validateHeaderIdentity(const WireHeader& header) {
    if (!hasWireMagic(header.magic)) return HeaderValidationResult::badMagic;
    if (!isSupportedWireVersion(header.version)) return HeaderValidationResult::unsupportedVersion;
    if (!isSupportedMessageType(header.messageType)) return HeaderValidationResult::unsupportedMessageType;
    return HeaderValidationResult::valid;
}

HeaderValidationResult validateHeaderLength(const WireHeader& header) {
    return header.headerLength == kWireHeaderSize
        ? HeaderValidationResult::valid
        : HeaderValidationResult::invalidHeaderLength;
}

HeaderValidationResult validateFragmentSize(const WireHeader& header, uint32_t maxFragmentLength) {
    return header.fragmentLength <= maxFragmentLength
        ? HeaderValidationResult::valid
        : HeaderValidationResult::fragmentTooLarge;
}

HeaderValidationResult validateMessageSize(const WireHeader& header) {
    return header.totalLength <= kMaxMessageSize
        ? HeaderValidationResult::valid
        : HeaderValidationResult::messageTooLarge;
}

HeaderValidationResult validateFragmentBounds(const WireHeader& header) {
    if (header.fragmentOffset > header.totalLength) {
        return HeaderValidationResult::fragmentOutOfBounds;
    }
    const uint32_t remainingMessageLength = header.totalLength - header.fragmentOffset;
    if (header.fragmentLength > remainingMessageLength) {
        return HeaderValidationResult::fragmentOutOfBounds;
    }
    return HeaderValidationResult::valid;
}

HeaderValidationResult validateHeader(const WireHeader& header, uint32_t maxFragmentLength) {
    if (const auto result = validateHeaderIdentity(header); result != HeaderValidationResult::valid) return result;
    if (const auto result = validateHeaderLength(header); result != HeaderValidationResult::valid) return result;
    if (const auto result = validateFragmentSize(header, maxFragmentLength); result != HeaderValidationResult::valid) return result;
    if (const auto result = validateMessageSize(header); result != HeaderValidationResult::valid) return result;
    if (const auto result = validateFragmentBounds(header); result != HeaderValidationResult::valid) return result;
    return HeaderValidationResult::valid;
}

std::optional<std::vector<uint8_t>> encodeWirePayload(
    const WireHeader& header, const std::vector<uint8_t>& fragment) {
    if (fragment.size() != header.fragmentLength) {
        return std::nullopt;
    }

    const WireBytes headerBytes = encodeHeader(header);
    std::vector<uint8_t> payload;
    payload.resize(headerBytes.size() + fragment.size());
    std::copy(headerBytes.begin(), headerBytes.end(), payload.begin());
    std::copy(fragment.begin(), fragment.end(), payload.begin() + headerBytes.size());
    return payload;
}

std::optional<WirePayloadView> decodeWirePayload(
    ByteView payload, uint32_t maxFragmentLength) {
    if (payload.data == nullptr || payload.size < kWireHeaderSize) return std::nullopt;

    WireBytes headerBytes{};
    std::copy_n(payload.data, kWireHeaderSize, headerBytes.begin());
    const WireHeader header = decodeHeader(headerBytes);

    const size_t fragmentLength = payload.size - kWireHeaderSize;
    if (header.fragmentLength != fragmentLength) return std::nullopt;
    if (validateHeader(header, maxFragmentLength) != HeaderValidationResult::valid) return std::nullopt;

    return WirePayloadView{
        header,
        ByteView{payload.data + kWireHeaderSize, fragmentLength}
    };
}

} // namespace netgpu

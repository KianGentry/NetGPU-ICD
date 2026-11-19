#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace netgpu {

// NGPU in ascii
constexpr uint32_t kWireMagic = 0x4E475055;
constexpr uint16_t kWireVersion = 1;

constexpr size_t kWireUint32Size = sizeof(uint32_t);
constexpr size_t kWireUint16Size = sizeof(uint16_t);
constexpr size_t kWireUint64Size = sizeof(uint64_t);

constexpr size_t kWireHeaderSize = kWireUint32Size * 4
    + kWireUint16Size * 4
    + kWireUint64Size * 4;
constexpr uint32_t kMaxMessageSize = 64u * 1024u * 1024u;
static_assert(kWireHeaderSize == 56, "Wire header layout must be 56 bytes");

enum class MessageType : uint16_t {
    discoveryRequest = 1,
    discoveryResponse = 2,
    handshakeRequest = 3,
    handshakeResponse = 4,
    capabilitiesRequest = 5,
    capabilitiesResponse = 6,
    request = 7,
    response = 8,
    error = 9
};

// native layout, never copied to the wire directly
struct WireHeader {
    uint32_t magic = kWireMagic;
    uint16_t version = kWireVersion;
    uint16_t headerLength = kWireHeaderSize;
    uint16_t messageType = 0;
    uint16_t flags = 0;
    uint32_t fragmentLength = 0;
    uint64_t deviceId = 0;
    uint64_t sessionId = 0;
    uint64_t sequenceId = 0;
    uint64_t requestId = 0;
    uint32_t fragmentOffset = 0;
    uint32_t totalLength = 0;

    bool operator==(const WireHeader& other) const {
        return magic == other.magic
            && version == other.version
            && headerLength == other.headerLength
            && messageType == other.messageType
            && flags == other.flags
            && fragmentLength == other.fragmentLength
            && deviceId == other.deviceId
            && sessionId == other.sessionId
            && sequenceId == other.sequenceId
            && requestId == other.requestId
            && fragmentOffset == other.fragmentOffset
            && totalLength == other.totalLength;
    }
};

using WireBytes = std::array<uint8_t, kWireHeaderSize>;

struct ByteView {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct MutableByteView {
    uint8_t* data = nullptr;
    size_t size = 0;
};

struct WirePayloadView {
    WireHeader header;
    ByteView fragment;
};

class WireWriter {
public:
    void writeUint16(uint16_t value);
    void writeUint32(uint32_t value);
    void writeUint64(uint64_t value);
    WireBytes finish() const;

private:
    WireBytes bytes_{};
    size_t cursor_ = 0;
};

class WireReader {
public:
    explicit WireReader(const WireBytes& bytes);
    explicit WireReader(ByteView bytes);
    uint16_t readUint16();
    uint32_t readUint32();
    uint64_t readUint64();

private:
    ByteView bytes_;
    size_t cursor_ = 0;
};

std::optional<std::vector<uint8_t>> encodeWirePayload(
    const WireHeader& header, const std::vector<uint8_t>& fragment);
std::optional<WirePayloadView> decodeWirePayload(
    ByteView payload, uint32_t maxFragmentLength);

enum class HeaderValidationResult {
    valid,
    badMagic,
    unsupportedVersion,
    invalidHeaderLength,
    fragmentTooLarge,
    messageTooLarge,
    fragmentOutOfBounds,
    unsupportedMessageType
};

bool isSupportedMessageType(uint16_t messageType);
bool hasWireMagic(uint32_t magic);
bool isSupportedWireVersion(uint16_t version);
HeaderValidationResult validateHeaderIdentity(const WireHeader& header);
HeaderValidationResult validateHeaderLength(const WireHeader& header);
HeaderValidationResult validateFragmentSize(const WireHeader& header, uint32_t maxFragmentLength);
HeaderValidationResult validateMessageSize(const WireHeader& header);
HeaderValidationResult validateFragmentBounds(const WireHeader& header);

// big-endian on the wire
WireBytes encodeHeader(const WireHeader& header);
WireHeader decodeHeader(const WireBytes& bytes);
HeaderValidationResult validateHeader(const WireHeader& header, uint32_t maxFragmentLength);

} // namespace netgpu

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace netgpu {

// NGPU in ascii
constexpr uint32_t kWireMagic = 0x4E475055;
constexpr uint16_t kWireVersion = 1;

// sum of every field in WireHeader
constexpr uint16_t kWireHeaderSize = 56;
constexpr uint32_t kMaxMessageSize = 64u * 1024u * 1024u;

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
};

using WireBytes = std::array<uint8_t, kWireHeaderSize>;

struct WirePayloadView {
    WireHeader header;
    const uint8_t* fragment = nullptr;
    size_t fragmentLength = 0;
};

bool encodeWirePayload(const WireHeader& header, const std::vector<uint8_t>& fragment, std::vector<uint8_t>& payload);
bool decodeWirePayload(const uint8_t* payload, size_t payloadLength, WirePayloadView& view, uint32_t maxFragmentLength);

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

// big-endian on the wire
WireBytes encodeHeader(const WireHeader& header);
WireHeader decodeHeader(const WireBytes& bytes);
HeaderValidationResult validateHeader(const WireHeader& header, uint32_t maxFragmentLength);

} // namespace netgpu

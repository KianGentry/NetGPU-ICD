#pragma once

#include "netgpu/wire.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace netgpu {

struct TransportMessage {
    WireHeader header;
    std::vector<uint8_t> payload;
};

class ITransport {
public:
    explicit ITransport(uint32_t maxFragmentLength);
    virtual ~ITransport() = default;

    bool sendMessage(const WireHeader& header, const std::vector<uint8_t>& payload);
    bool receiveMessage(TransportMessage& message);

protected:
    virtual bool readExactly(MutableByteView destination) = 0;
    virtual bool writeExactly(ByteView source) = 0;

    uint32_t maxFragmentLength_;

private:
    bool validateSendRequest(const WireHeader& header,
        const std::vector<uint8_t>& payload) const;
    bool writeHeaderAndPayload(const WireBytes& headerBytes,
        const std::vector<uint8_t>& payload);
    std::optional<WireHeader> readAndValidateHeader();
    std::optional<size_t> validateAndGetPayloadSize(
        const WireHeader& header) const;
    std::optional<std::vector<uint8_t>> readPayload(size_t payloadSize);
    TransportMessage buildTransportMessage(WireHeader header,
        std::vector<uint8_t> payload) const;

    std::mutex sendMutex_;
    std::mutex receiveMutex_;
};

} // namespace netgpu

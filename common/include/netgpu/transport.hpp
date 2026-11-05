#pragma once

#include "netgpu/wire.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
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
    virtual bool readExactBytes(uint8_t* bytes, size_t byteCount) = 0;
    virtual bool writeExactBytes(const uint8_t* bytes, size_t byteCount) = 0;

    uint32_t maxFragmentLength_;

private:
    std::mutex sendMutex_;
    std::mutex receiveMutex_;
};

} // namespace netgpu

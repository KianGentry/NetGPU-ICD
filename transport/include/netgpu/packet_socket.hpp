#pragma once

#include "netgpu/ethernet_frame.hpp"
#include "netgpu/socket_descriptor.hpp"

#include <string>
#include <vector>

namespace netgpu {

class PacketSocket {
public:
    PacketSocket() = default;
    ~PacketSocket();

    PacketSocket(const PacketSocket&) = delete;
    PacketSocket& operator=(const PacketSocket&) = delete;

    bool openOnInterface(const std::string& interfaceName);
    bool sendFrame(const std::vector<uint8_t>& frame);
    bool receiveFrame(std::vector<uint8_t>& frame, int timeoutMilliseconds);
    const MacAddress& localAddress() const;
    size_t mtu() const;
    size_t maxPayloadLength() const;
    const std::string& lastError() const;

private:
    SocketDescriptor socketDescriptor_;
    int interfaceIndex_ = 0;
    MacAddress localAddress_{};
    size_t mtu_ = 0;
    size_t maxPayloadLength_ = 0;
    std::string lastError_;
};

} // namespace netgpu

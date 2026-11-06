#pragma once

#include "netgpu/ethernet_frame.hpp"

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
    const std::string& lastError() const;

private:
    int socketFd_ = -1;
    int interfaceIndex_ = 0;
    MacAddress localAddress_{};
    std::string lastError_;
};

} // namespace netgpu

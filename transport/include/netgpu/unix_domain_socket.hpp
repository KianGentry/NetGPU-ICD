#pragma once

#include "netgpu/transport.hpp"
#include "netgpu/socket_descriptor.hpp"
#include <string>

namespace netgpu {

class UnixDomainSocket final : public ITransport {
public:
    explicit UnixDomainSocket(uint32_t maxFragmentLength);
    // socket descriptor ownership transfers to this object
    UnixDomainSocket(int socketFd, uint32_t maxFragmentLength);
    ~UnixDomainSocket() override;

    UnixDomainSocket(const UnixDomainSocket&) = delete;
    UnixDomainSocket& operator=(const UnixDomainSocket&) = delete;

    bool connectTo(const std::string& path);
    bool listenAt(const std::string& path, int backlog);
    bool acceptClient(UnixDomainSocket& client);

private:
    bool readExactly(MutableByteView destination) override;
    bool writeExactly(ByteView source) override;

    SocketDescriptor socketDescriptor_;
    std::string boundPath_;
};

} // namespace netgpu

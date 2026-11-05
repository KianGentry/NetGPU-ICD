#include "netgpu/unix_domain_socket.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace netgpu {

namespace {

bool makeUnixAddress(const std::string& path, sockaddr_un& address, socklen_t& addressLength) {
    if (path.empty() || path.size() >= sizeof(address.sun_path)) return false;

    address = {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    addressLength = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    return true;
}

} // namespace

UnixDomainSocket::UnixDomainSocket(uint32_t maxFragmentLength)
    : ITransport(maxFragmentLength), socketFd_(-1) {}

UnixDomainSocket::UnixDomainSocket(int socketFd, uint32_t maxFragmentLength)
    : ITransport(maxFragmentLength), socketFd_(socketFd) {}

UnixDomainSocket::~UnixDomainSocket() {
    if (socketFd_ >= 0) close(socketFd_);
    if (!boundPath_.empty()) unlink(boundPath_.c_str());
}

bool UnixDomainSocket::connectTo(const std::string& path) {
    if (socketFd_ >= 0) return false;

    sockaddr_un address{};
    socklen_t addressLength = 0;
    if (!makeUnixAddress(path, address, addressLength)) return false;

    const int socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd < 0) return false;
    if (connect(socketFd, reinterpret_cast<const sockaddr*>(&address), addressLength) < 0) {
        close(socketFd);
        return false;
    }

    socketFd_ = socketFd;
    return true;
}

bool UnixDomainSocket::listenAt(const std::string& path, int backlog) {
    if (socketFd_ >= 0) return false;

    sockaddr_un address{};
    socklen_t addressLength = 0;
    if (!makeUnixAddress(path, address, addressLength)) return false;

    const int socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd < 0) return false;
    if (bind(socketFd, reinterpret_cast<const sockaddr*>(&address), addressLength) < 0) {
        close(socketFd);
        return false;
    }
    if (listen(socketFd, backlog) < 0) {
        close(socketFd);
        unlink(path.c_str());
        return false;
    }

    socketFd_ = socketFd;
    boundPath_ = path;
    return true;
}

bool UnixDomainSocket::acceptClient(UnixDomainSocket& client) {
    if (socketFd_ < 0 || client.socketFd_ >= 0) return false;

    int clientFd;
    do {
        clientFd = accept(socketFd_, nullptr, nullptr);
    } while (clientFd < 0 && errno == EINTR);
    if (clientFd < 0) return false;

    client.socketFd_ = clientFd;
    client.maxFragmentLength_ = maxFragmentLength_;
    return true;
}

bool UnixDomainSocket::readExactBytes(uint8_t* bytes, size_t byteCount) {
    size_t bytesRead = 0;
    while (bytesRead < byteCount) {
        const ssize_t result = recv(socketFd_, bytes + bytesRead, byteCount - bytesRead, 0);
        if (result == 0) return false;
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        bytesRead += static_cast<size_t>(result);
    }
    return true;
}

bool UnixDomainSocket::writeExactBytes(const uint8_t* bytes, size_t byteCount) {
    size_t bytesWritten = 0;
    while (bytesWritten < byteCount) {
        const ssize_t result = send(socketFd_, bytes + bytesWritten, byteCount - bytesWritten, MSG_NOSIGNAL);
        if (result == 0) return false;
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        bytesWritten += static_cast<size_t>(result);
    }
    return true;
}

} // namespace netgpu

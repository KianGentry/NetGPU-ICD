#include "netgpu/unix_domain_socket.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <optional>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace netgpu {

namespace {

struct UnixAddress {
    sockaddr_un address{};
    socklen_t length = 0;
};

bool isValidUnixSocketPath(const std::string& path) {
    return !path.empty() && path.size() < sizeof(sockaddr_un::sun_path);
}

socklen_t calculateUnixAddressLength(const std::string& path) {
    return static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path) + path.size() + 1);
}

bool copyUnixSocketPath(sockaddr_un& address, const std::string& path) {
    if (!isValidUnixSocketPath(path)) return false;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

UnixAddress buildUnixAddress(const std::string& path) {
    UnixAddress result;
    if (!copyUnixSocketPath(result.address, path)) return {};
    result.address.sun_family = AF_UNIX;
    result.length = calculateUnixAddressLength(path);
    return result;
}

std::optional<SocketDescriptor> createStreamSocket() {
    const int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) return std::nullopt;
    return SocketDescriptor(descriptor);
}

bool bindSocket(SocketDescriptor& socket, const UnixAddress& address) {
    return bind(socket.get(),
        reinterpret_cast<const sockaddr*>(&address.address),
        address.length) == 0;
}

bool startListening(SocketDescriptor& socket, int backlog) {
    return listen(socket.get(), backlog) == 0;
}

enum class TransferDirection {
    read,
    write
};

ssize_t readSomeBytes(int socketFd, uint8_t* bytes, size_t byteCount) {
    return recv(socketFd, bytes, byteCount, 0);
}

ssize_t writeSomeBytes(int socketFd, const uint8_t* bytes, size_t byteCount) {
    return send(socketFd, bytes, byteCount, MSG_NOSIGNAL);
}

bool transferExactly(int socketFd, uint8_t* bytes, size_t byteCount,
    TransferDirection direction) {
    size_t transferred = 0;
    while (transferred < byteCount) {
        const ssize_t result = direction == TransferDirection::read
            ? readSomeBytes(socketFd, bytes + transferred, byteCount - transferred)
            : writeSomeBytes(socketFd, bytes + transferred, byteCount - transferred);
        if (result == 0) return false;
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        transferred += static_cast<size_t>(result);
    }
    return true;
}

} // namespace

UnixDomainSocket::UnixDomainSocket(uint32_t maxFragmentLength)
    : ITransport(maxFragmentLength) {}

UnixDomainSocket::UnixDomainSocket(int socketFd, uint32_t maxFragmentLength)
    : ITransport(maxFragmentLength), socketDescriptor_(socketFd) {}

UnixDomainSocket::~UnixDomainSocket() {
    if (!boundPath_.empty()) unlink(boundPath_.c_str());
}

bool UnixDomainSocket::connectTo(const std::string& path) {
    if (socketDescriptor_.isValid()) return false;

    const UnixAddress address = buildUnixAddress(path);
    if (address.length == 0) return false;

    auto socket = createStreamSocket();
    if (!socket) return false;
    if (connect(socket->get(),
        reinterpret_cast<const sockaddr*>(&address.address),
        address.length) < 0) return false;

    socketDescriptor_ = std::move(*socket);
    return true;
}

bool UnixDomainSocket::listenAt(const std::string& path, int backlog) {
    if (socketDescriptor_.isValid()) return false;

    const UnixAddress address = buildUnixAddress(path);
    if (address.length == 0) return false;

    auto socket = createStreamSocket();
    if (!socket) return false;
    if (!bindSocket(*socket, address)) return false;
    if (!startListening(*socket, backlog)) {
        unlink(path.c_str());
        return false;
    }

    socketDescriptor_ = std::move(*socket);
    boundPath_ = path;
    return true;
}

bool UnixDomainSocket::acceptClient(UnixDomainSocket& client) {
    if (!socketDescriptor_.isValid() || client.socketDescriptor_.isValid()) return false;

    int clientFd;
    do {
        clientFd = accept(socketDescriptor_.get(), nullptr, nullptr);
    } while (clientFd < 0 && errno == EINTR);
    if (clientFd < 0) return false;

    client.socketDescriptor_ = SocketDescriptor(clientFd);
    client.maxFragmentLength_ = maxFragmentLength_;
    return true;
}

bool UnixDomainSocket::readExactly(MutableByteView destination) {
    return socketDescriptor_.isValid()
        && transferExactly(socketDescriptor_.get(), destination.data,
            destination.size, TransferDirection::read);
}

bool UnixDomainSocket::writeExactly(ByteView source) {
    uint8_t* mutableBytes = const_cast<uint8_t*>(source.data);
    return socketDescriptor_.isValid()
        && transferExactly(socketDescriptor_.get(), mutableBytes,
            source.size, TransferDirection::write);
}

} // namespace netgpu

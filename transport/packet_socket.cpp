#include "netgpu/packet_socket.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <linux/if_packet.h>
#include <net/if.h>
#include <optional>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <utility>

namespace netgpu {

namespace {

constexpr size_t kPacketBufferSize = kEthernetHeaderSize + kMaxEthernetPayloadSize;

enum class PacketWaitResult {
    ready,
    timeout,
    closedSocket,
    interrupted,
    error
};

enum class PacketReceiveResult {
    valid,
    closedSocket,
    interrupted,
    truncated,
    error
};

void setSystemError(std::string& errorMessage, const char* operation) {
    errorMessage = operation;
    errorMessage += ": ";
    errorMessage += std::strerror(errno);
}

std::optional<int> findInterfaceIndex(const std::string& interfaceName) {
    if (interfaceName.empty()) return std::nullopt;
    const unsigned int interfaceIndex = if_nametoindex(interfaceName.c_str());
    if (interfaceIndex == 0) return std::nullopt;
    return static_cast<int>(interfaceIndex);
}

std::optional<SocketDescriptor> createPacketSocket(int interfaceIndex) {
    (void)interfaceIndex;
    const int descriptor = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC,
        htons(kNetGpuEtherType));
    if (descriptor < 0) return std::nullopt;
    return SocketDescriptor(descriptor);
}

bool bindPacketSocket(SocketDescriptor& socket, int interfaceIndex) {
    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(kNetGpuEtherType);
    address.sll_ifindex = interfaceIndex;
    return bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
}

std::optional<MacAddress> readLocalMacAddress(SocketDescriptor& socket,
    const std::string& interfaceName) {
    ifreq interfaceRequest{};
    std::strncpy(interfaceRequest.ifr_name, interfaceName.c_str(),
        sizeof(interfaceRequest.ifr_name) - 1);
    if (ioctl(socket.get(), SIOCGIFHWADDR, &interfaceRequest) < 0) {
        return std::nullopt;
    }

    MacAddress localAddress{};
    std::copy_n(reinterpret_cast<const uint8_t*>(interfaceRequest.ifr_hwaddr.sa_data),
        kEthernetAddressSize, localAddress.begin());
    return localAddress;
}

PacketWaitResult waitForPacket(int socketFd, int timeoutMilliseconds) {
    pollfd socketPoll{};
    socketPoll.fd = socketFd;
    socketPoll.events = POLLIN;

    const int pollResult = poll(&socketPoll, 1, timeoutMilliseconds);
    if (pollResult < 0) {
        if (errno == EINTR) return PacketWaitResult::interrupted;
        return PacketWaitResult::error;
    }
    if (pollResult == 0) return PacketWaitResult::timeout;
    if ((socketPoll.revents & POLLNVAL) != 0) return PacketWaitResult::closedSocket;
    if ((socketPoll.revents & POLLIN) == 0) return PacketWaitResult::error;
    return PacketWaitResult::ready;
}

PacketReceiveResult receivePacketBytes(int socketFd,
    std::array<uint8_t, kPacketBufferSize>& packetBuffer,
    size_t& packetSize) {
    iovec packetData{};
    packetData.iov_base = packetBuffer.data();
    packetData.iov_len = packetBuffer.size();

    msghdr packetMessage{};
    packetMessage.msg_iov = &packetData;
    packetMessage.msg_iovlen = 1;
    const ssize_t receivedSize = recvmsg(socketFd, &packetMessage, MSG_TRUNC);
    if (receivedSize < 0) {
        if (errno == EINTR) return PacketReceiveResult::interrupted;
        if (errno == EBADF) return PacketReceiveResult::closedSocket;
        return PacketReceiveResult::error;
    }
    if ((packetMessage.msg_flags & MSG_TRUNC) != 0) {
        return PacketReceiveResult::truncated;
    }
    if (static_cast<size_t>(receivedSize) > packetBuffer.size()) {
        return PacketReceiveResult::truncated;
    }
    packetSize = static_cast<size_t>(receivedSize);
    return PacketReceiveResult::valid;
}

std::optional<std::vector<uint8_t>> copyReceivedFrame(
    const std::array<uint8_t, kPacketBufferSize>& packetBuffer,
    size_t packetSize) {
    EthernetFrameView view;
    if (!decodeEthernetFrame(packetBuffer.data(), packetSize,
        kMaxEthernetPayloadSize, view)) return std::nullopt;
    return std::vector<uint8_t>(packetBuffer.begin(), packetBuffer.begin() + packetSize);
}

} // namespace

PacketSocket::~PacketSocket() = default;

bool PacketSocket::openOnInterface(const std::string& interfaceName) {
    lastError_.clear();
    if (socketDescriptor_.isValid()) {
        lastError_ = "socket is already open";
        return false;
    }
    if (interfaceName.empty()) {
        lastError_ = "interface name is empty";
        return false;
    }

    const auto interfaceIndex = findInterfaceIndex(interfaceName);
    if (!interfaceIndex) {
        lastError_ = "interface not found: ";
        lastError_ += interfaceName;
        return false;
    }

    auto socket = createPacketSocket(*interfaceIndex);
    if (!socket) {
        setSystemError(lastError_, "socket(AF_PACKET)");
        return false;
    }

    if (!bindPacketSocket(*socket, *interfaceIndex)) {
        setSystemError(lastError_, "bind(AF_PACKET)");
        return false;
    }

    const auto localAddress = readLocalMacAddress(*socket, interfaceName);
    if (!localAddress) {
        setSystemError(lastError_, "ioctl(SIOCGIFHWADDR)");
        return false;
    }

    socketDescriptor_ = std::move(*socket);
    interfaceIndex_ = *interfaceIndex;
    localAddress_ = *localAddress;
    return true;
}

const std::string& PacketSocket::lastError() const {
    return lastError_;
}

const MacAddress& PacketSocket::localAddress() const {
    return localAddress_;
}

bool PacketSocket::sendFrame(const std::vector<uint8_t>& frame) {
    if (!socketDescriptor_.isValid()) return false;

    EthernetFrameView view;
    if (!decodeEthernetFrame(frame.data(), frame.size(), kMaxEthernetPayloadSize, view)) return false;

    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(kNetGpuEtherType);
    address.sll_ifindex = interfaceIndex_;
    address.sll_halen = static_cast<unsigned char>(kEthernetAddressSize);
    std::copy(view.destination.begin(), view.destination.end(), address.sll_addr);

    const ssize_t sentBytes = sendto(socketDescriptor_.get(), frame.data(), frame.size(), 0,
        reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (sentBytes < 0) {
        setSystemError(lastError_, "sendto(AF_PACKET)");
        return false;
    }
    if (sentBytes != static_cast<ssize_t>(frame.size())) {
        lastError_ = "sendto(AF_PACKET) sent a partial frame";
        return false;
    }
    return true;
}

bool PacketSocket::receiveFrame(std::vector<uint8_t>& frame, int timeoutMilliseconds) {
    if (!socketDescriptor_.isValid() || timeoutMilliseconds < 0) return false;
    if (waitForPacket(socketDescriptor_.get(), timeoutMilliseconds) != PacketWaitResult::ready) {
        return false;
    }

    std::array<uint8_t, kPacketBufferSize> packetBuffer{};
    size_t packetSize = 0;
    if (receivePacketBytes(socketDescriptor_.get(), packetBuffer, packetSize)
        != PacketReceiveResult::valid) return false;
    const auto receivedFrame = copyReceivedFrame(packetBuffer, packetSize);
    if (!receivedFrame) return false;
    frame = std::move(*receivedFrame);
    return true;
}

} // namespace netgpu

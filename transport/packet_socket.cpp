#include "netgpu/packet_socket.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace netgpu {

namespace {

constexpr size_t kPacketBufferSize = kEthernetHeaderSize + kMaxEthernetPayloadSize;

void setSystemError(std::string& errorMessage, const char* operation) {
    errorMessage = operation;
    errorMessage += ": ";
    errorMessage += std::strerror(errno);
}

} // namespace

PacketSocket::~PacketSocket() {
    if (socketFd_ >= 0) close(socketFd_);
}

bool PacketSocket::openOnInterface(const std::string& interfaceName) {
    lastError_.clear();
    if (socketFd_ >= 0) {
        lastError_ = "socket is already open";
        return false;
    }
    if (interfaceName.empty()) {
        lastError_ = "interface name is empty";
        return false;
    }

    const unsigned int interfaceIndex = if_nametoindex(interfaceName.c_str());
    if (interfaceIndex == 0) {
        lastError_ = "interface not found: ";
        lastError_ += interfaceName;
        return false;
    }

    const int socketFd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(kNetGpuEtherType));
    if (socketFd < 0) {
        setSystemError(lastError_, "socket(AF_PACKET)");
        return false;
    }

    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(kNetGpuEtherType);
    address.sll_ifindex = static_cast<int>(interfaceIndex);
    if (bind(socketFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        setSystemError(lastError_, "bind(AF_PACKET)");
        close(socketFd);
        return false;
    }

    ifreq interfaceRequest{};
    std::strncpy(interfaceRequest.ifr_name, interfaceName.c_str(), sizeof(interfaceRequest.ifr_name) - 1);
    if (ioctl(socketFd, SIOCGIFHWADDR, &interfaceRequest) < 0) {
        setSystemError(lastError_, "ioctl(SIOCGIFHWADDR)");
        close(socketFd);
        return false;
    }

    socketFd_ = socketFd;
    interfaceIndex_ = address.sll_ifindex;
    std::copy_n(reinterpret_cast<const uint8_t*>(interfaceRequest.ifr_hwaddr.sa_data),
        kEthernetAddressSize, localAddress_.begin());
    return true;
}

const std::string& PacketSocket::lastError() const {
    return lastError_;
}

const MacAddress& PacketSocket::localAddress() const {
    return localAddress_;
}

bool PacketSocket::sendFrame(const std::vector<uint8_t>& frame) {
    if (socketFd_ < 0) return false;

    EthernetFrameView view;
    if (!decodeEthernetFrame(frame.data(), frame.size(), kMaxEthernetPayloadSize, view)) return false;

    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(kNetGpuEtherType);
    address.sll_ifindex = interfaceIndex_;
    address.sll_halen = static_cast<unsigned char>(kEthernetAddressSize);
    std::copy(view.destination.begin(), view.destination.end(), address.sll_addr);

    const ssize_t sentBytes = sendto(socketFd_, frame.data(), frame.size(), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    return sentBytes == static_cast<ssize_t>(frame.size());
}

bool PacketSocket::receiveFrame(std::vector<uint8_t>& frame, int timeoutMilliseconds) {
    if (socketFd_ < 0 || timeoutMilliseconds < 0) return false;

    pollfd socketPoll{};
    socketPoll.fd = socketFd_;
    socketPoll.events = POLLIN;

    const int pollResult = poll(&socketPoll, 1, timeoutMilliseconds);
    if (pollResult <= 0 || (socketPoll.revents & POLLIN) == 0) return false;

    std::array<uint8_t, kPacketBufferSize> packetBuffer{};
    iovec packetData{};
    packetData.iov_base = packetBuffer.data();
    packetData.iov_len = packetBuffer.size();

    msghdr packetMessage{};
    packetMessage.msg_iov = &packetData;
    packetMessage.msg_iovlen = 1;
    const ssize_t packetSize = recvmsg(socketFd_, &packetMessage, MSG_TRUNC);
    if (packetSize < 0 || (packetMessage.msg_flags & MSG_TRUNC) != 0) return false;
    if (static_cast<size_t>(packetSize) > packetBuffer.size()) return false;

    EthernetFrameView view;
    if (!decodeEthernetFrame(packetBuffer.data(), static_cast<size_t>(packetSize), kMaxEthernetPayloadSize, view)) return false;

    frame.assign(packetBuffer.begin(), packetBuffer.begin() + packetSize);
    return true;
}

} // namespace netgpu

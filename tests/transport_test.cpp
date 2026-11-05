#include "netgpu/unix_domain_socket.hpp"

#include <array>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

constexpr uint32_t kMaxFragmentLength = 256 * 1024;
constexpr size_t kPayloadSize = 256 * 1024;
constexpr int kSmallSocketBufferSize = 4096;
constexpr uint16_t kTestMessageType = 1;
constexpr uint64_t kTestRequestId = 42;
constexpr size_t kPartialHeaderSize = 12;
constexpr size_t kSmallPayloadSize = 4;
constexpr size_t kPartialPayloadSize = 2;
constexpr uint32_t kSmallMaxFragmentLength = 16;

bool setSocketBufferSize(int socketFd, int option, int size) {
    return setsockopt(socketFd, SOL_SOCKET, option, &size, sizeof(size)) == 0;
}

bool makeSocketPair(int socketFds[2]) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socketFds) < 0) return false;
    if (!setSocketBufferSize(socketFds[0], SO_SNDBUF, kSmallSocketBufferSize)) return false;
    if (!setSocketBufferSize(socketFds[1], SO_RCVBUF, kSmallSocketBufferSize)) return false;
    return true;
}

std::vector<uint8_t> makePayload() {
    std::vector<uint8_t> payload(kPayloadSize);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }
    return payload;
}

netgpu::WireHeader makeHeader() {
    netgpu::WireHeader header;
    header.messageType = kTestMessageType;
    header.requestId = kTestRequestId;
    header.fragmentLength = static_cast<uint32_t>(kPayloadSize);
    header.totalLength = header.fragmentLength;
    return header;
}

bool largeMessageSurvivesStreamTransport() {
    int socketFds[2];
    if (!makeSocketPair(socketFds)) return false;

    netgpu::UnixDomainSocket sender(socketFds[0], kMaxFragmentLength);
    netgpu::UnixDomainSocket receiver(socketFds[1], kMaxFragmentLength);
    const netgpu::WireHeader expectedHeader = makeHeader();
    const std::vector<uint8_t> expectedPayload = makePayload();

    bool sent = false;
    std::thread senderThread([&] {
        sent = sender.sendMessage(expectedHeader, expectedPayload);
    });

    netgpu::TransportMessage received;
    const bool receivedMessage = receiver.receiveMessage(received);
    senderThread.join();

    return sent
        && receivedMessage
        && received.header.requestId == expectedHeader.requestId
        && received.header.fragmentLength == expectedHeader.fragmentLength
        && received.payload == expectedPayload;
}

bool receiveFailsOnPartialHeader() {
    int socketFds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socketFds) < 0) return false;

    netgpu::UnixDomainSocket receiver(socketFds[0], kSmallMaxFragmentLength);
    const netgpu::WireBytes headerBytes = netgpu::encodeHeader(makeHeader());
    const ssize_t sentBytes = send(socketFds[1], headerBytes.data(), kPartialHeaderSize, 0);
    shutdown(socketFds[1], SHUT_WR);
    close(socketFds[1]);

    netgpu::TransportMessage message;
    return sentBytes == static_cast<ssize_t>(kPartialHeaderSize) && !receiver.receiveMessage(message);
}

bool receiveFailsOnPartialPayload() {
    int socketFds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socketFds) < 0) return false;

    netgpu::UnixDomainSocket receiver(socketFds[0], kSmallMaxFragmentLength);
    netgpu::WireHeader header;
    header.messageType = kTestMessageType;
    header.fragmentLength = static_cast<uint32_t>(kSmallPayloadSize);
    header.totalLength = header.fragmentLength;
    const netgpu::WireBytes headerBytes = netgpu::encodeHeader(header);
    const std::array<uint8_t, kPartialPayloadSize> payload{};
    const ssize_t sentHeaderBytes = send(socketFds[1], headerBytes.data(), headerBytes.size(), 0);
    const ssize_t sentPayloadBytes = send(socketFds[1], payload.data(), payload.size(), 0);
    shutdown(socketFds[1], SHUT_WR);
    close(socketFds[1]);

    netgpu::TransportMessage message;
    return sentHeaderBytes == static_cast<ssize_t>(headerBytes.size())
        && sentPayloadBytes == static_cast<ssize_t>(kPartialPayloadSize)
        && !receiver.receiveMessage(message);
}

bool sendFailsWhenPeerCloses() {
    int socketFds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socketFds) < 0) return false;

    netgpu::UnixDomainSocket sender(socketFds[0], kSmallMaxFragmentLength);
    close(socketFds[1]);

    netgpu::WireHeader header;
    header.messageType = kTestMessageType;
    const std::vector<uint8_t> payload;
    return !sender.sendMessage(header, payload);
}

// exit code zero means every check passed
int main() {
    if (!largeMessageSurvivesStreamTransport()) return 1;
    if (!receiveFailsOnPartialHeader()) return 1;
    if (!receiveFailsOnPartialPayload()) return 1;
    if (!sendFailsWhenPeerCloses()) return 1;

    return 0;
}

#include "netgpu/unix_domain_socket.hpp"

#include <sys/socket.h>
#include <thread>

constexpr uint32_t kMaxFragmentLength = 256 * 1024;
constexpr size_t kPayloadSize = 256 * 1024;
constexpr int kSmallSocketBufferSize = 4096;
constexpr uint16_t kTestMessageType = 1;
constexpr uint64_t kTestRequestId = 42;

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

// exit code zero means every check passed
int main() {
    return largeMessageSurvivesStreamTransport() ? 0 : 1;
}

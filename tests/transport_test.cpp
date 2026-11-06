#include "netgpu/unix_domain_socket.hpp"

#include <array>
#include <cstdlib>
#include <string>
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
constexpr size_t kConcurrentMessageCount = 64;
constexpr size_t kConcurrentSenderCount = 4;
constexpr size_t kConcurrentPayloadSize = 32;
constexpr uint16_t kRequestMessageType = 2;
constexpr uint16_t kResponseMessageType = 3;
constexpr int kListenBacklog = 1;

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

std::vector<uint8_t> makeConcurrentPayload(uint64_t requestId) {
    std::vector<uint8_t> payload(kConcurrentPayloadSize);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(requestId + i);
    }
    return payload;
}

netgpu::WireHeader makeConcurrentHeader(uint16_t messageType, uint64_t requestId) {
    netgpu::WireHeader header;
    header.messageType = messageType;
    header.requestId = requestId;
    header.fragmentLength = static_cast<uint32_t>(kConcurrentPayloadSize);
    header.totalLength = header.fragmentLength;
    return header;
}

bool isValidConcurrentRequest(const netgpu::TransportMessage& message) {
    const uint64_t requestId = message.header.requestId;
    if (message.header.messageType != kRequestMessageType) return false;
    if (requestId == 0 || requestId > kConcurrentMessageCount) return false;
    if (message.header.fragmentLength != kConcurrentPayloadSize) return false;
    return message.payload == makeConcurrentPayload(requestId);
}

bool runHostSession(netgpu::UnixDomainSocket& listener) {
    netgpu::UnixDomainSocket host(kMaxFragmentLength);
    if (!listener.acceptClient(host)) return false;

    std::array<bool, kConcurrentMessageCount> seenRequests{};
    for (size_t i = 0; i < kConcurrentMessageCount; ++i) {
        netgpu::TransportMessage request;
        if (!host.receiveMessage(request) || !isValidConcurrentRequest(request)) return false;

        const size_t requestIndex = static_cast<size_t>(request.header.requestId - 1);
        if (seenRequests[requestIndex]) return false;
        seenRequests[requestIndex] = true;

        const netgpu::WireHeader responseHeader = makeConcurrentHeader(
            kResponseMessageType, request.header.requestId);
        if (!host.sendMessage(responseHeader, request.payload)) return false;
    }

    return true;
}

bool sendConcurrentRequests(netgpu::UnixDomainSocket& client, size_t senderIndex) {
    for (size_t requestIndex = senderIndex; requestIndex < kConcurrentMessageCount; requestIndex += kConcurrentSenderCount) {
        const uint64_t requestId = requestIndex + 1;
        const netgpu::WireHeader header = makeConcurrentHeader(kRequestMessageType, requestId);
        if (!client.sendMessage(header, makeConcurrentPayload(requestId))) return false;
    }
    return true;
}

bool receiveConcurrentResponses(netgpu::UnixDomainSocket& client) {
    std::array<bool, kConcurrentMessageCount> seenResponses{};
    for (size_t i = 0; i < kConcurrentMessageCount; ++i) {
        netgpu::TransportMessage response;
        if (!client.receiveMessage(response)) return false;
        const uint64_t requestId = response.header.requestId;
        if (response.header.messageType != kResponseMessageType) return false;
        if (requestId == 0 || requestId > kConcurrentMessageCount) return false;
        if (response.payload != makeConcurrentPayload(requestId)) return false;

        const size_t responseIndex = static_cast<size_t>(requestId - 1);
        if (seenResponses[responseIndex]) return false;
        seenResponses[responseIndex] = true;
    }
    return true;
}

bool runConcurrentClient(netgpu::UnixDomainSocket& client) {
    std::array<bool, kConcurrentSenderCount> sendResults{};
    bool responsesReceived = false;
    std::thread responseThread([&] {
        responsesReceived = receiveConcurrentResponses(client);
    });

    std::array<std::thread, kConcurrentSenderCount> senderThreads;
    for (size_t i = 0; i < senderThreads.size(); ++i) {
        senderThreads[i] = std::thread([&, i] {
            sendResults[i] = sendConcurrentRequests(client, i);
        });
    }

    for (std::thread& senderThread : senderThreads) senderThread.join();
    responseThread.join();

    if (!responsesReceived) return false;
    for (bool sendSucceeded : sendResults) {
        if (!sendSucceeded) return false;
    }
    return true;
}

bool createTemporarySocketDirectory(std::string& directory) {
    char directoryTemplate[] = "/tmp/netgpu-transport-XXXXXX";
    char* createdDirectory = mkdtemp(directoryTemplate);
    if (createdDirectory == nullptr) return false;
    directory = createdDirectory;
    return true;
}

bool endToEndConcurrentMessagesSurvive() {
    std::string directory;
    if (!createTemporarySocketDirectory(directory)) return false;

    const std::string socketPath = directory + "/host.sock";
    bool testPassed = false;
    {
        netgpu::UnixDomainSocket listener(kMaxFragmentLength);
        if (listener.listenAt(socketPath, kListenBacklog)) {
            netgpu::UnixDomainSocket client(kMaxFragmentLength);
            if (client.connectTo(socketPath)) {
                bool hostSessionPassed = false;
                std::thread hostThread([&] {
                    hostSessionPassed = runHostSession(listener);
                });

                const bool clientSessionPassed = runConcurrentClient(client);
                hostThread.join();
                testPassed = clientSessionPassed && hostSessionPassed;
            }
        }
    }
    rmdir(directory.c_str());
    return testPassed;
}

// exit code zero means every check passed
int main() {
    if (!largeMessageSurvivesStreamTransport()) return 1;
    if (!receiveFailsOnPartialHeader()) return 1;
    if (!receiveFailsOnPartialPayload()) return 1;
    if (!sendFailsWhenPeerCloses()) return 1;
    if (!endToEndConcurrentMessagesSurvive()) return 1;

    return 0;
}

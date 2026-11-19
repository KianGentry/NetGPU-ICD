#include "netgpu/unix_domain_socket.hpp"

#include <array>
#include <cstdlib>
#include <future>
#include <iostream>
#include <optional>
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

class SocketPair {
public:
    SocketPair() {
        valid_ = socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors_) == 0;
    }

    ~SocketPair() {
        closeDescriptor(0);
        closeDescriptor(1);
    }

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    bool isValid() const {
        return valid_;
    }

    int get(size_t index) const {
        return descriptors_[index];
    }

    int release(size_t index) {
        const int descriptor = descriptors_[index];
        descriptors_[index] = -1;
        return descriptor;
    }

private:
    void closeDescriptor(size_t index) {
        if (descriptors_[index] >= 0) {
            close(descriptors_[index]);
            descriptors_[index] = -1;
        }
    }

    int descriptors_[2] = {-1, -1};
    bool valid_ = false;
};

class TemporarySocketDirectory {
public:
    ~TemporarySocketDirectory() {
        if (!path_.empty()) rmdir(path_.c_str());
    }

    bool create() {
        char directoryTemplate[] = "/tmp/netgpu-transport-XXXXXX";
        char* createdDirectory = mkdtemp(directoryTemplate);
        if (createdDirectory == nullptr) return false;
        path_ = createdDirectory;
        return true;
    }

    const std::string& path() const {
        return path_;
    }

private:
    std::string path_;
};

bool setSocketBufferSize(int socketFd, int option, int size) {
    return setsockopt(socketFd, SOL_SOCKET, option, &size, sizeof(size)) == 0;
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
    SocketPair sockets;
    if (!sockets.isValid()) return false;
    if (!setSocketBufferSize(sockets.get(0), SO_SNDBUF, kSmallSocketBufferSize)) return false;
    if (!setSocketBufferSize(sockets.get(1), SO_RCVBUF, kSmallSocketBufferSize)) return false;

    netgpu::UnixDomainSocket sender(sockets.release(0), kMaxFragmentLength);
    netgpu::UnixDomainSocket receiver(sockets.release(1), kMaxFragmentLength);
    const netgpu::WireHeader expectedHeader = makeHeader();
    const std::vector<uint8_t> expectedPayload = makePayload();

    std::future<bool> sendResult = std::async(std::launch::async,
        [&] { return sender.sendMessage(expectedHeader, expectedPayload); });

    netgpu::TransportMessage received;
    const bool receivedMessage = receiver.receiveMessage(received);
    const bool sent = sendResult.get();

    return sent
        && receivedMessage
        && received.header.requestId == expectedHeader.requestId
        && received.header.fragmentLength == expectedHeader.fragmentLength
        && received.payload == expectedPayload;
}

bool receiveFailsOnPartialHeader() {
    SocketPair sockets;
    if (!sockets.isValid()) return false;

    netgpu::UnixDomainSocket receiver(sockets.release(0), kSmallMaxFragmentLength);
    const netgpu::WireBytes headerBytes = netgpu::encodeHeader(makeHeader());
    const ssize_t sentBytes = send(sockets.get(1), headerBytes.data(), kPartialHeaderSize, 0);
    shutdown(sockets.get(1), SHUT_WR);

    netgpu::TransportMessage message;
    return sentBytes == static_cast<ssize_t>(kPartialHeaderSize) && !receiver.receiveMessage(message);
}

bool receiveFailsOnPartialPayload() {
    SocketPair sockets;
    if (!sockets.isValid()) return false;

    netgpu::UnixDomainSocket receiver(sockets.release(0), kSmallMaxFragmentLength);
    netgpu::WireHeader header;
    header.messageType = kTestMessageType;
    header.fragmentLength = static_cast<uint32_t>(kSmallPayloadSize);
    header.totalLength = header.fragmentLength;
    const netgpu::WireBytes headerBytes = netgpu::encodeHeader(header);
    const std::array<uint8_t, kPartialPayloadSize> payload{};
    const ssize_t sentHeaderBytes = send(sockets.get(1), headerBytes.data(), headerBytes.size(), 0);
    const ssize_t sentPayloadBytes = send(sockets.get(1), payload.data(), payload.size(), 0);
    shutdown(sockets.get(1), SHUT_WR);

    netgpu::TransportMessage message;
    return sentHeaderBytes == static_cast<ssize_t>(headerBytes.size())
        && sentPayloadBytes == static_cast<ssize_t>(kPartialPayloadSize)
        && !receiver.receiveMessage(message);
}

bool sendFailsWhenPeerCloses() {
    SocketPair sockets;
    if (!sockets.isValid()) return false;

    netgpu::UnixDomainSocket sender(sockets.release(0), kSmallMaxFragmentLength);
    close(sockets.release(1));

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
    std::future<bool> responsesReceived = std::async(std::launch::async,
        [&] { return receiveConcurrentResponses(client); });

    std::array<std::future<bool>, kConcurrentSenderCount> sendResults;
    for (size_t senderIndex = 0; senderIndex < sendResults.size(); ++senderIndex) {
        sendResults[senderIndex] = std::async(std::launch::async,
            [&client, senderIndex] {
                return sendConcurrentRequests(client, senderIndex);
            });
    }

    if (!responsesReceived.get()) return false;
    for (std::future<bool>& sendResult : sendResults) {
        if (!sendResult.get()) return false;
    }
    return true;
}

bool endToEndConcurrentMessagesSurvive() {
    TemporarySocketDirectory directory;
    if (!directory.create()) return false;

    const std::string socketPath = directory.path() + "/host.sock";
    bool testPassed = false;
    {
        netgpu::UnixDomainSocket listener(kMaxFragmentLength);
        if (listener.listenAt(socketPath, kListenBacklog)) {
            netgpu::UnixDomainSocket client(kMaxFragmentLength);
            if (client.connectTo(socketPath)) {
                std::future<bool> hostSessionPassed = std::async(std::launch::async,
                    [&] { return runHostSession(listener); });

                const bool clientSessionPassed = runConcurrentClient(client);
                testPassed = clientSessionPassed && hostSessionPassed.get();
            }
        }
    }
    return testPassed;
}

// exit code zero means every check passed
int main() {
    struct TransportTestCase {
        const char* name;
        bool (*run)();
    };
    const TransportTestCase tests[] = {
        {"largeMessageSurvivesStreamTransport", largeMessageSurvivesStreamTransport},
        {"receiveFailsOnPartialHeader", receiveFailsOnPartialHeader},
        {"receiveFailsOnPartialPayload", receiveFailsOnPartialPayload},
        {"sendFailsWhenPeerCloses", sendFailsWhenPeerCloses},
        {"endToEndConcurrentMessagesSurvive", endToEndConcurrentMessagesSurvive}
    };
    for (const TransportTestCase& test : tests) {
        if (!test.run()) {
            std::cerr << test.name << " failed\n";
            return 1;
        }
    }
    return 0;
}

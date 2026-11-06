#include "netgpu/packet_socket.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr size_t kSmokePayloadSize = 46;
constexpr int kReceiveTimeoutMilliseconds = 10000;
constexpr uint8_t kPingType = 'P';
constexpr uint8_t kPongType = 'Q';

int hexDigitValue(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

bool parseMacAddress(const std::string& text, netgpu::MacAddress& address) {
    constexpr size_t kMacTextSize = 17;
    constexpr size_t kMacDigitStride = 3;
    if (text.size() != kMacTextSize) return false;

    for (size_t i = 0; i < address.size(); ++i) {
        const size_t offset = i * kMacDigitStride;
        const int high = hexDigitValue(text[offset]);
        const int low = hexDigitValue(text[offset + 1]);
        if (high < 0 || low < 0) return false;
        if (i + 1 < address.size() && text[offset + 2] != ':') return false;
        address[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

std::vector<uint8_t> makeSmokePayload(uint8_t messageType) {
    std::vector<uint8_t> payload(kSmokePayloadSize);
    payload[0] = 'N';
    payload[1] = 'G';
    payload[2] = 'P';
    payload[3] = 'U';
    payload[4] = messageType;
    return payload;
}

bool payloadMatches(const netgpu::EthernetFrameView& frame, uint8_t messageType) {
    const std::vector<uint8_t> expected = makeSmokePayload(messageType);
    if (frame.payloadLength != expected.size()) return false;
    for (size_t i = 0; i < expected.size(); ++i) {
        if (frame.payload[i] != expected[i]) return false;
    }
    return true;
}

bool receiveSmokeMessage(netgpu::PacketSocket& socket, const netgpu::MacAddress& localAddress, 
    const netgpu::MacAddress* peerAddress, uint8_t messageType,
    netgpu::MacAddress& sourceAddress) {

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kReceiveTimeoutMilliseconds);

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        std::vector<uint8_t> frame;
        if (!socket.receiveFrame(frame, static_cast<int>(remaining))) return false;

        netgpu::EthernetFrameView view;
        if (!netgpu::decodeEthernetFrame(frame.data(), frame.size(), netgpu::kMaxEthernetPayloadSize, view)) continue;
        if (view.destination != localAddress) continue;
        if (peerAddress != nullptr && view.source != *peerAddress) continue;
        if (!payloadMatches(view, messageType)) continue;

        sourceAddress = view.source;
        return true;
    }
    return false;
}

bool sendSmokeMessage(netgpu::PacketSocket& socket, const netgpu::MacAddress& destination,
    const netgpu::MacAddress& source, uint8_t messageType) {

    std::vector<uint8_t> frame;
    if (!netgpu::encodeEthernetFrame(destination, source, makeSmokePayload(messageType),
        netgpu::kMaxEthernetPayloadSize, frame)) return false;
    return socket.sendFrame(frame);
}

int runListener(const std::string& interfaceName, const netgpu::MacAddress& localAddress) {
    netgpu::PacketSocket socket;
    if (!socket.openOnInterface(interfaceName)) {
        std::cerr << "could not open AF_PACKET socket on " << interfaceName
                  << ": " << socket.lastError() << '\n';
        return 1;
    }

    netgpu::MacAddress peerAddress{};
    if (!receiveSmokeMessage(socket, localAddress, nullptr, kPingType, peerAddress)) {
        std::cerr << "timed out waiting for ping\n";
        return 1;
    }
    if (!sendSmokeMessage(socket, peerAddress, localAddress, kPongType)) {
        std::cerr << "could not send pong\n";
        return 1;
    }
    std::cout << "received ping and sent pong\n";
    return 0;
}

int runSender(const std::string& interfaceName, const netgpu::MacAddress& localAddress,
    const netgpu::MacAddress& peerAddress) {

    netgpu::PacketSocket socket;
    if (!socket.openOnInterface(interfaceName)) {
        std::cerr << "could not open AF_PACKET socket on " << interfaceName << ": " << socket.lastError() << '\n';
        return 1;
    }
    if (!sendSmokeMessage(socket, peerAddress, localAddress, kPingType)) {
        std::cerr << "could not send ping\n";
        return 1;
    }
    netgpu::MacAddress responseAddress{};
    if (!receiveSmokeMessage(socket, localAddress, &peerAddress, kPongType, responseAddress)) {
        std::cerr << "timed out waiting for pong\n";
        return 1;
    }
    std::cout << "received pong from peer\n";
    return 0;
}

void printUsage(const char* programName) {
    std::cerr << "usage:\n" << programName << " listen <interface> <local-mac>\n" << programName << " send <interface> <local-mac> <peer-mac>\n";
}

} // namespace

int main(int argumentCount, char** arguments) {
    if (argumentCount != 4 && argumentCount != 5) {
        printUsage(arguments[0]);
        return 1;
    }

    netgpu::MacAddress localAddress{};
    if (!parseMacAddress(arguments[3], localAddress)) {
        printUsage(arguments[0]);
        return 1;
    }

    const std::string mode = arguments[1];
    const std::string interfaceName = arguments[2];
    if (mode == "listen" && argumentCount == 4) return runListener(interfaceName, localAddress);
    if (mode == "send" && argumentCount == 5) {
        netgpu::MacAddress peerAddress{};
        if (!parseMacAddress(arguments[4], peerAddress)) {
            printUsage(arguments[0]);
            return 1;
        }
        return runSender(interfaceName, localAddress, peerAddress);
    }

    printUsage(arguments[0]);
    return 1;
}

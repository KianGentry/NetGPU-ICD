#include "netgpu/packet_socket.hpp"
#include "netgpu/wire.hpp"

#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kReceiveTimeoutMilliseconds = 10000;
constexpr uint32_t kMaxWireFragmentLength =
    static_cast<uint32_t>(netgpu::kMaxEthernetPayloadSize - netgpu::kWireHeaderSize);
constexpr netgpu::MessageType kPingType = netgpu::MessageType::request;
constexpr netgpu::MessageType kPongType = netgpu::MessageType::response;

std::optional<uint8_t> parseHexNibble(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return std::nullopt;
}

std::optional<uint8_t> parseMacAddressByte(const std::string& text, size_t offset) {
    if (offset + 1 >= text.size()) return std::nullopt;
    const auto high = parseHexNibble(text[offset]);
    const auto low = parseHexNibble(text[offset + 1]);
    if (!high || !low) return std::nullopt;
    return static_cast<uint8_t>((*high << 4) | *low);
}

std::optional<netgpu::MacAddress> parseMacAddress(const std::string& text) {
    constexpr size_t kMacTextSize = 17;
    constexpr size_t kMacDigitStride = 3;
    if (text.size() != kMacTextSize) return std::nullopt;

    netgpu::MacAddress address{};
    for (size_t i = 0; i < address.size(); ++i) {
        const size_t offset = i * kMacDigitStride;
        if (i + 1 < address.size() && text[offset + 2] != ':') {
            return std::nullopt;
        }
        const auto byte = parseMacAddressByte(text, offset);
        if (!byte) return std::nullopt;
        address[i] = *byte;
    }
    return address;
}

bool payloadMatches(const netgpu::EthernetFrameView& frame, netgpu::MessageType messageType) {
    const auto payload = netgpu::decodeWirePayload(
        netgpu::ByteView{frame.payload, frame.payloadLength}, kMaxWireFragmentLength);
    return payload
        && payload->header.messageType == static_cast<uint16_t>(messageType)
        && payload->fragment.size == 0;
}

using ReceiveDeadline = std::chrono::steady_clock::time_point;

ReceiveDeadline makeReceiveDeadline(int timeoutMilliseconds) {
    return std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
}

std::optional<std::vector<uint8_t>> waitForNextFrame(
    netgpu::PacketSocket& socket, const ReceiveDeadline& deadline) {
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        std::vector<uint8_t> frame;
        if (socket.receiveFrame(frame, static_cast<int>(remaining))) {
            return frame;
        }
    }
    return std::nullopt;
}

std::optional<netgpu::EthernetFrameView> decodeSmokeFrame(
    const std::vector<uint8_t>& frame) {
    netgpu::EthernetFrameView view;
    if (!netgpu::decodeEthernetFrame(frame.data(), frame.size(),
        netgpu::kMaxEthernetPayloadSize, view)) return std::nullopt;
    return view;
}

bool frameMatchesPeerAndType(const netgpu::EthernetFrameView& frame,
    const netgpu::MacAddress& localAddress,
    const netgpu::MacAddress* peerAddress,
    netgpu::MessageType messageType) {
    if (frame.destination != localAddress) return false;
    if (peerAddress != nullptr && frame.source != *peerAddress) return false;
    return payloadMatches(frame, messageType);
}

bool receiveSmokeMessage(netgpu::PacketSocket& socket,
    const netgpu::MacAddress& localAddress,
    const netgpu::MacAddress* peerAddress,
    netgpu::MessageType messageType,
    netgpu::MacAddress& sourceAddress) {
    const ReceiveDeadline deadline = makeReceiveDeadline(kReceiveTimeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto frame = waitForNextFrame(socket, deadline);
        if (!frame) return false;
        const auto decoded = decodeSmokeFrame(*frame);
        if (!decoded) continue;
        if (!frameMatchesPeerAndType(*decoded, localAddress, peerAddress, messageType)) continue;
        sourceAddress = decoded->source;
        return true;
    }
    return false;
}

bool sendSmokeMessage(netgpu::PacketSocket& socket, const netgpu::MacAddress& destination,
    const netgpu::MacAddress& source, netgpu::MessageType messageType) {

    netgpu::WireHeader header;
    header.messageType = static_cast<uint16_t>(messageType);
    const auto payload = netgpu::encodeWirePayload(header, {});
    if (!payload) return false;
    const auto frame = netgpu::encodeEthernetFrame(destination, source, *payload,
        netgpu::kMaxEthernetPayloadSize);
    return frame && socket.sendFrame(*frame);
}

int runListener(const std::string& interfaceName) {
    std::cerr << "opening AF_PACKET on " << interfaceName << '\n';
    netgpu::PacketSocket socket;
    if (!socket.openOnInterface(interfaceName)) {
        std::cerr << "could not open AF_PACKET socket on " << interfaceName << ": " << socket.lastError() << '\n';
        return 1;
    }

    std::cerr << "listening on " << interfaceName << " for ping\n";
    const netgpu::MacAddress& localAddress = socket.localAddress();
    netgpu::MacAddress peerAddress{};
    if (!receiveSmokeMessage(socket, localAddress, nullptr, kPingType, peerAddress)) {
        std::cerr << "timed out waiting for ping\n";
        return 1;
    }
    std::cerr << "ping received; sending pong\n";
    if (!sendSmokeMessage(socket, peerAddress, localAddress, kPongType)) {
        std::cerr << "could not send pong: " << socket.lastError() << '\n';
        return 1;
    }
    std::cout << "received ping and sent pong\n";
    return 0;
}

int runSender(const std::string& interfaceName, const netgpu::MacAddress& peerAddress) {

    std::cerr << "opening AF_PACKET on " << interfaceName << '\n';
    netgpu::PacketSocket socket;
    if (!socket.openOnInterface(interfaceName)) {
        std::cerr << "could not open AF_PACKET socket on " << interfaceName << ": " << socket.lastError() << '\n';
        return 1;
    }
    std::cerr << "socket ready on " << interfaceName << "; sending ping\n";
    const netgpu::MacAddress& localAddress = socket.localAddress();
    if (!sendSmokeMessage(socket, peerAddress, localAddress, kPingType)) {
        std::cerr << "could not send ping: " << socket.lastError() << '\n';
        return 1;
    }
    std::cerr << "ping sent; waiting for pong\n";
    netgpu::MacAddress responseAddress{};
    if (!receiveSmokeMessage(socket, localAddress, &peerAddress, kPongType, responseAddress)) {
        std::cerr << "timed out waiting for pong\n";
        return 1;
    }
    std::cout << "received pong from peer\n";
    return 0;
}

void printUsage(const char* programName) {
    std::cerr << "usage:\n" << programName << " listen <interface>\n" << programName << " send <interface> <peer-mac>\n";
}

enum class SmokeCommandType {
    listen,
    send
};

struct SmokeCommand {
    SmokeCommandType type;
    std::string interfaceName;
    std::optional<netgpu::MacAddress> peerAddress;
};

std::optional<SmokeCommand> parseSmokeCommand(int argumentCount, char** arguments) {
    if (argumentCount < 3) return std::nullopt;

    const std::string mode = arguments[1];
    const std::string interfaceName = arguments[2];
    if (mode == "listen" && argumentCount == 3) {
        return SmokeCommand{SmokeCommandType::listen, interfaceName, std::nullopt};
    }
    if (mode == "send" && argumentCount == 4) {
        const auto peerAddress = parseMacAddress(arguments[3]);
        if (!peerAddress) return std::nullopt;
        return SmokeCommand{SmokeCommandType::send, interfaceName, peerAddress};
    }
    return std::nullopt;
}

} // namespace

int main(int argumentCount, char** arguments) {
    const auto command = parseSmokeCommand(argumentCount, arguments);
    if (!command) {
        printUsage(arguments[0]);
        return 1;
    }
    if (command->type == SmokeCommandType::listen) {
        return runListener(command->interfaceName);
    }
    return runSender(command->interfaceName, *command->peerAddress);
}

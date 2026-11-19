#include "netgpu/transport.hpp"

#include <utility>

namespace netgpu {

ITransport::ITransport(uint32_t maxFragmentLength) : maxFragmentLength_(maxFragmentLength) {}

bool ITransport::validateSendRequest(const WireHeader& header,
    const std::vector<uint8_t>& payload) const {
    return payload.size() == header.fragmentLength
        && validateHeader(header, maxFragmentLength_) == HeaderValidationResult::valid;
}

bool ITransport::writeHeaderAndPayload(const WireBytes& headerBytes,
    const std::vector<uint8_t>& payload) {
    if (!writeExactly(ByteView{headerBytes.data(), headerBytes.size()})) {
        return false;
    }
    return writeExactly(ByteView{payload.data(), payload.size()});
}

std::optional<WireHeader> ITransport::readAndValidateHeader() {
    WireBytes encodedHeader{};
    if (!readExactly(MutableByteView{encodedHeader.data(), encodedHeader.size()})) {
        return std::nullopt;
    }

    const WireHeader header = decodeHeader(encodedHeader);
    if (validateHeader(header, maxFragmentLength_) != HeaderValidationResult::valid) {
        return std::nullopt;
    }
    return header;
}

std::optional<size_t> ITransport::validateAndGetPayloadSize(
    const WireHeader& header) const {
    if (header.fragmentLength > maxFragmentLength_) return std::nullopt;
    return static_cast<size_t>(header.fragmentLength);
}

std::optional<std::vector<uint8_t>> ITransport::readPayload(size_t payloadSize) {
    std::vector<uint8_t> payload(payloadSize);
    if (!readExactly(MutableByteView{payload.data(), payload.size()})) {
        return std::nullopt;
    }
    return payload;
}

TransportMessage ITransport::buildTransportMessage(WireHeader header,
    std::vector<uint8_t> payload) const {
    return TransportMessage{header, std::move(payload)};
}

bool ITransport::sendMessage(const WireHeader& header, const std::vector<uint8_t>& payload) {
    if (!validateSendRequest(header, payload)) return false;
    const WireBytes encodedHeader = encodeHeader(header);
    std::lock_guard<std::mutex> lock(sendMutex_);
    return writeHeaderAndPayload(encodedHeader, payload);
}

bool ITransport::receiveMessage(TransportMessage& message) {
    std::lock_guard<std::mutex> lock(receiveMutex_);
    const auto header = readAndValidateHeader();
    if (!header) return false;
    const auto payloadSize = validateAndGetPayloadSize(*header);
    if (!payloadSize) return false;
    const auto payload = readPayload(*payloadSize);
    if (!payload) return false;
    message = buildTransportMessage(*header, std::move(*payload));
    return true;
}

} // namespace netgpu

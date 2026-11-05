#include "netgpu/transport.hpp"

#include <utility>

namespace netgpu {

ITransport::ITransport(uint32_t maxFragmentLength)
    : maxFragmentLength_(maxFragmentLength) {}

bool ITransport::sendMessage(const WireHeader& header, const std::vector<uint8_t>& payload) {
    if (payload.size() != header.fragmentLength) return false;
    if (validateHeader(header, maxFragmentLength_) != HeaderValidationResult::valid) return false;

    const WireBytes encodedHeader = encodeHeader(header);
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!writeExactBytes(encodedHeader.data(), encodedHeader.size())) return false;
    return writeExactBytes(payload.data(), payload.size());
}

bool ITransport::receiveMessage(TransportMessage& message) {
    std::lock_guard<std::mutex> lock(receiveMutex_);
    WireBytes encodedHeader{};
    if (!readExactBytes(encodedHeader.data(), encodedHeader.size())) return false;

    const WireHeader header = decodeHeader(encodedHeader);
    // reject invalid headers before allocating payload memory
    if (validateHeader(header, maxFragmentLength_) != HeaderValidationResult::valid) return false;

    std::vector<uint8_t> payload(header.fragmentLength);
    if (!readExactBytes(payload.data(), payload.size())) return false;

    message.header = header;
    message.payload = std::move(payload);
    return true;
}

} // namespace netgpu

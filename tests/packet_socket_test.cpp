#include "netgpu/packet_socket.hpp"

constexpr char kMissingInterface[] = "netgpu-no-such-interface";

int main() {
    netgpu::PacketSocket socket;
    if (socket.openOnInterface(kMissingInterface)) return 1;
    return socket.lastError().empty() ? 1 : 0;
}

#include "netgpu/packet_socket.hpp"

constexpr char kMissingInterface[] = "netgpu-no-such-interface";

int main() {
    netgpu::PacketSocket socket;
    return socket.openOnInterface(kMissingInterface) ? 1 : 0;
}

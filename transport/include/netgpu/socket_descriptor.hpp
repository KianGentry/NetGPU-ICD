#pragma once

#include <unistd.h>

namespace netgpu {

class SocketDescriptor {
public:
    SocketDescriptor() = default;
    explicit SocketDescriptor(int descriptor) : descriptor_(descriptor) {}

    ~SocketDescriptor() {
        close();
    }

    SocketDescriptor(const SocketDescriptor&) = delete;
    SocketDescriptor& operator=(const SocketDescriptor&) = delete;

    SocketDescriptor(SocketDescriptor&& other) noexcept
        : descriptor_(other.release()) {}

    SocketDescriptor& operator=(SocketDescriptor&& other) noexcept {
        if (this != &other) {
            close();
            descriptor_ = other.release();
        }
        return *this;
    }

    bool isValid() const {
        return descriptor_ >= 0;
    }

    int get() const {
        return descriptor_;
    }

    int release() {
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

private:
    void close() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
    }

    int descriptor_ = -1;
};

} // namespace netgpu

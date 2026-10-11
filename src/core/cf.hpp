#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <utility>

namespace watcher {

// Owning handle for a CoreFoundation-style object (CF, CG, CT types).
template <typename T>
class CFRef {
public:
    CFRef() = default;
    explicit CFRef(T ref) : ref_(ref) {}  // adopts a +1 reference
    CFRef(const CFRef& other) : ref_(other.ref_) {
        if (ref_) CFRetain(ref_);
    }
    CFRef(CFRef&& other) noexcept : ref_(std::exchange(other.ref_, nullptr)) {}
    CFRef& operator=(CFRef other) noexcept {
        std::swap(ref_, other.ref_);
        return *this;
    }
    ~CFRef() {
        if (ref_) CFRelease(ref_);
    }

    static CFRef retain(T ref) {
        if (ref) CFRetain(ref);
        return CFRef(ref);
    }

    T get() const { return ref_; }
    explicit operator bool() const { return ref_ != nullptr; }

private:
    T ref_ = nullptr;
};

}  // namespace watcher

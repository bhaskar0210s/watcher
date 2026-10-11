#pragma once

#include <array>
#include <cstddef>

namespace watcher {

// Fixed-size ring of the most recent samples.
template <size_t N>
class History {
public:
    static constexpr size_t capacity = N;

    void push(float value) {
        data_[head_] = value;
        head_ = (head_ + 1) % N;
        if (size_ < N) ++size_;
    }

    size_t size() const { return size_; }

    // Index 0 is the oldest retained sample, size() - 1 the newest.
    float operator[](size_t i) const { return data_[(head_ + N - size_ + i) % N]; }

private:
    std::array<float, N> data_{};
    size_t head_ = 0;
    size_t size_ = 0;
};

// Three minutes at one sample per second.
using UsageHistory = History<180>;

}  // namespace watcher

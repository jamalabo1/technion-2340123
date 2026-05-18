//
// Created by jamal on 19/05/2026.
//

#ifndef KINDASTACK_H
#define KINDASTACK_H

#include <array>
#include <cstddef>
#include <stdexcept>

template <typename T>
class KindaStack {
    std::array<T, 2> data_;
    std::size_t size_ = 0;
    std::size_t next_ = 0; // index to overwrite next

public:
    void push(const T& x) {
        data_[next_] = x;
        next_ = (next_ + 1) % 2;
        if (size_ < 2) ++size_;
    }

    std::size_t size() const {
        return size_;
    }


    // newest element
    const T& top() const {
        if (size_ == 0) throw std::runtime_error("no top");
        return data_[(next_ + 1) % 2];
    }

    // older element
    const T& previous() const {
        if (size_ == 0) throw std::runtime_error("no previous");
        return data_[next_];
    }
};

#endif //KINDASTACK_H

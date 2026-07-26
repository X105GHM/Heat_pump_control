#pragma once
#include <cstdint>
#include <cstddef>

static constexpr size_t WF_SAMPLES = 50;

namespace core
{
    template <typename T, size_t N>
    struct FixedArray
    {
        T data[N]{};
        size_t size{0};

        bool pushBack(const T value) noexcept
        {
            if (size >= N) {
                return false;
            }
            data[size++] = value;
            return true;
        }

        void clear() noexcept { size = 0; }
    };
}

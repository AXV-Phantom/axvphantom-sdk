#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MADV_DONTDUMP
#define MADV_DONTDUMP 16
#endif

namespace axvp::internal::detail {

inline constexpr std::size_t align_up(std::size_t value,
                                      std::size_t alignment) noexcept {
    if (alignment == 0U) {
        return value;
    }

    const std::size_t remainder = value % alignment;
    return remainder == 0U ? value : value + (alignment - remainder);
}

inline constexpr std::size_t bit_ceil_or_one(std::size_t value) noexcept {
    return value <= 1U ? 1U : std::bit_ceil(value);
}

inline void secure_zero(void *ptr, std::size_t bytes) noexcept {
    if (ptr == nullptr || bytes == 0U) {
        return;
    }

    auto *volatile data = static_cast<volatile std::byte *>(ptr);
    for (std::size_t index = 0U; index < bytes; ++index) {
        data[index] = std::byte{0};
    }

    std::atomic_signal_fence(std::memory_order_seq_cst);
}

inline std::size_t page_size() noexcept {
    const long size = ::sysconf(_SC_PAGESIZE);
    return size > 0 ? static_cast<std::size_t>(size) : 4096U;
}

} // namespace axvp::internal::detail

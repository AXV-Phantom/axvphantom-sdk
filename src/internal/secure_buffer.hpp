#pragma once

#include "error.hpp"
#include "support.hpp"

#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <utility>

namespace axvp::internal {

class SecureBuffer final {
  public:
    SecureBuffer() noexcept = default;

    ~SecureBuffer() noexcept { reset(); }

    SecureBuffer(const SecureBuffer &) = delete;
    SecureBuffer &operator=(const SecureBuffer &) = delete;

    SecureBuffer(SecureBuffer &&other) noexcept { move_from(std::move(other)); }

    SecureBuffer &operator=(SecureBuffer &&other) noexcept {
        if (this != &other) {
            reset();
            move_from(std::move(other));
        }

        return *this;
    }

    [[nodiscard]] static std::expected<SecureBuffer, Error>
    create(std::size_t size) noexcept {
        if (size == 0U) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        const std::size_t page = detail::page_size();
        const std::size_t mapped_size = detail::align_up(size, page);

        void *memory = ::mmap(nullptr, mapped_size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED) {
            return std::unexpected(Error::ResourceAllocationFailed);
        }

        if (::mlock(memory, mapped_size) != 0) {
            (void)::munmap(memory, mapped_size);
            return std::unexpected(Error::ResourceLockFailed);
        }

        if (::madvise(memory, mapped_size, MADV_DONTDUMP) != 0) {
            (void)::munlock(memory, mapped_size);
            (void)::munmap(memory, mapped_size);
            return std::unexpected(Error::SecurityIntegrityViolation);
        }

        detail::secure_zero(memory, mapped_size);
        return SecureBuffer{memory, size, mapped_size};
    }

    [[nodiscard]] static std::expected<SecureBuffer, Error>
    copy_from(std::span<const std::byte> source) noexcept {
        if (source.empty()) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        auto buffer = create(source.size());
        if (!buffer.has_value()) {
            return std::unexpected(buffer.error());
        }

        std::memcpy(buffer->data_, source.data(), source.size());
        return buffer;
    }

    [[nodiscard]] std::byte *data() noexcept { return data_; }

    [[nodiscard]] const std::byte *data() const noexcept { return data_; }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] std::size_t mapped_size() const noexcept {
        return mapped_size_;
    }

    [[nodiscard]] std::span<std::byte> bytes() noexcept {
        return data_ == nullptr ? std::span<std::byte>{}
                                : std::span<std::byte>{data_, size_};
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return data_ == nullptr ? std::span<const std::byte>{}
                                : std::span<const std::byte>{data_, size_};
    }

    void clear() noexcept { detail::secure_zero(data_, mapped_size_); }

    [[nodiscard]] explicit operator bool() const noexcept {
        return data_ != nullptr;
    }

  private:
    SecureBuffer(void *data, std::size_t size, std::size_t mapped_size) noexcept
        : data_(static_cast<std::byte *>(data)), size_(size),
          mapped_size_(mapped_size), locked_(true) {}

    void reset() noexcept {
        if (data_ == nullptr) {
            return;
        }

        detail::secure_zero(data_, mapped_size_);
        if (locked_) {
            (void)::munlock(data_, mapped_size_);
        }
        (void)::munmap(data_, mapped_size_);

        data_ = nullptr;
        size_ = 0U;
        mapped_size_ = 0U;
        locked_ = false;
    }

    void move_from(SecureBuffer &&other) noexcept {
        data_ = other.data_;
        size_ = other.size_;
        mapped_size_ = other.mapped_size_;
        locked_ = other.locked_;

        other.data_ = nullptr;
        other.size_ = 0U;
        other.mapped_size_ = 0U;
        other.locked_ = false;
    }

    std::byte *data_ = nullptr;
    std::size_t size_ = 0U;
    std::size_t mapped_size_ = 0U;
    bool locked_ = false;
};

} // namespace axvp::internal

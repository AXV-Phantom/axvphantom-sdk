#pragma once

#include "support.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <vector>

namespace axvp::internal {

class FramePoolAllocator final : public std::pmr::memory_resource {
  public:
    FramePoolAllocator(std::size_t block_size, std::size_t block_count,
                       std::size_t alignment = alignof(std::max_align_t))
        : block_size_(block_size == 0U ? 1U : block_size),
          block_count_(block_count == 0U ? 1U : block_count),
          alignment_(detail::bit_ceil_or_one(
              std::max(alignment, alignof(std::max_align_t)))),
          stride_(detail::align_up(block_size_, alignment_)),
          storage_bytes_(0U) {
        if (block_count_ != 0U &&
            stride_ >
                (std::numeric_limits<std::size_t>::max() / block_count_)) {
            throw std::bad_alloc();
        }

        storage_bytes_ = stride_ * block_count_;
        storage_ = static_cast<std::byte *>(
            ::operator new(storage_bytes_, std::align_val_t{alignment_}));
        detail::secure_zero(storage_, storage_bytes_);

        free_list_.reserve(block_count_);
        for (std::size_t index = 0U; index < block_count_; ++index) {
            free_list_.push_back(storage_ + (index * stride_));
        }
    }

    ~FramePoolAllocator() override {
        if (storage_ != nullptr) {
            detail::secure_zero(storage_, storage_bytes_);
            ::operator delete(storage_, std::align_val_t{alignment_});
        }
    }

    FramePoolAllocator(const FramePoolAllocator &) = delete;
    FramePoolAllocator &operator=(const FramePoolAllocator &) = delete;
    FramePoolAllocator(FramePoolAllocator &&) = delete;
    FramePoolAllocator &operator=(FramePoolAllocator &&) = delete;

    [[nodiscard]] std::size_t block_size() const noexcept {
        return block_size_;
    }

    [[nodiscard]] std::size_t block_count() const noexcept {
        return block_count_;
    }

    [[nodiscard]] std::size_t stride() const noexcept { return stride_; }

    [[nodiscard]] std::size_t available() const noexcept {
        std::scoped_lock lock(mutex_);
        return free_list_.size();
    }

  protected:
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (bytes > block_size_ || alignment > alignment_) {
            throw std::bad_alloc();
        }

        std::scoped_lock lock(mutex_);
        if (free_list_.empty()) {
            throw std::bad_alloc();
        }

        void *const block = free_list_.back();
        free_list_.pop_back();
        return block;
    }

    void do_deallocate(void *pointer, std::size_t /*bytes*/,
                       std::size_t /*alignment*/) override {
        if (pointer == nullptr || !contains(pointer)) {
            return;
        }

        detail::secure_zero(pointer, stride_);

        std::scoped_lock lock(mutex_);
        const auto *candidate = static_cast<const std::byte *>(pointer);
        if (std::find(free_list_.begin(), free_list_.end(), candidate) ==
            free_list_.end()) {
            free_list_.push_back(const_cast<std::byte *>(candidate));
        }
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource &other) const noexcept override {
        return this == &other;
    }

  private:
    [[nodiscard]] bool contains(const void *pointer) const noexcept {
        if (storage_ == nullptr) {
            return false;
        }

        const auto *data = static_cast<const std::byte *>(pointer);
        return data >= storage_ && data < (storage_ + storage_bytes_) &&
               ((static_cast<std::size_t>(data - storage_) % stride_) == 0U);
    }

    std::size_t block_size_ = 0U;
    std::size_t block_count_ = 0U;
    std::size_t alignment_ = 0U;
    std::size_t stride_ = 0U;
    std::size_t storage_bytes_ = 0U;
    std::byte *storage_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<std::byte *> free_list_;
};

} // namespace axvp::internal

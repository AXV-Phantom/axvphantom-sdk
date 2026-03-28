#include "internal/internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory_resource>
#include <opencv2/core.hpp>
#include <thread>

namespace {

using axvp::internal::Error;
using axvp::internal::FramePoolAllocator;
using axvp::internal::ScopedTimer;
using axvp::internal::SecureBuffer;
using axvp::internal::UniqueFrame;

class TickListener {
  public:
    virtual ~TickListener() = default;
    virtual void on_tick(std::uint32_t value) = 0;
};

class MockTickListener : public TickListener {
  public:
    MOCK_METHOD(void, on_tick, (std::uint32_t value), (override));
};

void emit_tick(TickListener &listener, std::uint32_t value) {
    listener.on_tick(value);
}

TEST(ErrorMessages, KnownCodesHaveDescriptions) {
    const std::array<Error, 12> errors{
        Error::Ok,
        Error::ConfigMissingValue,
        Error::ConfigInvalidValue,
        Error::ConfigUnsupportedValue,
        Error::ResourceAllocationFailed,
        Error::ResourceExhausted,
        Error::ResourceLockFailed,
        Error::ResourceUnlockFailed,
        Error::PipelineNotInitialized,
        Error::PipelineStageFailed,
        Error::SecurityWipeFailed,
        Error::SecurityIntegrityViolation,
    };

    for (const Error error : errors) {
        EXPECT_FALSE(axvp::internal::error_message(error).empty());
    }

    EXPECT_EQ(axvp::internal::error_message(Error::Ok), "ok");
}

TEST(SecureBuffer, CopyClearAndMovePreserveContract) {
    const std::array<std::byte, 4> seed{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0x44},
    };

    auto created = SecureBuffer::copy_from(seed);
    ASSERT_TRUE(created.has_value());

    SecureBuffer buffer = std::move(created.value());
    EXPECT_TRUE(buffer);
    EXPECT_EQ(buffer.size(), seed.size());
    EXPECT_EQ(buffer.bytes().size(), seed.size());

    for (std::size_t index = 0U; index < seed.size(); ++index) {
        EXPECT_EQ(buffer.bytes()[index], seed[index]);
    }

    buffer.clear();
    for (const std::byte value : buffer.bytes()) {
        EXPECT_EQ(value, std::byte{0});
    }

    SecureBuffer moved = std::move(buffer);
    EXPECT_FALSE(buffer);
    EXPECT_TRUE(moved);
    EXPECT_EQ(moved.size(), seed.size());
}

TEST(ScopedTimer, AccumulatesMicroseconds) {
    std::atomic<std::uint32_t> elapsed_us{0U};
    {
        ScopedTimer timer(elapsed_us);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_GT(elapsed_us.load(std::memory_order_relaxed), 0U);
}

TEST(FramePoolAllocator, RecyclesBlocksAndRejectsExhaustion) {
    FramePoolAllocator allocator{32U, 2U};
    std::pmr::polymorphic_allocator<std::byte> memory{&allocator};

    std::byte *first = memory.allocate(16U);
    std::byte *second = memory.allocate(16U);
    EXPECT_EQ(allocator.available(), 0U);

    bool exhausted = false;
    try {
        const auto *unused = memory.allocate(1U);
        (void)unused;
    } catch (const std::bad_alloc &) {
        exhausted = true;
    }
    EXPECT_TRUE(exhausted);

    std::fill_n(first, 16U, std::byte{0x7F});
    memory.deallocate(first, 16U);
    EXPECT_EQ(allocator.available(), 1U);

    std::byte *recycled = memory.allocate(8U);
    EXPECT_EQ(recycled, first);
    for (std::size_t index = 0U; index < 16U; ++index) {
        EXPECT_EQ(recycled[index], std::byte{0});
    }

    memory.deallocate(second, 16U);
    memory.deallocate(recycled, 8U);
    EXPECT_EQ(allocator.available(), 2U);
}

TEST(UniqueFrame, ClonesInputAndWipesOnDemand) {
    cv::Mat source(2, 3, CV_8UC1);
    source.setTo(cv::Scalar(5));

    UniqueFrame frame(source);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame.byte_size(), source.total() * source.elemSize());

    source.setTo(cv::Scalar(9));
    EXPECT_EQ(frame.mat().at<std::uint8_t>(0, 0), 5U);

    frame.wipe();
    for (const std::byte value : frame.bytes()) {
        EXPECT_EQ(value, std::byte{0});
    }

    UniqueFrame moved = std::move(frame);
    EXPECT_FALSE(frame.has_value());
    EXPECT_TRUE(moved.has_value());
    EXPECT_EQ(moved.byte_size(), 6U);
}

TEST(GMockIntegration, MockCallIsObserved) {
    testing::StrictMock<MockTickListener> listener;
    EXPECT_CALL(listener, on_tick(42U));
    emit_tick(listener, 42U);
}

} // namespace

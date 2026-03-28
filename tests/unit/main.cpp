#include "internal/internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory_resource>
#include <opencv2/core.hpp>
#include <thread>
#include <type_traits>
#include <vector>

#include "axvphantom/axvphantom.hpp"

namespace {

using axvp::internal::Error;
using axvp::internal::FramePoolAllocator;
using axvp::internal::ScopedTimer;
using axvp::internal::SecureBuffer;
using axvp::internal::UniqueFrame;

static_assert(!std::is_copy_constructible_v<SecureBuffer>);
static_assert(!std::is_copy_assignable_v<SecureBuffer>);
static_assert(!std::is_copy_constructible_v<UniqueFrame>);
static_assert(!std::is_copy_assignable_v<UniqueFrame>);
static_assert(!std::is_copy_constructible_v<axvp::Context>);
static_assert(!std::is_copy_assignable_v<axvp::Context>);
static_assert(std::is_move_constructible_v<axvp::Context>);
static_assert(!std::is_copy_constructible_v<axvp::Result>);
static_assert(!std::is_copy_assignable_v<axvp::Result>);
static_assert(std::is_move_constructible_v<axvp::Result>);
static_assert(std::is_copy_constructible_v<axvp::Frame>);
static_assert(std::is_copy_assignable_v<axvp::Frame>);
static_assert(std::is_copy_constructible_v<axvp::MetadataView>);
static_assert(std::is_copy_assignable_v<axvp::MetadataView>);
static_assert(std::is_move_constructible_v<axvp::MetadataView>);

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
    const std::array<Error, 16> errors{
        Error::Ok,
        Error::ConfigError,
        Error::ConfigMissingValue,
        Error::ConfigInvalidValue,
        Error::ConfigUnsupportedValue,
        Error::ResourceError,
        Error::ResourceAllocationFailed,
        Error::ResourceExhausted,
        Error::ResourceLockFailed,
        Error::ResourceUnlockFailed,
        Error::PipelineError,
        Error::PipelineNotInitialized,
        Error::PipelineStageFailed,
        Error::SecurityError,
        Error::SecurityWipeFailed,
        Error::SecurityIntegrityViolation,
    };

    for (const Error error : errors) {
        EXPECT_FALSE(axvp::internal::error_message(error).empty());
    }

    EXPECT_EQ(axvp::internal::error_message(Error::Ok), "ok");
    EXPECT_EQ(axvp::internal::error_message(Error::ConfigError),
              "config error");
    EXPECT_EQ(axvp::internal::error_message(Error::ResourceError),
              "resource error");
    EXPECT_EQ(axvp::internal::error_message(Error::PipelineError),
              "pipeline error");
    EXPECT_EQ(axvp::internal::error_message(Error::SecurityError),
              "security error");
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

TEST(CppWrapper, ThinLifecycleAndRoundtripWork) {
    axvp_config_t config{};
    config.size = static_cast<std::uint32_t>(sizeof(config));
    config.width = 1U;
    config.height = 1U;
    config.format = AXVP_FMT_BGR;
    config.policy = AXVP_POLICY_NONE;
    config.device_index = 0U;
    config.rppg_window_frames = 4U;
    config.model_dir = ".";

    auto context = axvp::Context::create(config);
    ASSERT_TRUE(context.has_value());
    EXPECT_TRUE(context->valid());
    EXPECT_NE(context->native(), nullptr);

    EXPECT_EQ(context->set_policy(AXVP_POLICY_BLOCK_ON_FAIL |
                                  AXVP_POLICY_BLUR_FALLBACK),
              AXVP_STATUS_OK);
    EXPECT_EQ(context->rotate_keys(), AXVP_STATUS_OK);

    const std::array<std::byte, 4> payload{
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
    };

    axvp::Frame frame(payload.data(), 1U, 1U, 4U, AXVP_FMT_BGR, 99U);
    EXPECT_EQ(frame.native()->size, sizeof(axvp_frame_t));
    EXPECT_EQ(frame.native()->data, payload.data());
    EXPECT_EQ(frame.native()->width, 1U);
    EXPECT_EQ(frame.native()->height, 1U);
    EXPECT_EQ(frame.native()->stride, 4U);
    EXPECT_EQ(frame.native()->format, AXVP_FMT_BGR);

    auto processed = context->process(frame);
    ASSERT_TRUE(processed.has_value());
    EXPECT_NE(processed->context(), nullptr);
    EXPECT_EQ(processed->context(), context->native());
    EXPECT_EQ(processed->native()->size, sizeof(axvp_result_t));
    EXPECT_EQ(processed->native()->status, AXVP_STATUS_OK);
    EXPECT_EQ(processed->native()->frame.data, frame.native()->data);
    EXPECT_EQ(processed->native()->frame.width, frame.native()->width);
    EXPECT_EQ(processed->native()->frame.height, frame.native()->height);
    EXPECT_EQ(processed->native()->frame.format, frame.native()->format);
    EXPECT_EQ(processed->native()->metadata, nullptr);
    EXPECT_EQ(processed->native()->metadata_size, 0U);
    EXPECT_EQ(processed->native()->faces_detected, 0U);
    EXPECT_EQ(processed->native()->faces_anonymized, 0U);
    EXPECT_EQ(processed->native()->anonymization_complete, 0U);
}

TEST(MetadataView, ReadsFlatBufferWithoutCopying) {
    flatbuffers::FlatBufferBuilder builder;

    std::array<std::uint8_t, 16> face_id{
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU,
    };
    const flatbuffers::span<const std::uint8_t, 16> face_id_span(
        face_id.data(), face_id.size());
    const axvp::fb::Rect bbox{10.5f, 20.5f, 30.5f, 40.5f};

    std::vector<axvp::fb::FaceRecord> faces;
    faces.emplace_back(face_id_span, bbox, 0.875f, 72U,
                       axvp::fb::LivenessVerdict_LIVE, true);

    const auto version = builder.CreateString("2.3.0");
    const auto faces_offset = builder.CreateVectorOfStructs(faces);
    const auto root = axvp::fb::CreateFrameMetadata(
        builder, 1234U, 987654321ULL, version, faces_offset, 1U, 1U, true,
        321U);
    axvp::fb::FinishFrameMetadataBuffer(builder, root);

    auto detached = builder.Release();
    axvp_result_t result{};
    result.size = sizeof(result);
    result.metadata = detached.data();
    result.metadata_size = detached.size();

    auto view = axvp::MetadataView::create(result);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->valid());
    EXPECT_EQ(view->frame_id(), 1234U);
    EXPECT_EQ(view->timestamp_ns(), 987654321ULL);
    EXPECT_EQ(view->pipeline_version(), "2.3.0");
    EXPECT_EQ(view->faces_detected(), 1U);
    EXPECT_EQ(view->faces_anonymized(), 1U);
    EXPECT_TRUE(view->anonymization_complete());
    EXPECT_EQ(view->processing_latency_us(), 321U);
    EXPECT_EQ(view->bytes().size(), detached.size());

    const auto faces_view = view->faces();
    ASSERT_EQ(faces_view.size(), 1U);
    EXPECT_EQ(faces_view[0].bbox().x(), 10.5f);
    EXPECT_EQ(faces_view[0].bbox().y(), 20.5f);
    EXPECT_EQ(faces_view[0].bbox().width(), 30.5f);
    EXPECT_EQ(faces_view[0].bbox().height(), 40.5f);
    EXPECT_EQ(faces_view[0].liveness_score(), 0.875f);
    EXPECT_EQ(faces_view[0].pulse_bpm(), 72U);
    EXPECT_EQ(faces_view[0].verdict(), axvp::fb::LivenessVerdict_LIVE);
    EXPECT_TRUE(faces_view[0].pixels_wiped());
    EXPECT_EQ(faces_view[0].face_id()->Get(0), face_id[0]);

    std::vector<std::byte> invalid_bytes(detached.size());
    std::memcpy(invalid_bytes.data(), detached.data(), detached.size());
    invalid_bytes[4] ^= std::byte{0x01};

    auto invalid_view = axvp::MetadataView::create(
        std::span<const std::byte>(invalid_bytes.data(), invalid_bytes.size()));
    ASSERT_FALSE(invalid_view.has_value());
    EXPECT_EQ(invalid_view.error(), AXVP_STATUS_INVALID_ARGUMENT);
}

} // namespace

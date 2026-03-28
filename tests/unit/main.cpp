#include "internal/internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <memory_resource>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include "axvphantom/axvphantom.hpp"

namespace {

using axvp::internal::DetectionResult;
using axvp::internal::DetectorModel;
using axvp::internal::Error;
using axvp::internal::AnonymizationResult;
using axvp::internal::ComposerStage;
using axvp::internal::FramePoolAllocator;
using axvp::internal::LivenessResult;
using axvp::internal::LivenessStage;
using axvp::internal::ScopedTimer;
using axvp::internal::SecureBuffer;
using axvp::internal::UniqueFrame;

static_assert(!std::is_copy_constructible_v<SecureBuffer>);
static_assert(!std::is_copy_assignable_v<SecureBuffer>);
static_assert(!std::is_copy_constructible_v<UniqueFrame>);
static_assert(!std::is_copy_assignable_v<UniqueFrame>);
static_assert(!std::is_copy_constructible_v<DetectorModel>);
static_assert(!std::is_copy_assignable_v<DetectorModel>);
static_assert(DetectionResult::max_faces() == axvp::internal::AXVP_MAX_FACES);
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

#ifndef AXVP_TEST_DATA_DIR
#define AXVP_TEST_DATA_DIR "."
#endif

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

[[nodiscard]] std::filesystem::path test_data_path(std::string_view relative) {
    return std::filesystem::path{AXVP_TEST_DATA_DIR} / relative;
}

[[nodiscard]] cv::Mat load_test_image(std::string_view relative) {
    return cv::imread(test_data_path(relative).string(), cv::IMREAD_COLOR);
}

[[nodiscard]] axvp_config_t make_test_config() {
    axvp_config_t config{};
    config.size = static_cast<std::uint32_t>(sizeof(config));
    config.width = 1U;
    config.height = 1U;
    config.format = AXVP_FMT_BGR;
    config.policy = AXVP_POLICY_NONE;
    config.device_index = 0U;
    config.rppg_window_frames = 4U;
    config.model_dir = AXVP_TEST_DATA_DIR;
    return config;
}

[[nodiscard]] std::shared_ptr<const DetectorModel> make_detector_model() {
    auto config = make_test_config();
    auto model = DetectorModel::create(config);
    EXPECT_TRUE(model.has_value());
    if (!model.has_value()) {
        return nullptr;
    }

    return std::make_shared<DetectorModel>(std::move(model).value());
}

[[nodiscard]] DetectionResult make_face_detection_result() {
    DetectionResult result;
    auto face = result.add_face({16.0f, 16.0f, 32.0f, 32.0f}, 0.99f);
    EXPECT_TRUE(face.has_value());
    if (face.has_value()) {
        EXPECT_TRUE(result.add_landmark(0U, 24.0f, 28.0f, 0.0f).has_value());
        EXPECT_TRUE(result.add_landmark(0U, 40.0f, 28.0f, 0.0f).has_value());
        EXPECT_TRUE(result.add_landmark(0U, 32.0f, 36.0f, 0.0f).has_value());
        EXPECT_TRUE(result.add_landmark(0U, 26.0f, 44.0f, 0.0f).has_value());
        EXPECT_TRUE(result.add_landmark(0U, 38.0f, 44.0f, 0.0f).has_value());
    }

    return result;
}

[[nodiscard]] cv::Mat make_synthetic_rppg_frame(float modulation) {
    cv::Mat frame(64, 64, CV_8UC3);
    frame.setTo(cv::Scalar(18, 18, 18));
    const cv::Rect roi{16, 16, 32, 32};
    frame(roi).setTo(cv::Scalar(52, 100 + modulation, 52));
    return frame;
}

class FailingLandmarkBackend final : public axvp::internal::LandmarkBackend {
  public:
    bool fit(const cv::Mat &, const std::vector<cv::Rect> &,
             std::vector<std::vector<cv::Point2f>> &) override {
        return false;
    }
};

TEST(ErrorMessages, KnownCodesHaveDescriptions) {
    const std::array<Error, 19> errors{
        Error::Ok,
        Error::ConfigError,
        Error::ConfigMissingValue,
        Error::ConfigInvalidValue,
        Error::ConfigUnsupportedValue,
        Error::ResourceError,
        Error::ResourceNotFound,
        Error::ResourceAllocationFailed,
        Error::ResourceExhausted,
        Error::ResourceLockFailed,
        Error::ResourceUnlockFailed,
        Error::PipelineError,
        Error::PipelineNotInitialized,
        Error::PipelineStageFailed,
        Error::PipelineDetectionIncomplete,
        Error::SecurityError,
        Error::SecurityModelTampered,
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
    EXPECT_EQ(axvp::internal::error_message(Error::ResourceNotFound),
              "resource: not found");
    EXPECT_EQ(axvp::internal::error_message(Error::PipelineError),
              "pipeline error");
    EXPECT_EQ(axvp::internal::error_message(Error::PipelineDetectionIncomplete),
              "pipeline: detection incomplete");
    EXPECT_EQ(axvp::internal::error_message(Error::SecurityError),
              "security error");
    EXPECT_EQ(axvp::internal::error_message(Error::SecurityModelTampered),
              "security: model tampered");
}

TEST(DetectorModel, LoadsDefaultYuNetModelFromDataDir) {
    auto config = make_test_config();

    auto model = DetectorModel::create(config);
    ASSERT_TRUE(model.has_value());
    EXPECT_NE(model->detector(), nullptr);
    EXPECT_EQ(model->model_size(), axvp::internal::kDefaultDetectorModelSize);
    EXPECT_EQ(model->checksum(), axvp::internal::kDefaultDetectorModelSha256);
    EXPECT_FALSE(model->model_path().empty());
    EXPECT_EQ(model->detector()->getInputSize(), cv::Size(320, 320));
}

TEST(DetectorModel, RejectsMissingModelPath) {
    auto config = make_test_config();
    std::string expected_sha256(axvp::internal::kDefaultDetectorModelSha256);
    config.detector_model_path = "models/face_detection_yunet/missing.onnx";
    config.detector_model_size = axvp::internal::kDefaultDetectorModelSize;
    config.detector_model_sha256 = expected_sha256.c_str();

    auto model = DetectorModel::create(config);
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error(), Error::ResourceNotFound);
}

TEST(DetectorModel, RejectsTamperedModelFile) {
    const std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "axvphantom-detector-model";
    std::filesystem::create_directories(temp_dir);

    const std::filesystem::path source =
        std::filesystem::path{AXVP_TEST_DATA_DIR} /
        axvp::internal::kDefaultDetectorModelRelativePath;
    const std::filesystem::path copy =
        temp_dir / "face_detection_yunet_2023mar.onnx";

    std::filesystem::copy_file(
        source, copy, std::filesystem::copy_options::overwrite_existing);

    std::fstream file(copy, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    char byte = 0;
    file.read(&byte, 1);
    ASSERT_EQ(file.gcount(), 1);
    byte ^= 0x01;
    file.seekp(0);
    file.write(&byte, 1);
    file.flush();

    std::string copy_path = copy.string();
    std::string expected_sha256(axvp::internal::kDefaultDetectorModelSha256);

    auto config = make_test_config();
    config.detector_model_path = copy_path.c_str();
    config.detector_model_size = axvp::internal::kDefaultDetectorModelSize;
    config.detector_model_sha256 = expected_sha256.c_str();

    auto model = DetectorModel::create(config);
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error(), Error::SecurityModelTampered);

    std::filesystem::remove_all(temp_dir);
}

TEST(DetectionResult, StoresFacesInSoAAndTracksLandmarks) {
    DetectionResult result;
    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(result.full());
    EXPECT_EQ(result.size(), 0U);

    auto face0 = result.add_face({10.0f, 20.0f, 30.0f, 40.0f}, 0.95f);
    ASSERT_TRUE(face0.has_value());
    EXPECT_EQ((*face0)->index, 0U);
    EXPECT_FLOAT_EQ((*face0)->bbox[0], 10.0f);
    EXPECT_FLOAT_EQ((*face0)->bbox[1], 20.0f);
    EXPECT_FLOAT_EQ((*face0)->bbox[2], 30.0f);
    EXPECT_FLOAT_EQ((*face0)->bbox[3], 40.0f);
    EXPECT_EQ(result.size(), 1U);
    EXPECT_FLOAT_EQ(result.confidence(0U), 0.95f);
    EXPECT_EQ(result.bbox(0U)[0], 10.0f);
    EXPECT_EQ(result.bbox(0U)[1], 20.0f);
    EXPECT_EQ(result.bbox(0U)[2], 30.0f);
    EXPECT_EQ(result.bbox(0U)[3], 40.0f);

    ASSERT_TRUE(result.add_landmark(0U, 1.0f, 2.0f, 3.0f).has_value());
    ASSERT_TRUE(result.add_landmark(0U, 4.0f, 5.0f, 6.0f).has_value());
    EXPECT_EQ(result.landmark_count(0U), 2U);
    EXPECT_EQ(result.landmark_x_for(0U).size(), 2U);
    EXPECT_EQ(result.landmark_y_for(0U).size(), 2U);
    EXPECT_EQ(result.landmark_z_for(0U).size(), 2U);
    EXPECT_FLOAT_EQ(result.landmark_x_for(0U)[0], 1.0f);
    EXPECT_FLOAT_EQ(result.landmark_y_for(0U)[0], 2.0f);
    EXPECT_FLOAT_EQ(result.landmark_z_for(0U)[0], 3.0f);
    EXPECT_FLOAT_EQ(result.landmark_x_for(0U)[1], 4.0f);
    EXPECT_FLOAT_EQ(result.landmark_y_for(0U)[1], 5.0f);
    EXPECT_FLOAT_EQ(result.landmark_z_for(0U)[1], 6.0f);

    auto face1 = result.add_face({50.0f, 60.0f, 70.0f, 80.0f}, 0.75f);
    ASSERT_TRUE(face1.has_value());
    EXPECT_EQ((*face1)->index, 1U);
    EXPECT_EQ(result.size(), 2U);
    EXPECT_EQ(result.face_rois().size(), 2U);
    EXPECT_TRUE(result.landmark_x_for(1U).empty());
    EXPECT_TRUE(result.add_landmark(1U, 7.0f, 8.0f, 9.0f).has_value());
    EXPECT_EQ(result.landmark_count(1U), 1U);
    EXPECT_FLOAT_EQ(result.landmark_x_for(1U)[0], 7.0f);
}

TEST(DetectionResult, RejectsOverflowAndInvalidLandmarkIndex) {
    DetectionResult result;

    for (std::size_t index = 0U; index < axvp::internal::AXVP_MAX_FACES;
         ++index) {
        auto face = result.add_face(
            {static_cast<float>(index), 0.0f, 1.0f, 1.0f}, 0.5f);
        ASSERT_TRUE(face.has_value());
        EXPECT_EQ((*face)->index, index);
    }

    EXPECT_TRUE(result.full());
    auto overflow = result.add_face({1.0f, 1.0f, 1.0f, 1.0f}, 0.1f);
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error(), Error::ResourceExhausted);

    DetectionResult empty;
    auto invalid = empty.add_landmark(0U, 1.0f, 2.0f, 3.0f);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), Error::ConfigInvalidValue);
    EXPECT_TRUE(empty.landmark_x_for(0U).empty());
    EXPECT_TRUE(empty.face_rois().empty());
}

TEST(DetectionStage, DetectsSingleFaceAndLandmarks) {
    auto detector_model = make_detector_model();
    ASSERT_NE(detector_model, nullptr);

    auto stage =
        axvp::internal::DetectionStage::create(make_test_config(),
                                               std::move(detector_model));
    ASSERT_TRUE(stage.has_value())
        << axvp::internal::error_message(stage.error());

    cv::Mat image = load_test_image(
        "test-images/face_detection/opencv_extra/gray_face.png");
    ASSERT_FALSE(image.empty());

    UniqueFrame frame(image);
    auto result = stage->process(frame);
    if (!result.has_value()) {
        std::cerr << "DetectionStage single-face error: "
                  << axvp::internal::error_message(result.error()) << '\n';
    }
    ASSERT_TRUE(result.has_value())
        << axvp::internal::error_message(result.error());
    ASSERT_EQ(result->size(), 1U);
    ASSERT_GE(result->landmark_count(0U), 5U);
    EXPECT_GT(result->confidence(0U), 0.0f);
    EXPECT_GE(result->bbox(0U)[0], 0.0f);
    EXPECT_GE(result->bbox(0U)[1], 0.0f);
    EXPECT_GT(result->bbox(0U)[2], 0.0f);
    EXPECT_GT(result->bbox(0U)[3], 0.0f);

    for (std::size_t face_index = 0U; face_index < result->size();
         ++face_index) {
        EXPECT_GE(result->landmark_count(face_index), 5U);
        for (const float x : result->landmark_x_for(face_index)) {
            EXPECT_GE(x, 0.0f);
            EXPECT_LE(x, static_cast<float>(image.cols));
        }
        for (const float y : result->landmark_y_for(face_index)) {
            EXPECT_GE(y, 0.0f);
            EXPECT_LE(y, static_cast<float>(image.rows));
        }
    }
}

TEST(DetectionStage, DetectsMultipleFacesInBenchmarkPack) {
    auto detector_model = make_detector_model();
    ASSERT_NE(detector_model, nullptr);

    auto stage =
        axvp::internal::DetectionStage::create(make_test_config(),
                                               std::move(detector_model));
    ASSERT_TRUE(stage.has_value())
        << axvp::internal::error_message(stage.error());

    cv::Mat image = load_test_image(
        "test-images/face_detection/opencv_zoo/group.jpg");
    ASSERT_FALSE(image.empty());

    UniqueFrame frame(image);
    auto result = stage->process(frame);
    if (!result.has_value()) {
        std::cerr << "DetectionStage multi-face error: "
                  << axvp::internal::error_message(result.error()) << '\n';
    }
    ASSERT_TRUE(result.has_value())
        << axvp::internal::error_message(result.error());
    EXPECT_GE(result->size(), 2U);

    for (std::size_t face_index = 0U; face_index < result->size();
         ++face_index) {
        EXPECT_EQ(result->landmark_count(face_index), 68U);
        for (const float x : result->landmark_x_for(face_index)) {
            EXPECT_GE(x, 0.0f);
            EXPECT_LE(x, static_cast<float>(image.cols));
        }
        for (const float y : result->landmark_y_for(face_index)) {
            EXPECT_GE(y, 0.0f);
            EXPECT_LE(y, static_cast<float>(image.rows));
        }
    }
}

TEST(DetectionStage, SkipsFacesWhenLandmarksFailWithoutBlockPolicy) {
    auto detector_model = make_detector_model();
    ASSERT_NE(detector_model, nullptr);

    auto backend = std::make_shared<FailingLandmarkBackend>();
    axvp::internal::DetectionStage stage(std::move(detector_model), backend,
                                         AXVP_POLICY_NONE, {});

    cv::Mat image = load_test_image(
        "test-images/face_detection/opencv_extra/gray_face.png");
    ASSERT_FALSE(image.empty());

    UniqueFrame frame(image);
    auto result = stage.process(frame);
    if (!result.has_value()) {
        std::cerr << "DetectionStage fail-policy error: "
                  << axvp::internal::error_message(result.error()) << '\n';
    }
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(DetectionStage, BlocksOnLandmarkFailureWhenPolicyRequestsIt) {
    auto detector_model = make_detector_model();
    ASSERT_NE(detector_model, nullptr);

    auto backend = std::make_shared<FailingLandmarkBackend>();
    axvp::internal::DetectionStage stage(
        std::move(detector_model), backend, AXVP_POLICY_BLOCK_ON_FAIL, {});

    cv::Mat image = load_test_image(
        "test-images/face_detection/opencv_extra/gray_face.png");
    ASSERT_FALSE(image.empty());

    UniqueFrame frame(image);
    auto result = stage.process(frame);
    if (!result.has_value()) {
        std::cerr << "DetectionStage block-policy error: "
                  << axvp::internal::error_message(result.error()) << '\n';
    }
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::PipelineDetectionIncomplete);
}

TEST(DetectionStage, ReturnsEmptyForNegativeImage) {
    auto detector_model = make_detector_model();
    ASSERT_NE(detector_model, nullptr);

    auto stage =
        axvp::internal::DetectionStage::create(make_test_config(),
                                               std::move(detector_model));
    ASSERT_TRUE(stage.has_value());

    cv::Mat image = load_test_image(
        "test-images/face_detection/opencv_extra/dog416.png");
    ASSERT_FALSE(image.empty());

    UniqueFrame frame(image);
    auto result = stage->process(frame);
    if (!result.has_value()) {
        std::cerr << "DetectionStage negative-image error: "
                  << axvp::internal::error_message(result.error()) << '\n';
    }
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
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
    config = make_test_config();

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
    EXPECT_NE(processed->native()->frame.data, frame.native()->data);
    EXPECT_EQ(processed->native()->frame.width, frame.native()->width);
    EXPECT_EQ(processed->native()->frame.height, frame.native()->height);
    EXPECT_EQ(processed->native()->frame.format, frame.native()->format);
    EXPECT_NE(processed->native()->metadata, nullptr);
    EXPECT_GT(processed->native()->metadata_size, 0U);
    EXPECT_EQ(processed->native()->faces_detected, 0U);
    EXPECT_EQ(processed->native()->faces_anonymized, 0U);
    EXPECT_EQ(processed->native()->anonymization_complete, 1U);

    auto view = axvp::MetadataView::create(*processed->native());
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->faces_detected(), 0U);
    EXPECT_EQ(view->faces_anonymized(), 0U);
    EXPECT_TRUE(view->anonymization_complete());
}

TEST(CppWrapper, ProcessesFaceImageAndAnonymizesOutput) {
    axvp_config_t config{};
    config = make_test_config();

    auto context = axvp::Context::create(config);
    ASSERT_TRUE(context.has_value());

    cv::Mat input = load_test_image(
        "test-images/face_detection/opencv_extra/gray_face.png");
    ASSERT_FALSE(input.empty());

    axvp::Frame frame(input, 42U);
    auto processed = context->process(frame);
    ASSERT_TRUE(processed.has_value());
    EXPECT_NE(processed->native()->frame.data, frame.native()->data);
    EXPECT_GT(processed->native()->faces_detected, 0U);
    EXPECT_EQ(processed->native()->faces_detected,
              processed->native()->faces_anonymized);
    EXPECT_EQ(processed->native()->anonymization_complete, 1U);
    EXPECT_NE(processed->native()->metadata, nullptr);
    EXPECT_GT(processed->native()->metadata_size, 0U);

    const cv::Mat anonymized(
        static_cast<int>(processed->native()->frame.height),
        static_cast<int>(processed->native()->frame.width), CV_8UC3,
        const_cast<void *>(processed->native()->frame.data),
        static_cast<std::size_t>(processed->native()->frame.stride));
    EXPECT_GT(cv::norm(input, anonymized, cv::NORM_L1), 0.0);

    auto view = axvp::MetadataView::create(*processed->native());
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->faces_detected(), processed->native()->faces_detected);
    EXPECT_EQ(view->faces_anonymized(), processed->native()->faces_anonymized);
    EXPECT_TRUE(view->anonymization_complete());
    EXPECT_EQ(view->faces().size(), processed->native()->faces_detected);
}

TEST(LivenessStage, ClassifiesSyntheticLiveAndSpoofSignals) {
    axvp_config_t config = make_test_config();
    config.rppg_window_frames = 16U;

    auto stage = LivenessStage::create(config);
    ASSERT_TRUE(stage.has_value())
        << axvp::internal::error_message(stage.error());

    DetectionResult detection = make_face_detection_result();
    ASSERT_EQ(detection.size(), 1U);

    const std::uint64_t frame_period_ns = 33'333'333ULL;
    LivenessResult live_result{};

    for (std::size_t frame_index = 0U; frame_index < 24U; ++frame_index) {
        const double phase = 2.0 * 3.14159265358979323846 * 1.4 *
                             (static_cast<double>(frame_index) / 30.0);
        const float modulation = 8.0f * static_cast<float>(std::sin(phase));
        cv::Mat frame = make_synthetic_rppg_frame(modulation);
        auto result =
            stage->process(frame, detection, frame_index * frame_period_ns);
        ASSERT_TRUE(result.has_value())
            << axvp::internal::error_message(result.error());
        live_result = *result;
    }

    const auto *live_face = live_result.find(0U);
    ASSERT_NE(live_face, nullptr);
    EXPECT_EQ(live_face->verdict, AXVP_LIVENESS_LIVE);
    EXPECT_GT(live_face->score, 0.0f);
    EXPECT_GT(live_face->pulse_bpm, 0U);

    stage->reset();

    LivenessResult spoof_result{};
    for (std::size_t frame_index = 0U; frame_index < 24U; ++frame_index) {
        cv::Mat frame = make_synthetic_rppg_frame(0.0f);
        auto result =
            stage->process(frame, detection, frame_index * frame_period_ns);
        ASSERT_TRUE(result.has_value())
            << axvp::internal::error_message(result.error());
        spoof_result = *result;
    }

    const auto *spoof_face = spoof_result.find(0U);
    ASSERT_NE(spoof_face, nullptr);
    EXPECT_EQ(spoof_face->verdict, AXVP_LIVENESS_SPOOF);
    EXPECT_EQ(spoof_face->pulse_bpm, 0U);
}

TEST(ComposerStage, BuildsMetadataAndRotatesFaceIds) {
    auto composer = ComposerStage::create();
    ASSERT_TRUE(composer.has_value())
        << axvp::internal::error_message(composer.error());

    DetectionResult detection = make_face_detection_result();
    ASSERT_EQ(detection.size(), 1U);

    AnonymizationResult anonymization;
    ASSERT_TRUE(anonymization.add_face(0U, detection.bbox(0U), true).has_value());

    LivenessResult liveness;
    ASSERT_TRUE(
        liveness.add_face(0U, 0.91f, 72U, AXVP_LIVENESS_LIVE).has_value());

    axvp_frame_t frame{};
    frame.size = sizeof(frame);
    frame.timestamp_ns = 987654321ULL;
    frame.width = 64U;
    frame.height = 64U;
    frame.stride = 192U;
    frame.format = AXVP_FMT_BGR;

    auto metadata1 = composer->compose(7U, frame, detection, anonymization,
                                       liveness, 321U);
    ASSERT_TRUE(metadata1.has_value())
        << axvp::internal::error_message(metadata1.error());

    axvp_result_t result1{};
    result1.size = sizeof(result1);
    result1.metadata = reinterpret_cast<const std::uint8_t *>(metadata1->data());
    result1.metadata_size = metadata1->size();
    auto view1 = axvp::MetadataView::create(result1);
    ASSERT_TRUE(view1.has_value());
    EXPECT_EQ(view1->pipeline_version(), AXVP_VERSION_STRING);
    EXPECT_EQ(view1->frame_id(), 7U);
    EXPECT_EQ(view1->timestamp_ns(), 987654321ULL);
    EXPECT_EQ(view1->faces_detected(), 1U);
    EXPECT_EQ(view1->faces_anonymized(), 1U);
    EXPECT_TRUE(view1->anonymization_complete());
    EXPECT_EQ(view1->processing_latency_us(), 321U);

    const auto faces1 = view1->faces();
    ASSERT_EQ(faces1.size(), 1U);
    EXPECT_TRUE(faces1[0].pixels_wiped());
    EXPECT_EQ(faces1[0].liveness_score(), 0.91f);
    EXPECT_EQ(faces1[0].pulse_bpm(), 72U);
    EXPECT_EQ(faces1[0].verdict(), axvp::fb::LivenessVerdict_LIVE);

    std::array<std::uint8_t, 16U> face_id_before{};
    for (std::size_t index = 0U; index < face_id_before.size(); ++index) {
        face_id_before[index] = faces1[0].face_id()->Get(index);
    }

    ASSERT_TRUE(composer->rotate_keys().has_value());

    auto metadata2 = composer->compose(8U, frame, detection, anonymization,
                                       liveness, 654U);
    ASSERT_TRUE(metadata2.has_value())
        << axvp::internal::error_message(metadata2.error());

    axvp_result_t result2{};
    result2.size = sizeof(result2);
    result2.metadata = reinterpret_cast<const std::uint8_t *>(metadata2->data());
    result2.metadata_size = metadata2->size();
    auto view2 = axvp::MetadataView::create(result2);
    ASSERT_TRUE(view2.has_value());

    const auto faces2 = view2->faces();
    ASSERT_EQ(faces2.size(), 1U);
    std::array<std::uint8_t, 16U> face_id_after{};
    for (std::size_t index = 0U; index < face_id_after.size(); ++index) {
        face_id_after[index] = faces2[0].face_id()->Get(index);
    }

    EXPECT_NE(face_id_before, face_id_after);
}

TEST(Stress, EightParallelContextsStayHealthy) {
    cv::Mat input = load_test_image(
        "test-images/face_detection/opencv_extra/gray_face.png");
    ASSERT_FALSE(input.empty());

    constexpr std::size_t kThreads = 8U;
    constexpr std::size_t kIterations = 16U;
    std::barrier start_gate(static_cast<std::ptrdiff_t>(kThreads + 1U));
    std::atomic<std::size_t> failures{0U};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (std::size_t thread_index = 0U; thread_index < kThreads;
         ++thread_index) {
        workers.emplace_back([&, thread_index]() {
            auto context = axvp::Context::create(make_test_config());
            if (!context.has_value()) {
                failures.fetch_add(1U, std::memory_order_relaxed);
                start_gate.arrive_and_wait();
                return;
            }

            start_gate.arrive_and_wait();
            for (std::size_t iteration = 0U; iteration < kIterations;
                 ++iteration) {
                axvp::Frame frame(
                    input, static_cast<std::uint64_t>(thread_index * 1'000U +
                                                       iteration));
                auto processed = context->process(frame);
                if (!processed.has_value() ||
                    processed->native()->status != AXVP_STATUS_OK ||
                    processed->native()->metadata == nullptr ||
                    processed->native()->metadata_size == 0U) {
                    failures.fetch_add(1U, std::memory_order_relaxed);
                    break;
                }

                auto view = axvp::MetadataView::create(*processed->native());
                if (!view.has_value() || !view->valid()) {
                    failures.fetch_add(1U, std::memory_order_relaxed);
                    break;
                }
            }
        });
    }

    start_gate.arrive_and_wait();
    for (auto &worker : workers) {
        worker.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0U);
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
    const auto root =
        axvp::fb::CreateFrameMetadata(builder, 1234U, 987654321ULL, version,
                                      faces_offset, 1U, 1U, true, 321U);
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

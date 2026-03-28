#include "axvphantom/axvphantom.h"

#include <stddef.h>
#include <stdint.h>

#ifndef AXVP_TEST_DATA_DIR
#define AXVP_TEST_DATA_DIR "."
#endif

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!(expr)) {                                                          \
            return 1;                                                           \
        }                                                                       \
    } while (0)

int main(void) {
    CHECK(offsetof(axvp_config_t, size) == 0U);
    CHECK(offsetof(axvp_frame_t, size) == 0U);
    CHECK(offsetof(axvp_result_t, size) == 0U);
    CHECK(offsetof(axvp_face_t, size) == 0U);

    CHECK(AXVP_STATUS_OK == 0);
    CHECK(AXVP_STATUS_INVALID_ARGUMENT == 1);
    CHECK(AXVP_STATUS_SECURITY_ERROR == 7);
    CHECK(AXVP_FMT_BGR == 1);
    CHECK(AXVP_POLICY_BLOCK_ON_FAIL == (1u << 0));
    CHECK(AXVP_POLICY_BLUR_FALLBACK == (1u << 1));

    axvp_config_t config = {0};
    config.size = (uint32_t)sizeof(config);
    config.width = 2U;
    config.height = 2U;
    config.format = AXVP_FMT_BGR;
    config.policy = AXVP_POLICY_NONE;
    config.device_index = 0U;
    config.rppg_window_frames = 4U;
    config.model_dir = AXVP_TEST_DATA_DIR;

    axvp_status_t status = AXVP_STATUS_INTERNAL_ERROR;
    axvp_context_t *ctx = axvp_create(&config, &status);
    CHECK(ctx != NULL);
    CHECK(status == AXVP_STATUS_OK);

    status = axvp_set_policy(ctx, AXVP_POLICY_BLOCK_ON_FAIL | AXVP_POLICY_BLUR_FALLBACK);
    CHECK(status == AXVP_STATUS_OK);

    status = axvp_rotate_keys(ctx);
    CHECK(status == AXVP_STATUS_OK);

    uint8_t pixels[4] = {1U, 2U, 3U, 4U};
    axvp_frame_t frame = {0};
    frame.size = (uint32_t)sizeof(frame);
    frame.data = pixels;
    frame.width = 1U;
    frame.height = 1U;
    frame.stride = 4U;
    frame.format = AXVP_FMT_BGR;
    frame.timestamp_ns = 1234U;

    axvp_result_t result = {0};
    result.size = (uint32_t)sizeof(result);

    status = axvp_process_frame(ctx, &frame, &result);
    CHECK(status == AXVP_STATUS_OK);
    CHECK(result.status == AXVP_STATUS_OK);
    CHECK(result.frame.data != frame.data);
    CHECK(result.frame.width == frame.width);
    CHECK(result.frame.height == frame.height);
    CHECK(result.frame.format == frame.format);
    CHECK(result.metadata != NULL);
    CHECK(result.metadata_size > 0U);
    CHECK(result.faces_detected == 0U);
    CHECK(result.faces_anonymized == 0U);
    CHECK(result.anonymization_complete == 1U);

    axvp_release_result(ctx, &result);
    CHECK(result.size == 0U);
    CHECK(result.metadata == NULL);

    axvp_destroy(ctx);
    return 0;
}

#ifndef AXVPHANTOM_AXVPHANTOM_H
#define AXVPHANTOM_AXVPHANTOM_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define AXVP_NOEXCEPT noexcept
extern "C" {
#else
#define AXVP_NOEXCEPT
#endif

typedef enum axvp_status_t {
    AXVP_STATUS_OK = 0,
    AXVP_STATUS_INVALID_ARGUMENT = 1,
    AXVP_STATUS_INVALID_CONFIG = 2,
    AXVP_STATUS_OUT_OF_MEMORY = 3,
    AXVP_STATUS_NOT_INITIALIZED = 4,
    AXVP_STATUS_UNSUPPORTED = 5,
    AXVP_STATUS_INTERNAL_ERROR = 6,
} axvp_status_t;

typedef enum axvp_policy_t {
    AXVP_POLICY_NONE = 0u,
    AXVP_POLICY_BLOCK_ON_FAIL = 1u << 0,
    AXVP_POLICY_BLUR_FALLBACK = 1u << 1,
} axvp_policy_t;

typedef enum axvp_pixel_format_t {
    AXVP_FMT_UNKNOWN = 0,
    AXVP_FMT_BGR = 1,
    AXVP_FMT_YUV420 = 2,
    AXVP_FMT_NV12 = 3,
} axvp_pixel_format_t;

typedef enum axvp_liveness_verdict_t {
    AXVP_LIVENESS_UNCERTAIN = 0,
    AXVP_LIVENESS_SPOOF = 1,
    AXVP_LIVENESS_LIVE = 2,
} axvp_liveness_verdict_t;

typedef struct axvp_config_t {
    uint32_t size;              /**< Size of this structure in bytes. */
    uint32_t width;             /**< Target frame width in pixels. */
    uint32_t height;            /**< Target frame height in pixels. */
    axvp_pixel_format_t format; /**< Input frame pixel format. */
    axvp_policy_t policy;       /**< Bitmask of processing policies. */
    uint32_t device_index; /**< Vulkan device index; implementation-defined. */
    uint32_t
        rppg_window_frames; /**< Number of frames in the liveness window. */
    const char *model_dir;  /**< UTF-8 path to the model directory, or NULL. */
    uint64_t reserved[8]; /**< Reserved for future ABI extensions; zero-fill. */
} axvp_config_t;

typedef struct axvp_frame_t {
    uint32_t size;    /**< Size of this structure in bytes. */
    const void *data; /**< Pointer to the first byte of the frame payload. */
    uint32_t width;   /**< Frame width in pixels. */
    uint32_t height;  /**< Frame height in pixels. */
    uint32_t stride;  /**< Number of bytes between successive rows. */
    axvp_pixel_format_t format; /**< Pixel format of the payload. */
    uint64_t timestamp_ns;      /**< Capture timestamp in nanoseconds. */
    uint64_t reserved[4]; /**< Reserved for future ABI extensions; zero-fill. */
} axvp_frame_t;

typedef struct axvp_face_t {
    uint32_t size;        /**< Size of this structure in bytes. */
    uint8_t face_id[16];  /**< 16-byte face pseudonym derived from HMAC. */
    float bbox_x;         /**< Bounding box left coordinate in pixels. */
    float bbox_y;         /**< Bounding box top coordinate in pixels. */
    float bbox_width;     /**< Bounding box width in pixels. */
    float bbox_height;    /**< Bounding box height in pixels. */
    float liveness_score; /**< Normalized liveness score in the range 0..1. */
    uint32_t pulse_bpm;   /**< Estimated pulse in beats per minute. */
    axvp_liveness_verdict_t verdict; /**< Liveness verdict for this face. */
    uint8_t pixels_wiped; /**< Non-zero when the source pixels were wiped. */
    uint8_t reserved[7];  /**< Reserved for future ABI extensions; zero-fill. */
} axvp_face_t;

typedef struct axvp_result_t {
    uint32_t size;      /**< Size of this structure in bytes. */
    axvp_frame_t frame; /**< Anonymized output frame descriptor. */
    const uint8_t
        *metadata;        /**< FlatBuffers metadata buffer owned by library. */
    size_t metadata_size; /**< Size of the metadata buffer in bytes. */
    axvp_status_t status; /**< Result status for the completed pipeline call. */
    uint32_t faces_detected;   /**< Number of faces detected in the frame. */
    uint32_t faces_anonymized; /**< Number of faces anonymized in the frame. */
    uint8_t anonymization_complete; /**< Non-zero when pixel wipe completed. */
    uint8_t reserved[7]; /**< Reserved for future ABI extensions; zero-fill. */
} axvp_result_t;

typedef struct axvp_context_t axvp_context_t;

axvp_context_t *axvp_create(const axvp_config_t *cfg,
                            axvp_status_t *status) AXVP_NOEXCEPT;
void axvp_destroy(axvp_context_t *ctx) AXVP_NOEXCEPT;
axvp_status_t axvp_process_frame(axvp_context_t *ctx, const axvp_frame_t *in,
                                 axvp_result_t *out) AXVP_NOEXCEPT;
void axvp_release_result(axvp_context_t *ctx,
                         axvp_result_t *result) AXVP_NOEXCEPT;
axvp_status_t axvp_set_policy(axvp_context_t *ctx,
                              axvp_policy_t policy) AXVP_NOEXCEPT;
axvp_status_t axvp_rotate_keys(axvp_context_t *ctx) AXVP_NOEXCEPT;

#if defined(__cplusplus)
} // extern "C"
#undef AXVP_NOEXCEPT
#endif

#undef AXVP_NOEXCEPT

#endif // AXVPHANTOM_AXVPHANTOM_H

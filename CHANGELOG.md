# Changelog

## Unreleased

### Phase 8

- Step 8.5. Added documentation generation, publication assets, and release
  packaging for the SDK.
- Step 8.4. Added sanitizer runs for ASan, UBSan, LeakSanitizer, and
  ThreadSanitizer across the full test corpus.
- Step 8.3. Added the long-running stress-test scenario for parallel contexts
  and memory stability.
- Step 8.2. Added a latency benchmark for `axvp_process_frame` with
  stage-level measurement hooks.
- Step 8.1. Added end-to-end integration coverage for live subjects, groups,
  static scenes, empty frames, and partial occlusions.

### Phase 7

- Step 7.3. Implemented safe result release and return of frame storage to the
  pool allocator.
- Step 7.2. Replaced the frame-processing stub with the full Detection ->
  Anonymization -> Liveness -> Composer chain and blur fallback.
- Step 7.1. Completed full context initialization with allocator, model,
  Vulkan, stage, and key setup.

### Phase 6

- Step 6.2. Added FlatBuffers composition of anonymization and liveness
  results into the public output buffer.
- Step 6.1. Implemented face pseudonymization with HMAC-SHA256 over normalized
  landmarks and a session key.

### Phase 5

- Step 5.5. Added synthetic LIVE, SPOOF, and UNCERTAIN test coverage for the
  liveness pipeline.
- Step 5.4. Added liveness scoring, verdict heuristics, and pulse estimation
  from FFT-derived frequency peaks.
- Step 5.3. Implemented the POS algorithm over sliding windows using
  span-based, allocation-free processing.
- Step 5.2. Added the band-pass IIR filter with compile-time coefficient
  generation for common FPS values.
- Step 5.1. Implemented the rPPG ring buffer for ROI-based signal accumulation
  without post-init allocations.

### Phase 4

- Step 4.5. Added the contract test that verifies anonymized regions are
  actually wiped and measurably altered.
- Step 4.4. Added explicit pixel wiping after compositing and propagated the
  wipe flag into anonymization results.
- Step 4.3. Wired the GPU anonymization pipeline, including image upload,
  compute dispatch, and ROI compositing.
- Step 4.2. Implemented the compute-shader blur path and integrated SPIR-V
  generation into the build.
- Step 4.1. Added Vulkan context initialization with a CPU fallback path when
  Vulkan is unavailable.

### Phase 3

- Step 3.4. Enabled compiler and host-aware SIMD tuning for x86 AVX2/AVX-512
  and ARM dotprod/fp16 targets.
- Step 3.3. Implemented two-pass face detection and landmark extraction with
  policy-controlled failure handling.
- Step 3.2. Defined the SoA-based `DetectionResult` and `FaceROI` data flow
  between stages.
- Step 3.1. Added immutable detector model loading with checksum and size
  validation.

### Phase 2

- Step 2.3. Implemented `MetadataView` as a zero-copy FlatBuffers accessor with
  identifier validation and expected-based error handling.
- Step 2.2. Wired `flatc` into CMake so schema changes regenerate headers in
  the build tree automatically.
- Step 2.1. Defined the FlatBuffers schema for frame metadata, face records,
  rectangles, and liveness verdicts.

### Phase 1

- Step 1.3. Added the C++23 wrapper skeleton with RAII context, frame, result,
  and metadata types.
- Step 1.2. Implemented the C API stubs so downstream integrators could compile
  against the full surface before the pipeline was wired in.
- Step 1.1. Added the public C header `axvphantom.h` with ABI-stable
  `size`-prefixed structs and C99/C++23 compatibility.

### Phase 0

- Step 0.4. Introduced the internal foundation types: `Error`,
  `SecureBuffer`, `ScopedTimer`, `FramePoolAllocator`, and `UniqueFrame`, each
  backed by unit tests.
- Step 0.3. Kept the CI pipeline outside the initial scaffold and reserved it
  for a later implementation phase.
- Step 0.2. Set up CMake 3.28+, Conan profiles, and CMake presets for Debug,
  Release, RelWithDebInfo, and EdgeARM builds.
- Step 0.1. Established the repository layout for public headers, internal
  sources, schema files, tests, CMake helpers, Conan manifests, and formatting
  rules.

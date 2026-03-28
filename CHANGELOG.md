# Changelog

## Unreleased

- Implemented the full SDK pipeline across detection, anonymization, liveness,
  and composition stages.
- Added the public C API and C++23 wrapper, with `size`-prefixed ABI-safe
  structs and zero-copy metadata reading.
- Added FlatBuffers metadata generation and the `MetadataView` accessor layer.
- Wired GoogleTest and GoogleMock into the test suite and added integration,
  benchmark, and stress coverage.
- Added sanitizer presets for ASan/UBSan and TSan, plus narrow suppressions for
  known external runtime noise in the local test environment.
- Added `make install-data` bootstrap assets for detector models and face test
  images.

## 0.0.4-dev

Initial SDK work-in-progress milestone used for the phase 0 scaffold and
subsequent implementation steps.

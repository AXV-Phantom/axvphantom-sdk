# AXV Phantom SDK - biometric anonymization and liveness verification

AXV Phantom SDK is a C++23 library for biometric anonymization and liveness verification in video streams.
This repository contains the SDK library, the public C API and C++23 wrapper, build tooling, tests, benchmarks,
and the data bootstrap scripts described in the project documentation.

## Contents

- [Overview](#overview)
- [Status Snapshot](#status-snapshot)
- [Design Principles](#design-principles)
- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Command Reference](#command-reference)
- [Build Targets](#build-targets)
- [Presets](#presets)
- [Conan Profiles](#conan-profiles)
- [Dependencies](#dependencies)
- [Documentation](#documentation)
- [Repository Layout](#repository-layout)
- [Runtime Data](#runtime-data)

## Overview

The project provides a shared library target named `axvphantom` and a reproducible local workflow built around
CMake 3.28+, Conan 2, Ninja, and a compact Makefile wrapper.

## Status Snapshot

| Area | Status |
| --- | --- |
| Core pipeline | Implemented end to end across detection, anonymization, liveness, and composition. |
| Public API | C API and C++23 wrapper are available. |
| Metadata | FlatBuffers metadata generation and zero-copy `MetadataView` access are implemented. |
| Tests | Unit, integration smoke, benchmark, and sanitizer coverage are wired in. |
| Tooling | CMake presets, Conan profiles, Makefile helpers, and data bootstrap scripts are present. |
| SIMD tuning | Enabled automatically when the compiler and host support the relevant flags. |

## Design Principles

| Principle | Meaning |
| --- | --- |
| No exceptions in the public API | Public entry points return explicit status codes. |
| Explicit error handling | Internal code uses `std::expected` where it improves clarity. |
| No global mutable state | Resources live inside a context and are torn down deterministically. |
| Low-copy data flow | Frames and metadata are passed with minimal copying. |
| Pixel wiping is contractual | Sensitive pixels are wiped as part of the processing flow. |

## Requirements

| Tool | Required | Notes |
| --- | --- | --- |
| CMake | Yes | Version 3.28 or newer. |
| Conan | Yes | Conan 2 is used for dependency management. |
| Ninja | Yes | The default generator used by the presets and Makefile helpers. |
| `flatc` | Yes | Required for FlatBuffers code generation during configure/build. |
| C++23 compiler | Yes | Any compiler that can build the project with the selected preset. |
| `clang-format` | Optional | Needed for `make fmt`. |
| `clang-tidy` | Optional | Needed for `make lint`. |

If a local GoogleTest package is unavailable, CMake fetches the official `googletest` release automatically.

## Quick Start

1. `cd axvphantom-sdk`
2. `make install`
3. `make install-data`
4. `make build`
5. `make test`
6. `make lint`

Optional optimized build:

```bash
make release
```

Sanitizer validation:

```bash
ctest --preset asan --output-on-failure
ctest --preset tsan --output-on-failure
```

## Command Reference

| Command | Purpose |
| --- | --- |
| `make install` | Installs Conan dependencies into `build/conan/<profile>`. |
| `make install-data` | Downloads the detector model, the face-landmark model, and face-test images into `data/`. |
| `make build` | Configures and builds the Debug preset. |
| `make test` | Builds the Debug preset and runs `ctest --preset debug`. |
| `make release` | Configures and builds the Release preset. |
| `make fmt` | Runs `clang-format` over headers and sources. |
| `make lint` | Runs `clang-tidy` on production code in `src/` only. |

You can change the Conan profile used by `make install`:

```bash
make install CONAN_PROFILE=Debug
make install CONAN_PROFILE=RelWithDebInfo
make install CONAN_PROFILE=EdgeARM
```

## Build Targets

| Target | Type | Purpose |
| --- | --- | --- |
| `axvphantom` | Shared library | Main SDK library. |
| `axvphantom_tests` | Executable | GoogleTest unit suite. |
| `axvphantom_capi_smoke` | Executable | C API integration smoke test. |
| `axvphantom_bench` | Executable | Latency benchmark for `axvp_process_frame`. |

The default presets enable tests and benchmarks. The `lint` preset disables both so static analysis stays focused
on production code.

## Presets

### CMake configure and build presets

| Preset | Purpose |
| --- | --- |
| `debug` | Development build with Debug flags. |
| `release` | Optimized release build. |
| `relwithdebinfo` | Release build with debug symbols. |
| `edgearm` | ARM edge-device build configuration. |
| `lint` | Static-analysis configuration using `clang++` and no tests or benchmarks. |
| `asan` | AddressSanitizer + UBSan configuration. |
| `tsan` | ThreadSanitizer configuration. |

### CTest presets

| Preset | Purpose |
| --- | --- |
| `debug` | Runs the debug test suite. |
| `release` | Runs tests for the release configuration. |
| `relwithdebinfo` | Runs tests for the RelWithDebInfo configuration. |
| `edgearm` | Runs tests for the ARM edge configuration. |
| `asan` | Runs sanitizer tests with `AXVP_FORCE_CPU_BACKEND=1` and `LSAN_OPTIONS` pointing to `cmake/lsan.supp`. |
| `tsan` | Runs sanitizer tests with `AXVP_FORCE_CPU_BACKEND=1` and `TSAN_OPTIONS=ignore_noninstrumented_modules=1`. |

The normal build presets probe the compiler for SIMD flags and pass them through to the SDK target when available.

## Conan Profiles

| Profile | Intended use |
| --- | --- |
| `Release` | Default dependency install profile. |
| `Debug` | Development dependency install profile. |
| `RelWithDebInfo` | Release-like profile with symbols. |
| `EdgeARM` | ARM edge-device dependency profile. |

## Dependencies

| Dependency | Role |
| --- | --- |
| OpenCV | Image processing, detection, liveness, and fallback processing. |
| FlatBuffers | Metadata serialization. |
| FFmpeg | Media stack and codec-related support. |
| liburing | Async I/O support. |
| Vulkan Loader | GPU anonymization path. |
| OpenSSL | HMAC and crypto primitives. |

Test support uses GoogleTest and GoogleMock. If they are not installed locally, CMake fetches them.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | Main build definition. |
| `CMakePresets.json` | Configure, build, and test presets. |
| `Makefile` | Shortcuts for install, build, test, lint, and formatting. |
| `conanfile.py` | Conan package definition and dependency graph. |
| `conan/profiles/` | Conan host profiles for local development and release builds. |
| `include/axvphantom/` | Public C and C++ headers. |
| `src/` | Library implementation. |
| `schema/` | FlatBuffers schema files. |
| `tests/unit/` | Unit tests. |
| `tests/integration/` | Integration tests, including the C API smoke test. |
| `tests/bench/` | Benchmark sources. |
| `scripts/install-data.sh` | Bootstrap script for local model and image assets. |
| `cmake/lsan.supp` | Narrow LSan suppression file for known external runtime noise. |
| `data/` | Local cache for downloaded model and image assets. |
| `CHANGELOG.md` | Release and implementation notes. |
| `VERSION` | SDK version string. |

## Runtime Data

`data/` is not tracked by git. `make install-data` populates it with the detector model, the face-landmark model,
and test image assets used by the SDK and its tests.

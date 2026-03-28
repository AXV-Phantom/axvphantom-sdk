# AXV Phantom SDK

AXV Phantom SDK is a C++23 library for biometric anonymization and liveness verification in video streams.
The repository now contains the implemented SDK pipeline, build tooling, tests, benchmark harness, and
supporting data bootstrap scripts described in the project documentation.

## What this repository is for

- Providing a shared library target named `axvphantom`.
- Keeping the build reproducible with CMake 3.28+, Conan 2, and Ninja.
- Hosting the future C and C++ wrapper APIs for frame processing.
- Tracking the implementation plan, testing strategy, and technical design in the docs folder.

## Current status

- `CMake` is configured for `Ninja`.
- `CTest` is enabled at the build-system level.
- `GTest` and `GMock` are wired into the unit-test target.
- `axvphantom_tests` is a GoogleTest suite; `axvphantom_bench` is a real latency harness for `axvp_process_frame`.
- `make lint` checks production sources under `src/` only.
- `make install` bootstraps Conan dependencies; it does not install the SDK itself.
- `make install-data` downloads local model assets into `data/`, including the YuNet 2022mar detector, the face-landmark LBF model, and a small face-image pack for detection-stage tests. The directory is ignored by git.
- Sanitizer presets are available for `asan` and `tsan`; they force the CPU backend in test runs to avoid noisy system-runtime false positives.
- CMake auto-enables SIMD tuning flags when the compiler and host CPU support them: AVX2/AVX-512 on x86 and dotprod/fp16 on EdgeARM-capable ARM builds.

## Design goals

- No exceptions in the public API.
- Explicit error handling with `std::expected`.
- No global mutable state.
- Zero-copy or low-copy data flow where possible.
- Sensitive pixel data must be wiped inside the processing call contract.

## Quick start

```bash
cd axvphantom-sdk
make install
make install-data
make build
make test
make lint
```

If you want the optimized build:

```bash
make release
```

For sanitizer validation:

```bash
ctest --preset asan --output-on-failure
ctest --preset tsan --output-on-failure
```

## Make targets

| Command | Description |
| --- | --- |
| `make install` | Installs Conan dependencies into `build/conan/<profile>`. |
| `make install-data` | Downloads the compatible YuNet 2022mar detector, the face-landmark model, and face-test images into local `data/` folders. |
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

## Build presets

The repository ships with these CMake presets:

- `debug`
- `release`
- `relwithdebinfo`
- `edgearm`
- `lint`
- `asan`
- `tsan`

The `lint` preset uses `clang++` and disables tests and benchmarks so static analysis stays focused on production code.
The `asan` preset enables AddressSanitizer and UBSan, and the `tsan` preset enables ThreadSanitizer.

The normal build presets also probe the compiler for SIMD flags and pass them through to the SDK target when available.

If no local GoogleTest package is available, CMake fetches the official `googletest` release for the test build.

## Dependencies

Dependencies are managed through Conan 2 and currently include:

- OpenCV
- FlatBuffers
- FFmpeg
- liburing
- Vulkan Loader
- OpenSSL

The Conan configuration is intentionally trimmed to keep the graph headless and avoid GUI-specific system packages.

## Documentation

- [Documentation index](../docs/README.md)
- [Technical document](../docs/AXPhantom_SDK_TechDoc.md)
- [Implementation plan](../docs/AXPhantom_SDK_ImplPlan.md)
- [Testing appendix](../docs/AXPhantom_SDK_TestingAppendix.md)
- [Project knowledge base](../docs/AXPhantom_Project_Knowledge.md)

## Repository layout

The main tracked files and directories in this repo are:

- `CMakeLists.txt`
- `CMakePresets.json`
- `Makefile`
- `conanfile.py`
- `conan/profiles/`
- `src/`
- `schema/`
- `tests/unit/`
- `tests/bench/`
- `CHANGELOG.md`
- `LICENSE`
- `VERSION`
- `cmake/lsan.supp` contains a narrow sanitizer suppression for a known external runtime leak.
- `data/` is a local cache for downloaded models and face-test images, and is ignored by git.

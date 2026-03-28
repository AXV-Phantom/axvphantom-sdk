# AXV Phantom SDK

AXV Phantom SDK is a C++23 library for biometric anonymization and liveness verification in video streams.
This repository currently contains the build scaffold, dependency management, and placeholder targets that
will evolve into the full pipeline described in the project documentation.

## What this repository is for

- Providing a shared library target named `axvphantom`.
- Keeping the build reproducible with CMake 3.28+, Conan 2, and Ninja.
- Hosting the future C and C++ wrapper APIs for frame processing.
- Tracking the implementation plan, testing strategy, and technical design in the docs folder.

## Current status

- `CMake` is configured for `Ninja`.
- `CTest` is enabled at the build-system level.
- `GTest` and `GMock` are wired into the unit-test target.
- `axvphantom_tests` is a GoogleTest suite; `axvphantom_bench` remains a placeholder executable.
- `make lint` checks production sources under `src/` only.
- `make install` bootstraps Conan dependencies; it does not install the SDK itself.
- `make install-data` downloads local model assets into `data/`, including the YuNet 2022mar detector, the face-landmark LBF model, and a small face-image pack for detection-stage tests. The directory is ignored by git.

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
```

If you want the optimized build:

```bash
make release
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

The `lint` preset uses `clang++` and disables tests and benchmarks so static analysis stays focused on production code.

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
- `data/` is a local cache for downloaded models and face-test images, and is ignored by git.

SHELL := /bin/bash

CMAKE ?= cmake
CONAN ?= conan
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy

PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DEBUG_BUILD_DIR := $(PROJECT_ROOT)/build/debug
RELEASE_BUILD_DIR := $(PROJECT_ROOT)/build/release
LINT_BUILD_DIR := $(PROJECT_ROOT)/build/lint
CONAN_PROFILE ?= Release
CONAN_SYSTEM_PACKAGE_MANAGER_MODE ?= report
CONAN_PROFILE_FILE := $(PROJECT_ROOT)/conan/profiles/$(CONAN_PROFILE).profile
CONAN_OUTPUT_DIR := $(PROJECT_ROOT)/build/conan/$(CONAN_PROFILE)

FMT_FILES := $(shell find include src tests -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) | sort)
TIDY_FILES := $(shell find src -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) | sort)

.DEFAULT_GOAL := build

.PHONY: build release test install fmt lint

build:
	$(CMAKE) --preset debug
	$(CMAKE) --build --preset debug

release:
	$(CMAKE) --preset release
	$(CMAKE) --build --preset release

test: build
	ctest --preset debug --output-on-failure

install:
	$(CONAN) install $(PROJECT_ROOT) \
		--output-folder $(CONAN_OUTPUT_DIR) \
		--profile:host $(CONAN_PROFILE_FILE) \
		--profile:build default \
		--build=missing \
		--conf tools.system.package_manager:mode=$(CONAN_SYSTEM_PACKAGE_MANAGER_MODE)

fmt:
	@if [ -n "$(FMT_FILES)" ]; then \
		$(CLANG_FORMAT) -i --style=file $(FMT_FILES); \
	fi

lint:
	$(CMAKE) --preset lint
	$(CMAKE) --build $(LINT_BUILD_DIR)
	@if [ -n "$(TIDY_FILES)" ]; then \
		$(CLANG_TIDY) -p $(LINT_BUILD_DIR) --config-file=$(PROJECT_ROOT)/.clang-tidy $(TIDY_FILES); \
	fi

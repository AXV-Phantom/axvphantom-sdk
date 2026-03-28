cmake_minimum_required(VERSION 3.28)

set(AXVP_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(READ "${AXVP_VERSION_FILE}" AXVP_VERSION_RAW)
string(STRIP "${AXVP_VERSION_RAW}" AXVP_VERSION_RAW)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" AXVP_PROJECT_VERSION "${AXVP_VERSION_RAW}")

if(NOT AXVP_PROJECT_VERSION)
    message(FATAL_ERROR "Invalid version string in ${AXVP_VERSION_FILE}: ${AXVP_VERSION_RAW}")
endif()

project(axvphantom VERSION "${AXVP_PROJECT_VERSION}" LANGUAGES C CXX)

set(AXVP_VERSION "${AXVP_VERSION_RAW}")

option(AXVP_BUILD_TESTS "Build the axvphantom_tests target" ON)
option(AXVP_BUILD_BENCH "Build the axvphantom_bench target" ON)
option(AXVP_EDGEARM "Enable EdgeARM-specific build switches" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
enable_testing()
include(FetchContent)
include(GoogleTest)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs objdetect dnn face)
find_package(OpenSSL REQUIRED)
find_package(flatbuffers CONFIG QUIET)
if(NOT TARGET flatbuffers::flatbuffers)
    find_path(AXVP_FLATBUFFERS_INCLUDE_DIR NAMES flatbuffers/flatbuffers.h REQUIRED)
    add_library(flatbuffers::flatbuffers INTERFACE IMPORTED)
    set_target_properties(flatbuffers::flatbuffers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${AXVP_FLATBUFFERS_INCLUDE_DIR}"
    )
endif()

set(AXVP_SCHEMA_FILE "${PROJECT_SOURCE_DIR}/schema/axvphantom.fbs")
set(AXVP_GENERATED_ROOT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(AXVP_GENERATED_DIR "${AXVP_GENERATED_ROOT_DIR}/axvphantom/generated")
set(AXVP_GENERATED_HEADER "${AXVP_GENERATED_DIR}/axvphantom_generated.h")

find_program(AXVP_FLATC_EXECUTABLE NAMES flatc REQUIRED)

add_custom_command(
    OUTPUT "${AXVP_GENERATED_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${AXVP_GENERATED_DIR}"
    COMMAND "${AXVP_FLATC_EXECUTABLE}" --cpp -o "${AXVP_GENERATED_DIR}" "${AXVP_SCHEMA_FILE}"
    DEPENDS "${AXVP_SCHEMA_FILE}"
    COMMENT "Generating FlatBuffers header from axvphantom.fbs"
    VERBATIM
)

add_custom_target(axvphantom_flatbuffers_codegen DEPENDS "${AXVP_GENERATED_HEADER}")

set(AXVP_PUBLIC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

add_library(axvphantom SHARED src/axvphantom.cpp)
add_dependencies(axvphantom axvphantom_flatbuffers_codegen)

target_compile_features(axvphantom PUBLIC cxx_std_23)
target_include_directories(axvphantom
    PUBLIC
        $<BUILD_INTERFACE:${AXVP_PUBLIC_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${AXVP_GENERATED_ROOT_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        ${OpenCV_INCLUDE_DIRS}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(axvphantom PUBLIC ${OpenCV_LIBS} flatbuffers::flatbuffers OpenSSL::Crypto)

target_compile_definitions(axvphantom
    PUBLIC
        $<$<BOOL:${AXVP_EDGEARM}>:AXVP_EDGEARM=1>
)

set_target_properties(axvphantom PROPERTIES
    OUTPUT_NAME axvphantom
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

if(AXVP_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG QUIET)

    if(NOT TARGET GTest::gmock_main AND NOT TARGET gmock_main)
        set(INSTALL_GTEST OFF CACHE BOOL "Install GoogleTest" FORCE)
        set(BUILD_GMOCK ON CACHE BOOL "Build GoogleMock" FORCE)
        set(BUILD_GTEST ON CACHE BOOL "Build GoogleTest" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.17.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    if(TARGET gmock_main AND NOT TARGET GTest::gmock_main)
        add_library(GTest::gmock_main ALIAS gmock_main)
    endif()
    if(TARGET gmock AND NOT TARGET GTest::gmock)
        add_library(GTest::gmock ALIAS gmock)
    endif()
    if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    if(TARGET gtest AND NOT TARGET GTest::gtest)
        add_library(GTest::gtest ALIAS gtest)
    endif()

    add_executable(axvphantom_tests tests/unit/main.cpp)
    target_compile_features(axvphantom_tests PRIVATE cxx_std_23)
    target_include_directories(axvphantom_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(axvphantom_tests PRIVATE
        AXVP_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    )
    target_link_libraries(axvphantom_tests PRIVATE axvphantom GTest::gmock_main)
    gtest_discover_tests(axvphantom_tests)

    add_executable(axvphantom_capi_smoke tests/integration/api/main.c)
    target_compile_features(axvphantom_capi_smoke PRIVATE c_std_99)
    target_include_directories(axvphantom_capi_smoke PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(axvphantom_capi_smoke PRIVATE
        AXVP_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    )
    target_link_libraries(axvphantom_capi_smoke PRIVATE axvphantom)
    set_target_properties(axvphantom_capi_smoke PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )
    add_test(NAME axvphantom_capi_smoke COMMAND axvphantom_capi_smoke)
endif()

if(AXVP_BUILD_BENCH)
    add_executable(axvphantom_bench tests/bench/main.cpp)
    target_compile_features(axvphantom_bench PRIVATE cxx_std_23)
    target_include_directories(axvphantom_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(axvphantom_bench PRIVATE axvphantom)
endif()

install(TARGETS axvphantom
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/axvphantom/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/axvphantom
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(FILES "${AXVP_GENERATED_HEADER}"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/axvphantom/generated
)

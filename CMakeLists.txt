cmake_minimum_required(VERSION 3.28)

project(axvphantom VERSION 0.0.2-dev LANGUAGES CXX)

option(AXVP_BUILD_TESTS "Build the axvphantom_tests target" ON)
option(AXVP_BUILD_BENCH "Build the axvphantom_bench target" ON)
option(AXVP_EDGEARM "Enable EdgeARM-specific build switches" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

set(AXVP_PUBLIC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

add_library(axvphantom SHARED src/axvphantom.cpp)

target_compile_features(axvphantom PUBLIC cxx_std_23)
target_include_directories(axvphantom
    PUBLIC
        $<BUILD_INTERFACE:${AXVP_PUBLIC_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

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
    add_executable(axvphantom_tests tests/unit/main.cpp)
    target_compile_features(axvphantom_tests PRIVATE cxx_std_23)
    target_link_libraries(axvphantom_tests PRIVATE axvphantom)
    add_test(NAME axvphantom_tests COMMAND axvphantom_tests)
endif()

if(AXVP_BUILD_BENCH)
    add_executable(axvphantom_bench tests/bench/main.cpp)
    target_compile_features(axvphantom_bench PRIVATE cxx_std_23)
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

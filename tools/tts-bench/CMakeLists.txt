# Standalone build for the TTS bake-off probe.
#
# Deliberately separate from the plugin build: this tool needs sherpa-onnx but
# not Qt, libobs or the OBS SDK, so a host machine can benchmark engines
# without the full OBS development stack.
#
#   cmake -S tools/tts-bench -B build-host/tts-bench/build \
#         -DCMAKE_BUILD_TYPE=Release -DSHERPA_ONNX_ROOT=<prefix>
#   cmake --build build-host/tts-bench/build

cmake_minimum_required(VERSION 3.16)
project(ytchat_tts_bench CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SHERPA_ONNX_ROOT "" CACHE PATH "Prefix containing sherpa-onnx headers and libraries")

find_path(SHERPA_ONNX_INCLUDE_DIR NAMES sherpa-onnx/c-api/cxx-api.h
    HINTS "${SHERPA_ONNX_ROOT}/include")
find_library(SHERPA_ONNX_CXX_LIBRARY NAMES sherpa-onnx-cxx-api
    HINTS "${SHERPA_ONNX_ROOT}/lib")
find_library(SHERPA_ONNX_C_LIBRARY NAMES sherpa-onnx-c-api
    HINTS "${SHERPA_ONNX_ROOT}/lib")

if(NOT SHERPA_ONNX_INCLUDE_DIR OR NOT SHERPA_ONNX_CXX_LIBRARY
   OR NOT SHERPA_ONNX_C_LIBRARY)
    message(FATAL_ERROR
        "Set -DSHERPA_ONNX_ROOT=<prefix> to a sherpa-onnx install tree "
        "(include/sherpa-onnx/c-api/cxx-api.h and lib/libsherpa-onnx-*.so)")
endif()

add_executable(tts_probe tts_probe.cpp)
set_target_properties(tts_probe PROPERTIES
    OUTPUT_NAME tts-probe
    BUILD_RPATH "${SHERPA_ONNX_ROOT}/lib"
)
target_include_directories(tts_probe PRIVATE "${SHERPA_ONNX_INCLUDE_DIR}")
# The prebuilt sherpa-onnx shared libraries are compiled with the old
# libstdc++ ABI.  Build the probe the same way or the std::string parameters
# in the C++ wrapper fail to link (the class-only signatures still resolve,
# which makes the mismatch look like a missing symbol).
target_compile_definitions(tts_probe PRIVATE _GLIBCXX_USE_CXX11_ABI=0)
target_link_libraries(tts_probe PRIVATE
    "${SHERPA_ONNX_CXX_LIBRARY}"
    "${SHERPA_ONNX_C_LIBRARY}"
)

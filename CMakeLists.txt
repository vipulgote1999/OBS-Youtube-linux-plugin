cmake_minimum_required(VERSION 3.28)

project(obs-yt-chat-tts VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(libobs REQUIRED CONFIG)
find_package(obs-frontend-api REQUIRED CONFIG)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Native Piper runtime.  Host builds and host tests deliberately do not need
# libpiper or ONNX Runtime: the real backend is only compiled into the plugin
# when PIPER_ROOT (or the normal system paths) provides piper.h and both
# libraries.  The Flatpak build passes -DPIPER_ROOT=/app/plugins/YtChatTts.
set(PIPER_ROOT "" CACHE PATH "Prefix containing libpiper and ONNX Runtime")

find_path(PIPER_INCLUDE_DIR NAMES piper.h HINTS "${PIPER_ROOT}/include")
find_library(PIPER_LIBRARY NAMES piper HINTS "${PIPER_ROOT}/lib")
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime HINTS "${PIPER_ROOT}/lib")

if(PIPER_INCLUDE_DIR AND PIPER_LIBRARY AND ONNXRUNTIME_LIBRARY)
    set(YTCHAT_PIPER_AVAILABLE ON)
    message(STATUS "Piper backend: ${PIPER_LIBRARY} (headers: ${PIPER_INCLUDE_DIR})")
else()
    set(YTCHAT_PIPER_AVAILABLE OFF)
    message(STATUS "Piper backend: not found; building without libpiper")
endif()

add_library(yt-chat-tts MODULE
    src/plugin-main.cpp
    src/yt-chat-tts-dock.cpp
    src/chat-client.cpp
    src/paths.cpp
    src/settings.cpp
    src/text-normalizer.cpp
    src/message-filter.cpp
    src/tts-queue.cpp
    src/wav.cpp
    src/espeak-engine.cpp
    src/speech-conditioner.cpp
    src/speech-conditioner.hpp
    src/audio-resampler.cpp
    src/audio-resampler.hpp
    src/local-tts-engine.cpp
    src/local-tts-engine.hpp
    src/audio-buffer.cpp
    src/audio-bus.cpp
    src/audio-source.cpp
    src/tts-worker.cpp
    src/plugin-context.cpp
    src/plugin-context.hpp
    src/tts-worker.hpp
)

# libobs (and OBS Flatpak plugin extensions) load modules as <name>.so on
# Linux; suppress CMake's automatic "lib" prefix.  Required by the smoke test
# path /app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so.
set_target_properties(yt-chat-tts PROPERTIES PREFIX "")

target_include_directories(yt-chat-tts PRIVATE src)

target_link_libraries(yt-chat-tts PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

# Product code defines HAVE_LIBPIPER=1 when the bundled runtime is linked so
# the engine wiring can choose Piper; host builds compile without it.
if(YTCHAT_PIPER_AVAILABLE)
    target_sources(yt-chat-tts PRIVATE
        src/piper-engine.cpp
        src/piper-engine.hpp
        src/libpiper-api.cpp
        src/libpiper-api.hpp
    )
    target_include_directories(yt-chat-tts PRIVATE "${PIPER_INCLUDE_DIR}")
    target_compile_definitions(yt-chat-tts PRIVATE HAVE_LIBPIPER=1)
    target_link_libraries(yt-chat-tts PRIVATE
        "${PIPER_LIBRARY}"
        "${ONNXRUNTIME_LIBRARY}"
    )

    # yt-chat-tts.so is installed in <prefix>/lib/obs-plugins, while libpiper
    # and libonnxruntime are installed in <prefix>/lib.
    set_target_properties(yt-chat-tts PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

install(TARGETS yt-chat-tts
    LIBRARY DESTINATION lib/obs-plugins
)

install(DIRECTORY data/
    DESTINATION share/obs/obs-plugins/yt-chat-tts
)

# The Go helper is a gitignored build artifact.  scripts/build-helper.sh (also
# run by scripts/build-flatpak.sh) produces dist/ytchat-helper before configure.
# Host builds and tests do not need it; the Flatpak extension always does, and
# scripts/build-flatpak.sh fails if the helper is missing after its own build.
if(EXISTS "${CMAKE_SOURCE_DIR}/dist/ytchat-helper")
    install(PROGRAMS "${CMAKE_SOURCE_DIR}/dist/ytchat-helper"
        DESTINATION share/obs/obs-plugins/yt-chat-tts/bin
    )
else()
    message(WARNING
        "dist/ytchat-helper is missing; run scripts/build-helper.sh first. "
        "The installed plugin cannot read chat without the bundled helper.")
endif()

install(FILES flatpak/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml
    DESTINATION share/metainfo
)

# Ship the project license and every third-party notice with the extension so
# the installed tree is self-describing.  Only files that exist in the source
# tree are installed; no license text is generated here.
set(YTCHAT_LICENSE_DIR "share/licenses/com.obsproject.Studio.Plugin.YtChatTts")
install(FILES LICENSE THIRD_PARTY_NOTICES.md
    DESTINATION ${YTCHAT_LICENSE_DIR}
)
install(DIRECTORY LICENSES/
    DESTINATION ${YTCHAT_LICENSE_DIR}/LICENSES
)

# Packaging probe for the Flatpak build.  It is off by default so host builds
# and host tests do not need libpiper or ONNX Runtime.  The Flatpak manifest
# enables it once the bundled libraries and voice model have been installed.
# Besides the raw C API path, the probe can exercise the real LibPiperApi via
# PiperEngine so the shipped backend is verified, not just libpiper itself.
option(BUILD_PIPER_PROBE "Build the Flatpak Piper probe" OFF)

if(BUILD_PIPER_PROBE)
    if(NOT YTCHAT_PIPER_AVAILABLE)
        message(FATAL_ERROR
            "BUILD_PIPER_PROBE requires libpiper and ONNX Runtime; "
            "set -DPIPER_ROOT=<prefix>")
    endif()

    add_executable(piper_probe
        tools/piper_probe.cpp
        src/libpiper-api.cpp
        src/piper-engine.cpp
    )
    set_target_properties(piper_probe PROPERTIES
        OUTPUT_NAME piper-probe
        # Installed next to the other extension binaries:
        #   <prefix>/share/obs/obs-plugins/yt-chat-tts/bin/piper-probe
        # Libraries live in <prefix>/lib, five directories up:
        #   bin -> yt-chat-tts -> obs-plugins -> obs -> share -> <prefix>
        INSTALL_RPATH "$ORIGIN/../../../../../lib"
        BUILD_RPATH "${PIPER_ROOT}/lib"
    )
    target_include_directories(piper_probe PRIVATE
        src
        "${PIPER_INCLUDE_DIR}"
    )
    target_compile_definitions(piper_probe PRIVATE HAVE_LIBPIPER=1)
    target_link_libraries(piper_probe PRIVATE
        Qt6::Core
        "${PIPER_LIBRARY}"
        "${ONNXRUNTIME_LIBRARY}"
    )

    install(TARGETS piper_probe
        RUNTIME DESTINATION share/obs/obs-plugins/yt-chat-tts/bin
    )
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(test_chat_client
        tests/test_chat_client.cpp
        src/chat-client.cpp
        src/paths.cpp
    )

    target_include_directories(test_chat_client PRIVATE src)
    target_compile_definitions(test_chat_client PRIVATE
        TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    target_link_libraries(test_chat_client PRIVATE
        OBS::libobs
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_chat_client COMMAND test_chat_client)

    add_executable(test_settings
        tests/test_settings.cpp
        src/settings.cpp
        src/paths.cpp
    )

    target_include_directories(test_settings PRIVATE src)
    target_link_libraries(test_settings PRIVATE
        OBS::libobs
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_settings COMMAND test_settings)

    add_executable(test_tts_backend
        tests/test_tts_backend.cpp
        src/settings.cpp
        src/paths.cpp
    )

    target_include_directories(test_tts_backend PRIVATE src)
    target_link_libraries(test_tts_backend PRIVATE
        OBS::libobs
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_tts_backend COMMAND test_tts_backend)

    add_executable(test_text_normalizer
        tests/test_text_normalizer.cpp
        src/text-normalizer.cpp
    )

    target_include_directories(test_text_normalizer PRIVATE src)
    target_link_libraries(test_text_normalizer PRIVATE
        OBS::libobs
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_text_normalizer COMMAND test_text_normalizer)

    add_executable(test_message_filter
        tests/test_message_filter.cpp
        src/message-filter.cpp
        src/text-normalizer.cpp
    )

    target_include_directories(test_message_filter PRIVATE src)
    target_link_libraries(test_message_filter PRIVATE
        OBS::libobs
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_message_filter COMMAND test_message_filter)

    add_executable(test_tts_queue
        tests/test_tts_queue.cpp
        src/tts-queue.cpp
    )

    target_include_directories(test_tts_queue PRIVATE src)
    target_link_libraries(test_tts_queue PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_tts_queue COMMAND test_tts_queue)

    add_executable(test_wav
        tests/test_wav.cpp
        src/wav.cpp
    )

    target_include_directories(test_wav PRIVATE src)
    target_compile_definitions(test_wav PRIVATE
        TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    target_link_libraries(test_wav PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_wav COMMAND test_wav)

    add_executable(test_espeak_engine
        tests/test_espeak_engine.cpp
        src/espeak-engine.cpp
        src/wav.cpp
    )

    target_include_directories(test_espeak_engine PRIVATE src)
    target_compile_definitions(test_espeak_engine PRIVATE
        TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    target_link_libraries(test_espeak_engine PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_espeak_engine COMMAND test_espeak_engine)

    add_executable(test_audio_buffer
        tests/test_audio_buffer.cpp
        src/audio-buffer.cpp
    )

    target_include_directories(test_audio_buffer PRIVATE src)
    target_link_libraries(test_audio_buffer PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_audio_buffer COMMAND test_audio_buffer)

    add_executable(test_speech_conditioner
        tests/test_speech_conditioner.cpp
        src/speech-conditioner.cpp
    )

    target_include_directories(test_speech_conditioner PRIVATE src)
    target_link_libraries(test_speech_conditioner PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_speech_conditioner COMMAND test_speech_conditioner)

    add_executable(test_audio_resampler
        tests/test_audio_resampler.cpp
        src/audio-resampler.cpp
    )

    target_include_directories(test_audio_resampler PRIVATE src)
    target_link_libraries(test_audio_resampler PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_audio_resampler COMMAND test_audio_resampler)

    add_executable(test_tts_worker
        tests/test_tts_worker.cpp
        src/tts-worker.hpp
        src/tts-worker.cpp
        src/tts-queue.cpp
        src/audio-buffer.cpp
        src/speech-conditioner.cpp
        src/text-normalizer.cpp
    )

    target_include_directories(test_tts_worker PRIVATE src)
    target_link_libraries(test_tts_worker PRIVATE
        OBS::libobs
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_tts_worker COMMAND test_tts_worker)
    set_tests_properties(test_tts_worker PROPERTIES TIMEOUT 30)

    # Engine-level Piper coverage through the injectable PiperApi.  The real
    # libpiper/ONNX backend is added to the plugin target in a later task, so
    # this host test has no native runtime dependency.
    add_executable(test_piper_engine
        tests/test_piper_engine.cpp
        src/piper-engine.cpp
    )

    target_include_directories(test_piper_engine PRIVATE src)
    target_link_libraries(test_piper_engine PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_piper_engine COMMAND test_piper_engine)
endif()

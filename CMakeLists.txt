cmake_minimum_required(VERSION 3.28)

project(obs-yt-chat-tts VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(libobs REQUIRED CONFIG)
find_package(obs-frontend-api REQUIRED CONFIG)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Supertonic is the only bundled neural engine.  Host builds and host tests
# do not need ONNX Runtime: the real backend is compiled only when
# ONNXRUNTIME_ROOT (or the system paths) provides the headers and library.
# The Flatpak build passes -DONNXRUNTIME_ROOT=/app/plugins/YtChatTts.
set(ONNXRUNTIME_ROOT "" CACHE PATH "Prefix containing ONNX Runtime headers and library")

find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h
    HINTS "${ONNXRUNTIME_ROOT}/include")
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
    HINTS "${ONNXRUNTIME_ROOT}/lib")

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    set(YTCHAT_SUPERTONIC_AVAILABLE ON)
    message(STATUS "Supertonic backend: ${ONNXRUNTIME_LIBRARY} (headers: ${ONNXRUNTIME_INCLUDE_DIR})")
else()
    set(YTCHAT_SUPERTONIC_AVAILABLE OFF)
    message(STATUS "Supertonic backend: ONNX Runtime not found; building without it")
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

# The Supertonic engine needs ONNX Runtime and the bundled model.
if(YTCHAT_SUPERTONIC_AVAILABLE)
    target_sources(yt-chat-tts PRIVATE
        src/supertonic-text.cpp
        src/supertonic-text.hpp
        src/supertonic-engine.cpp
        src/supertonic-engine.hpp
        src/supertonic-api.hpp
        src/libsupertonic-api.cpp
        src/libsupertonic-api.hpp
    )
    target_include_directories(yt-chat-tts PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
    target_compile_definitions(yt-chat-tts PRIVATE HAVE_SUPERTONIC=1)
    target_link_libraries(yt-chat-tts PRIVATE "${ONNXRUNTIME_LIBRARY}")
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

# Packaging probe for the bundled Supertonic 3 model.  It exercises the same
# LibSupertonicApi/SupertonicEngine path the plugin uses, including the
# 44,100 -> 22,050 Hz conversion, so a packaging regression is visible without
# opening the OBS UI.
option(BUILD_SUPERTONIC_PROBE "Build the Flatpak Supertonic probe" OFF)

if(BUILD_SUPERTONIC_PROBE)
    if(NOT YTCHAT_SUPERTONIC_AVAILABLE)
        message(FATAL_ERROR
            "BUILD_SUPERTONIC_PROBE requires ONNX Runtime; "
            "set -DONNXRUNTIME_ROOT=<prefix>")
    endif()

    get_filename_component(ONNXRUNTIME_LIBRARY_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)

    add_executable(supertonic_probe
        tools/supertonic_probe.cpp
        src/libsupertonic-api.cpp
        src/supertonic-text.cpp
        src/supertonic-engine.cpp
        src/audio-resampler.cpp
    )
    set_target_properties(supertonic_probe PROPERTIES
        OUTPUT_NAME supertonic-probe
        # Installed next to the other extension binaries; libraries live five
        # directories up in <prefix>/lib.
        INSTALL_RPATH "$ORIGIN/../../../../../lib"
        BUILD_RPATH "${ONNXRUNTIME_LIBRARY_DIR}"
    )
    target_include_directories(supertonic_probe PRIVATE
        src
        "${ONNXRUNTIME_INCLUDE_DIR}"
    )
    target_link_libraries(supertonic_probe PRIVATE
        Qt6::Core
        "${ONNXRUNTIME_LIBRARY}"
    )

    install(TARGETS supertonic_probe
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

    add_executable(test_audio_consumer
        tests/test_audio_consumer.cpp
    )

    target_include_directories(test_audio_consumer PRIVATE src)
    target_link_libraries(test_audio_consumer PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_audio_consumer COMMAND test_audio_consumer)

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

    # Pure text preprocessing for the Supertonic engine: no ONNX Runtime.
    add_executable(test_supertonic_text
        tests/test_supertonic_text.cpp
        src/supertonic-text.cpp
    )

    target_include_directories(test_supertonic_text PRIVATE src)
    target_link_libraries(test_supertonic_text PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_supertonic_text COMMAND test_supertonic_text)

    # Supertonic engine policy (rate conversion, clamps, cancellation) with a
    # fake API: no ONNX Runtime needed on the host.
    add_executable(test_supertonic_engine
        tests/test_supertonic_engine.cpp
        src/supertonic-engine.cpp
        src/audio-resampler.cpp
    )

    target_include_directories(test_supertonic_engine PRIVATE src)
    target_link_libraries(test_supertonic_engine PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME test_supertonic_engine COMMAND test_supertonic_engine)

    add_executable(test_supertonic_pimpl_header tests/compile_supertonic_pimpl.cpp)
    target_include_directories(test_supertonic_pimpl_header PRIVATE src)
    target_link_libraries(test_supertonic_pimpl_header PRIVATE Qt6::Core)
    add_test(NAME test_supertonic_pimpl_header COMMAND test_supertonic_pimpl_header)
endif()

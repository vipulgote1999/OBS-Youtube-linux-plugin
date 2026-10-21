cmake_minimum_required(VERSION 3.28)

project(obs-yt-chat-tts VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(libobs REQUIRED CONFIG)
find_package(obs-frontend-api REQUIRED CONFIG)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_library(yt-chat-tts MODULE
    src/plugin-main.cpp
    src/yt-chat-tts-dock.cpp
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

install(TARGETS yt-chat-tts
    LIBRARY DESTINATION lib/obs-plugins
)

install(DIRECTORY data/
    DESTINATION share/obs/obs-plugins/yt-chat-tts
)

install(FILES flatpak/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml
    DESTINATION share/metainfo
)

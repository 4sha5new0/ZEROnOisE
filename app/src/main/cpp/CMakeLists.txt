cmake_minimum_required(VERSION 3.22)
project(audioplayer CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Snapdragon 665 / ARMv8.0-A (-mfpu=neon は arm64 では不要)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=armv8-a")
set(CMAKE_C_FLAGS_RELEASE   "-O2 -march=armv8-a")

# ── libFLAC 1.4.3 (FetchContent) ──────────────────────────────────────────────
# minimp3 は third_party/ にベンダリング済みなので FetchContent 不要
include(FetchContent)

set(BUILD_SHARED_LIBS             OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES                OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING                 OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS                    OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS                OFF CACHE BOOL "" FORCE)
set(BUILD_UTILS                   OFF CACHE BOOL "" FORCE)
set(WITH_OGG                      OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES              OFF CACHE BOOL "" FORCE)
set(INSTALL_PKGCONFIG_MODULES     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(flac
    GIT_REPOSITORY https://github.com/xiph/flac.git
    GIT_TAG        1.4.3          # 実在するタグ
    GIT_SHALLOW    TRUE           # shallow clone で高速化
)
FetchContent_MakeAvailable(flac)

# ── ソースファイル ─────────────────────────────────────────────────────────────
add_library(audioplayer SHARED
    main_jni.cpp
    audio/audio_buffer.cpp
    audio/level_meter.cpp
    audio/audio_engine.cpp
    decoder/wav_decoder.cpp
    decoder/mp3_decoder.cpp
    decoder/flac_decoder.cpp
    decoder/mediacodec_decoder.cpp
    metadata/mqa_detector.cpp
    metadata/metadata_reader.cpp
    filesystem/file_scanner.cpp
)

target_include_directories(audioplayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    # minimp3 ベンダーパス (MINIMP3_IMPLEMENTATION は mp3_decoder.cpp 内のみで定義)
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimp3
)

target_link_libraries(audioplayer
    FLAC       # libFLAC static
    aaudio
    mediandk
    android
    log
)

cmake_minimum_required(VERSION 3.22)
project(audioplayer CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Compiler flags (Snapdragon 665 / ARMv8.0-A) ─────────────────────────────
# -mfpu=neon は ARM32 専用フラグのため指定しない（arm64-v8a では NEON 常時有効）
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=armv8-a")
set(CMAKE_C_FLAGS_RELEASE   "-O2 -march=armv8-a")

# ── FetchContent ─────────────────────────────────────────────────────────────
include(FetchContent)

# minimp3 (header-only, CC0)
FetchContent_Declare(minimp3
    GIT_REPOSITORY https://github.com/lieff/minimp3.git
    GIT_TAG        2e86945b48b812fd56a8cf06820b55b90c3727a4
)
FetchContent_MakeAvailable(minimp3)

# libFLAC 1.4.3
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES      OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING       OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS          OFF CACHE BOOL "" FORCE)
set(WITH_OGG            OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES    OFF CACHE BOOL "" FORCE)
set(INSTALL_PKGCONFIG_MODULES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(flac
    GIT_REPOSITORY https://github.com/xiph/flac.git
    GIT_TAG        1.4.3
)
FetchContent_MakeAvailable(flac)

# ── Source files ─────────────────────────────────────────────────────────────
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
    ${minimp3_SOURCE_DIR}
)

target_link_libraries(audioplayer
    FLAC
    aaudio
    mediandk
    android
    log
)

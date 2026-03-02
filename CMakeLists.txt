cmake_minimum_required(VERSION 3.15)
project(altaychat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC Runtime (MultiThreaded = /MT). Can be overridden via -DCMAKE_MSVC_RUNTIME_LIBRARY.
# Set before project() would be ideal, but setting here still works for our targets.
if(MSVC)
    if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()



# Enable libdatachannel media support (RTP packetizers, etc.)
option(RTC_ENABLE_MEDIA "Enable media support" ON)
add_compile_definitions(RTC_ENABLE_MEDIA=1)

# Find LibDataChannel (provided by vcpkg or system)
find_package(LibDataChannel REQUIRED)

# Find Opus
find_package(Opus QUIET)
if(NOT Opus_FOUND)
    # Fallback to pkg-config on Linux/macOS if not found via vcpkg
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS REQUIRED opus)
    else()
        message(FATAL_ERROR "Opus not found. Install via vcpkg: vcpkg install opus")
    endif()
endif()

# Find nlohmann_json
find_package(nlohmann_json CONFIG REQUIRED)

# Find PortAudio
find_package(portaudio QUIET)
if(NOT portaudio_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    else()
        message(FATAL_ERROR "PortAudio not found. Install via vcpkg: vcpkg install portaudio")
    endif()
endif()

add_executable(altaychat 
    src/main.cpp
    src/audio/audio_callback.cpp
    src/signaling/signaling_client.cpp
    src/webrtc/webrtc_manager.cpp
)

# Define version
target_compile_definitions(altaychat PRIVATE ALTAYCHAT_VERSION=\"v0.2\")

target_include_directories(altaychat PRIVATE
    src
    $<$<BOOL:${OPUS_INCLUDE_DIRS}>:${OPUS_INCLUDE_DIRS}>
    $<$<BOOL:${PORTAUDIO_INCLUDE_DIRS}>:${PORTAUDIO_INCLUDE_DIRS}>
)

# Link libraries - handle both vcpkg and system pkg-config paths
target_link_libraries(altaychat PRIVATE
    LibDataChannel::LibDataChannel
    nlohmann_json::nlohmann_json
    $<IF:$<TARGET_EXISTS:Opus::opus>,Opus::opus,${OPUS_LIBRARIES}>
    $<IF:$<TARGET_EXISTS:portaudio_static>,portaudio_static,$<IF:$<TARGET_EXISTS:portaudio>,portaudio,${PORTAUDIO_LIBRARIES}>>
)

# Windows-specific settings
if(WIN32)
    # Basic Windows libraries
    target_link_libraries(altaychat PRIVATE ws2_32 winmm)

    if(MSVC AND CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "^MultiThreaded")
        # Find OpenSSL (vcpkg should have found it already, but just to be sure)
        find_package(OpenSSL REQUIRED)
        
        # Manually locate applink.c
        find_path(OPENSSL_APPLINK_DIR openssl/applink.c
            PATHS ${OPENSSL_INCLUDE_DIR}
            NO_DEFAULT_PATH
        )

        if(OPENSSL_APPLINK_DIR)
            # Add the file if found
            target_sources(altaychat PRIVATE "${OPENSSL_APPLINK_DIR}/openssl/applink.c")
        else()
            # If missing in the vcpkg folder, vcpkg's static port might not have copied it.
            # Print a warning instead of failing.
            message(WARNING "OpenSSL applink.c not found! If you get a runtime crash, add this file manually.")
        endif()
    endif()
endif()

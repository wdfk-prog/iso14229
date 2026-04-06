cmake_minimum_required(VERSION 3.16)

project(client_demo LANGUAGES C)

option(ENABLE_DOWNLOAD "Enable deploy/run helper target (Linux only)" OFF)
option(UDS_ENABLE_WINDOWS "Enable Windows / MSYS2 MINGW64 build skeleton" OFF)
set(UDS_WIN_BACKEND "TSMASTER_API" CACHE STRING "Windows backend")
set_property(CACHE UDS_WIN_BACKEND PROPERTY STRINGS TSMASTER_API)
set(UDS_WIN_ARCH "x64" CACHE STRING "Windows target arch")
set_property(CACHE UDS_WIN_ARCH PROPERTY STRINGS x64)
set(UDS_TSMASTER_SDK_DIR "" CACHE PATH "Path to TSMaster SDK root (for example D:/TOSUN/TSMaster/Data/SDK)")
option(UDS_BUILD_WINDOWS_SMOKE "Build the Windows smoke executable" ON)

set(CLIENT_COMMON_INCLUDE_DIRS
    .
    core
    services
    utils
    platform
    transport
    ..
)

set(CLIENT_COMMON_COMPILE_DEFS
    ISOTP_FC_BS=0
    ISOTP_FC_STMIN=0
    UDS_LINES
    UDS_LOG_LEVEL=2
)

set(CLIENT_WARNING_OPTIONS)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND CLIENT_WARNING_OPTIONS
        -g
        -Wall
        -Wextra
        -Wno-strict-prototypes
    )
endif()

function(client_demo_apply_common_settings target_name)
    target_compile_features(${target_name} PRIVATE c_std_11)
    target_include_directories(${target_name}
        PRIVATE
            ${CLIENT_COMMON_INCLUDE_DIRS}
    )
    target_compile_definitions(${target_name}
        PRIVATE
            ${CLIENT_COMMON_COMPILE_DEFS}
    )
    if(CLIENT_WARNING_OPTIONS)
        target_compile_options(${target_name}
            PRIVATE
                ${CLIENT_WARNING_OPTIONS}
        )
    endif()
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Configuring Linux SocketCAN build")
    message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
    message(STATUS "C compiler ID: ${CMAKE_C_COMPILER_ID}")
    message(STATUS "C sysroot: ${CMAKE_SYSROOT}")

    include(CheckIncludeFile)
    check_include_file("linux/can/isotp.h" UDS_HAVE_LINUX_CAN_ISOTP_H)
    if(NOT UDS_HAVE_LINUX_CAN_ISOTP_H)
        message(FATAL_ERROR
            "Missing linux/can/isotp.h required by SocketCAN backend (UDS_TP_ISOTP_SOCK).\n"
            "Use Yocto toolchain/sysroot or install matching Linux UAPI headers."
        )
    endif()

    add_executable(client
        main.c

        core/client_config.c
        core/client_shell.c
        core/cmd_registry.c
        core/response_registry.c
        core/uds_context.c

        services/client_0x10_session.c
        services/client_0x11_reset.c
        services/client_0x22_0x2E_param.c
        services/client_0x27_security.c
        services/client_0x28_comm.c
        services/client_0x2A_ulog.c
        services/client_0x2F_io.c
        services/client_0x31_console.c
        services/client_0x36_0x37_0x38_file.c

        utils/linenoise.c
        utils/utils.c

        platform/platform_linux.c

        transport/transport.c
        transport/transport_socketcan.c

        ../iso14229.c
    )
    add_executable(client_demo::client ALIAS client)
    client_demo_apply_common_settings(client)
    target_compile_definitions(client
        PRIVATE
            UDS_TP_ISOTP_SOCK
            UDS_TRANSPORT_ENABLE_SOCKETCAN
    )

    if(ENABLE_DOWNLOAD)
        set(TARGET_IP "172.168.1.130" CACHE STRING "Target board IP for download target")
        set(TARGET_USER "root" CACHE STRING "Target board SSH user for download target")
        set(TARGET_DIR "/opt/Ultra/Debug" CACHE STRING "Target deploy directory for download target")
        set(TARGET_BIN_NAME "client" CACHE STRING "Remote binary name for download target")
        set(DOWNLOAD_CLIENT_IF "can1" CACHE STRING "CAN interface passed to client on download target")
        set(DOWNLOAD_CLIENT_SA "7D1" CACHE STRING "Source address passed to client (-s)")
        set(DOWNLOAD_CLIENT_TA "7E1" CACHE STRING "Target address passed to client (-t)")
        set(DOWNLOAD_CLIENT_FA "7E0" CACHE STRING "Functional address passed to client (-f)")
        set(DOWNLOAD_CLIENT_EXTRA_ARGS "" CACHE STRING "Optional extra suffix args appended to remote client command")
        option(DOWNLOAD_KILL_OLD "Kill old process on target before uploading" ON)

        find_program(SSH_PROGRAM ssh REQUIRED)
        find_program(SCP_PROGRAM scp REQUIRED)

        set(REMOTE_BIN_PATH "${TARGET_DIR}/${TARGET_BIN_NAME}")

        if(DOWNLOAD_KILL_OLD)
            set(DOWNLOAD_PREPARE_CMD "mkdir -p '${TARGET_DIR}' && killall -9 '${TARGET_BIN_NAME}' || true")
        else()
            set(DOWNLOAD_PREPARE_CMD "mkdir -p '${TARGET_DIR}'")
        endif()

        string(CONCAT DOWNLOAD_RUN_CMD
            "cd '${TARGET_DIR}' && chmod +x './${TARGET_BIN_NAME}' && './${TARGET_BIN_NAME}'"
            " -i '${DOWNLOAD_CLIENT_IF}'"
            " -s '${DOWNLOAD_CLIENT_SA}'"
            " -t '${DOWNLOAD_CLIENT_TA}'"
            " -f '${DOWNLOAD_CLIENT_FA}'"
            " ${DOWNLOAD_CLIENT_EXTRA_ARGS}"
        )

        add_custom_target(download
            COMMENT "Build, deploy and run ${TARGET_BIN_NAME} on ${TARGET_USER}@${TARGET_IP}"
            COMMAND ${SSH_PROGRAM} ${TARGET_USER}@${TARGET_IP} ${DOWNLOAD_PREPARE_CMD}
            COMMAND ${SCP_PROGRAM} $<TARGET_FILE:client> ${TARGET_USER}@${TARGET_IP}:${REMOTE_BIN_PATH}
            COMMAND ${SSH_PROGRAM} -t ${TARGET_USER}@${TARGET_IP} ${DOWNLOAD_RUN_CMD}
            VERBATIM
        )
        add_dependencies(download client)
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT UDS_ENABLE_WINDOWS)
        message(FATAL_ERROR
            "Windows target detected, but the Windows skeleton is disabled.\n"
            "Reconfigure with -DUDS_ENABLE_WINDOWS=ON in an MSYS2 MINGW64 shell."
        )
    endif()

    if(NOT UDS_WIN_BACKEND STREQUAL "TSMASTER_API")
        message(FATAL_ERROR "Task 6B currently fixes UDS_WIN_BACKEND to TSMASTER_API.")
    endif()

    if(NOT UDS_WIN_ARCH STREQUAL "x64")
        message(FATAL_ERROR "Task 6B currently fixes UDS_WIN_ARCH to x64.")
    endif()

    if(UDS_TSMASTER_SDK_DIR STREQUAL "")
        message(FATAL_ERROR
            "UDS_TSMASTER_SDK_DIR is required for the Windows skeleton.\n"
            "Example: -DUDS_TSMASTER_SDK_DIR=D:/TOSUN/TSMaster/Data/SDK"
        )
    endif()

    set(_tsmaster_search_paths
        "${UDS_TSMASTER_SDK_DIR}/lib/${UDS_WIN_ARCH}"
        "${UDS_TSMASTER_SDK_DIR}/${UDS_WIN_ARCH}"
        "${UDS_TSMASTER_SDK_DIR}"
    )

    find_path(UDS_TSMASTER_INCLUDE_DIR
        NAMES TSMaster.h
        PATHS ${_tsmaster_search_paths}
        NO_DEFAULT_PATH
    )
    find_file(UDS_TSMASTER_IMPORT_LIB
        NAMES TSMaster.lib
        PATHS ${_tsmaster_search_paths}
        NO_DEFAULT_PATH
    )

    if(NOT UDS_TSMASTER_INCLUDE_DIR OR NOT UDS_TSMASTER_IMPORT_LIB)
        message(FATAL_ERROR
            "Failed to locate TSMaster.h / TSMaster.lib under ${UDS_TSMASTER_SDK_DIR}.\n"
            "Expected a layout like <SDK>/lib/x64/TSMaster.h and <SDK>/lib/x64/TSMaster.lib."
        )
    endif()

    get_filename_component(UDS_TSMASTER_IMPORT_LIB_DIR "${UDS_TSMASTER_IMPORT_LIB}" DIRECTORY)
    set(UDS_TSMASTER_DLL_HINT_PATH "${UDS_TSMASTER_IMPORT_LIB_DIR}/TSMaster.dll")
    set(UDS_TSMASTER_DLL_HINT_PATH_ALT "${UDS_TSMASTER_SDK_DIR}/TSMaster.dll")

    add_library(tsmaster_sdk INTERFACE)
    add_library(client_demo::tsmaster_sdk ALIAS tsmaster_sdk)
    target_include_directories(tsmaster_sdk
        INTERFACE
            "${UDS_TSMASTER_INCLUDE_DIR}"
    )
    target_link_libraries(tsmaster_sdk
        INTERFACE
            "${UDS_TSMASTER_IMPORT_LIB}"
    )
    target_compile_definitions(tsmaster_sdk
        INTERFACE
            UDS_TRANSPORT_ENABLE_TSMASTER
            UDS_WINDOWS_BUILD
            UDS_WINDOWS_TSMaster_API
            UDS_WIN_ARCH_X64
            UDS_TSMASTER_DLL_HINT_PATH="${UDS_TSMASTER_DLL_HINT_PATH}"
            UDS_TSMASTER_DLL_HINT_PATH_ALT="${UDS_TSMASTER_DLL_HINT_PATH_ALT}"
    )

    if(UDS_BUILD_WINDOWS_SMOKE)
        add_executable(client_smoke
            platform/windows_smoke_main.c
            platform/platform_windows.c
            transport/transport.c
            transport/transport_tsmaster_api.c
            ../iso14229.c
        )
        add_executable(client_demo::client_smoke ALIAS client_smoke)
        client_demo_apply_common_settings(client_smoke)
        target_link_libraries(client_smoke
            PRIVATE
                client_demo::tsmaster_sdk
        )
        target_compile_definitions(client_smoke
            PRIVATE
                UDS_TRANSPORT_ENABLE_TSMASTER
                UDS_WINDOWS_BUILD
                UDS_WINDOWS_TSMaster_API
                UDS_WIN_ARCH_X64
                UDS_TP_ISOTP_C
                UDS_CUSTOM_MILLIS=1
        )
    endif()

    message(STATUS "Configuring Windows / MSYS2 MINGW64 skeleton")
    message(STATUS "TSMaster include dir: ${UDS_TSMASTER_INCLUDE_DIR}")
    message(STATUS "TSMaster import lib: ${UDS_TSMASTER_IMPORT_LIB}")
else()
    message(FATAL_ERROR "Unsupported target system: ${CMAKE_SYSTEM_NAME}")
endif()

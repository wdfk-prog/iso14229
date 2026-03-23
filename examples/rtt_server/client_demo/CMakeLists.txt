cmake_minimum_required(VERSION 3.16)

project(client_demo_linux LANGUAGES C)
option(ENABLE_DOWNLOAD "Enable deploy/run helper target" OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "client_demo CMake build currently supports Linux only.")
endif()

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

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

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

target_include_directories(client
    PRIVATE
        .
        core
        services
        utils
        platform
        transport
        ..
)

target_compile_definitions(client
    PRIVATE
        ISOTP_FC_BS=0
        ISOTP_FC_STMIN=0
        UDS_TP_ISOTP_SOCK
        UDS_TRANSPORT_ENABLE_SOCKETCAN
        UDS_LINES
        UDS_LOG_LEVEL=2
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(client
        PRIVATE
            -g
            -Wall
            -Wextra
            -Wno-strict-prototypes
    )
endif()

if(ENABLE_DOWNLOAD)
    # Deployment target (make download)
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

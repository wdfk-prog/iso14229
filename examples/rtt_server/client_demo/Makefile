# ==============================================================================
# @file    Makefile
# @brief   Linux/Yocto build script for the UDS Client application.
# @details This Makefile intentionally remains Linux-only. The Windows / MSYS2
#          MINGW64 skeleton introduced by Task 6B is driven by CMake so that the
#          TSMaster SDK include/link configuration stays explicit and reviewable.
#
# @author  [wdfk-prog]
# @version 1.1.0
# ==============================================================================

ifeq ($(OS),Windows_NT)
$(error Windows / MSYS2 MINGW64 builds are CMake-only for Task 6B. Use CMake from the MINGW64 shell instead of this Linux Makefile.)
endif

# ==============================================================================
# 1. Environment & Compiler Configuration
# ==============================================================================

# Default to cross-compilation.
# Usage: make NATIVE=1 (for Host PC Linux build)
NATIVE ?= 0

# Path to the Yocto SDK environment setup script.
# NOTE: This path is specific to the build server/environment.
YOCTO_ENV_SCRIPT = /opt/fsl-imx-xwayland/6.1-mickledore/environment-setup-armv8a-poky-linux

# --- Toolchain Selection ---
ifeq ($(NATIVE), 0)
    # Check if the Yocto script exists before proceeding
    ifneq ($(wildcard $(YOCTO_ENV_SCRIPT)),)
        $(info [INFO] Sourcing Yocto SDK environment...)

        # HACK: Make does not persist environment variables set in a shell command.
        # We source the script, print the environment, and extract the compiler definitions.
        export $(shell . $(YOCTO_ENV_SCRIPT); env | grep -E '^(CC|CXX|LD|CFLAGS|CXXFLAGS|LDFLAGS)=')

        # Extract variables from the sourced environment
        CC          := $(shell . $(YOCTO_ENV_SCRIPT); echo $$CC)
        LD          := $(shell . $(YOCTO_ENV_SCRIPT); echo $$LD)
        SDK_CFLAGS  := $(shell . $(YOCTO_ENV_SCRIPT); echo $$CFLAGS)
        SDK_LDFLAGS := $(shell . $(YOCTO_ENV_SCRIPT); echo $$LDFLAGS)
    else
        $(error [ERR ] Yocto environment script not found at: $(YOCTO_ENV_SCRIPT))
    endif
else
    $(info [INFO] Building natively for host PC Linux.)
    CC          = gcc
    LD          = gcc
    SDK_CFLAGS  =
    SDK_LDFLAGS =
endif

# Output executable name
TARGET = client

# ==============================================================================
# 2. Source Files & Directories
# ==============================================================================

# Directories to scan for source files
SRC_DIRS = . core services utils

# Explicit platform implementation selection (Linux only for current task scope)
PLATFORM_SRCS = platform/platform_linux.c
TRANSPORT_SRCS = transport/transport.c \
                 transport/transport_socketcan.c

# Automatically find all .c files in the specified directories
CLIENT_SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c)) \
              $(PLATFORM_SRCS) \
              $(TRANSPORT_SRCS)

# External UDS Core Library Configuration
UDS_LIB_DIR = ..
UDS_LIB_SRC = $(UDS_LIB_DIR)/iso14229.c

# ==============================================================================
# 3. Compiler Flags & Linker Options
# ==============================================================================

# Base flags from SDK
CFLAGS   = $(SDK_CFLAGS)
CPPFLAGS ?=
LDFLAGS  = $(SDK_LDFLAGS)

# --- Include Paths ---
CPPFLAGS += -I. -Icore -Iservices -Iutils -Iplatform -Itransport -I$(UDS_LIB_DIR)

# --- Optimization Flags ---
CFLAGS  += -DISOTP_FC_BS=0
CFLAGS  += -DISOTP_FC_STMIN=0

# --- Feature Flags ---
CFLAGS  += -DUDS_TP_ISOTP_SOCK
CFLAGS  += -DUDS_TRANSPORT_ENABLE_SOCKETCAN
CFLAGS  += -DUDS_LINES

# --- Logging Configuration ---
CFLAGS  += -DUDS_LOG_LEVEL=2

# --- Warnings & Debugging ---
CFLAGS  += -g -Wall -Wextra
CFLAGS  += -Wno-strict-prototypes

# ==============================================================================
# 4. Object Generation Definitions
# ==============================================================================

OBJ_DIR = obj
CLIENT_OBJS = $(CLIENT_SRCS:%.c=$(OBJ_DIR)/%.o)
LIB_OBJ = $(OBJ_DIR)/$(notdir $(UDS_LIB_SRC:.c=.o))
OBJS = $(CLIENT_OBJS) $(LIB_OBJ)

# ==============================================================================
# 5. Build Rules
# ==============================================================================

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo
	@echo "==> Linking target: $@"
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "==> Build finished: $(TARGET)"
	@echo

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "==> Compiling Local: $<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB_OBJ): $(UDS_LIB_SRC)
	@mkdir -p $(dir $@)
	@echo "==> Compiling Lib:   $<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: all clean

clean:
	@echo "==> Cleaning build files..."
	rm -rf $(OBJ_DIR) $(TARGET)

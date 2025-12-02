# ==============================================================================
# @file    Makefile
# @brief   Build script for the UDS Client application (Refactored Architecture).
# @details This Makefile handles both native (Host PC) and Cross-compilation
#          builds. It automatically scans subdirectories for source files and
#          manages an external UDS core library.
#
# @author  [wdfk-prog]
# @version 1.0.0
# ==============================================================================

# ==============================================================================
# 1. Environment & Compiler Configuration
# ==============================================================================

# Default to cross-compilation.
# Usage: make NATIVE=1 (for Host PC build)
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
    $(info [INFO] Building natively for host PC.)
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

# Automatically find all .c files in the specified directories
CLIENT_SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))

# External UDS Core Library Configuration
UDS_LIB_DIR = ..
UDS_LIB_SRC = $(UDS_LIB_DIR)/iso14229.c

# ==============================================================================
# 3. Compiler Flags & Linker Options
# ==============================================================================

# Base flags from SDK
CFLAGS   = $(SDK_CFLAGS)
LDFLAGS  = $(SDK_LDFLAGS)

# --- Include Paths ---
# Add all source directories and the parent lib directory to include path
CFLAGS  += -I. -Icore -Iservices -Iutils -I$(UDS_LIB_DIR)

# --- Optimization Flags ---
# ISO-TP Flow Control: Disable block separation for maximum throughput
CFLAGS  += -DISOTP_FC_BS=0
CFLAGS  += -DISOTP_FC_STMIN=0

# --- Feature Flags ---
CFLAGS  += -DUDS_TP_ISOTP_SOCK      # Use SocketCAN transport
CFLAGS  += -DUDS_LINES              # Enable line tracing/debugging

# --- Logging Configuration ---
# Log Level: 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug
CFLAGS  += -DUDS_LOG_LEVEL=2

# --- Warnings & Debugging ---
CFLAGS  += -g -Wall -Wextra
CFLAGS  += -Wno-strict-prototypes   # Suppress warnings for old-style function declarations

# ==============================================================================
# 4. Object Generation Definitions
# ==============================================================================

# Directory for intermediate object files
OBJ_DIR = obj

# 1. Map Client Sources to Object files (preserves directory structure)
#    Example: core/file.c -> obj/core/file.o
CLIENT_OBJS = $(CLIENT_SRCS:%.c=$(OBJ_DIR)/%.o)

# 2. Map Library Source to Object file (Flattened structure for external lib)
#    Example: ../iso14229.c -> obj/iso14229.o
LIB_OBJ = $(OBJ_DIR)/$(notdir $(UDS_LIB_SRC:.c=.o))

# Combine all objects
OBJS = $(CLIENT_OBJS) $(LIB_OBJ)

# ==============================================================================
# 5. Build Rules
# ==============================================================================

# Default Target
all: $(TARGET)

# Linking Rule
# Combines all object files to create the final executable
$(TARGET): $(OBJS)
	@echo
	@echo "==> Linking target: $@"
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "==> Build finished: $(TARGET)"
	@echo

# Compilation Rule: Client Sources
# Automatically handles subdirectory creation in obj/
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "==> Compiling Local: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Compilation Rule: External UDS Library
# Special rule for the file located in the parent directory
$(LIB_OBJ): $(UDS_LIB_SRC)
	@mkdir -p $(dir $@)
	@echo "==> Compiling Lib:   $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Phony Targets (Actions, not files)
.PHONY: all clean

# Clean Rule
clean:
	@echo "==> Cleaning build files..."
	rm -rf $(OBJ_DIR) $(TARGET)
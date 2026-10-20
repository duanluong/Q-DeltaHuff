# =====================================
# Compiler and Flags
# =====================================
CC := gcc
CFLAGS := -O0 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function

# =====================================
# Directory Structure
# =====================================
HUFFMAN_SRC_DIR := ../src
HUFFMAN_INC_DIR := ../inc

TEST_DIR        := .
TEST_SRC_DIR    := $(TEST_DIR)/src
UNITY_DIR       := ../../../third-party/Unity/src
BUILD_DIR       := build

# =====================================
# Source Files
# =====================================
SRC_FILES       := $(HUFFMAN_SRC_DIR)/decoder.c  $(HUFFMAN_SRC_DIR)/bit_manager.c
UNITY_SRC       := $(UNITY_DIR)/unity.c
TEST_SRC_FILE   := $(TEST_SRC_DIR)/decoder_test.c

# =====================================
# Include Paths
# =====================================
# NOTE: MOCKS_DIR is listed first so its stub Types.h shadows any other
# Types.h that might be found on the include path.
INCLUDES := -I$(HUFFMAN_INC_DIR) -I$(UNITY_DIR)

# =====================================
# Output Target
# =====================================
OUTPUT := $(BUILD_DIR)/huffman_decode_test_runner

# =====================================
# Build Rules
# =====================================
run_test: check_src_exists $(OUTPUT)
	@echo "Running Q-HUFDEL Package Decoder unit tests..."
	@./$(OUTPUT)

# Check if the test source file exists
check_src_exists:
	@if [ ! -f "$(TEST_SRC_FILE)" ]; then \
		echo "ERROR: File $(TEST_SRC_FILE) not found."; \
		exit 1; \
	fi

$(OUTPUT): $(SRC_FILES) $(UNITY_SRC) $(TEST_SRC_FILE) | $(BUILD_DIR)
	@$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

.PHONY: run_test clean check_src_exists

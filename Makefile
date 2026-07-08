CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Isrc
LDFLAGS ?=
BUILD_DIR ?= build-host

ifeq ($(TARGET_AARCH64),1)
CFLAGS += -DETE_TRBE_TARGET=1
BUILD_DIR := build-target
endif

LIB_OBJ := $(BUILD_DIR)/src/ete_trbe.o
TRACE_OBJ := $(BUILD_DIR)/tools/ete_trace/ete_trace.o
UNIT_OBJ := $(BUILD_DIR)/tests/unit/test_metadata_json.o
RING_OBJ := $(BUILD_DIR)/tests/unit/test_ring_buffer.o
MOCK_OBJ := $(BUILD_DIR)/tests/mock/test_mock_probe.o
SYSREG_MOCK_OBJ := $(BUILD_DIR)/tests/mock/test_sysreg_mock.o
CAPTURE_MOCK_OBJ := $(BUILD_DIR)/tests/mock/test_capture_state_mock.o

.PHONY: all test clean

all: $(BUILD_DIR)/ete_trace $(BUILD_DIR)/test_metadata_json $(BUILD_DIR)/test_ring_buffer $(BUILD_DIR)/test_mock_probe $(BUILD_DIR)/test_sysreg_mock $(BUILD_DIR)/test_capture_state_mock

$(BUILD_DIR)/ete_trace: $(TRACE_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/test_metadata_json: $(UNIT_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/test_ring_buffer: $(RING_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/test_mock_probe: $(MOCK_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/test_sysreg_mock: $(SYSREG_MOCK_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/test_capture_state_mock: $(CAPTURE_MOCK_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c include/ete_trbe.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	$(BUILD_DIR)/test_metadata_json
	$(BUILD_DIR)/test_ring_buffer
	$(BUILD_DIR)/test_mock_probe
	$(BUILD_DIR)/test_sysreg_mock
	$(BUILD_DIR)/test_capture_state_mock

clean:
	rm -rf build-host build-target build

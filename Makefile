CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++26 -fno-exceptions -freflection -Wpedantic -mclflushopt \
            -g -O0 -Wno-unused-parameter -Wno-unused-variable -MMD -MP

BUILD_DIR := build
SHADER_DIR := $(BUILD_DIR)/shaders
TARGET := $(BUILD_DIR)/sd2
DEPS := $(BUILD_DIR)/main.d

SLANGC ?= slangc
SLANG_SOURCES := shaders/shader.slang
SLANG_OUTPUTS := $(patsubst shaders/%.slang,$(SHADER_DIR)/%.spv,$(SLANG_SOURCES))
SLANG_FLAGS := -target spirv -profile spirv_1_4 -emit-spirv-directly \
               -fvk-use-entrypoint-name -entry vert_main -entry frag_main

all: $(TARGET)

$(TARGET): src/main.cpp $(SLANG_OUTPUTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/main.cpp -o $@ -lglfw -lvulkan

$(SHADER_DIR)/%.spv: shaders/%.slang | $(SHADER_DIR)
	$(SLANGC) $< $(SLANG_FLAGS) -o $@

$(BUILD_DIR):
	mkdir -p $@

$(SHADER_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

.PHONY: all run clean
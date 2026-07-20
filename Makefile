CXX := g++
INCLUDES := -I/.include -I./vendor
CXXFLAGS := -Wall -Wextra -std=c++26 -fno-exceptions -freflection -Wpedantic -mclflushopt \
            -g -O0 -Wno-unused-parameter -Wno-unused-variable -fno-omit-frame-pointer -MMD -MP $(INCLUDES)

BUILD_DIR := build
SHADER_DIR := $(BUILD_DIR)/assets/shaders
TEXTURE_DIR := $(BUILD_DIR)/assets/textures
MODEL_DIR := $(BUILD_DIR)/assets/models
TARGET := $(BUILD_DIR)/sd2
DEPS := $(BUILD_DIR)/main.d

SLANGC ?= slangc
SLANG_SOURCES := assets/shaders/shader.slang assets/shaders/line.slang
SLANG_OUTPUTS := $(patsubst assets/shaders/%.slang,$(SHADER_DIR)/%.spv,$(SLANG_SOURCES))
SLANG_FLAGS := -target spirv -profile spirv_1_4 -emit-spirv-directly \
               -fvk-use-entrypoint-name -entry vert_main -entry frag_main

TEXTURE_SOURCES := $(wildcard assets/textures/*)
TEXTURE_OUTPUTS := $(patsubst assets/textures/%,$(TEXTURE_DIR)/%,$(TEXTURE_SOURCES))

MODEL_SOURCES := $(wildcard assets/models/*)
MODEL_OUTPUTS := $(patsubst assets/models/%,$(MODEL_DIR)/%,$(MODEL_SOURCES))

all: $(TARGET)

$(TARGET): src/sd2_main.cpp $(SLANG_OUTPUTS) $(TEXTURE_OUTPUTS) $(MODEL_OUTPUTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/sd2_main.cpp -o $@ -lglfw -lvulkan

$(SHADER_DIR)/%.spv: assets/shaders/%.slang | $(SHADER_DIR)
	$(SLANGC) $< $(SLANG_FLAGS) -o $@

$(TEXTURE_DIR)/%: assets/textures/%
	mkdir -p $(@D)
	cp $< $@

$(MODEL_DIR)/%: assets/models/%
	mkdir -p $(@D)
	cp $< $@

$(BUILD_DIR):
	mkdir -p $@

$(SHADER_DIR):
	mkdir -p $@

$(TEXTURE_DIR):
	mkdir -p $@

$(MODEL_DIR):
	mkdir -p $@

run: $(TARGET)
	cd $(BUILD_DIR) && ./$(notdir $(TARGET))

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

.PHONY: all run clean
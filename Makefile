CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++26 -fno-exceptions -freflection -Wpedantic -mclflushopt -g -O0 -Wno-unused-parameter -Wno-unused-variable
ERROR_FLAGS =

BUILD_DIR= build
TARGET = $(BUILD_DIR)/sd2

all: $(TARGET)

$(TARGET): src/main.cpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/main.cpp -o $(TARGET) -lglfw -lvulkan

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -std=c++17 -Iinclude

# Directories
SRC_DIR = src
OBJ_DIR = obj
TARGET  = main

# Pick up .cpp in src/ and key subdirs
SRCS := $(shell find $(SRC_DIR) -name '*.cpp')

# Map e.g. src/disassembler/foo.cpp -> obj/disassembler/foo.o
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all clean

# Default rule
all: $(TARGET)

# Link object files into executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Compile (Windows-safe: make sure the obj subdir exists)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean (Windows-safe)
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

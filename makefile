CXXFLAGS = -Wfatal-errors -Wall -Wextra -Wpedantic -Wconversion -Wshadow -std=c++11 -O2

# Put all auto generated stuff to this build dir.
BUILD_DIR = ./build

INCLUDE := -I /usr/local/include/gtest/ -I./src

# List of all .cpp source files.
CPP = $(wildcard src/*.cpp) #$(wildcard src/*.hpp)
TEST = $(wildcard test/*.cpp) src/logger.cpp

# All .o files go to build dir.
OBJ = $(CPP:%.cpp=$(BUILD_DIR)/%.o)
TESTOBJ = $(TEST:%.cpp=$(BUILD_DIR)/%.o)

# Gcc/Clang will create these .d files containing dependencies.
DEP = $(OBJ:%.o=%.d)

quarrellib=libqr.a

# Actual target of the binary - depends on all .o files.
qr: $(OBJ)
	# Create build directories - same structure as sources.
	mkdir -p $(@D)
	ar -rcs ${quarrellib} $^

test: $(TESTOBJ)
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o quarreltest $^ -lpthread -lgtest -lgtest_main
	./quarreltest

# Include all .d files
-include $(DEP)

# Build target for every single object file.
# The potential dependency on header files is covered
# by calling `-include $(DEP)`.
$(BUILD_DIR)/%.o : %.cpp
	mkdir -p $(@D)
	# The -MMD flags additionaly creates a .d file with
	# the same name as the .o file.
	$(CXX) $(CXXFLAGS) $(INCLUDE) -MMD -c $< -o $@

.PHONY : clean
clean :
	# This should remove all generated files.
	-rm $(BUILD_DIR)/$(BIN) $(OBJ) $(DEP)

CXXFLAGS = -Wfatal-errors -Wall -Wextra -Wpedantic -Wconversion -Wshadow -std=c++11 -O2 -g -Werror

# Put all auto generated stuff to this build dir.
BUILD_DIR = ./build

INCLUDE := -I /usr/local/include/gtest/ -I./core -I./network -I./util

# List of all .cpp source files.
CPP = $(wildcard core/*.cpp) $(wildcard util/*.cpp) $(wildcard network/*.cpp)
TEST = $(wildcard test/*.cpp) util/logger.cpp network/conn.cpp core/ptype.cpp core/proposer.cpp core/acceptor.cpp

# All .o files go to build dir.
OBJ = $(CPP:%.cpp=$(BUILD_DIR)/%.o)
TESTOBJ = $(TEST:%.cpp=$(BUILD_DIR)/%.o)

# Gcc/Clang will create these .d files containing dependencies.
DEP = $(OBJ:%.o=%.d)
TESTDEP = $(TESTOBJ:%.o=%.d)

quarrellib=libqr.a

# Include all .d files
-include $(DEP)
-include $(TESTDEP)

# Actual target of the binary - depends on all .o files.
qr: $(OBJ)
	# Create build directories - same structure as sources.
	mkdir -p $(@D)
	ar -rcs ${quarrellib} $^

test: $(TESTOBJ)
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o quarreltest $^ -lpthread -lgtest -lgtest_main
	./quarreltest

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
	-rm $(DEP)
	-rm $(OBJ)
	-rm $(TESTOBJ)
	-rm $(TESTDEP)

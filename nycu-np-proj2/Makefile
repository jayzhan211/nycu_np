NP_SHELL_CMD_SRC = $(wildcard src/cmd/*.cpp)
NP_SHELL_CMD = $(patsubst src/cmd/%.cpp,working_dir/bin/%,$(NP_SHELL_CMD_SRC))

FILE_NAME = $(wildcard src/file/*)
FILE = $(patsubst src/file/%,working_dir/%,$(FILE_NAME))

BUILD_IN_CMD_NAME = ls cat wc
BUILD_IN_CMD = $(patsubst %,working_dir/bin/%,$(BUILD_IN_CMD_NAME))

# Flags passed to the C++ compiler.
CXXFLAGS += -std=c++17 -g -Wall -Wextra

# $@ The file name of the target of the rule
# $< The name of the first prerequisite
# $^ The names of all the prerequisites, with spaces between them

all: working_dir/np_simple working_dir/np_single_proc working_dir/np_multi_proc

working_dir/np_multi_proc: src/np_multi_proc.cpp working_dir/ $(FILE) $(NP_SHELL_CMD) $(BUILD_IN_CMD) working_dir/user_pipe
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

working_dir/np_single_proc: src/np_single_proc.cpp working_dir/ $(FILE) $(NP_SHELL_CMD) $(BUILD_IN_CMD) 
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

working_dir/np_simple: src/np_simple.cpp working_dir/ $(FILE) $(NP_SHELL_CMD) $(BUILD_IN_CMD) 
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

working_dir/bin/%: src/cmd/%.cpp | working_dir/bin/
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
	

TEST_CASE_START=1
working_dir/user_pipe: working_dir/
	mkdir -p working_dir/user_pipe/

working_dir/bin/%: /bin/%
	cp -f $< $@

working_dir/bin/: working_dir/
	mkdir -p working_dir/bin/

working_dir/%: src/file/%
	cp -r $< $@

working_dir/:
	mkdir -p working_dir


# Add this so 'make clean' is executed even file named 'clean' is exist.
.PHONY: clean np_simple
clean:
	rm -f $(NP_SHELL)
	rm -rf working_dir

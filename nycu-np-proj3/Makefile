CXXFLAGS=-std=c++17 -Wall -pedantic -pthread -lboost_system
CXX_INCLUDE_DIRS=/usr/local/include
CXX_INCLUDE_PARAMS=$(addprefix -I , $(CXX_INCLUDE_DIRS))
CXX_LIB_DIRS=/usr/local/lib
CXX_LIB_PARAMS=$(addprefix -L , $(CXX_LIB_DIRS))

NP_SHELL_CMD_SRC = $(wildcard src/cmd/*.cpp)
NP_SHELL_CMD = $(patsubst src/cmd/%.cpp,working_dir/bin/%,$(NP_SHELL_CMD_SRC))

TEST_CASE_FOLDER = $(wildcard src/test_case/*)
TEST_CASE = $(patsubst src/test_case/%,working_dir/test_case/%,$(TEST_CASE_FOLDER))

FILE_NAME = $(wildcard src/file/*)
FILE = $(patsubst src/file/%,working_dir/%,$(FILE_NAME))

CGI_NAME = $(wildcard src/cgi/*)
CGI = $(patsubst src/cgi/%,working_dir/%,$(CGI_NAME))

BUILD_IN_CMD_NAME = ls cat wc
BUILD_IN_CMD = $(patsubst %,working_dir/bin/%,$(BUILD_IN_CMD_NAME))

all: | setup http_server console

console: src/code/console.cpp
	$(CXX) src/code/console.cpp -o working_dir/console.cgi $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS)

http_server: src/code/http_server.cpp
	$(CXX) src/code/http_server.cpp -o working_dir/http_server $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS)

setup: | make_working_dir $(FILE) $(CGI) $(TEST_CASE) $(NP_SHELL_CMD) $(BUILD_IN_CMD) working_dir/np_single_golden

make_working_dir:
	mkdir -p working_dir
	mkdir -p working_dir/test_case
	mkdir -p working_dir/bin

working_dir/bin/%: src/cmd/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

working_dir/np_single_golden: src/np_single_golden
	cp -r $< $@

working_dir/bin/%: /bin/%
	cp -f $< $@

working_dir/%: src/file/% 
	cp -r $< $@

working_dir/test_case/%: src/test_case/% 
	cp -r $< $@

working_dir/%: src/cgi/%
	cp -r $< $@

working_dir/%: src/%
	cp -r $< $@

.PHONY: all clean
clean:
	rm -rf working_dir
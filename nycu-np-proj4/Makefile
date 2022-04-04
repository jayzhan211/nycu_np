CXXFLAGS = -std=c++17 -I./include -Wall -pedantic -pthread -lboost_system

# CXXFLAGS = -std=c++17 -I./include -Wall -O2

CXX_INCLUDE_DIRS=/usr/local/include
CXX_INCLUDE_PARAMS=$(addprefix -I , $(CXX_INCLUDE_DIRS))
CXX_LIB_DIRS=/usr/local/lib
CXX_LIB_PARAMS=$(addprefix -L , $(CXX_LIB_DIRS))

SOURCES_SOCKS = $(wildcard src/*.cpp)

OBJS_SOCKS = $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SOURCES_SOCKS)))))


LIBS = -lpthread -lboost_system -lboost_filesystem

EXE = socks_server hw4.cgi
OBJ_DIR = obj

all: create_object_directory $(EXE)

$(OBJ_DIR)/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

hw4.cgi: src/console/console.cpp
	$(CXX) $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS) $< -o $@

socks_server: $(OBJS_SOCKS)
	$(CXX) $(CXXFLAGS) -o $@ $^

create_object_directory:
	mkdir -p $(OBJ_DIR)

.PHONY: all clean
clean:
	rm -rf $(EXE) $(OBJ_DIR)


# $< first prerequisite
# $^ all prerequisite
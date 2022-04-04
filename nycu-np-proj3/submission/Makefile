CXXFLAGS=-std=c++17 -Wall -pedantic -pthread -lboost_system

CXX_INCLUDE_DIRS=/usr/local/include
CXX_INCLUDE_PARAMS=$(addprefix -I , $(CXX_INCLUDE_DIRS))

CXX_LIB_DIRS=/usr/local/lib
CXX_LIB_PARAMS=$(addprefix -L , $(CXX_LIB_DIRS))


all: http_server console.cgi

console.cgi: console.cpp
	$(CXX) $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS) $< -o $@

http_server: http_server.cpp
	$(CXX) $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS) $< -o $@

.PHONY: all clean
clean:
	rm -f http_server console.cgi
# Flags passed to the C++ compiler.
CXXFLAGS += -std=c++17 -g -Wall -Wextra

# $@ The file name of the target of the rule
# $< The name of the first prerequisite
# $^ The names of all the prerequisites, with spaces between them

all: np_simple np_single_proc np_multi_proc

np_simple: np_simple.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

np_single_proc: np_single_proc.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

np_multi_proc: np_multi_proc.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

# Add this so 'make clean' is executed even file named 'clean' is exist.
.PHONY: clean np_simple
clean:
	rm -f np_simple np_single_proc np_multi_proc

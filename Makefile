CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -pthread
INCLUDES = -Iinclude

SRCS = src/lockfree_skiplist.cpp tests/test_concurrent.cpp
BIN = build/test_concurrent

.PHONY: all test clean tsan asan

all: $(BIN)

$(BIN): $(SRCS) include/lockfree_skiplist.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $@

test: $(BIN)
	./$(BIN)

tsan:
	@mkdir -p build
	$(CXX) -std=c++17 -O1 -g -fsanitize=thread -pthread $(INCLUDES) $(SRCS) -o build/test_tsan
	TSAN_OPTIONS="halt_on_error=0" ./build/test_tsan

asan:
	@mkdir -p build
	$(CXX) -std=c++17 -O1 -g -fsanitize=address -pthread $(INCLUDES) $(SRCS) -o build/test_asan
	./build/test_asan

clean:
	rm -rf build

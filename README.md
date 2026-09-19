# lockfree-skiplist

A header-friendly, lock-free concurrent skip list for C++17, based on the
Herlihy–Shavit algorithm. Originally extracted from a trading-system codebase.

## Features

- Lock-free `insert` / `find` / `remove` using `std::atomic` + CAS loops
- Configurable max level, fixed 1/2 probabilistic level promotion
- `marked` + `fullyLinked` lazy deletion markers
- No external dependencies — standard C++17 only
- Build with `clang++` / `g++` / MSVC; tested on macOS arm64

## Quick start

```bash
make          # build
make test     # run concurrent test (4 threads × 1000 ops)
make tsan     # run under ThreadSanitizer
make asan     # run under AddressSanitizer
```

## Usage

```cpp
#include "lockfree_skiplist.h"

int cmp(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

LockFreeSkipList list(/*maxLevel=*/16, cmp);

int k = 42;
list.insert(&k);        // returns SkipListNode*
list.find(&k);          // returns SkipListNode* or nullptr
list.remove(&k);
```

The list stores opaque `const void*` keys; the caller owns the key storage
and must keep it alive while the list holds it.

## Verified

- **ThreadSanitizer**: clean (no data races)
- **AddressSanitizer**: clean (no UAF / OOB)
- **Stress test**: 10/10 runs, 4 threads × 1000 ops insert/find/remove

## Layout

```
include/lockfree_skiplist.h   public API
src/lockfree_skiplist.cpp    implementation
tests/test_concurrent.cpp     threaded smoke test
```

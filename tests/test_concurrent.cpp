// 并发测试：4 线程 insert / find / remove 各 1000 次
#include "lockfree_skiplist.h"

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

static int compareInt(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

// 值放在全局数组，保证跳表持有期间指针有效
static int values[4000];

static void concurrentInsert(LockFreeSkipList& list, int start, int end) {
    for (int i = start; i < end; ++i) {
        values[i] = i;
        list.insert(&values[i]);
    }
}

static void concurrentFind(LockFreeSkipList& list, int start, int end) {
    for (int i = start; i < end; ++i) {
        assert(list.find(&values[i]) != nullptr);
    }
}

static void concurrentRemove(LockFreeSkipList& list, int start, int end) {
    for (int i = start; i < end; ++i) {
        list.remove(&values[i]);
    }
}

int main() {
    const int kThreads = 4;
    const int kOps = 1000;

    LockFreeSkipList list(16, compareInt);

    // 并发插入
    {
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back(concurrentInsert, std::ref(list),
                            t * kOps, (t + 1) * kOps);
        }
        for (auto& t : ts) t.join();
    }
    for (int i = 0; i < kThreads * kOps; ++i) {
        assert(list.find(&values[i]) != nullptr);
    }
    std::cout << "[PASS] concurrent insert, size=" << list.size() << std::endl;

    // 并发查找
    {
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back(concurrentFind, std::ref(list),
                            t * kOps, (t + 1) * kOps);
        }
        for (auto& t : ts) t.join();
    }
    std::cout << "[PASS] concurrent find" << std::endl;

    // 并发删除
    {
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back(concurrentRemove, std::ref(list),
                            t * kOps, (t + 1) * kOps);
        }
        for (auto& t : ts) t.join();
    }
    for (int i = 0; i < kThreads * kOps; ++i) {
        assert(list.find(&values[i]) == nullptr);
    }
    std::cout << "[PASS] concurrent remove, size=" << list.size() << std::endl;

    std::cout << "ALL TESTS PASSED" << std::endl;
    return 0;
}

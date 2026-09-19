#include "lockfree_skiplist.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

static int cmp(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

int main() {
    const int N = 100000;
    const int THREADS[] = {1, 2, 4, 8};
    std::vector<int> vals(N);
    for (int i = 0; i < N; i++) vals[i] = i;

    printf("%-8s %-12s %-12s %-12s\n", "threads", "insert(s)", "find(s)", "remove(s)");
    printf("-------- ---------- ---------- ----------\n");

    for (int t : THREADS) {
        LockFreeSkipList list(16, cmp);

        // insert
        auto t0 = std::chrono::high_resolution_clock::now();
        {
            std::vector<std::thread> ts;
            int per = N / t;
            for (int i = 0; i < t; i++) {
                int start = i * per;
                int end = (i == t-1) ? N : (i+1)*per;
                ts.emplace_back([&, start, end]() {
                    for (int j = start; j < end; j++) list.insert(&vals[j]);
                });
            }
            for (auto& th : ts) th.join();
        }
        auto t1 = std::chrono::high_resolution_clock::now();

        // find
        {
            std::vector<std::thread> ts;
            int per = N / t;
            for (int i = 0; i < t; i++) {
                int start = i * per;
                int end = (i == t-1) ? N : (i+1)*per;
                ts.emplace_back([&, start, end]() {
                    for (int j = start; j < end; j++) list.find(&vals[j]);
                });
            }
            for (auto& th : ts) th.join();
        }
        auto t2 = std::chrono::high_resolution_clock::now();

        // remove
        {
            std::vector<std::thread> ts;
            int per = N / t;
            for (int i = 0; i < t; i++) {
                int start = i * per;
                int end = (i == t-1) ? N : (i+1)*per;
                ts.emplace_back([&, start, end]() {
                    for (int j = start; j < end; j++) list.remove(&vals[j]);
                });
            }
            for (auto& th : ts) th.join();
        }
        auto t3 = std::chrono::high_resolution_clock::now();

        double ins = std::chrono::duration<double>(t1-t0).count();
        double fin = std::chrono::duration<double>(t2-t1).count();
        double rem = std::chrono::duration<double>(t3-t2).count();
        printf("%-8d %-12.3f %-12.3f %-12.3f\n", t, ins, fin, rem);
    }
    return 0;
}

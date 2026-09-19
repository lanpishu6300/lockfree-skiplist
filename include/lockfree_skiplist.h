#pragma once

#include <atomic>
#include <vector>
#include <random>
#include <cstddef>
#include <mutex>

// 用户提供的比较函数：a < b 返回负数，a == b 返回 0，a > b 返回正数
typedef int (*TCompareFunc)(const void* a, const void* b);

// 跳表节点（无锁）
struct SkipListNode {
    const void* value;
    int level;  // 实际层数（forward 的有效下标范围 0..level）
    std::vector<std::atomic<SkipListNode*>> forward;
    std::atomic<bool> marked;        // 逻辑删除标记
    std::atomic<bool> fullyLinked;  // 是否已完全链接到所有层

    SkipListNode(int lvl, const void* val = nullptr)
        : value(val), level(lvl), forward(lvl + 1), marked(false), fullyLinked(false) {
        for (auto& ptr : forward) {
            ptr.store(nullptr, std::memory_order_relaxed);
        }
    }
};

// 无锁跳表（Herlihy-Shavit 算法，带 epoch-based 内存回收）
//
// 线程安全：insert / find / remove 可多线程并发调用。
// 使用前调用 enterThread()，结束后调用 leaveThread()，
// 否则被 remove 的节点可能被其他正在遍历的线程访问到。
class LockFreeSkipList {
public:
    LockFreeSkipList(int maxLevel, TCompareFunc compareFunc);
    ~LockFreeSkipList();

    // 线程注册：每个访问跳表的线程必须在开始时调用 enterThread()，
    // 结束时调用 leaveThread()。这是安全内存回收的前提。
    static void enterThread();
    static void leaveThread();

    // 插入一个 key（value 指针由调用方保证生命周期）。
    // 已存在相同 key 时返回 nullptr，否则返回新节点。
    SkipListNode* insert(const void* value);

    // 查找等于 value 的节点；不存在返回 nullptr。
    SkipListNode* find(const void* value) const;

    // 删除等于 value 的节点。
    void remove(const void* value);
    void remove(SkipListNode* node);

    // 清空跳表（不是并发安全的，需在没有其他线程访问时调用）
    void clear();

    size_t size() const { return size_.load(std::memory_order_relaxed); }

    // 顺序遍历辅助
    SkipListNode* findMin() const;
    SkipListNode* findMax() const;
    SkipListNode* getNextNode(SkipListNode* node) const;

private:
    int maxLevel_;
    TCompareFunc compareFunc_;
    SkipListNode* head_;
    SkipListNode* tail_;
    std::atomic<size_t> size_;

    int randomLevel() const;
    SkipListNode* createNode(int level, const void* value);
    bool findNode(const void* value,
                  std::vector<SkipListNode*>& preds,
                  std::vector<SkipListNode*>& succs) const;

    // 安全内存回收（EBR 简化版）
    void retireNode(SkipListNode* node);
    void tryReclaim();

    // EBR 全局状态
    struct EBRState {
        std::atomic<uint64_t> global_epoch{0};
        struct ThreadInfo {
            std::atomic<bool> active{false};
            std::atomic<uint64_t> epoch{0};
        };
        static constexpr int MAX_THREADS = 64;
        ThreadInfo threads[MAX_THREADS];
        std::atomic<int> next_tid{0};
    };

    struct RetiredNode {
        SkipListNode* node;
        uint64_t epoch;
    };

    static EBRState& ebr();
    static std::mutex retired_mutex_;
    static std::vector<RetiredNode> retired_;
    static thread_local int t_tid;
};

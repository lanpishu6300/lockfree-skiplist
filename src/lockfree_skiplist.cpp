#include "lockfree_skiplist.h"

#include <iostream>
#include <algorithm>

// ---------------------------------------------------------------------------
// EBR static state
// ---------------------------------------------------------------------------
LockFreeSkipList::EBRState& LockFreeSkipList::ebr() {
    static EBRState state;
    return state;
}

std::mutex LockFreeSkipList::retired_mutex_;
std::vector<LockFreeSkipList::RetiredNode> LockFreeSkipList::retired_;
thread_local int LockFreeSkipList::t_tid = -1;

void LockFreeSkipList::enterThread() {
    auto& e = ebr();
    if (t_tid < 0) {
        t_tid = e.next_tid.fetch_add(1, std::memory_order_relaxed);
    }
    auto& info = e.threads[t_tid];
    info.epoch.store(e.global_epoch.load(std::memory_order_relaxed),
                     std::memory_order_relaxed);
    info.active.store(true, std::memory_order_release);
}

void LockFreeSkipList::leaveThread() {
    if (t_tid < 0) return;
    ebr().threads[t_tid].active.store(false, std::memory_order_release);
}

namespace {
// RAII 守卫：每次操作自动进入/离开 EBR 临界区
// 这样线程 epoch 随每次操作刷新，tryReclaim 才能安全回收
struct EbrGuard {
    EbrGuard()  { LockFreeSkipList::enterThread(); }
    ~EbrGuard() { LockFreeSkipList::leaveThread(); }
};
}

void LockFreeSkipList::retireNode(SkipListNode* node) {
    auto& e = ebr();
    uint64_t epoch = e.global_epoch.fetch_add(1, std::memory_order_relaxed);
    bool needReclaim = false;
    {
        std::lock_guard<std::mutex> lock(retired_mutex_);
        retired_.push_back({node, epoch});
        needReclaim = (retired_.size() > 64);
    }
    if (needReclaim) {
        tryReclaim();
    }
}

void LockFreeSkipList::tryReclaim() {
    auto& e = ebr();
    // 找所有活跃线程中最小的 epoch
    // 所有 epoch < minEpoch 的节点都已安全回收
    uint64_t minEpoch = UINT64_MAX;
    int activeCount = 0;
    int nthreads = e.next_tid.load(std::memory_order_acquire);
    for (int i = 0; i < nthreads; ++i) {
        if (e.threads[i].active.load(std::memory_order_acquire)) {
            activeCount++;
            uint64_t ep = e.threads[i].epoch.load(std::memory_order_acquire);
            if (ep < minEpoch) minEpoch = ep;
        }
    }
    if (activeCount == 0) minEpoch = UINT64_MAX;

    std::lock_guard<std::mutex> lock(retired_mutex_);
    if (retired_.empty()) return;
    std::vector<RetiredNode> keep;
    keep.reserve(retired_.size());
    for (auto& r : retired_) {
        if (r.epoch < minEpoch) {
            delete r.node;
        } else {
            keep.push_back(r);
        }
    }
    retired_.swap(keep);
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------
LockFreeSkipList::LockFreeSkipList(int maxLevel, TCompareFunc compareFunc)
    : maxLevel_(maxLevel), compareFunc_(compareFunc), size_(0) {
    head_ = createNode(maxLevel_, nullptr);
    tail_ = createNode(maxLevel_, nullptr);
    for (int i = 0; i <= maxLevel_; ++i) {
        head_->forward[i].store(tail_, std::memory_order_relaxed);
    }
}

LockFreeSkipList::~LockFreeSkipList() {
    clear();
    // 强制回收所有 retired 节点
    {
        std::lock_guard<std::mutex> lock(retired_mutex_);
        for (auto& r : retired_) delete r.node;
        retired_.clear();
    }
    delete head_;
    delete tail_;
}

SkipListNode* LockFreeSkipList::createNode(int level, const void* value) {
    return new SkipListNode(level, value);
}

// ---------------------------------------------------------------------------
// randomLevel
// ---------------------------------------------------------------------------
int LockFreeSkipList::randomLevel() const {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_real_distribution<> dis(0.0, 1.0);

    int level = 1;
    while (dis(gen) < 0.5 && level < maxLevel_) {
        level++;
    }
    return level;
}

// ---------------------------------------------------------------------------
// insert
// ---------------------------------------------------------------------------
SkipListNode* LockFreeSkipList::insert(const void* value) {
    EbrGuard guard;
    std::vector<SkipListNode*> preds(maxLevel_ + 1);
    std::vector<SkipListNode*> succs(maxLevel_ + 1);

    while (true) {
        if (findNode(value, preds, succs)) {
            return nullptr;
        }

        int topLevel = randomLevel();
        SkipListNode* newNode = createNode(topLevel, value);

        for (int i = 0; i < topLevel; ++i) {
            newNode->forward[i].store(succs[i], std::memory_order_relaxed);
        }

        SkipListNode* pred = preds[0];
        SkipListNode* succ = succs[0];

        if (!pred->forward[0].compare_exchange_strong(
                succ, newNode,
                std::memory_order_release, std::memory_order_relaxed)) {
            delete newNode;
            continue;
        }

        for (int i = 1; i < topLevel; ++i) {
            while (true) {
                pred = preds[i];
                succ = succs[i];
                // 更新 newNode 的 forward 指向最新的 succ
                newNode->forward[i].store(succ, std::memory_order_relaxed);
                if (pred->forward[i].compare_exchange_strong(
                        succ, newNode,
                        std::memory_order_release, std::memory_order_relaxed)) {
                    break;
                }
                findNode(value, preds, succs);
            }
        }

        newNode->fullyLinked.store(true, std::memory_order_release);
        size_.fetch_add(1, std::memory_order_relaxed);
        return newNode;
    }
}

// ---------------------------------------------------------------------------
// find
// ---------------------------------------------------------------------------
SkipListNode* LockFreeSkipList::find(const void* value) const {
    EbrGuard guard;
    SkipListNode* curr = head_;
    for (int i = maxLevel_; i >= 0; --i) {
        while (true) {
            SkipListNode* next = curr->forward[i].load(std::memory_order_acquire);
            // 跳过 marked 节点
            while (next != tail_ && next->marked.load(std::memory_order_acquire)) {
                next = next->forward[i].load(std::memory_order_acquire);
            }
            if (next == tail_ || compareFunc_(next->value, value) >= 0) {
                break;
            }
            curr = next;
        }
    }
    curr = curr->forward[0].load(std::memory_order_acquire);
    while (curr != tail_ && curr->marked.load(std::memory_order_acquire)) {
        curr = curr->forward[0].load(std::memory_order_acquire);
    }
    if (curr != tail_ && compareFunc_(curr->value, value) == 0) {
        return curr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------
void LockFreeSkipList::remove(const void* value) {
    EbrGuard guard;
    std::vector<SkipListNode*> preds(maxLevel_ + 1);
    std::vector<SkipListNode*> succs(maxLevel_ + 1);

    while (true) {
        if (!findNode(value, preds, succs)) {
            return;
        }

        SkipListNode* nodeToRemove = succs[0];
        if (!nodeToRemove->fullyLinked.load(std::memory_order_acquire) ||
            nodeToRemove->marked.load(std::memory_order_acquire)) {
            return;
        }

        // 逻辑删除
        bool expected = false;
        if (!nodeToRemove->marked.compare_exchange_strong(
                expected, true,
                std::memory_order_release, std::memory_order_relaxed)) {
            continue;
        }

        // 逐层物理摘除（节点链接在 0..level-1，level 是 1-based 层数）
        for (int i = nodeToRemove->level - 1; i >= 0; --i) {
            while (true) {
                SkipListNode* pred = preds[i];
                SkipListNode* succ = succs[i];
                SkipListNode* nextNode = nodeToRemove->forward[i].load(std::memory_order_acquire);
                if (pred->forward[i].compare_exchange_strong(
                        succ, nextNode,
                        std::memory_order_release, std::memory_order_relaxed)) {
                    break;
                }
                // 重新查找；如果找不到节点，说明已被其他线程摘除
                if (!findNode(value, preds, succs)) {
                    return;
                }
            }
        }

        size_.fetch_sub(1, std::memory_order_relaxed);
        retireNode(nodeToRemove);
        return;
    }
}

void LockFreeSkipList::remove(SkipListNode* node) {
    if (!node) return;
    remove(node->value);
}

// ---------------------------------------------------------------------------
// clear / traversal
// ---------------------------------------------------------------------------
void LockFreeSkipList::clear() {
    SkipListNode* curr = head_->forward[0].load(std::memory_order_acquire);
    while (curr != tail_) {
        SkipListNode* next = curr->forward[0].load(std::memory_order_acquire);
        delete curr;
        curr = next;
    }
    for (int i = 0; i <= maxLevel_; ++i) {
        head_->forward[i].store(tail_, std::memory_order_relaxed);
    }
    size_.store(0, std::memory_order_relaxed);
}

SkipListNode* LockFreeSkipList::findMin() const {
    SkipListNode* curr = head_->forward[0].load(std::memory_order_acquire);
    while (curr != tail_ && curr->marked.load(std::memory_order_acquire)) {
        curr = curr->forward[0].load(std::memory_order_acquire);
    }
    return (curr != tail_) ? curr : nullptr;
}

SkipListNode* LockFreeSkipList::findMax() const {
    SkipListNode* curr = head_;
    for (int i = maxLevel_; i >= 0; --i) {
        while (true) {
            SkipListNode* next = curr->forward[i].load(std::memory_order_acquire);
            while (next != tail_ && next->marked.load(std::memory_order_acquire)) {
                next = next->forward[i].load(std::memory_order_acquire);
            }
            if (next == tail_) break;
            curr = next;
        }
    }
    return (curr != head_) ? curr : nullptr;
}

SkipListNode* LockFreeSkipList::getNextNode(SkipListNode* node) const {
    if (!node) return nullptr;
    SkipListNode* next = node->forward[0].load(std::memory_order_acquire);
    while (next != tail_ && next->marked.load(std::memory_order_acquire)) {
        next = next->forward[0].load(std::memory_order_acquire);
    }
    return (next != tail_) ? next : nullptr;
}

// ---------------------------------------------------------------------------
// findNode：Herlihy-Shavit search，跳过 marked 节点
// ---------------------------------------------------------------------------
bool LockFreeSkipList::findNode(const void* value,
                                std::vector<SkipListNode*>& preds,
                                std::vector<SkipListNode*>& succs) const {
    SkipListNode* pred = head_;
    for (int level = maxLevel_; level >= 0; --level) {
        SkipListNode* curr = pred->forward[level].load(std::memory_order_acquire);

        while (true) {
            if (curr == tail_ || curr == nullptr) break;
            SkipListNode* next = curr->forward[level].load(std::memory_order_acquire);
            if (compareFunc_(curr->value, value) < 0) {
                pred = curr;
                curr = next;
            } else {
                break;
            }
        }
        preds[level] = pred;
        succs[level] = curr;
    }

    SkipListNode* curr = succs[0];
    return curr != tail_ && compareFunc_(curr->value, value) == 0;
}

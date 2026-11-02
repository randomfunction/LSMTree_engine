#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

template <typename Key, typename Comparator>
class SkipList {
private:
    enum { kMaxHeight = 12 };

    struct Node {
        explicit Node(const Key& k) : key(k) {
            for (int i = 0; i < kMaxHeight; ++i) {
                next[i].store(nullptr, std::memory_order_relaxed);
            }
        }

        Key const key;
        std::atomic<Node*> next[kMaxHeight];

        Node* Next(int n) { return next[n].load(std::memory_order_acquire); }
        void SetNext(int n, Node* x) { next[n].store(x, std::memory_order_release); }
        Node* NoBarrier_Next(int n) { return next[n].load(std::memory_order_relaxed); }
        void NoBarrier_SetNext(int n, Node* x) {
            next[n].store(x, std::memory_order_relaxed);
        }
    };

public:
    explicit SkipList(Comparator comparator)
        : compare_(comparator),
          head_(new Node(Key())),
          max_height_(1),
          rnd_(0x12345678u) {}

    ~SkipList() {
        Node* node = head_;
        while (node != nullptr) {
            Node* next = node->NoBarrier_Next(0);
            delete node;
            node = next;
        }
    }

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;

    void Insert(const Key& key) {
        Node* prev[kMaxHeight];
        FindGreaterOrEqual(key, prev);

        int height = RandomHeight();
        if (height > GetMaxHeight()) {
            for (int i = GetMaxHeight(); i < height; ++i) {
                prev[i] = head_;
            }
            max_height_.store(height, std::memory_order_relaxed);
        }

        Node* node = new Node(key);
        for (int i = 0; i < height; ++i) {
            node->NoBarrier_SetNext(i, prev[i]->NoBarrier_Next(i));
            prev[i]->SetNext(i, node);
        }
    }

    bool Contains(const Key& key) const {
        Node* node = FindGreaterOrEqual(key, nullptr);
        return node != nullptr && compare_(node->key, key) == 0;
    }

    class Iterator {
    public:
        explicit Iterator(const SkipList* list) : list_(list), node_(nullptr) {}

        bool Valid() const { return node_ != nullptr; }
        const Key& key() const { return node_->key; }
        void Next() { node_ = node_->Next(0); }
        void SeekToFirst() { node_ = list_->head_->Next(0); }
        void Seek(const Key& target) {
            node_ = list_->FindGreaterOrEqual(target, nullptr);
        }

    private:
        const SkipList* list_;
        Node* node_;
    };

private:
    int GetMaxHeight() const {
        return max_height_.load(std::memory_order_relaxed);
    }

    uint32_t NextRandom() {
        rnd_ = rnd_ * 1103515245u + 12345u;
        return (rnd_ >> 16) & 0x7fffu;
    }

    int RandomHeight() {
        static const unsigned int kBranching = 4;
        int height = 1;
        while (height < kMaxHeight && (NextRandom() % kBranching) == 0) {
            ++height;
        }
        return height;
    }

    bool KeyIsAfterNode(const Key& key, Node* node) const {
        return node != nullptr && compare_(node->key, key) < 0;
    }

    Node* FindGreaterOrEqual(const Key& key, Node** prev) const {
        Node* x = head_;
        int level = GetMaxHeight() - 1;
        while (true) {
            Node* next = x->Next(level);
            if (KeyIsAfterNode(key, next)) {
                x = next;
            } else {
                if (prev != nullptr) {
                    prev[level] = x;
                }
                if (level == 0) {
                    return next;
                }
                --level;
            }
        }
    }

    Comparator const compare_;
    Node* const head_;
    std::atomic<int> max_height_;
    uint32_t rnd_;
};

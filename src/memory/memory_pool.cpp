#include "playground/memory_pool.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <stdexcept>

namespace playground {

namespace {

constexpr std::size_t kMinBlockSize = sizeof(void*);
constexpr std::size_t kAlignment = alignof(std::max_align_t);

std::size_t align_up(std::size_t n, std::size_t align) {
    return (n + align - 1) & ~(align - 1);
}

}  // namespace

MemoryPool::MemoryPool(std::size_t block_size, std::size_t block_count)
    : block_size_(align_up(std::max(block_size, kMinBlockSize), kAlignment)),
      block_count_(block_count),
      free_count_(block_count),
      storage_(nullptr),
      free_head_(nullptr) {
    if (block_count_ == 0) {
        throw std::invalid_argument("MemoryPool: block_count must be > 0");
    }
    storage_ = static_cast<char*>(
        ::operator new(block_size_ * block_count_, std::align_val_t(kAlignment)));
    // 逐块串成空闲链表（头插，最后 free_head_ 指向第一块）
    for (std::size_t i = 0; i < block_count_; ++i) {
        auto* node = as_node(storage_ + i * block_size_);
        node->next = free_head_;
        free_head_ = node;
    }
}

MemoryPool::~MemoryPool() {
    ::operator delete(storage_, std::align_val_t(kAlignment));
}

void* MemoryPool::allocate() {
    if (free_head_ == nullptr) {
        return nullptr;
    }
    FreeNode* node = free_head_;
    free_head_ = node->next;
    --free_count_;
    return node;
}

void MemoryPool::deallocate(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
    // 练习：校验 ptr 确实在 [storage_, storage_ + block_size_ * block_count_) 内，
    // 以及没有被重复归还
    auto* node = as_node(ptr);
    node->next = free_head_;
    free_head_ = node;
    ++free_count_;
}

}  // namespace playground

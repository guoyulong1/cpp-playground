#pragma once

#include <cstddef>

namespace playground {

// 固定块大小的内存池：
//   构造时一次性向系统申请整块内存，之后 allocate/deallocate 只在空闲链表上
//   摘取/归还节点，不再走全局 new/delete。
// 练习重点：内存对齐、空闲链表、RAII（析构统一释放）。
class MemoryPool {
public:
    MemoryPool(std::size_t block_size, std::size_t block_count);
    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // 返回一块可用的内存；池满时返回 nullptr（练习：改成抛 std::bad_alloc）
    void* allocate();

    // 归还之前 allocate 出去的块；nullptr 直接忽略
    void deallocate(void* ptr) noexcept;

    std::size_t block_size() const noexcept { return block_size_; }
    std::size_t capacity() const noexcept { return block_count_; }
    std::size_t free_blocks() const noexcept { return free_count_; }

private:
    // 空闲块自身内存被复用来存 next 指针，这也是块大小有下限的原因
    struct FreeNode {
        FreeNode* next;
    };

    static FreeNode* as_node(void* ptr) noexcept {
        return static_cast<FreeNode*>(ptr);
    }

    std::size_t block_size_;    // 按 max_align_t 对齐后的块大小
    std::size_t block_count_;
    std::size_t free_count_;
    char* storage_;             // 整块 buffer，析构时统一释放
    FreeNode* free_head_;
};

}  // namespace playground

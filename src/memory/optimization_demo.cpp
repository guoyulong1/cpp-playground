// 内存优化演示（是 src/memory/ 内存管理主题的进阶篇）：
//   1. 结构体布局：同样的成员不同顺序，sizeof 相差 1/3（对齐与填充）
//   2. 分配次数：vector 不 reserve（约 18 次扩容）vs reserve（1 次）
//   3. SSO：短字符串存在 string 对象内部（0 次堆分配），长字符串才上堆
//   4. 池化：逐块 new/delete vs 已有的 MemoryPool（空闲链表 O(1) 分配）
// 手法：重载全局 operator new/delete 数一数堆分配次数，「看到」而不是「背下」这些结论。
// 更多优化主题（缓存友好、pmr、工具链）见 docs/memory-optimization.md。
#include <new>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "playground/memory_pool.hpp"

namespace {

std::size_t g_alloc_count = 0;  // 只在单线程 demo 里使用

}  // namespace

// 替换全局 new/delete：malloc/free + 计数。真实工程别这么干，这里是教学观测手段。
void* operator new(std::size_t size) {
    ++g_alloc_count;
    void* ptr = std::malloc(size == 0 ? 1 : size);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void* operator new(std::size_t size, std::align_val_t align) {
    ++g_alloc_count;
    const auto alignment = static_cast<std::size_t>(align);
    const std::size_t rounded =
        (size + alignment - 1) & ~(alignment - 1);  // aligned_alloc 要求大小是对齐值的倍数
    void* ptr = std::aligned_alloc(alignment, rounded == 0 ? alignment : rounded);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](std::size_t size, std::align_val_t align) {
    return ::operator new(size, align);
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t size) noexcept {
    static_cast<void>(size);
    std::free(ptr);
}
void operator delete(void* ptr, std::align_val_t alignment) noexcept {
    static_cast<void>(alignment);
    std::free(ptr);
}
void operator delete(void* ptr, std::size_t size, std::align_val_t alignment) noexcept {
    static_cast<void>(size);
    static_cast<void>(alignment);
    std::free(ptr);
}

namespace {

struct BadLayout {
    char tag;     // 后面补 7 字节填充，double 才能落在 8 对齐的位置
    double value;
    char flag;    // 后面补 3 字节填充
    int count;
};

struct GoodLayout {
    double value;  // 成员按对齐要求从大到小排，填充最少
    int count;
    char tag;
    char flag;
};

static_assert(sizeof(GoodLayout) < sizeof(BadLayout), "重排成员应显著减小体积");

void layout_demo() {
    std::cout << "== 1. 结构体布局 ==\n";
    std::cout << "    BadLayout  (char,double,char,int): sizeof = " << sizeof(BadLayout)
              << "，alignof = " << alignof(BadLayout) << '\n';
    std::cout << "    GoodLayout (double,int,char,char): sizeof = " << sizeof(GoodLayout)
              << "，alignof = " << alignof(GoodLayout) << '\n';
    std::cout << "    一百万个对象就能省 " << (sizeof(BadLayout) - sizeof(GoodLayout)) * 1000000
              << " 字节\n";
}

void vector_demo() {
    std::cout << "\n== 2. vector 扩容：直接 push_back vs 先 reserve ==\n";
    constexpr std::size_t kCount = 100000;

    g_alloc_count = 0;
    {
        std::vector<int> v;
        for (std::size_t i = 0; i < kCount; ++i) {
            v.push_back(static_cast<int>(i));  // 容量不够就整块搬家
        }
        std::cout << "    不 reserve: " << g_alloc_count << " 次堆分配\n";
    }

    g_alloc_count = 0;
    {
        std::vector<int> v;
        v.reserve(kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            v.push_back(static_cast<int>(i));
        }
        std::cout << "    先 reserve: " << g_alloc_count << " 次堆分配\n";
    }
}

void sso_demo() {
    std::cout << "\n== 3. SSO（短字符串优化）==\n";
    g_alloc_count = 0;
    {
        const std::string short_str = "short";  // 15 字符以内：直接存在对象内部缓冲里
        static_cast<void>(short_str);
    }
    std::cout << "    \"short\"（5 字符）: " << g_alloc_count << " 次堆分配\n";

    g_alloc_count = 0;
    {
        const std::string long_str = "this string is definitely longer than fifteen bytes";
        static_cast<void>(long_str);
    }
    std::cout << "    53 字符长串: " << g_alloc_count
              << " 次堆分配（libstdc++ 的 SSO 容量是 15）\n";
}

class Stopwatch {
public:
    void reset() { start_ = clock::now(); }
    double ms() const {
        return std::chrono::duration<double, std::milli>(clock::now() - start_).count();
    }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point start_ = clock::now();
};

void pool_demo() {
    std::cout << "\n== 4. 逐块 new/delete vs MemoryPool ==\n";
    constexpr std::size_t kBlocks = 1000000;
    constexpr std::size_t kBlockSize = sizeof(std::int64_t);
    std::vector<void*> blocks(kBlocks);

    Stopwatch watch;
    watch.reset();
    for (std::size_t i = 0; i < kBlocks; ++i) {
        blocks[i] = ::operator new(kBlockSize);  // 每块都走一次全局分配器
    }
    for (std::size_t i = 0; i < kBlocks; ++i) {
        ::operator delete(blocks[i]);
    }
    const double heap_ms = watch.ms();

    playground::MemoryPool pool(kBlockSize, kBlocks);  // 一次性向系统要 16MB
    watch.reset();
    for (std::size_t i = 0; i < kBlocks; ++i) {
        blocks[i] = pool.allocate();  // 空闲链表上摘一个节点，O(1)
    }
    for (std::size_t i = 0; i < kBlocks; ++i) {
        pool.deallocate(blocks[i]);
    }
    const double pool_ms = watch.ms();

    std::cout << "    全局 new/delete: " << heap_ms << " ms\n";
    std::cout << "    MemoryPool     : " << pool_ms << " ms（加速 " << heap_ms / pool_ms
              << "x；代价是块大小固定、整块内存常驻）\n";
}

}  // namespace

int main() {
    layout_demo();
    vector_demo();
    sso_demo();
    pool_demo();
    std::cout << "\n提示: Debug 构建差距偏大，用 release preset 重跑看真实数量级\n";
    return 0;
}

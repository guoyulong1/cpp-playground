#include "playground/memory_pool.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

TEST(MemoryPoolTest, AllocateAndDeallocate) {
    playground::MemoryPool pool(sizeof(std::int64_t), 4);
    void* block = pool.allocate();
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(pool.free_blocks(), 3u);

    pool.deallocate(block);
    EXPECT_EQ(pool.free_blocks(), 4u);
}

TEST(MemoryPoolTest, PoolExhaustsThenRecovers) {
    playground::MemoryPool pool(sizeof(std::int64_t), 2);
    void* first = pool.allocate();
    void* second = pool.allocate();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(pool.allocate(), nullptr);  // 池已用尽

    pool.deallocate(first);
    EXPECT_NE(pool.allocate(), nullptr);  // 归还的块可被复用
}

TEST(MemoryPoolTest, BlocksAreWritableAndAligned) {
    playground::MemoryPool pool(sizeof(std::int64_t), 1);
    auto* value = static_cast<std::int64_t*>(pool.allocate());
    ASSERT_NE(value, nullptr);
    *value = 42;
    EXPECT_EQ(*value, 42);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(value) % alignof(std::max_align_t),
              0u);
}

TEST(MemoryPoolTest, SmallBlockSizeIsRoundedUp) {
    // 小于一个指针的请求也会被放大到至少 sizeof(void*) 并对齐
    playground::MemoryPool pool(1, 2);
    EXPECT_GE(pool.block_size(), sizeof(void*));
    EXPECT_EQ(pool.capacity(), 2u);
    EXPECT_EQ(pool.free_blocks(), 2u);
}

TEST(MemoryPoolTest, ZeroBlockCountThrows) {
    EXPECT_THROW(playground::MemoryPool(sizeof(int), 0), std::invalid_argument);
}

}  // namespace

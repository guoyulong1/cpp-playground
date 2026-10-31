// 算法改良演示：同一件事的「朴素写法」与「改良写法」各跑一遍，计时并校验结果一致。
//   1. 降低复杂度：两数之和 O(n²) 暴力双循环 -> O(n) 哈希表一次扫描
//   2. 减少分配：循环内 string += 触发多次扩容拷贝 -> 先 reserve 再追加
//   3. 缓存友好：同为 O(n²) 的矩阵求和，按行访问 vs 按列访问
// 注意：Debug 构建差距会被放大且不稳定，看数量级请用 release preset：
//   cmake --preset release && cmake --build --preset release && ./build/release/algorithm_optimization_demo
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

class Stopwatch {
public:
    void reset() { start_ = clock::now(); }
    [[nodiscard]] double ms() const {
        return std::chrono::duration<double, std::milli>(clock::now() - start_).count();
    }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point start_ = clock::now();
};

void report(const std::string& name, double before_ms, double after_ms) {
    std::cout << "    " << name << ": 朴素 " << before_ms << " ms -> 改良 " << after_ms
              << " ms（加速 " << before_ms / after_ms << "x）\n";
}

// ---- 1. 两数之和：在 nums 中找和等于 target 的两个下标 ----

std::pair<std::size_t, std::size_t> two_sum_brute(const std::vector<int>& nums, int target) {
    for (std::size_t i = 0; i < nums.size(); ++i) {
        for (std::size_t j = i + 1; j < nums.size(); ++j) {
            if (nums[i] + nums[j] == target) {
                return {i, j};
            }
        }
    }
    return {0, 0};  // demo 数据保证一定有解
}

std::pair<std::size_t, std::size_t> two_sum_hash(const std::vector<int>& nums, int target) {
    std::unordered_map<int, std::size_t> seen;
    seen.reserve(nums.size());
    for (std::size_t i = 0; i < nums.size(); ++i) {
        const auto it = seen.find(target - nums[i]);
        if (it != seen.end()) {
            return {it->second, i};
        }
        seen.emplace(nums[i], i);
    }
    return {0, 0};
}

void two_sum_demo() {
    std::cout << "== 1. 两数之和：O(n^2) 暴力 vs O(n) 哈希 ==\n";
    constexpr std::size_t kCount = 20000;
    std::vector<int> nums(kCount);
    std::iota(nums.begin(), nums.end(), 1);
    std::mt19937 rng(42);
    std::shuffle(nums.begin(), nums.end(), rng);

    // 目标取「两最大值之和」（19999 + 20000）：这个和只有唯一一对解，
    // 暴力法必须扫完全程，比较才公平
    const auto max_pos = static_cast<std::size_t>(
        std::find(nums.begin(), nums.end(), static_cast<int>(kCount)) - nums.begin());
    std::swap(nums[kCount - 1], nums[max_pos]);
    const auto second_pos = static_cast<std::size_t>(
        std::find(nums.begin(), nums.end(), static_cast<int>(kCount) - 1) - nums.begin());
    std::swap(nums[kCount - 2], nums[second_pos]);
    const int target = nums[kCount - 1] + nums[kCount - 2];

    Stopwatch watch;
    watch.reset();
    const auto brute = two_sum_brute(nums, target);
    const double brute_ms = watch.ms();

    watch.reset();
    const auto hashed = two_sum_hash(nums, target);
    const double hash_ms = watch.ms();

    const int brute_sum = nums[brute.first] + nums[brute.second];
    const int hash_sum = nums[hashed.first] + nums[hashed.second];
    std::cout << "    正确性: " << (brute_sum == target && hash_sum == target ? "OK" : "FAIL")
              << '\n';
    report("查找", brute_ms, hash_ms);
}

// ---- 2. 拼接 10 万段文本：反复扩容 vs 先 reserve ----

constexpr std::size_t kPieces = 100000;
const std::string& piece() {
    static const std::string value = "playground";
    return value;
}

std::string concat_naive() {
    std::string text;
    for (std::size_t i = 0; i < kPieces; ++i) {
        text += piece();  // 容量不够就整块重新分配 + 拷贝旧数据
    }
    return text;
}

std::string concat_reserved() {
    std::string text;
    text.reserve(kPieces * piece().size());  // 一次到位，后面只剩纯拷贝
    for (std::size_t i = 0; i < kPieces; ++i) {
        text += piece();
    }
    return text;
}

void concat_demo() {
    std::cout << "\n== 2. 字符串拼接: 反复扩容 vs 先 reserve ==\n";
    Stopwatch watch;
    watch.reset();
    const std::string naive = concat_naive();
    const double naive_ms = watch.ms();
    watch.reset();
    const std::string reserved = concat_reserved();
    const double reserved_ms = watch.ms();

    std::cout << "    正确性: " << (naive == reserved ? "OK" : "FAIL")
              << "（长度 " << naive.size() << "）\n";
    report("拼接", naive_ms, reserved_ms);
}

// ---- 3. 矩阵元素和：按行 vs 按列 ----

constexpr std::size_t kRows = 2048;
constexpr std::size_t kCols = 2048;

// 矩阵用 float、累加用 double：浮点加法不可结合，编译器就不能把按列的循环
// 交换成按行（整型加法会被 -O3 合法地交换，两个版本就测不出差距了）。
// 值都是 0~6 的小整数，double 累加精确无舍入，结果仍可对比。
std::vector<float> make_matrix() {
    std::vector<float> matrix(kRows * kCols);
    for (std::size_t i = 0; i < matrix.size(); ++i) {
        matrix[i] = static_cast<float>(i % 7);
    }
    return matrix;
}

// noinline：这两个函数若被内联进 main 这种大函数，GCC 可能把累加器挤到栈上
// （每元素多一次 load/store），访存差距就被寄存器分配的运气淹没了
__attribute__((noinline)) double sum_by_row(const std::vector<float>& matrix) {
    double sum = 0;
    for (std::size_t r = 0; r < kRows; ++r) {
        for (std::size_t c = 0; c < kCols; ++c) {
            sum += matrix[r * kCols + c];  // 地址连续：一次 cacheline 带出后面 16 个 float
        }
    }
    return sum;
}

__attribute__((noinline)) double sum_by_col(const std::vector<float>& matrix) {
    double sum = 0;
    for (std::size_t c = 0; c < kCols; ++c) {
        for (std::size_t r = 0; r < kRows; ++r) {
            sum += matrix[r * kCols + c];  // 步长 8KB：几乎每次访问都 miss
        }
    }
    return sum;
}

void matrix_demo() {
    std::cout << "\n== 3. 矩阵求和: 按行访问 vs 按列访问 ==\n";
    const std::vector<float> matrix = make_matrix();

    Stopwatch watch;
    watch.reset();
    const double by_row = sum_by_row(matrix);
    const double row_ms = watch.ms();
    watch.reset();
    const double by_col = sum_by_col(matrix);
    const double col_ms = watch.ms();

    std::cout << "    正确性: " << (by_row == by_col ? "OK" : "FAIL") << "（和 = " << by_row
              << "）\n";
    std::cout << "    矩阵求和: 按行 " << row_ms << " ms -> 按列 " << col_ms << " ms（慢 "
              << col_ms / row_ms << "x；算法相同，差的只是访存模式）\n";
}

}  // namespace

int main() {
    two_sum_demo();
    concat_demo();
    matrix_demo();
    std::cout << "\n提示: Debug 构建差距偏大，用 release preset 重跑看真实数量级\n";
    return 0;
}

// fork 的内存代价 vs posix_spawn 基准：
//   fork 必须复制整份页表并逐页置为写时复制（哪怕 exec 一执行就全部作废），
//   代价随「驻留内存」线性增长；posix_spawn 内部用 clone(CLONE_VM|CLONE_VFORK)：
//   子进程直接共享父进程地址空间、父进程挂起，子进程完成 exec 后父进程才恢复
//   —— 没有页表拷贝，这就是它避开 fork 内存问题的原因。
//   （glibc 2.28 起 system/popen 内部同样改用 posix_spawn。）
// 三组实验：
//   1. 基线：小内存进程，两种方式差距不明显
//   2. 驻留 512MB 后再测：fork 变慢，posix_spawn 几乎不变
//   3. COW「写时才付款」：子进程写穿 512MB，把 fork 延迟的拷贝真正发生一遍
// 提示：透明大页（THP）会把页表项减少 512 倍，开了 THP 的机器上第 2 节差距会缩小；
//       结论以 release 构建为准。
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr std::size_t kBigBytes = std::size_t{512} << 20;  // 512MB 驻留内存
constexpr int kRounds = 20;                                // 第 1/2 节的取样次数
constexpr int kCowRounds = 3;                              // 第 3 节写穿太贵，只跑 3 次

[[noreturn]] void die(const char* what) {
    std::perror(what);
    std::exit(1);
}

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

template <std::size_t N>
double average_us(const std::array<double, N>& samples) {
    double total = 0;
    for (double sample : samples) {
        total += sample;
    }
    return total / static_cast<double>(N) * 1000.0;  // ms -> μs
}

// fork + exec(/bin/true) + wait 一轮：fork 拷贝的页表在 exec 一瞬间全部作废，纯属白拷
double fork_exec_round() {
    Stopwatch watch;
    const pid_t pid = ::fork();
    if (pid < 0) {
        die("fork");
    }
    if (pid == 0) {
        ::execl("/bin/true", "true", static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return watch.ms();
}

// posix_spawn(/bin/true) + wait 一轮：共享内存 + 父进程挂起，无需拷贝页表
double spawn_round() {
    Stopwatch watch;
    pid_t pid = 0;
    char program[] = "true";
    char* const argv[] = {program, nullptr};
    const int err = ::posix_spawn(&pid, "/bin/true", nullptr, nullptr, argv, environ);
    if (err != 0) {
        std::cerr << "posix_spawn: " << std::strerror(err) << '\n';
        std::exit(1);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return watch.ms();
}

void run_comparison(const char* label) {
    std::array<double, kRounds> fork_samples{};
    std::array<double, kRounds> spawn_samples{};
    for (int i = 0; i < kRounds; ++i) {
        fork_samples[static_cast<std::size_t>(i)] = fork_exec_round();
    }
    for (int i = 0; i < kRounds; ++i) {
        spawn_samples[static_cast<std::size_t>(i)] = spawn_round();
    }
    std::cout << "    " << label << "fork+exec+wait : 平均 " << average_us(fork_samples)
              << " μs/次\n";
    std::cout << "    " << label << "posix_spawn+wait: 平均 " << average_us(spawn_samples)
              << " μs/次\n";
}

// 子进程把父进程的 512MB 全部写一遍：每页首次写都触发缺页 + 整页复制（真正付款）
double cow_write_round(std::vector<char>& memory) {
    Stopwatch watch;
    const pid_t pid = ::fork();
    if (pid < 0) {
        die("fork");
    }
    if (pid == 0) {
        for (std::size_t off = 0; off < memory.size(); off += 4096) {
            ++memory[off];  // 只碰每页第一个字节，缺页内核也会复制整页
        }
        _exit(0);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return watch.ms();
}

}  // namespace

int main() {
    std::cout << std::unitbuf;  // fork 前保证输出已冲刷（教学程序铁律）

    std::cout << "== 1. 基线：小内存进程 ==\n";
    run_comparison("");

    // 分配并全部写一遍：512MB 真实驻留（4KB 页 × 131072 个页表项）
    std::vector<char> big(kBigBytes, 1);
    std::cout << "\n== 2. 驻留 512MB 之后 ==\n";
    run_comparison("");
    std::cout << "    ↑ fork 要把整份页表抄一遍再全部作废（exec）；posix_spawn 共享内存免抄\n";

    std::cout << "\n== 3. COW「写时才付款」：子进程写穿 512MB ==\n";
    std::array<double, kCowRounds> cow_samples{};
    for (int i = 0; i < kCowRounds; ++i) {
        cow_samples[static_cast<std::size_t>(i)] = cow_write_round(big);
    }
    std::cout << "    子进程写穿全部页: 平均 " << average_us(cow_samples) / 1000.0
              << " ms/次（写多少页拷多少页；fork 只是延迟付款，没有消灭付款）\n";

    std::cout << "\n结论:\n"
              << "  - 只想 exec 一个新程序 → posix_spawn（或 system/popen，内部同款）\n"
              << "  - 子进程 exec 前要做一堆自己的事 → fork（COW 让你只为自己写的页付钱）\n"
              << "  - 裸 vfork 今天不再推荐：posix_spawn 就是它的「受控版」（同一 clone 标志，"
                 "但库替你把子进程能安全做什么都管好了）\n";
    return 0;
}

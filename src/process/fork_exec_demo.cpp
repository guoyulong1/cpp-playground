// fork / exec / wait 及 fork 家族的内存语义演示：
//   1. 最小闭环：fork → exec(/bin/echo) → waitpid 解析退出状态
//   2. fork 是怎么"拷贝"内存的：写时复制（COW）——虚拟地址相同、写后分叉
//   3. 陷阱：fork 连 stdio 用户态缓冲一起复制，未 flush 的输出会打两遍
//   4. vfork：不拷贝、直接共享地址空间（与 2 的"拷贝"对照）
//   5. system()/popen() 的真身：fork + exec + wait 的封装（手写复刻对比）
// fork 的代价与 posix_spawn 的计时对比见 fork_cost_demo；系统讲解见 docs/process-fork.md。
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void die(const char* what) {
    std::perror(what);
    std::exit(1);
}

void fork_exec_wait_demo() {
    std::cout << "== 1. fork → exec → waitpid 最小闭环 ==\n";
    const pid_t pid = ::fork();
    if (pid < 0) {
        die("fork");
    }
    if (pid == 0) {
        // 子进程：execl 成功后本程序镜像被替换，下面的代码不会再执行
        ::execl("/bin/echo", "echo", "hello from child process", static_cast<char*>(nullptr));
        _exit(127);  // 惯例：exec 失败用 127 退出
    }

    // 父进程：阻塞等待这个子进程，并解析 wait 状态字
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        die("waitpid");
    }
    if (WIFEXITED(status)) {
        std::cout << "child (pid=" << pid << ") exited with code " << WEXITSTATUS(status)
                  << '\n';
    } else if (WIFSIGNALED(status)) {
        std::cout << "child (pid=" << pid << ") killed by signal " << WTERMSIG(status) << '\n';
    }
}

void cow_demo() {
    std::cout << "\n== 2. fork 如何拷贝内存：写时复制（Copy-On-Write） ==\n";
    // fork 瞬间内核做两件事：复制页表（虚拟地址布局原样照抄）、把私有页标记为只读共享。
    // 物理内存并不真的拷贝；谁先写那一页，谁触发缺页异常，内核才把那一页复制成私有。
    int value = 111;
    std::cout << "[parent] fork 前: value = " << value << " @ " << &value << '\n';

    const pid_t pid = ::fork();
    if (pid < 0) {
        die("fork");
    }
    if (pid == 0) {
        std::cout << "[child ] fork 后: value = " << value << " @ " << &value
                  << "（虚拟地址一模一样）\n";
        value = 222;  // 首次写：缺页异常 → 内核此刻才复制这一页
        std::cout << "[child ] 写入后: value = " << value << " @ " << &value
                  << "（还是同一虚拟地址，物理页已一分为二）\n";
        _exit(0);
    }

    int status = 0;
    ::waitpid(pid, &status, 0);
    std::cout << "[parent] 子进程退出后: value = " << value
              << "（父进程这页没被写，看到的仍是原值——拷贝发生在写入方）\n";
}

void stdio_duplication_demo() {
    std::cout << "\n== 3. 陷阱：fork 会连 stdio 缓冲一起复制 ==\n";
    // C stdio 是用户态缓冲：没 flush 的输出只存在于缓冲区里，fork 时被完整复制一份。
    std::fputs("【这行输出不带换行、还躺在 stdio 缓冲里】", stdout);  // 故意不 flush

    const pid_t pid = ::fork();
    if (pid < 0) {
        die("fork");
    }
    if (pid == 0) {
        ::exit(0);  // exit 会冲刷缓冲 → 子进程把它那份"副本"先打出来
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    // 父进程这份副本随后也会被冲出来（cout 默认与 stdio 同步冲刷）
    std::cout << " ← 父进程醒来。注意上面那句话打了两遍：缓冲被 fork 复制了\n"
              << "对策：fork 前 fflush(NULL)，或子进程用 _exit（它不冲刷任何缓冲）\n";
}

// POSIX 对 vfork 子进程只特批两件事：调用 _exit/exec；以及修改 pid_t 类型的变量。
// 其余一切（printf、malloc、return、写其他数据）都是未定义行为——因为根本没有"自己的内存"。
volatile pid_t g_shared_with_vfork_child = 0;  // volatile：确保真的写内存而非被优化进寄存器

void vfork_demo() {
    std::cout << "\n== 4. vfork：不是拷贝，是共享（父进程挂起，直到子进程 _exit/exec） ==\n";
    const pid_t pid = ::vfork();
    if (pid < 0) {
        die("vfork");
    }
    if (pid == 0) {
        g_shared_with_vfork_child = 42;  // 写的就是父进程的这 4 个字节（共享地址空间）
        _exit(0);                        // 只能 _exit 或 exec；绝不能 return（栈也是共享的）
    }

    int status = 0;
    ::waitpid(pid, &status, 0);
    std::cout << "[parent] 子进程写过的 g_shared = " << g_shared_with_vfork_child
              << "（若是 fork，这里必然还是 0——「拷贝」与「共享」的区别就在这一行）\n"
              << "vfork 是 4.3BSD 为 fork+exec 省内存而生的老接口；今天的正规继任者是 "
                 "posix_spawn（见 fork_cost_demo）\n";
}

// 手写 system：fork → exec(/bin/sh -c cmd) → waitpid，返回 wait 状态字
int mini_system(const char* command) {
    const pid_t pid = ::fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        ::execl("/bin/sh", "sh", "-c", command, static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return status;
}

// 手写 popen(读)：pipe + fork + dup2(管道写端→stdout) + exec；父进程读完全部输出再 wait
std::string capture_output(const char* command) {
    int fds[2];
    if (::pipe(fds) != 0) {
        die("pipe");
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        die("fork");
    }
    if (pid == 0) {
        ::close(fds[0]);
        if (::dup2(fds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        ::close(fds[1]);
        ::execl("/bin/sh", "sh", "-c", command, static_cast<char*>(nullptr));
        _exit(127);
    }
    ::close(fds[1]);

    std::string output;
    char buf[256];
    while (true) {
        const ssize_t n = ::read(fds[0], buf, sizeof(buf));
        if (n < 0) {
            die("read");
        }
        if (n == 0) {
            break;  // 子进程关闭了写端
        }
        output.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    return output;
}

void wrapper_demo() {
    std::cout << "\n== 5. system()/popen() 的真身：fork + exec + wait 的封装 ==\n";

    static_cast<void>(mini_system("echo 我来自 mini_system（手写 fork+exec+wait）"));
    std::cout << "mini_system(\"exit 3\") 返回 " << mini_system("exit 3")
              << " = 3 左移 8 位：wait 状态字的编码\n";
    static_cast<void>(::system("echo 我来自 glibc system"));
    std::cout << "glibc  system(\"exit 3\") 返回 " << ::system("exit 3") << "（编码一致）\n";

    std::cout << "手写 popen 收到: " << capture_output("echo 我来自手写 capture_output");
    std::FILE* pipe = ::popen("echo 我来自 glibc popen", "r");
    if (pipe == nullptr) {
        die("popen");
    }
    char line[256];
    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        std::cout << "glibc  popen 收到: " << line;
    }
    ::pclose(pipe);
    std::cout << "结论：system/popen 就是上面几步的库封装；glibc 2.28 起内部已改用 "
                 "posix_spawn 实现（语义不变，绕开了 fork 的拷贝代价）\n";
}

}  // namespace

int main() {
    // fork 教学程序的铁律：fork 前保证所有输出已冲刷，否则子进程会把缓冲副本再打一遍。
    // unitbuf 让每次 << 立即落盘（并与 C stdio 同步冲刷）；第 3 节会故意用 C stdio 违反一次。
    std::cout << std::unitbuf;

    fork_exec_wait_demo();
    cow_demo();
    stdio_duplication_demo();
    vfork_demo();
    wrapper_demo();
    return 0;
}

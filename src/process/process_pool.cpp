// 简单进程池：父进程 fork 出 N 个 worker，每个 worker 一条任务管道，
// 计算结果通过共享的结果管道写回父进程。
// fd 关系（以 2 个 worker 为例）：
//   parent --task[i]--> worker i      worker i --result--> parent
// 练习：改 epoll 派发、加 SIGINT 优雅退出、加超时回收，见 README。
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr int kWorkers = 4;
constexpr int kTasksPerWorker = 3;
constexpr std::int64_t kStop = -1;

struct Pipe {
    int read_end = -1;
    int write_end = -1;
};

[[noreturn]] void die(const char* what) {
    std::perror(what);
    std::exit(1);
}

Pipe make_pipe() {
    int fds[2];
    if (::pipe(fds) != 0) {
        die("pipe");
    }
    return Pipe{fds[0], fds[1]};
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

std::int64_t read_int(int fd) {
    unsigned char buf[sizeof(std::int64_t)] = {};
    std::size_t got = 0;
    while (got < sizeof(buf)) {
        const ssize_t n = ::read(fd, buf + got, sizeof(buf) - got);
        if (n < 0) {
            die("read");
        }
        if (n == 0) {
            return kStop;  // 写端已全部关闭
        }
        got += static_cast<std::size_t>(n);
    }
    std::int64_t value = 0;
    std::memcpy(&value, buf, sizeof(value));
    return value;
}

void write_int(int fd, std::int64_t value) {
    unsigned char buf[sizeof(std::int64_t)] = {};
    std::memcpy(buf, &value, sizeof(value));
    std::size_t sent = 0;
    while (sent < sizeof(buf)) {
        const ssize_t n = ::write(fd, buf + sent, sizeof(buf) - sent);
        if (n < 0) {
            die("write");
        }
        sent += static_cast<std::size_t>(n);
    }
}

void worker_loop(int task_fd, int result_fd) {
    while (true) {
        const std::int64_t task = read_int(task_fd);
        if (task == kStop) {
            _exit(0);
        }
        write_int(result_fd, task * task);  // “任务”：求平方
    }
}

}  // namespace

int main() {
    Pipe result = make_pipe();
    std::vector<Pipe> tasks(static_cast<std::size_t>(kWorkers));
    for (auto& task_pipe : tasks) {
        task_pipe = make_pipe();
    }

    std::vector<pid_t> workers;
    workers.reserve(tasks.size());
    for (int i = 0; i < kWorkers; ++i) {
        const pid_t pid = ::fork();
        if (pid < 0) {
            die("fork");
        }
        if (pid == 0) {
            // 子进程只保留：自己的任务管道读端 + 结果管道写端
            for (int j = 0; j < kWorkers; ++j) {
                if (j != i) {
                    ::close(tasks[static_cast<std::size_t>(j)].read_end);
                    ::close(tasks[static_cast<std::size_t>(j)].write_end);
                }
            }
            ::close(result.read_end);
            worker_loop(tasks[static_cast<std::size_t>(i)].read_end, result.write_end);
        }
        workers.push_back(pid);
        std::cout << "[parent] forked worker pid=" << pid << '\n';
    }

    // 父进程用不到的端：各任务管道读端、结果管道写端
    for (auto& task_pipe : tasks) {
        close_fd(task_pipe.read_end);
    }
    close_fd(result.write_end);

    // 轮流派发任务，随后给每个 worker 发停止信号（数据量远小于 PIPE_BUF，不会阻塞）
    for (int t = 0; t < kWorkers * kTasksPerWorker; ++t) {
        write_int(tasks[static_cast<std::size_t>(t % kWorkers)].write_end, t + 1);
    }
    for (auto& task_pipe : tasks) {
        write_int(task_pipe.write_end, kStop);
    }

    for (int t = 0; t < kWorkers * kTasksPerWorker; ++t) {
        std::cout << "[parent] result = " << read_int(result.read_end) << '\n';
    }

    for (auto& task_pipe : tasks) {
        close_fd(task_pipe.write_end);
    }
    close_fd(result.read_end);

    for (pid_t pid : workers) {
        int status = 0;
        if (::waitpid(pid, &status, 0) < 0) {
            die("waitpid");
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::cerr << "[parent] worker " << pid << " 异常退出\n";
        }
    }
    std::cout << "[parent] all workers reaped\n";
    return 0;
}

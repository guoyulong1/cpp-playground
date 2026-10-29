// fork / exec / wait 最小闭环：
//   父进程 fork 出子进程，子进程 exec 成 /bin/echo，父进程 waitpid 回收
//   并解析退出状态（正常退出码 / 被信号杀死）。
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

int main() {
    const pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "fork failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    if (pid == 0) {
        // 子进程：execl 成功后本程序镜像被替换，下面的代码不会再执行
        ::execl("/bin/echo", "echo", "hello from child process",
                static_cast<char*>(nullptr));
        std::cerr << "execl failed: " << std::strerror(errno) << '\n';
        _exit(127);  // 惯例：exec 失败用 127 退出
    }

    // 父进程：阻塞等待这个子进程
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        std::cerr << "waitpid failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (WIFEXITED(status)) {
        std::cout << "child (pid=" << pid
                  << ") exited with code " << WEXITSTATUS(status) << '\n';
    } else if (WIFSIGNALED(status)) {
        std::cout << "child (pid=" << pid
                  << ") killed by signal " << WTERMSIG(status) << '\n';
    }
    return 0;
}

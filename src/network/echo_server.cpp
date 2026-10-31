// epoll 回显服务器（LT 水平触发 + 非阻塞 fd + 每连接一个输出缓冲）：
//   1. listen fd 注册 EPOLLIN；可读就批量 accept，新连接同样注册 EPOLLIN
//   2. 读到的数据先入缓冲再尽力回写；写不完注册 EPOLLOUT，等可写事件续写
//   3. recv 返回 0 表示对端关闭，清理连接（LT 下不关会一直触发可读事件）
// 单线程 Reactor 雏形：所有 fd 都在一个线程里被 epoll_wait 统一调度。
// 用法：
//   ./build/default/echo_server              # 自测模式：内置客户端，跑完即退（CI 用）
//   ./build/default/echo_server serve 9000   # 前台服务模式，nc localhost 9000 测试
// 学习路线（select/poll/多线程版本、ET、粘包处理）见 docs/network-programming.md。
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <atomic>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>

namespace {

constexpr int kMaxEvents = 64;
constexpr std::size_t kReadBufSize = 4096;
constexpr int kBacklog = 64;

[[noreturn]] void die(const char* what) {
    std::perror(what);
    std::exit(1);
}

void set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        die("fcntl F_GETFL");
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        die("fcntl F_SETFL");
    }
}

int make_listener(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket");
    }
    const int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        die("setsockopt SO_REUSEADDR");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        die("bind");
    }
    if (::listen(fd, kBacklog) != 0) {
        die("listen");
    }
    set_nonblocking(fd);
    return fd;
}

// 每个连接的状态：尚未写回对端的数据 + 对端是否已关闭写端（FIN）
struct ConnState {
    std::string out;
    bool read_closed = false;
};

void close_connection(int fd, std::map<int, ConnState>& conns) {
    conns.erase(fd);
    ::close(fd);  // close() 会把 fd 自动从 epoll 摘除，不必显式 EPOLL_CTL_DEL
}

// 尽力把输出缓冲写回对端，然后按缓冲状态调整关注的事件；
// 对端已 FIN 且缓冲清空时，这才轮到我们关闭连接（否则会丢最后一次回写）
void try_flush(int ep, int fd, std::map<int, ConnState>& conns) {
    ConnState& state = conns[fd];
    std::string& pending = state.out;
    std::size_t sent = 0;
    while (sent < pending.size()) {
        const ssize_t n =
            ::send(fd, pending.data() + sent, pending.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // 内核发送缓冲满，剩下的等 EPOLLOUT
            }
            if (errno == EINTR) {
                continue;
            }
            break;  // 其他错误：不重试，等读事件发现连接已断
        }
        sent += static_cast<std::size_t>(n);
    }
    pending.erase(0, sent);

    if (state.read_closed && pending.empty()) {
        close_connection(fd, conns);  // 对端话说完了、我们也回完了：礼貌收尾
        return;
    }
    epoll_event ev{};
    ev.data.fd = fd;
    if (state.read_closed) {
        ev.events = EPOLLOUT;  // 只剩回写一件事；不再关注 EPOLLIN，避免 LT 忙轮询
    } else {
        ev.events = pending.empty() ? EPOLLIN : (EPOLLIN | EPOLLOUT);
    }
    if (::epoll_ctl(ep, EPOLL_CTL_MOD, fd, &ev) != 0) {
        die("epoll_ctl mod");
    }
}

// 读 fd 直到 EAGAIN 或对端 FIN；数据入缓冲并立即尝试回写。
// 返回本次读到的字节数；连接彻底结束时顺手清理。
std::size_t handle_read(int ep, int fd, std::map<int, ConnState>& conns,
                        std::vector<char>& buf) {
    std::size_t got = 0;
    while (true) {
        const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // LT 模式：读到 EAGAIN 表示这轮数据已读完
            }
            if (errno == EINTR) {
                continue;
            }
            close_connection(fd, conns);  // 出错按断连处理
            return got;
        }
        if (n == 0) {
            conns[fd].read_closed = true;  // 对端 FIN：不再有输入，但输出可能还没回完
            break;
        }
        conns[fd].out.append(buf.data(), static_cast<std::size_t>(n));
        got += static_cast<std::size_t>(n);
    }
    if (conns.find(fd) != conns.end()) {  // 连接仍存活才需要回写
        try_flush(ep, fd, conns);
    }
    return got;
}

void handle_write(int ep, int fd, std::map<int, ConnState>& conns) {
    if (conns.find(fd) == conns.end()) {
        return;  // 同一批事件里该连接可能已被读事件关闭
    }
    try_flush(ep, fd, conns);
}

void accept_pending(int ep, int listener, std::map<int, ConnState>& conns) {
    while (true) {
        const int fd = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;  // backlog 已取空
            }
            if (errno == EINTR) {
                continue;
            }
            die("accept4");
        }
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (::epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) != 0) {
            ::close(fd);
            die("epoll_ctl add conn");
        }
        conns.emplace(fd, ConnState{});
    }
}

// 事件循环，直到 should_stop() 返回 true；返回服务期间回显的总字节数
std::size_t serve_on_epoll(int listener, const std::function<bool()>& should_stop) {
    const int ep = ::epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0) {
        die("epoll_create1");
    }
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listener;
    if (::epoll_ctl(ep, EPOLL_CTL_ADD, listener, &ev) != 0) {
        die("epoll_ctl add listener");
    }

    std::map<int, ConnState> conns;  // fd -> 连接状态（输出缓冲 + 对端 FIN 标记）
    std::vector<char> buf(kReadBufSize);
    std::size_t echoed = 0;

    while (!should_stop()) {
        std::array<epoll_event, kMaxEvents> events{};
        // 100ms 超时：既能定期回头看退出条件，也让 EINTR 退出循环不拖太久
        const int n = ::epoll_wait(ep, events.data(), kMaxEvents, 100);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            die("epoll_wait");
        }
        for (int i = 0; i < n; ++i) {
            const epoll_event& event = events[static_cast<std::size_t>(i)];
            const int fd = event.data.fd;
            if (fd == listener) {
                accept_pending(ep, listener, conns);
            } else if ((event.events & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0) {
                echoed += handle_read(ep, fd, conns, buf);
            } else if ((event.events & EPOLLOUT) != 0) {
                handle_write(ep, fd, conns);
            }
        }
    }

    for (const auto& entry : conns) {
        ::close(entry.first);
    }
    ::close(ep);
    return echoed;
}

// 阻塞式客户端：连上后发一条消息，收回同样内容即通过
bool run_client(std::uint16_t port, const std::string& message) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("client socket");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        die("inet_pton");
    }
    // listener 已在主线程完成 listen，内核会替我们缓存连接请求，无需等待服务线程
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        die("connect");
    }

    std::size_t sent = 0;
    while (sent < message.size()) {
        const ssize_t n = ::send(fd, message.data() + sent, message.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            die("client send");
        }
        sent += static_cast<std::size_t>(n);
    }
    // 半关闭写端（发 FIN）：考验服务端「对端话说完了也要把没回完的数据回完再关」
    if (::shutdown(fd, SHUT_WR) != 0) {
        die("client shutdown");
    }
    std::string echoed(message.size(), '\0');
    std::size_t got = 0;
    while (got < echoed.size()) {
        const ssize_t n = ::recv(fd, echoed.data() + got, echoed.size() - got, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            die("client recv");
        }
        if (n == 0) {
            break;  // 服务端提前断开
        }
        got += static_cast<std::size_t>(n);
    }
    ::close(fd);
    return got == echoed.size() && echoed == message;
}

void run_selftest() {
    const int listener = make_listener(0);  // 端口 0：让内核挑一个空闲端口
    sockaddr_in actual{};
    socklen_t len = static_cast<socklen_t>(sizeof(actual));
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&actual), &len) != 0) {
        die("getsockname");
    }
    const std::uint16_t port = ntohs(actual.sin_port);

    std::atomic<bool> stop{false};
    std::size_t echoed = 0;
    std::thread server([&listener, &stop, &echoed] {
        echoed = serve_on_epoll(listener, [&stop] { return stop.load(); });
    });

    const std::string message = "hello epoll\n";
    const bool first = run_client(port, message);
    const bool second = run_client(port, message);  // 第二个连接：验证连接生命周期管理

    stop.store(true);
    server.join();
    ::close(listener);
    std::cout << "[selftest] 两次回显" << (first && second ? "一致，" : "不一致！")
              << "服务端共回显 " << echoed << " 字节\n";
    if (!first || !second) {
        std::exit(1);
    }
}

volatile std::sig_atomic_t g_stop = 0;  // 信号处理函数里只能做这种最简单的赋值

void on_signal(int signal_number) {
    static_cast<void>(signal_number);
    g_stop = 1;
}

void run_serve(std::uint16_t port) {
    const int listener = make_listener(port);
    // Ctrl-C / kill 只置标志：主循环在 epoll 超时或 EINTR 后自然退出
    struct sigaction action {};
    action.sa_handler = on_signal;
    ::sigemptyset(&action.sa_mask);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);

    std::cout << "[echo_server] 监听 0.0.0.0:" << port << "，Ctrl-C 退出。试试: nc localhost "
              << port << '\n';
    const std::size_t echoed = serve_on_epoll(listener, [] { return g_stop != 0; });
    std::cout << "\n[echo_server] 收到退出信号，共回显 " << echoed << " 字节，bye\n";
    ::close(listener);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "serve") {
        const long port = std::strtol(argv[2], nullptr, 10);
        if (port <= 0 || port > 65535) {
            std::cerr << "端口非法: " << argv[2] << '\n';
            return 1;
        }
        run_serve(static_cast<std::uint16_t>(port));
        return 0;
    }
    if (argc != 1) {
        std::cerr << "用法: " << argv[0] << " [serve <port>]\n";
        return 1;
    }
    run_selftest();
    return 0;
}

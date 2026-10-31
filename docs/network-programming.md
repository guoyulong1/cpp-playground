# 网络编程学习方案

> 回答「网络编程该怎么做」：从阻塞 socket 起步，理解为什么需要 IO 多路复用，
> 吃透仓库里的 epoll echo server，最后走向真正的协议服务器（HTTP）。
> 仓库对应：`src/network/echo_server.cpp`（单线程 Reactor 雏形，自带自测模式）。

## 学习目标

1. 手写 TCP 客户端/服务端的三步握手语义：`socket → bind → listen → accept` / `socket → connect`。
2. 说清阻塞与非阻塞、LT 与 ET 的区别，以及为什么 epoll 循环里到处是 `EAGAIN`。
3. 处理四个经典难题：**部分写、SIGPIPE、半关闭、粘包**。
4. 给 echo server 加一层简单 HTTP 协议解析。

## 阶段 1：阻塞 socket（先懂最朴素的版本）

单连接、每步都阻塞：`accept` 等连接、`recv` 等数据、`send` 等缓冲。这正是自测模式里
`run_client()` 的写法（阻塞客户端连非阻塞服务端）。

- [ ] 不看仓库代码，写一个「单连接阻塞 echo server」（socket/bind/listen/accept/recv/send 循环），
      用 `nc localhost 端口` 验证，再开第二个 nc 观察它为什么服务不了
- [ ] 用 `ss -tan` 观察连接状态变化：ESTABLISHED / CLOSE_WAIT / TIME_WAIT 各出现在哪一侧
- [ ] 把 `SO_REUSEADDR` 去掉，重启立刻 bind 报 `EADDRINUSE`，理解 TIME_WAIT 与 2MSL

## 阶段 2：从阻塞到多路复用（为什么要有 epoll）

| 方案 | 一万个连接时 | 问题 |
|---|---|---|
| 阻塞 + 多进程/多线程 | 1 万个进程/线程 | 内存与调度爆炸（C10K 的起点） |
| `select`/`poll` | 单线程但每轮传全部 fd | 每次拷贝 + 内核/用户两遍全量扫描，O(n) |
| `epoll`（LT） | 只返回**有事件的** fd | O(活跃数)；fd 上限高；本仓库实现 |

LT（水平触发，默认）vs ET（边缘触发）：

- LT：只要还有数据没读完，`epoll_wait` 会一直报告 → 配非阻塞 fd + 读到 `EAGAIN` 即可，宽容；
- ET：只在状态变化时报告**一次** → 必须一口气循环读到 `EAGAIN`、accept 到空，漏读就永久饿死；省事件量但易错。

- [ ] 把仓库 echo_server 的注释逐行读完，画一张「listen fd / conn fd / EPOLLIN / EPOLLOUT」状态流转图
- [ ] 用 `strace -f -e trace=epoll_wait,epoll_ctl,accept4,recvfrom,sendto ./build/default/echo_server serve 9000`
      跑起来，nc 连上去敲字，看系统调用序列与代码一一对应

## 阶段 3：吃透仓库 echo_server 的四个关键设计

1. **非阻塞 + LT + 每连接输出缓冲**（`ConnState::out`）：
   `send` 可能只写出一部分（内核发送缓冲满）→ 剩下的存缓冲、注册 `EPOLLOUT`、可写时续写（`try_flush`）。
   这就是**部分写**问题的标准解法。
2. **半关闭（FIN）处理**：`recv` 返回 0 只是「对方话说完」，不代表能立刻 `close` ——
   输出缓冲可能还没回完。`read_closed` 标记 + 「FIN 且缓冲清空才关」+ 只留 `EPOLLOUT`
   （否则 LT 会对 EOF 一直报告可读，忙轮询烧满 CPU）。自测模式里客户端特意 `shutdown(SHUT_WR)` 覆盖此路径。
3. **SIGPIPE**：向已断开的连接 `send` 会触发 SIGPIPE（默认杀进程），`MSG_NOSIGNAL` 关掉它改成 `EPIPE`。
4. **信号与事件循环**：`volatile std::sig_atomic_t` 标志 + `epoll_wait` 100ms 超时/`EINTR`，
   Ctrl-C 优雅退出（进阶做法是 `signalfd` 把信号也变成 fd 事件）。

练习清单（都在 echo_server.cpp 上改）：

- [ ] 加 `EPOLLRDHUP` 订阅，让对端半关闭不必等 `recv` 返回 0 就能感知
- [ ] 改造成 ET 模式：`EPOLLET | EPOLLIN | EPOLLOUT`，`accept` 与 `recv` 都循环到 `EAGAIN`，跑自测验证
- [ ] 限制最大连接数（如 1024）：超过就 `accept` 后立刻关闭，防资源耗尽
- [ ] 把回显改成「收到一行（`\n`）才回写」：引入 `ConnState::in` 输入缓冲，就势解决**粘包**（见阶段 4）
- [ ] 多线程版：main 线程 accept，把 conn fd 派发给 [线程池](thread-pool.md)（每线程一个独立 epoll，one loop per thread）

## 阶段 4：从字节流到协议（HTTP 小服务器）

TCP 是字节流，没有消息边界——「粘包」不是 bug 是本性。两种切分法：定长包头（长度前缀）或分隔符（`\r\n\r\n`）。

- [ ] 实现长度前缀协议：4 字节大端长度 + payload，服务端按长度切包回显（配合上一节的 `in` 缓冲）
- [ ] 写一个 mini HTTP/1.1 server：解析 `GET /path HTTP/1.1\r\n...` 请求行，返回 200 + 固定 body；
      支持 `Connection: keep-alive`（一个连接多个请求）与 `close`
- [ ] 用 `curl -v localhost:9000/` 与浏览器实测你的 HTTP server
- [ ] 大文件响应：对比 `read + send` 循环与 `sendfile(2)` 零拷贝的 CPU 占用

## 阶段 5：压测与观测（工程化收尾）

```bash
# 压测（先装：sudo apt install wrk）
wrk -t4 -c100 -d10s http://localhost:9000/          # 4 线程 100 连接压 10 秒

# 观测
ss -tanp | grep 9000          # 连接与进程
netstat -s | grep -i listen   # listen 溢出/重试计数（backlog 太小的信号）
tcpdump -i lo -A port 9000    # 抓包看交互细节
cat /proc/net/sockstat        # socket 使用量
```

- [ ] 用 wrk 压 echo/HTTP server，记录 QPS；调大 `kBacklog`、加线程池，看数字变化
- [ ] 故意把客户端杀掉（RST），确认服务端 `recv/send` 错误路径正常清理连接不泄漏（`ss -tan` 数连接）

## 常见坑

- **阻塞 fd 塞进 epoll**：一旦内核说可读，你可能只处理了半条数据就返回，LT 下次还会叫你；ET 下直接饿死。
- **LT 下收到 FIN 不关闭/不改事件**：EOF 永远「可读」，`epoll_wait` 空转 100% CPU。
- **忽略 `send` 返回值**：部分写丢数据；忽略 `EPIPE`/SIGPIPE 直接被信号杀掉。
- **`write` 大小超过 `SO_SNDBUF`**：非阻塞下返回 `EAGAIN` 不是错误，走输出缓冲路径。
- **惊群**：多进程/线程同时 `accept` 同一 fd，唤醒风暴；epoll 下用 `EPOLLEXCLUSIVE` 或单点 accept。

## 延伸阅读

- `man 7 epoll`、`man 2 epoll_ctl`（LT/ET 官方说明，例子代码值得手敲一遍）
- 《UNIX 网络编程 卷1》（UNP）：ch6（IO 复用）、ch16（非阻塞 IO）——本主题主线书
- 《Linux/UNIX 系统编程手册》（TLPI）ch 61-63
- 陈硕《Linux 多线程服务端编程》：one loop per thread、Reactor 模式中文最佳实践
- Dan Kegel: *The C10K Problem*（历史视角，理解为什么是 epoll）

# fork 家族学习方案（fork / vfork / system / popen / posix_spawn）

> 回答四个问题：**fork 是怎么"拷贝"父进程内存的？system/popen 和 fork 什么关系？
> posix_spawn 为什么能避开 fork 的内存问题？vfork 和 fork 差在哪？**
> 仓库对应：`src/process/fork_exec_demo.cpp`（五个定性实验）、`src/process/fork_cost_demo.cpp`（定量基准）。

## 学习目标

完成后你应该能不查资料回答：

1. fork 之后父子进程的同一虚拟地址为什么可以存不同的值？
2. 「fork 的内存代价」到底付在拷贝数据上，还是拷贝页表上？
3. 多线程进程里 fork 为什么危险？为什么 posix_spawn 没这个问题？
4. `system()`、`popen()`、`posix_spawn()`、裸 `fork()+exec()` 各适合什么场景？

## 一、fork 是怎么"拷贝"内存的：写时复制（COW）

fork 的瞬间内核做两件事，**并不搬运数据**：

1. **复制页表**：子进程拿到一份一模一样的虚拟地址空间布局（`fork_exec_demo` 第 2 节里
   父子进程打印的变量地址完全相同）；
2. **私有页降级为只读共享**：数据页父子共用同一份物理内存，双方页表都标成只读。

之后谁先**写**，谁触发缺页异常：内核把那一页复制一份、给写入方改成可写——
拷贝被推迟到「确有写入」的那一刻，这就是写时复制（demo 第 2 节：同一地址，子进程改成 222，
父进程仍是 111）。

推论：

- **fork 只是延迟付款，不是免单**：子进程写多少页，就复制多少页
  （`fork_cost_demo` 第 3 节：子进程写穿 512MB ≈ 213ms，这就是"补票"的价格）。
- **read-only 场景近乎免费**：只读共享父进程内存（如共享一大张只读配置表）非常便宜。
- **用户态缓冲不是 COW 能救的**：stdio 缓冲在用户内存里，fork 时连内容一起复制，
  子进程退出再冲一次缓冲 → 输出打两遍（demo 第 3 节）。对策：fork 前 `fflush(NULL)`，
  子进程用 `_exit`（不冲任何缓冲）。

fork 还有两笔隐性开销，写在这备查：

- **页表本身要拷**：驻留内存越大页表越多，fork 越慢（512MB 驻留 ≈ 13 万个页表项），
  且 exec 一执行就全部作废——`fork_cost_demo` 第 2 节实测 fork 慢一个数量级。
- **多线程进程 fork 只保留调用线程**：其他线程持有的锁（malloc arena 锁、stdio 锁、
  你自己的 mutex）在子进程里**永远无人释放**；fork 与 exec 之间只能调用
  async-signal-safe 函数，否则随时死锁。

## 二、system()/popen() 的真身：fork + exec + wait 的封装

```text
system(cmd)   = fork → execve("/bin/sh", ["sh","-c",cmd]) → waitpid
                返回值 = wait 状态字（demo 第 5 节：system("exit 3") 返回 768 = 3<<8）

popen(cmd,"r")= pipe → fork → 子进程 dup2(管道写端→stdout) → exec(sh -c cmd)
                父进程读管道 = 拿到命令输出；pclose 负责 wait
```

`fork_exec_demo` 第 5 节手写了 `mini_system` 与 `capture_output`（= popen 读模式），
与 glibc 版输出逐行对上——它们没有任何魔法，就是这三步的组合。

历史注脚：**glibc 2.28（2018）起，system/popen 内部已改用 posix_spawn 实现**，
语义不变，但绕开了 fork 的页表拷贝与多线程锁问题（这正是下一节的主题）。

## 三、posix_spawn 为什么能避开 fork 的内存问题

Linux 上 glibc 的 posix_spawn 用 **`clone(CLONE_VM | CLONE_VFORK)`** 实现：

| clone 标志 | 效果 |
|---|---|
| `CLONE_VM` | 子进程**直接共享**父进程地址空间——不复制页表、不设 COW，512MB 驻留也不影响耗时 |
| `CLONE_VFORK` | 父进程**挂起**直到子进程 exec/退出——共享内存才是安全的（没人并发改） |

中间的脏活（改 fd、重定向）由 `file_actions` 参数声明，库在子进程 exec 前按协议执行
（`posix_spawn_file_actions_adddup2/open/close`）。所以 posix_spawn ≈ **受控的 vfork**：
vfork 的原始语义（共享 + 挂起）一样，但「子进程能安全做什么」由库管好了，还有独立的子栈。

`fork_cost_demo` 实测（Debug 构建，20 次取样均值）：

| 场景 | fork+exec+wait | posix_spawn+wait |
|---|---|---|
| 小内存进程 | ~460-620 μs | ~320-460 μs |
| 驻留 512MB 后 | **~5500 μs（慢 10 倍）** | **~440 μs（纹丝不动）** |

一个值得知道的 nuance：共享内存也意味着「子进程在 clone 与 exec 之间崩溃可能连带父进程」
（glibc 邮件列表讨论过），所以 posix_spawn 的 file_actions 只提供白名单操作，不让你跑任意代码。

## 四、vfork 与 fork 的区别

| | fork | vfork |
|---|---|---|
| 地址空间 | 复制页表 + COW，写时才分家 | **完全共享**（同一块物理内存） |
| 父进程 | 立即继续，父子并发 | **挂起**，直到子进程 `_exit`/`execve` |
| 页表拷贝成本 | 随驻留内存线性 | 无 |
| 子进程能干什么 | 任意（但有 stdio 缓冲/锁陷阱） | **几乎什么都不能干**：只能 `_exit`/`exec`，至多改 pid_t 变量；`return`/`printf`/`malloc`/写数据 = 未定义行为（栈也是共享的） |
| 出身 | POSIX 标准 | 4.3BSD 为「fork 完马上 exec」省内存而生；POSIX.1-2008 已删除，官方继任者就是 posix_spawn |

Linux 的 vfork 本身就是 `clone(CLONE_VM | CLONE_VFORK | SIGCHLD)`——和 posix_spawn
内部用的标志一模一样；区别只在于 posix_spawn 替你写好了「安全跑道」。

demo 第 4 节：vfork 子进程写一个 `volatile pid_t` 全局后 `_exit`，父进程直接看到 42——
同样的事用 fork 做父进程必然还是 0，这就是「共享」与「拷贝」的分水岭。

## 五、选型速查

| 需求 | 用什么 |
|---|---|
| 只想跑一个外部程序（无输出交互） | `posix_spawn` / `system` |
| 要拿命令的输出（或喂输入） | `popen`（内部 posix_spawn）/ 自己 pipe + posix_spawn + file_actions |
| 子进程 exec 前要做复杂初始化（改 uid、chroot、批量 fd 操作） | `fork` + exec（COW 只让你为写的页付费） |
| 高频产生短命子进程（每秒上千个） | posix_spawn（fork 的页表成本会累积） |
| 多线程服务进程里起子进程 | **绝对优先 posix_spawn**；坚持 fork 的话 exec 前只做 async-signal-safe 操作 |
| 裸 vfork | 2026 年的新代码不要再写了，用 posix_spawn |

## 练习清单

- [ ] `strace -f -e trace=clone,execve,wait4 ./build/cn/fork_exec_demo`：
      数一数每个 demo 节各触发几次 clone/execve；确认 glibc system 的底层是 clone（带 CLONE_VM 标志）
- [ ] `fork_cost_demo` 里加一组「vfork + exec」计时，验证它与 posix_spawn 同量级
- [ ] 把 demo 第 2 节的 `int` 换成 `std::string` 再改一次：改的是 string 内部指针指向的堆页
- [ ] 用 `/proc/<pid>/statm` 或 `smem -r` 对比 fork 前后父子的 RSS 与共享页（USS 才是各自私有的）
- [ ] 在多线程程序里 fork：子进程 fork 后、exec 前调用 `malloc`/`printf`，理解锁死锁风险（可只写注释分析）
- [ ] 把 `mini_system` 改造成忽略 SIGINT/SIGQUIT（对齐 man 3 system 的语义）

## 工具箱

```bash
strace -f -e trace=clone,fork,vfork,execve,wait4 ./build/cn/fork_exec_demo  # 系统调用层看真相
grep -c . /proc/<pid>/statm        # size resident shared ... 单位为页
smem -rk -P fork_cost_demo         # USS/PSS 直观展示共享 vs 私有内存
/usr/bin/time -v ./build/cn/fork_cost_demo   # 观察峰值 RSS（第 3 节会翻倍：COW 真复制了一份）
```

## 延伸阅读

- `man 2 fork` / `man 2 vfork`（含 POSIX 对 vfork 子进程的完整禁令清单）
- `man 3 posix_spawn` / `man 3 system` / `man 3 popen`
- 《Linux/UNIX 系统编程手册》（TLPI）第 24 章（进程创建）、25 章（进程终止）、26.3（COW）
- Baumann 等：*A Fork() in the Road*（USENIX ATC'19，重新审视 fork 的论文）
- glibc 2.28 NEWS：system/popen 改用 posix_spawn；`spawni.c`：clone(CLONE_VM|CLONE_VFORK) 实现

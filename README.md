# cpp-playground

![CI](https://github.com/guoyulong1/cpp-playground/actions/workflows/ci.yml/badge.svg)

C++ 练手场，围绕五个主题各搭好了最小可跑的骨架：

| 主题 | 位置 | 内容 |
|---|---|---|
| 进程管理 | `src/process/` | fork/exec/wait 闭环、多 worker 进程池（管道通信） |
| 内存管理 | `src/memory/`、`include/playground/` | RAII、智能指针、固定块内存池（带 GoogleTest 单元测试） |
| clang-tidy | `.clang-tidy` | 本地与 CI 共用一套规则 |
| Code Review | `.github/workflows/code-review.yml` | 阿里开源 [Open Code Review](https://github.com/alibaba/open-code-review)，PR 自动 AI 评审 |
| CI/CD | `.github/workflows/ci.yml` | 双编译器构建 + 单测、clang-tidy、ASan/UBSan |

## 快速开始

需要 CMake ≥ 3.21、Ninja、GCC ≥ 11 或 Clang ≥ 14：

```bash
sudo apt install build-essential cmake ninja-build   # Ubuntu 为例
```

```bash
cmake --preset default          # 配置（Debug，生成 compile_commands.json）
cmake --build --preset default  # 编译
ctest --preset default          # 单元测试（GoogleTest，首次构建自动下载）
./build/default/raii_demo       # 依次跑三个 demo
./build/default/fork_exec_demo
./build/default/process_pool
```

其他 preset：`release`（优化构建）、`asan`（AddressSanitizer + UBSan，用法同上）。

## 仓库结构

```
.
├── .clang-tidy                  # clang-tidy 规则（check 集合 + 命名约定）
├── .github/workflows/
│   ├── ci.yml                   # build(双编译器) / clang-tidy / sanitize
│   └── code-review.yml          # Open Code Review（PR 触发，可选）
├── CMakeLists.txt
├── CMakePresets.json            # default / release / asan
├── include/playground/
│   └── memory_pool.hpp          # 固定块内存池接口
├── src/
│   ├── CMakeLists.txt
│   ├── memory/
│   │   ├── memory_pool.cpp      # 内存池实现（playground_core 库）
│   │   └── raii_demo.cpp        # RAII / unique_ptr / shared_ptr / weak_ptr
│   └── process/
│       ├── fork_exec_demo.cpp   # fork → exec → waitpid
│       └── process_pool.cpp     # N worker 进程池 + 管道通信
└── tests/
    ├── CMakeLists.txt           # FetchContent 引入 GoogleTest
    └── test_memory_pool.cpp
```

## 练习主题与 TODO

### 1. 进程管理（`src/process/`）

- `fork_exec_demo.cpp`：fork 出子进程 → exec 成 `/bin/echo` → waitpid 回收并解析退出状态。
- `process_pool.cpp`：父进程 fork 出 N 个 worker，每个 worker 一条任务管道，结果经共享管道写回。

- [ ] 给 `process_pool` 加 SIGINT 优雅退出（注册信号处理，先停派发再回收）
- [ ] 用 `waitpid(WNOHANG)` 演示僵尸进程的产生与消灭
- [ ] 把「轮流派发」改成 epoll 事件驱动
- [ ] 实现一个 mini shell：解析命令、支持 `|` 管道与 `>` 重定向

### 2. 内存管理（`include/playground/` + `src/memory/`）

- `MemoryPool`：一次性向系统申请大块内存，内部用空闲链表分配/回收，块内复用自身内存存 next 指针。
- `raii_demo`：手写 RAII 守卫类、`unique_ptr` 自定义 deleter、`shared_ptr` 引用计数、`weak_ptr` 不延长生命周期。

- [ ] `allocate()` 池满时改为抛 `std::bad_alloc`，或实现自动扩容
- [ ] `deallocate()` 增加越界 / 重复释放检测
- [ ] 写一个 `shared_ptr` 环形引用案例，再用 `weak_ptr` 修复
- [ ] 故意写一个 use-after-free，用 `cmake --preset asan` 体验 ASan 报告

### 3. clang-tidy（`.clang-tidy`）

本地运行：

```bash
sudo apt install clang-tidy
clang-tidy -p build/default src/memory/memory_pool.cpp
```

`-p` 指向含 `compile_commands.json` 的构建目录（preset 已自动生成）。

- [ ] 注释掉 `.clang-tidy` 里的某条 `-xxx` 否决项，看看多报了什么
- [ ] 把 `ci.yml` 里 clang-tidy 步骤改成「有 warning 就失败」（文件里有提示行）

### 4. AI Code Review（Open Code Review）

PR 时自动跑行级 AI 评审。首次使用先配置 repository secrets
（仓库 → Settings → Secrets and variables → Actions → New repository secret）：

| Secret | 示例值 |
|---|---|
| `OCR_LLM_URL` | `https://openrouter.ai/api/v1` |
| `OCR_LLM_TOKEN` | `sk-or-v1-...` |
| `OCR_LLM_MODEL` | `anthropic/claude-sonnet-4` |

兼容 OpenAI / Anthropic 协议（工作流里 `llm_protocol` 已设为 `openai`，可自行修改）。
未配置 secrets 时该 job 自动跳过，不影响 CI 绿灯。

- [ ] 配好三个 secrets 后提一个 PR，观察评审评论
- [ ] 调整 `language`、`effort` 等参数对比评审质量

### 5. CI/CD（`.github/workflows/ci.yml`）

三个 job：`build-and-test`（gcc/clang 矩阵：构建 → 单测 → 跑 demo）、`clang-tidy`（基于 compile_commands.json）、`sanitize`（ASan + UBSan 下跑测试与 demo）。

- [ ] 加一个 Release job：打 tag 触发，把构建产物上传到 GitHub Releases
- [ ] 构建矩阵加 macOS runner
- [ ] 给 GoogleTest 的 FetchContent 下载加 actions/cache 缓存
- [ ] 给 README 再加一个 codecov / clang-tidy badge

## 推荐练习工作流

每个练习走一遍完整闭环：开分支 → 提 PR → 看 CI 与 AI 评审 → 改绿 → 合并。

```bash
git switch -c feat/memory-pool-grow
# ...改代码、本地 ctest --preset default...
git push -u origin feat/memory-pool-grow
gh pr create --fill
```

> `gh` 已安装在 `~/.local/bin`。若 `command -v gh` 找不到，
> 把 `export PATH="$HOME/.local/bin:$PATH"` 加进 `~/.bashrc` 后重开终端。

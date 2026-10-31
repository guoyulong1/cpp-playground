#pragma once

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace playground {

// 统一日志入口（spdlog 封装，可观测性基建）：
//   控制台彩色 + 滚动文件双 sink；进程内单例，首次调用初始化。
//   部署视角：日志是「可观测性三支柱」之一，rotate 防磁盘打爆，格式留采集器解析。
// 练习重点：
//   1. 为什么用单例而不是全局 logger 指针（初始化顺序、静态销毁）；
//   2. rotating vs daily sink 的取舍；flush_level 与性能的关系（每次 flush 一次系统调用）；
//   3. 把 pattern 改成 JSON 格式，方便 Loki/ELK 采集（部署方向的衔接练习）。
class Logger {
public:
    struct Options {
        std::string name = "playground";
        std::string file_path = "logs/playground.log";   // 空串则只写控制台
        std::size_t max_size = 5 * 1024 * 1024;          // 单文件 5MB
        std::size_t max_files = 3;                       // 最多 3 个滚动文件
        spdlog::level::level_enum level = spdlog::level::info;
    };

    // 进程内首次调用完成初始化，之后直接返回；无参版本用默认 Options
    static std::shared_ptr<spdlog::logger> get();
    static std::shared_ptr<spdlog::logger> get(const Options& opts);

    Logger() = delete;
};

}  // namespace playground

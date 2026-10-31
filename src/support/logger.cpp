#include "playground/logger.hpp"

#include <filesystem>
#include <mutex>
#include <stdexcept>

namespace playground {

std::shared_ptr<spdlog::logger> Logger::get() {
    return get(Options{});
}

std::shared_ptr<spdlog::logger> Logger::get(const Options& opts) {
    // 已注册则复用（spdlog 注册表本身线程安全）
    if (auto existing = spdlog::get(opts.name)) {
        return existing;
    }
    static std::mutex init_mutex;
    std::lock_guard<std::mutex> lock(init_mutex);

    // 双重检查：等锁期间别的线程可能已完成注册
    if (auto existing = spdlog::get(opts.name)) {
        return existing;
    }

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (!opts.file_path.empty()) {
        // 日志目录不存在先建好，避免 sink 构造抛异常
        std::filesystem::path p(opts.file_path);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);  // 已存在不算错误
        }
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            opts.file_path, opts.max_size, opts.max_files));
    }

    auto logger = std::make_shared<spdlog::logger>(opts.name, sinks.begin(), sinks.end());
    logger->set_level(opts.level);
    // 时间戳.线程号.等级.名 —— 部署时按线程排查问题的最小充分信息
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%l] [%n] %v");
    spdlog::register_logger(logger);
    return logger;
}

}  // namespace playground

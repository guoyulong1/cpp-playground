#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace playground::metrics {

// 手写 Prometheus 指标库（不引入 prometheus-cpp，文本格式本身就是学习点）：
//   Counter —— 只增不减（请求数、字节数）
//   Gauge   —— 可升可降的瞬时值（队列长度、连接数）
// 全部指标注册进全局 Registry，expose() 输出 Prometheus 文本协议：
//   # TYPE playground_http_requests_total counter
//   playground_http_requests_total{path="/api/echo"} 42
// 练习重点：
//   1. counter 用 relaxed 原子递增为什么够（无跨字段一致性要求）；
//   2. 指标名/label 命名规范：total 后缀、snake_case、label 基数要有限（别把 user_id 当 label）；
//   3. expose() 在读侧拼字符串，锁粒度怎么选（shared_mutex 读锁）；高基数下还有什么方案。

class Counter {
public:
    explicit Counter(std::string_view help) : help_(help) {}
    void increment(double v = 1.0) noexcept {
        // relaxed：计数器只保证最终一致，不需要与其它变量同步序
        value_.fetch_add(static_cast<std::uint64_t>(v * 1000), std::memory_order_relaxed);
    }
    [[nodiscard]] double value() const noexcept {
        return static_cast<double>(value_.load(std::memory_order_relaxed)) / 1000.0;
    }
    [[nodiscard]] const std::string& help() const noexcept { return help_; }

private:
    std::string help_;
    // 定点存储：std::atomic<double> 没有 fetch_add，千分位精度对计数器足够
    std::atomic<std::uint64_t> value_{0};
};

class Gauge {
public:
    explicit Gauge(std::string_view help) : help_(help) {}
    void set(double v) noexcept { value_.store(v, std::memory_order_relaxed); }
    [[nodiscard]] double value() const noexcept { return value_.load(std::memory_order_relaxed); }
    [[nodiscard]] const std::string& help() const noexcept { return help_; }

private:
    std::string help_;
    std::atomic<double> value_{0.0};
};

// 全局注册表：进程单例；name+labels -> 指标。
// 设计取舍：Counter/Gauge 含 atomic 不可移动，map 里放 unique_ptr 间接持有；
// map 节点 + heap 对象双重稳定，注册一次后返回的引用长期有效（采集线程可缓存）。
class Registry {
public:
    static Registry& instance() {
        static Registry reg;
        return reg;
    }

    Counter& counter(std::string_view name, std::string_view help,
                     std::map<std::string, std::string> labels = {}) {
        std::unique_lock lock(mutex_);
        const std::string key = make_key(name, labels);
        auto it = counters_.find(key);
        if (it == counters_.end()) {
            it = counters_
                     .emplace(key, Entry<Counter>{std::make_unique<Counter>(std::string(help)),
                                                  std::string(name), std::move(labels)})
                     .first;
        }
        return *it->second.metric;
    }

    Gauge& gauge(std::string_view name, std::string_view help,
                 std::map<std::string, std::string> labels = {}) {
        std::unique_lock lock(mutex_);
        const std::string key = make_key(name, labels);
        auto it = gauges_.find(key);
        if (it == gauges_.end()) {
            it = gauges_
                     .emplace(key, Entry<Gauge>{std::make_unique<Gauge>(std::string(help)),
                                                std::string(name), std::move(labels)})
                     .first;
        }
        return *it->second.metric;
    }

    // 输出 Prometheus 文本格式（GET /metrics 直接返回这段）
    std::string expose() const {
        std::shared_lock lock(mutex_);
        std::ostringstream out;
        for (const auto& item : counters_) {
            const auto& e = item.second;
            out << "# TYPE " << e.base_name << " counter\n"
                << "# HELP " << e.base_name << ' ' << e.metric->help() << '\n'
                << render(e) << ' ' << e.metric->value() << '\n';
        }
        for (const auto& item : gauges_) {
            const auto& e = item.second;
            out << "# TYPE " << e.base_name << " gauge\n"
                << "# HELP " << e.base_name << ' ' << e.metric->help() << '\n'
                << render(e) << ' ' << e.metric->value() << '\n';
        }
        return out.str();
    }

private:
    Registry() = default;

    template <typename Metric>
    struct Entry {
        std::unique_ptr<Metric> metric;
        std::string base_name;                        // 不含 label 的指标名
        std::map<std::string, std::string> labels;    // 有序 map 保证输出稳定
    };

    static std::string make_key(std::string_view name,
                                const std::map<std::string, std::string>& labels) {
        std::string key(name);
        for (const auto& [k, v] : labels) key += '|' + k + '=' + v;
        return key;
    }

    // 渲染为 name{label="v",...} 的展示形式
    static std::string render(const auto& entry) {
        if (entry.labels.empty()) return entry.base_name;
        std::string s = entry.base_name + '{';
        bool first = true;
        for (const auto& [k, v] : entry.labels) {
            if (!first) s += ',';
            s += k + "=\"" + v + '"';
            first = false;
        }
        return s + '}';
    }

    mutable std::shared_mutex mutex_;
    std::map<std::string, Entry<Counter>> counters_;
    std::map<std::string, Entry<Gauge>> gauges_;
};

}  // namespace playground::metrics

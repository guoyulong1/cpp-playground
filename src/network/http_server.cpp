// http_server：cpp-httplib 实现的可观测 REST 服务（软件开发→系统部署的衔接练习）
//
// 端点一览：
//   GET  /healthz   存活探针（K8s liveness / 负载均衡健康检查的标配）
//   GET  /api/stats 返回 JSON 状态（nlohmann-json）
//   POST /api/echo  回显请求 JSON（字段校验 + 错误响应规范）
//   GET  /metrics   Prometheus 文本格式指标（playground::metrics 手写实现）
//
// 与 src/network/echo_server.cpp 的关系：手写 epoll 解决「网络怎么收发」，
// 本文件解决「服务怎么组织」——路由、状态码、可观测。两者对照着读。
//
// 自测模式（默认，CI 可跑）：起在随机端口 → 自己发请求断言 → 退出码 0/1。
// 前台模式：./http_server serve [port]。
//
// 练习重点：
//   1. 把 /metrics 喂给 dev-stack 的 Prometheus（deploy/docker-compose.yml 已配抓取），
//      在 Grafana 画出来——打通「代码指标→采集→可视化」全链路；
//   2. 给 /api/echo 加更严的输入校验，体会「信任边界」：客户端输入永远不可信；
//   3. httplib 是 thread-per-connection，压测下线程数会涨——想想怎么用 thread_pool 改造。

#include "playground/logger.hpp"
#include "playground/metrics.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

using playground::metrics::Registry;

void register_routes(httplib::Server& server,
                     std::shared_ptr<std::atomic<int>> in_flight) {
    // 注意：这里故意不把 Registry 引用按引用捕获进 handler——
    // 「局部引用变量的地址」会随栈帧销毁而悬空（本仓库的真实踩坑案例），
    // handler 存活期跨越 register_routes 生命周期，必须在调用点取单例。
    auto count = [](const char* path) -> playground::metrics::Counter& {
        return Registry::instance().counter("playground_http_requests_total",
                                            "HTTP 请求总数", {{"path", path}});
    };

    server.Get("/healthz", [&count](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        count("/healthz").increment();
    });

    server.Get("/api/stats", [&count](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body = {
            {"service", "cpp-playground"},
            {"version", "0.2.0"},
            {"endpoints", {"/healthz", "/api/stats", "/api/echo", "/metrics"}},
        };
        res.set_content(body.dump(), "application/json");
        res.status = 200;
        count("/api/stats").increment();
    });

    server.Post("/api/echo", [&count](const httplib::Request& req, httplib::Response& res) {
        count("/api/echo").increment();
        // 解析失败返回 400 而不是 500：客户端错误 vs 服务端错误的分界
        nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            res.status = 400;
            res.set_content(R"({"error":"invalid json"})", "application/json");
            return;
        }
        body["echoed"] = true;
        res.set_content(body.dump(), "application/json");
        res.status = 200;
    });

    server.Get("/metrics", [in_flight](const httplib::Request&, httplib::Response& res) {
        auto& reg = Registry::instance();
        // 抓取瞬间同步一次 gauge，暴露「处理中」的瞬时水位
        reg.gauge("playground_http_in_flight", "正在处理的请求数").set(in_flight->load());
        res.set_content(reg.expose(), "text/plain; version=0.0.4");
        res.status = 200;
    });
}

// in_flight 配对计数：pre_routing_handler 请求进来 +1，set_logger 请求完成 -1。
// shared_ptr 值捕获：handler 生命周期由 httplib 管理，栈引用会悬空（见 register_routes 注释）。
// 练习：为什么不能在 pre_routing_handler 里放 thread_local RAII 对象？
// （httplib 线程会被下一个请求复用，析构时机会被推迟到「下一个请求到来」。）
void install_in_flight_middleware(httplib::Server& server,
                                  std::shared_ptr<std::atomic<int>> in_flight) {
    server.set_pre_routing_handler([in_flight](const httplib::Request&, httplib::Response&) {
        in_flight->fetch_add(1);
        return httplib::Server::HandlerResponse::Unhandled;
    });
    server.set_logger([in_flight](const httplib::Request&, const httplib::Response&) {
        in_flight->fetch_sub(1);
    });
}

int run_self_test() {
    httplib::Server server;
    auto in_flight = std::make_shared<std::atomic<int>>(0);
    register_routes(server, in_flight);
    install_in_flight_middleware(server, in_flight);

    int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        std::cerr << "bind 失败\n";
        return 1;
    }
    // listen_after_bind() 进入阻塞事件循环，放后台线程；主线程发请求做断言
    std::thread server_thread([&server] { server.listen_after_bind(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    httplib::Client client(base);
    int failures = 0;

    auto expect = [&failures](bool cond, const std::string& what) {
        std::cout << (cond ? "[PASS] " : "[FAIL] ") << what << '\n';
        if (!cond) ++failures;
    };

    auto r1 = client.Get("/healthz");
    expect(r1 && r1->status == 200, "GET /healthz -> 200");

    auto r2 = client.Post("/api/echo", R"({"msg":"hello"})", "application/json");
    expect(r2 && r2->status == 200 && r2->body.find("\"echoed\":true") != std::string::npos,
           "POST /api/echo 回显并加 echoed 字段");

    auto r3 = client.Post("/api/echo", "not-a-json", "application/json");
    expect(r3 && r3->status == 400, "POST /api/echo 非法 JSON -> 400");

    auto r4 = client.Get("/api/stats");
    expect(r4 && r4->status == 200 && r4->body.find("cpp-playground") != std::string::npos,
           "GET /api/stats 返回 JSON");

    auto r5 = client.Get("/metrics");
    expect(r5 && r5->status == 200 &&
               r5->body.find("playground_http_requests_total") != std::string::npos,
           "GET /metrics 输出 Prometheus 文本（含 counter）");

    server.stop();
    server_thread.join();
    std::cout << (failures == 0 ? "自测全部通过\n" : "存在失败项\n");
    return failures == 0 ? 0 : 1;
}

int run_serve(const std::string& host, int port) {
    auto log = playground::Logger::get();

    httplib::Server server;
    auto in_flight = std::make_shared<std::atomic<int>>(0);
    register_routes(server, in_flight);
    install_in_flight_middleware(server, in_flight);

    std::cout << "listening on " << host << ':' << port << '\n';
    log->info("http_server 启动于 {}:{}", host, port);
    if (!server.listen(host, port)) {
        log->error("listen 失败: {}:{}", host, port);
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "selftest";
    if (mode == "serve") {
        int port = argc >= 3 ? std::atoi(argv[2]) : 18080;
        return run_serve("0.0.0.0", port);
    }
    // 默认/显式 selftest：容器 HEALTHCHECK 也在用
    return run_self_test();
}

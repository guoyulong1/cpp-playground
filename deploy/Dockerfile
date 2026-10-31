# syntax=docker/dockerfile:1
# 多阶段构建：builder 编译 → runtime 只带产物（系统部署工程师的第一课）
# 构建：docker build -f deploy/Dockerfile -t cpp-playground:http-demo .
# 运行：docker run -p 18080:18080 cpp-playground:http-demo
# 验证：curl localhost:18080/metrics

# ---------- 阶段 1：构建 ----------
FROM debian:bookworm AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake ninja-build protobuf-compiler libprotobuf-dev \
        curl unzip xz-utils ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# 预热第三方依赖（走镜像加速下载）后用 cn preset 离线配置（覆盖为 Release）；容器里没有 ROS2，模块自动跳过
RUN ./scripts/fetch-deps-cn.sh \
    && cmake --preset cn -DCMAKE_BUILD_TYPE=Release \
    && cmake --build --preset cn --target http_server

# ---------- 阶段 2：运行时（最小化） ----------
# debian-slim 而非 distroless：保留 sh 方便 exec 进去排查（练习：换 distroless 有什么得失？）
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --uid 10001 appuser
# 非 root：容器逃逸时的纵深防御（K8s runAsNonRoot 的前提）
USER appuser
WORKDIR /app
COPY --from=builder /src/build/cn/http_server /usr/local/bin/http_server
EXPOSE 18080
# 健康检查让编排系统能自动重启坏实例（自测模式：起随机端口→请求断言→退出）
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD ["/usr/local/bin/http_server", "selftest"]
ENTRYPOINT ["/usr/local/bin/http_server"]
CMD ["serve", "18080"]

# 系统部署学习方案（`deploy/` + `src/network/http_server.cpp`）

对应岗位：**系统部署工程师 / DevOps / SRE**。仓库里有一条完整的「可观测服务 → 容器 → 编排 → 监控 → 自动化」练习链。

## 0. 部署对象

`http_server`（cpp-httplib REST）：`/healthz` 存活探针、`/api/echo`、`/api/stats`、`/metrics`（Prometheus 文本）。
指标由手写的 `playground::metrics`（counter/gauge）产生——**代码侧的最后一个函数就是运维的第一手数据**。

## 1. 镜像：`deploy/Dockerfile`

多阶段构建三件套：

- **builder**（debian:bookworm + g++/cmake/protoc）：预热依赖 → cn preset → 编译；
- **runtime**（bookworm-slim + libstdc++6）：只拷一个二进制；
- **安全基线**：专用 UID 10001 非 root 用户、HEALTHCHECK（自测模式当探针）。

**练习阶梯**：
- [ ] L1：`docker build` → `docker run -p 18080:18080` → `curl /metrics` 全链路
- [ ] L1：对比 builder 与 runtime 的镜像体积（`docker images`），理解分层与多阶段的收益
- [ ] L2：换 `gcr.io/distroxy/cc`（distroless）基础镜像——没有 shell，`docker exec` 进不去，安全性 vs 可调试性
- [ ] L2：`dive` 工具分析镜像层，找出可精简的层
- [ ] L3：buildx 多平台（linux/amd64 + arm64——Jetson 边缘部署的前置技能）

## 2. 编排：`deploy/docker-compose.yml`

关键设计：`external: true` 加入本机 dev-stack 的网络，让 **dev-stack 的 Prometheus 直接抓取本服务的 /metrics**——跨 compose 栈服务发现的标准做法。

```bash
# 前置：本机监控栈已起（~/dev-stack && docker compose up -d）
docker compose -f deploy/docker-compose.yml up -d
curl localhost:18080/metrics
# Grafana（localhost:3000）→ Explore → 查 playground_http_requests_total
```

- [ ] L1：给 dev-stack 的 `prometheus.yml` 加抓取 job（`cpp-playground-http:18080`）并热加载
- [ ] L1：压测 `wrk -t2 -c50 http://localhost:18080/healthz`，在 Grafana 看 QPS 曲线
- [ ] L2：`deploy.resources.limits` 改 32M，压测触发 OOM，观察 restart 行为（资源配额的意义）
- [ ] L2：把日志 pattern 改 JSON（logger.hpp 注释有提示）+ Loki 收集，Grafana 里按 trace_id 查链路

## 3. 裸机服务：`deploy/systemd/http-demo.service`

- [ ] L1：`sudo cp ... && systemctl enable --now`，`journalctl -u http-demo -f` 看日志
- [ ] L1：`systemctl kill -s SIGKILL`，验证 `Restart=on-failure` 自动拉起
- [ ] L2：加 `ExecReload=/bin/kill -HUP $MAINPID` + 服务代码处理 SIGHUP 热重载配置
- [ ] L2：逐项打开 `ProtectSystem/ProtectHome/PrivateTmp`，用 `systemd-analyze security http-demo` 打分

## 4. 批量自动化：`deploy/ansible/deploy-http.yml`

```bash
cd deploy/ansible
ansible-playbook -i localhost, -c local deploy-http.yml -K   # 本机演练
ansible-playbook -i inventory.ini deploy-http.yml             # 真实远端
```

- [ ] L1：连跑两遍，确认 `changed` 归零（幂等性验证——配置管理的核心价值）
- [ ] L1：`--check --diff` 干跑预演
- [ ] L2：把「编译」角色化（src 仓库 → 构建 → 分发 → 重启），对照「CI 出产物 + 只分发」的两条路线
- [ ] L3：加 `handlers` 里 `uri` 模块做部署后健康检查（smoke test 即 deployment gate）

## 5. 交付流水线：`.github/workflows/release.yml`

tag `v*` 触发：双编译器产物 → GitHub Releases；Docker 镜像 → GHCR（buildx + GHA 缓存）。

- [ ] L1：打 `v0.2.0` tag 观察 Release 页与 ghcr.io 包
- [ ] L2：给镜像加 cosign 签名（供应链安全）；SBOM（syft）附件
- [ ] L3：ArgoCD/GitOps：把 compose 换成 Helm Chart 部署到 minikube（本机已装）

## 6. K8s 方向衔接（minikube 已就绪）

- [ ] Deployment（探针用 /healthz、资源 requests/limits）+ Service + Ingress
- [ ] HPA 基于 CPU 自动扩缩容；`kubectl top pod` 验证
- [ ] ConfigMap 管端口参数（对照 ROS2 参数注入的思路同构性）

## 7. 资源

- Dockerfile 最佳实践（docs.docker.com）
- Prometheus 文本格式规范（裸 spec，配合手写 metrics 阅读）
- Google SRE 书（免费在线）——SLI/SLO/告警哲学
- Awesome systemd / Ansible docs

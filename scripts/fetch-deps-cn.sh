#!/usr/bin/env bash
# 第三方依赖预热脚本（国内被墙网络专用）：
#   依次尝试 直连 → ghproxy.net → gh-proxy.com，每个包下载后做 SHA256 校验，
#   校验不过自动换下一个源重试（镜像偶发返回 200 错误页是真实踩坑）。
#   解压到 ~/.cache/cpp-playground-deps/<name>/，`cmake --preset cn` 用它离线配置。
#   CI（境外网络）不需要本脚本——configure 时 FetchContent 直接下载。
set -euo pipefail

CACHE="${HOME}/.cache/cpp-playground-deps"
mkdir -p "$CACHE"

# key=FETCHCONTENT 名 | 解压目录 | URL | 类型 | sha256（与 cmake/ThirdParty.cmake 的 URL_HASH 一致）
declare -A PKGS=(
  [FMT]="fmt|https://github.com/fmtlib/fmt/releases/download/12.2.0/fmt-12.2.0.zip|zip|a2f4a8d51178f954e4c339007f77edd76ba0cb2e36f87a48e5a5403d9be5878f"
  [SPDLOG]="spdlog|https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz|tgz|15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67"
  [NLOHMANN_JSON]="nlohmann_json|https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz|txz|42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa"
  [HTTPLIB]="httplib|https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.63.0.tar.gz|tgz|44ab42e16926e95f7641d286bc90d739ab1c16886114d7cc5bc7a9f905c5edf8"
  [LIBZMQ]="libzmq|https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.tar.gz|tgz|6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43"
  [CPPZMQ]="cppzmq|https://github.com/zeromq/cppzmq/archive/refs/tags/v4.11.0.tar.gz|tgz|0fff4ff311a7c88fdb76fceefba0e180232d56984f577db371d505e4d4c91afd"
)

MIRRORS=("" "https://ghproxy.net/" "https://gh-proxy.com/")

fetch() {
  local url="$1" want="$2" dst="$3"
  if [[ -s "$dst" ]]; then
    if echo "$want  $dst" | sha256sum -c --status - 2>/dev/null; then
      echo "  已缓存: $(basename "$dst")"
      return 0
    fi
    rm -f "$dst"
  fi
  for m in "${MIRRORS[@]}"; do
    local full="${m}${url}"
    echo "  下载: $full"
    if curl -fSL --connect-timeout 10 --retry 1 --max-time 600 -o "$dst.part" "$full" \
       && echo "$want  $dst.part" | sha256sum -c --status - 2>/dev/null; then
      mv "$dst.part" "$dst"
      return 0
    fi
    rm -f "$dst.part"
  done
  echo "  所有源均失败: $url" >&2
  return 1
}

echo "==> 下载并解压第三方依赖到 $CACHE"
for key in FMT SPDLOG NLOHMANN_JSON HTTPLIB LIBZMQ CPPZMQ; do
  IFS='|' read -r dir url kind want_sha <<< "${PKGS[$key]}"
  archive="$CACHE/${dir}-src.${kind}"
  fetch "$url" "$want_sha" "$archive"
  if [[ ! -d "$CACHE/$dir" ]]; then
    tmp="$CACHE/.extract-$$"
    mkdir -p "$tmp"
    case "$kind" in
      zip) unzip -q "$archive" -d "$tmp" ;;
      tgz) tar -xzf "$archive" -C "$tmp" ;;
      txz) tar -xJf "$archive" -C "$tmp" ;;
    esac
    # 归档内通常是一层顶层目录，拍平到目标名（find 精确列出，不经过 shell 词法）
    inner=$(find "$tmp" -mindepth 1 -maxdepth 1 | head -1)
    mv "$inner" "$CACHE/$dir"
    rm -rf "$tmp"
  fi
done

echo ""
echo "==> 使用方式：cmake --preset cn（preset 已配置好全部 FETCHCONTENT_SOURCE_DIR_*）"

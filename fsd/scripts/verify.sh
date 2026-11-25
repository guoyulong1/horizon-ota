#!/usr/bin/env bash
# verify.sh —— 一键本地验证（任何 Linux：WSL2/x86_64 或地平线X3 板载）
# 阶段: 1) 拉取第三方源码（如缺） 2) 配置 3) 构建 4) 本地自检
# 用法: ./scripts/verify.sh
# 验证标准: --self-test 退出码 0 才视为通过。
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> [1/3] 第三方源码（如缺则拉取并校验 SHA-256）"
if [[ ! -f 3rdparty/openssl/.fsd-source-tag || ! -f 3rdparty/curl/.fsd-source-tag ]]; then
  ./3rdparty/fetch.sh
else
  echo "    已存在，跳过"
fi

echo "==> [2/3] 配置 + 构建"
cmake -S . -B build
cmake --build build

echo "==> [3/3] 本地自检 (--self-test)"
./build/firmware_secure_download --self-test

echo "verify: OK"

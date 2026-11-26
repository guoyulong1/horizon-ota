#!/usr/bin/env bash
# verify.sh —— monorepo 一键本地验证（任何 Linux：WSL2/x86_64 或地平线X3 板载）
# 聚合构建 fsd + verify + agent + pack，ctest 编排 GoogleTest 单元测试。
# 用法: ./scripts/verify.sh
# 验证标准: ctest 全绿（含 fsd_self_test 这条 --self-test 底线）。
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> [1/4] googletest 源码（如缺则拉取并校验 SHA-256）"
if [[ ! -f 3rdparty/googletest/.ota-source-tag ]]; then
  ./3rdparty/fetch.sh
else
  echo "    已存在，跳过"
fi

echo "==> [2/4] fsd 第三方源码（如缺则拉取并校验 SHA-256）"
if [[ ! -f fsd/3rdparty/openssl/.fsd-source-tag || \
      ! -f fsd/3rdparty/curl/.fsd-source-tag ]]; then
  (cd fsd && bash 3rdparty/fetch.sh)
else
  echo "    已存在，跳过"
fi

echo "==> [3/4] 配置 + 构建（聚合：fsd/verify/agent/pack）"
cmake -S . -B build
cmake --build build

echo "==> [4/4] 单元测试 (ctest + GoogleTest，含 fsd_self_test)"
ctest --test-dir build --output-on-failure

echo "verify: OK"

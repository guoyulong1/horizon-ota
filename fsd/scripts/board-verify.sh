#!/usr/bin/env bash
# board-verify.sh —— 真机验证（地平线X3 板载构建 + 自检）
# 用法: ./fsd/scripts/board-verify.sh（从任意目录执行均可）
# 环境变量:
#   BOARD_HOST      默认 root@192.168.31.35
#   BOARD_REPO_DIR  板子上 monorepo 路径，默认 /home/root/workspace/horizon-ota
# 前提: 已配好免密（.ssh/id_ed25519），且板子上已克隆本 monorepo。
set -euo pipefail
cd "$(dirname "$0")/../.."   # -> monorepo 根

HOST="${BOARD_HOST:-root@192.168.31.35}"
REPO_DIR="${BOARD_REPO_DIR:-/home/root/workspace/horizon-ota}"
SSH_KEY="$(pwd)/.ssh/id_ed25519"
KNOWN_HOSTS="$(pwd)/.ssh/known_hosts"

if [[ ! -f "$SSH_KEY" ]]; then
  echo "缺少私钥 $SSH_KEY，请先完成免密配置（见 AGENTS.md）" >&2
  exit 1
fi

SSH_OPTS=(-i "$SSH_KEY" -o UserKnownHostsFile="$KNOWN_HOSTS" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10)

echo "==> 检查板子可达性"
ssh "${SSH_OPTS[@]}" "$HOST" "uname -a"

echo "==> 同步代码并在板子上构建 + 自检"
ssh "${SSH_OPTS[@]}" "$HOST" \
  "cd '$REPO_DIR' && git fetch --all && git checkout -q main && git pull --ff-only && ./fsd/scripts/verify.sh"

echo "board-verify: OK"

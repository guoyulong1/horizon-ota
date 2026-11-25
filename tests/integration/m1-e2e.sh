#!/usr/bin/env bash
# M1 端到端集成测试：ota-pack → ota-sign → 本地 HTTPS → ota-fetch（验签 + 下载）。
#
# 用法:
#   tests/integration/m1-e2e.sh [--bin-dir <dir>]
# 不给 --bin-dir 时，用顶层 CMake 聚合构建后测试（产物在 build/bin/）。
#
# 依赖: bash, openssl, python3, curl, cmake/g++(构建时)
# 运行环境: Linux（板载 / WSL2 / CI）。
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
bin_dir=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin-dir) bin_dir="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$bin_dir" ]]; then
  cmake -S "$repo" -B "$repo/build" >/dev/null
  cmake --build "$repo/build" --target ota-pack ota-sign ota-fetch -j >/dev/null
  bin_dir="$repo/build/bin"
fi

OTA_PACK="$bin_dir/ota-pack"
OTA_SIGN="$bin_dir/ota-sign"
OTA_FETCH="$bin_dir/ota-fetch"
for bin in "$OTA_PACK" "$OTA_SIGN" "$OTA_FETCH"; do
  [[ -x "$bin" ]] || { echo "missing binary: $bin" >&2; exit 2; }
done

work="$(mktemp -d)"
srv_pid=""
cleanup() {
  [[ -n "$srv_pid" ]] && kill "$srv_pid" 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT

pass=0
fail=0
check() {
  local name="$1" want="$2" got="$3"
  if [[ "$want" == "$got" ]]; then
    echo "PASS  $name (exit $got)"; pass=$((pass + 1))
  else
    echo "FAIL  $name (want $want, got $got)"; fail=$((fail + 1))
  fi
}

# 伪造组件
mkdir -p "$work/images"
head -c 1024 /dev/urandom > "$work/images/Image"
head -c 512  /dev/urandom > "$work/images/demo.dtb"
head -c 2048 /dev/urandom > "$work/images/rootfs.squashfs"

# 打包
"$OTA_PACK" --images "$work/images" --model demo-board --version 1.2.3 \
  --dtb demo.dtb --security-version 5 --min-version-code 10000 \
  --hardware-compat "example,demo-board" \
  --hardware-compat "example,demo-board-revb" \
  --output "$work/out" >/dev/null
check "P1 pack" 0 $?

rel="$(ls -d "$work"/out/releases/demo-board/1.2.3/*/ | head -n 1)"
rel="${rel%/}"
rid="$(basename "$rel")"

# 离线签名
openssl genpkey -algorithm ed25519 -out "$work/private.pem" >/dev/null 2>&1
"$OTA_SIGN" --manifest "$rel/manifest.json" --key "$work/private.pem" \
  --output "$rel/manifest.json.sig" >/dev/null
check "P2 sign" 0 $?
pubhex="$(openssl pkey -in "$work/private.pem" -pubout -outform DER | \
  tail -c 32 | od -An -tx1 | tr -d ' \n')"

# TLS 证书 + 本地 HTTPS
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -keyout "$work/key.pem" -out "$work/cert.pem" \
  -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1
port=$(( (RANDOM % 2000) + 18000 ))
python3 "$here/serve.py" "$port" "$work/cert.pem" "$work/key.pem" "$work/out" &
srv_pid=$!
for _ in $(seq 1 50); do
  curl -sk "https://127.0.0.1:$port/" -o /dev/null 2>/dev/null && break
  sleep 0.1
done

base="https://127.0.0.1:$port"
manifest_rel="releases/demo-board/1.2.3/$rid/manifest.json"

# P3 正常取清单 + 验签 + 下载
mkdir -p "$work/fetched"
"$OTA_FETCH" --base-url "$base" --manifest "$manifest_rel" \
  --out-dir "$work/fetched" --pubkey "$pubhex" --ca-file "$work/cert.pem" \
  --model demo-board --version-code 10202 \
  --hardware-compat "example,demo-board" --layout ab >/dev/null 2>&1
check "P3 fetch success" 0 $?
for f in Image demo.dtb rootfs.squashfs; do
  if cmp -s "$work/fetched/$f" "$work/images/$f"; then
    echo "PASS  P3 content $f"; pass=$((pass + 1))
  else
    echo "FAIL  P3 content $f"; fail=$((fail + 1))
  fi
done

# P4 篡改清单 → 验签失败
cp "$rel/manifest.json" "$work/manifest.json.bak"
printf 'x' >> "$rel/manifest.json"
mkdir -p "$work/fetched_bad_manifest"
"$OTA_FETCH" --base-url "$base" --manifest "$manifest_rel" \
  --out-dir "$work/fetched_bad_manifest" --pubkey "$pubhex" \
  --ca-file "$work/cert.pem" >/dev/null 2>&1
check "P4 tampered manifest rejected" 1 $?
cp "$work/manifest.json.bak" "$rel/manifest.json"

# P5 篡改组件 → 哈希不符
printf 'x' >> "$rel/files/Image"
mkdir -p "$work/fetched_bad_component"
"$OTA_FETCH" --base-url "$base" --manifest "$manifest_rel" \
  --out-dir "$work/fetched_bad_component" --pubkey "$pubhex" \
  --ca-file "$work/cert.pem" >/dev/null 2>&1
check "P5 tampered component rejected" 1 $?

echo "----------------------------------------"
echo "RESULT: PASS=$pass FAIL=$fail"
[[ $fail -eq 0 ]]

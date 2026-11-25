#!/usr/bin/env bash
# 集成测试：本地 HTTPS + 自签 CA，验证 fsd 的安全下载行为。
#
# 用法:
#   tests/integration/run.sh --fsd <binary>   # 用已编译的 fsd
#   tests/integration/run.sh                  # 从 fsd/ 构建后测试
#
# 依赖: bash, openssl, python3, sha256sum, curl；构建时另需 cmake/g++。
# 运行环境: Linux（板载 / WSL2 / CI）。
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
fsd_bin=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --fsd) fsd_bin="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$fsd_bin" ]]; then
  ( cd "$repo/fsd"
    if [[ ! -f 3rdparty/openssl/.fsd-source-tag ]]; then ./3rdparty/fetch.sh; fi
    cmake -S . -B build >/dev/null
    cmake --build build >/dev/null )
  fsd_bin="$repo/fsd/build/firmware_secure_download"
fi
[[ -x "$fsd_bin" ]] || { echo "fsd binary not found: $fsd_bin" >&2; exit 2; }

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
    echo "PASS  $name (exit $got)"
    pass=$((pass + 1))
  else
    echo "FAIL  $name (want exit $want, got $got)"
    fail=$((fail + 1))
  fi
}

# 自签证书（SAN 含 127.0.0.1），fsd 用 --ca-file 信任它
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -keyout "$work/key.pem" -out "$work/cert.pem" \
  -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1
[[ -s "$work/cert.pem" ]] || { echo "openssl req failed" >&2; exit 2; }

head -c 65536 /dev/urandom > "$work/payload.bin"
size=$(stat -c %s "$work/payload.bin")
sha=$(sha256sum "$work/payload.bin" | cut -d' ' -f1)

port=$(( (RANDOM % 2000) + 18000 ))
python3 "$here/serve.py" "$port" "$work/cert.pem" "$work/key.pem" "$work" &
srv_pid=$!
ready=0
for _ in $(seq 1 50); do
  if curl -sk "https://127.0.0.1:$port/payload.bin" -o /dev/null 2>/dev/null; then
    ready=1
    break
  fi
  sleep 0.1
done
[[ "$ready" == "1" ]] || { echo "server not ready" >&2; exit 2; }

url="https://127.0.0.1:$port/payload.bin"

# T1 正常下载
"$fsd_bin" --url "$url" --sha256 "$sha" --expected-size "$size" \
  --ca-file "$work/cert.pem" --output "$work/out1.bin" >/dev/null 2>&1
check "T1 success" 0 $?
if cmp -s "$work/out1.bin" "$work/payload.bin"; then
  echo "PASS  T1 content"; pass=$((pass + 1))
else
  echo "FAIL  T1 content"; fail=$((fail + 1))
fi

# T2 哈希不符 → 9，且不留下输出/临时文件
zeros=$(printf '0%.0s' $(seq 1 64))
"$fsd_bin" --url "$url" --sha256 "$zeros" --expected-size "$size" \
  --ca-file "$work/cert.pem" --output "$work/out2.bin" >/dev/null 2>&1
check "T2 checksum mismatch" 9 $?
for f in "$work/out2.bin" "$work/out2.bin.part"; do
  if [[ -e "$f" ]]; then
    echo "FAIL  T2 left $(basename "$f")"; fail=$((fail + 1))
  else
    echo "PASS  T2 no $(basename "$f")"; pass=$((pass + 1))
  fi
done

# T3 明文 HTTP 拒绝 → 7
"$fsd_bin" --url "http://127.0.0.1:$port/payload.bin" --sha256 "$sha" \
  --output "$work/out3.bin" >/dev/null 2>&1
check "T3 reject plain http" 7 $?

# T5 清单驱动下载 → 0
cat > "$work/pkg.download" <<EOF
name=payload
url=$url
sha256=$sha
size=$size
output=$work/out5.bin
EOF
"$fsd_bin" --manifest "$work/pkg.download" --ca-file "$work/cert.pem" >/dev/null 2>&1
check "T5 manifest download" 0 $?

# T6 断点续传（预置 .part 前半段）→ 0，内容一致
head -c $((size / 2)) "$work/payload.bin" > "$work/out6.bin.part"
"$fsd_bin" --url "$url" --sha256 "$sha" --expected-size "$size" \
  --ca-file "$work/cert.pem" --output "$work/out6.bin" --resume >/dev/null 2>&1
check "T6 resume" 0 $?
if cmp -s "$work/out6.bin" "$work/payload.bin"; then
  echo "PASS  T6 content"; pass=$((pass + 1))
else
  echo "FAIL  T6 content"; fail=$((fail + 1))
fi

echo "----------------------------------------"
echo "RESULT: PASS=$pass FAIL=$fail"
[[ $fail -eq 0 ]]

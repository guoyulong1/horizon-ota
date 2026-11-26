#!/usr/bin/env bash
# Author: GuoYuLong
# monorepo 根级第三方：单元测试框架。
#   google/googletest  v1.17.0（GTest + CTest 编排，构建单元测试时需要）
# 设备端传输/摘要依赖（OpenSSL/curl）由 fsd/3rdparty/fetch.sh 负责。
# 下载固定走 IPv4，归档与 Git 标签一致，校验不过拒绝解压。

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
work="${root}/.cache"
mkdir -p "${work}"

have_tag() {
  local dest="$1"
  local tag="$2"
  [[ -f "${dest}/.ota-source-tag" && "$(cat "${dest}/.ota-source-tag")" == "${tag}" ]]
}

# googletest v1.17.0 的 codeload 归档（目标板实测）。
googletest_sha256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c

sha256_matches() {
  local file="$1"
  local expected="$2"
  local actual
  actual="$(sha256sum "${file}" | awk '{print $1}')"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "checksum mismatch for ${file}" >&2
    echo "expected ${expected}" >&2
    echo "actual   ${actual}" >&2
    rm -f "${file}"
    return 1
  fi
}

download() {
  local dest="$1"
  local expected="$2"
  local url="$3"
  echo "get ${url}"
  local attempt
  for attempt in 1 2 3 4 5; do
    rm -f "${dest}"
    # codeload 不支持 Range，失败后整包重下，不使用续传。
    if curl --ipv4 --http1.1 -fL --max-time 0 -o "${dest}" "${url}"; then
      if sha256_matches "${dest}" "${expected}"; then
        return 0
      fi
      echo "rejected ${url}" >&2
      break
    fi
    echo "retry ${attempt} ${url}" >&2
    sleep 2
  done
  echo "failed ${url}" >&2
  rm -f "${dest}"
  return 1
}

extract_to() {
  local archive="$1"
  local dest="$2"
  local tag="$3"
  rm -rf "${dest}"
  mkdir -p "${dest}"
  tar -xzf "${archive}" -C "${dest}" --strip-components=1
  printf '%s\n' "${tag}" > "${dest}/.ota-source-tag"
}

# googletest 的 GitHub 发布页不提供与 codeload 字节一致的源码包，
# 这里只走 codeload 一条链路，校验不过就拒绝解压。
if have_tag "${root}/googletest" "v1.17.0"; then
  echo "keep googletest v1.17.0"
else
  download "${work}/googletest.tar.gz" "${googletest_sha256}" \
    "https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.17.0"
  extract_to "${work}/googletest.tar.gz" "${root}/googletest" "v1.17.0"
fi

echo "googletest $(cat "${root}/googletest/.ota-source-tag")"

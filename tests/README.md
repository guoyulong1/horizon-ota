# tests

单元测试（CTest）与集成测试。

## 单元测试（CTest）

根目录 `cmake -S . -B build && cmake --build build` 后：

```bash
cd build && ctest --output-on-failure
```

| 测试 | 覆盖 |
|---|---|
| `pack_crypto` | pack 的 SHA-256 已知向量、文件哈希（OpenSSL 系统版） |
| `verify_crypto` | 公钥十六进制解析、Ed25519 验签（签名现场生成） |
| `fsd_self_test` | fsd 的 SHA-256/MD5 向量与清单解析 |

## 集成测试（`tests/integration/`）

- `run.sh`：驱动 `fsd` 二进制，验证下载行为（T1/T2/T3/T5/T6）。
- `m1-e2e.sh`：M1 端到端（`ota-pack` → `ota-sign` → 本地 HTTPS → `ota-fetch` 验签 + 下载 + 篡改拒绝）。
- `serve.py`：支持 Range 的本地 HTTPS 测试服务器。

```bash
tests/integration/run.sh
tests/integration/m1-e2e.sh
tests/integration/m1-e2e.sh --bin-dir <dir>   # 用现成二进制
```

运行环境：Linux（板载 / WSL2 / CI），需要 `openssl`、`python3`、`curl`，构建时另需 `cmake`、`g++`。

## 状态

- `run.sh`：在地平线 X3 板载实测通过（PASS=9 FAIL=0）。
- `m1-e2e.sh`：在地平线 X3 板载实测通过（PASS=8 FAIL=0）。
- CTest 单元测试：需在 Linux 环境跑一次 `ctest` 确认。

## 参考

- 测试用例：`docs/OTA.md` 5.2

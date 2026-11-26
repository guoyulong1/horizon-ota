# verify

设备端**验签库**：发布清单验签（Ed25519）+ 适用性判断。纯函数、无状态、
无网络/下载/状态编排，**不依赖 fsd API**。边界见 `docs/adr/0001`。

## 职责（`docs/OTA.md` ③④）

- `VerifyEd25519` / `ParseHexPublicKey`：对清单**原始字节**的 Ed25519 验签
  （OpenSSL EVP；摘要参数为空、一次喂完）。
- `ParseManifest`：发布清单 JSON 解析 + schema 校验。
- `CheckApplicability`：适用性判断（model / hardware_compat / layouts /
  security_version 下限 / min_version_code / version_code），输出 `Decision`。

**不做**：② 发现、⑤ 下载、⑧ 安装、状态机、上报——这些是 `agent/` 的职责。

## 构建

monorepo 聚合构建（`./scripts/verify.sh`）自动包含本模块，OpenSSL 复用 fsd
构建的同一份（`fsd_openssl` 接口目标）。单独构建与 fsd 同构，需要系统 OpenSSL：

```bash
cd verify
cmake -S . -B build
cmake --build build
./build/ota-verify --self-test
```

## ota-verify 命令行

```bash
# 内置自检（离线）：生成 Ed25519 密钥对 → 签名样例清单 → 验签往返
# （篡改/坏长度/错钥拒绝）→ 清单解析与适用性用例。退出码 0 为通过。
# ctest 底线条目：verify_self_test。
ota-verify --self-test

# 用给定公钥验一份清单的签名。
# 退出码：0 通过；1 验签失败；2 用法/IO 错误。
ota-verify check --manifest <manifest.json> --sig <manifest.json.sig> \
    --pubkey <64hex>
```

## 模块

| 文件 | 职责 |
|---|---|
| `src/crypto.*` | Ed25519 验签（OpenSSL EVP） |
| `src/manifest.*` | 清单解析 + schema 校验 + 适用性（纯函数，便于 gtest） |
| `src/ota_verify.cc` | `ota-verify` CLI 入口（--self-test / check） |

## 测试

- 单元测试：`tests/unit/test_verify_crypto.cc`、`tests/unit/test_verify_manifest.cc`
- ctest 底线：`verify_self_test`（`ota-verify --self-test`）
- M2 历史（编排部分已迁出）：见 `PLAN.md` 与 `docs/adr/0001`

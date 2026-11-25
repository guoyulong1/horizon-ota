# verify

设备端「发布清单验签 + 适用性判断 + 触发下载」的最小实现（M1），M2 起长成 OTA 代理。

## ota-fetch

链接 `fsd` 静态库，复用它做 HTTPS 传输与组件下载：

1. `fsd::HttpClient` 取 `manifest.json` + `.sig`（**无摘要**，`max_bytes` 限制）。
2. OpenSSL `EVP_DigestVerify`（Ed25519）验证清单**原始字节**。
3. nlohmann 解析，校验 `schema_version`。
4. 适用性判断：`model`、`hardware_compat`、`layouts`、`version_code` / `min_version_code`。
5. 用 `fsd::SecureDownloader` 下载各组件（`url` + `sha256` + `expected_size` + `resume`）。

```bash
ota-fetch --base-url https://ota.example.com \
          --manifest releases/demo-board/1.2.3/<release_id>/manifest.json \
          --out-dir /data/ota/staging/<release_id> \
          --pubkey <64-hex> \
          --ca-file /etc/ota/ca.pem \
          --model demo-board --version-code 10202 --layout ab
```

## 依赖

- `fsd` 静态库（同属设备端，通过顶层 `CMakeLists.txt` 的 `add_subdirectory(fsd)` 提供）。
- OpenSSL、nlohmann/json（`3rdparty/nlohmann`）。

## 说明

- `--pubkey` 是设备内置的 32 字节原始 Ed25519 公钥（64 个十六进制字符）。
- 清单取回后先验签、再解析；验签失败不下载任何组件。
- 这是 M1 的最小版，后续把状态机、安装、确认、回滚并入 `agent/`。

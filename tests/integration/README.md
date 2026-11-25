# tests/integration

本地 HTTPS 集成测试：用自签 CA 启一个支持 Range 的 HTTPS 服务器，直接驱动 `fsd` 二进制验证下载行为。

## 运行

```bash
# 在 Linux 上（板载 / WSL2 / CI）：
tests/integration/run.sh                 # 自动构建 fsd 再测
tests/integration/run.sh --fsd /path/to/firmware_secure_download   # 用现成二进制
```

依赖：`bash`、`openssl`、`python3`、`sha256sum`、`curl`；构建时另需 `cmake`、`g++`。

## 覆盖

| 用例 | 行为 | 期望 |
|---|---|---|
| T1 | 正常下载（哈希一致） | 退出 0，内容一致 |
| T2 | 哈希不符 | 退出 9，不留下输出与 `.part` |
| T3 | 明文 HTTP | 退出 7（协议错误） |
| T5 | 清单驱动下载 | 退出 0 |
| T6 | 断点续传（预置 `.part` 前半段） | 退出 0，内容一致 |

## 待补

- T4 声明大小不符（退出码待确认）
- T7 服务器不支持 Range → 自动整包
- 弱网 / 低速中止（`tc netem`）
- 签名与清单校验（属于 `verify/` 模块，接入后再补 e2e）

## 参考

- 用例定义：`docs/OTA.md` 5.2

# tests

集成测试：在本地 HTTPS 服务器上验证下载模块的安全行为。

## 结构

- `tests/integration/`：本地 HTTPS + 自签 CA + Range 服务器，驱动 `fsd` 二进制。见 [integration/README.md](integration/README.md)。

## 运行

```bash
tests/integration/run.sh                 # 自动构建 fsd 再测
tests/integration/run.sh --fsd <binary>  # 用现成二进制
```

运行环境：Linux（板载 / WSL2 / CI），需要 `openssl`、`python3`、`cmake`、`g++`、`curl`。

## 状态

基础 harness 已提供（T1/T2/T3/T5/T6），已在地平线 X3 板载（ARM64 / Ubuntu，Python 3.10 / OpenSSL 3.0）实测通过（PASS=9 FAIL=0）。T4/T7、弱网、签名 e2e 待补。

## 参考

- 测试用例：`docs/OTA.md` 5.2

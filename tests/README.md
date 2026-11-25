# tests

集成测试：在本地 HTTPS 服务器上验证下载模块的安全行为。

## 计划

- `tests/integration/`：本地 HTTPS + 自签 CA，覆盖
  - 正常下载（哈希一致）
  - 组件篡改 → 校验失败、临时文件被删
  - 明文 HTTP → 拒绝
  - 声明大小不符 → 拒绝
  - 断点续传（Range）
  - 清单驱动下载
- 运行环境：Linux（板载 / WSL2 / CI），需要 `openssl`、`python3`、`cmake`、`g++`

## 状态

待实现（下一步）。

## 参考

- 测试用例：`docs/OTA.md` 5.2

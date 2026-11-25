# agent

OTA 代理：设备端状态机与编排。

## 职责

- ①–⑩ 状态机（见 `docs/OTA.md` 1.3）
- 调用 `verify` 验签、`fsd` 下载
- 安装后端（A/B 或 recovery）、提交、确认、回滚
- 上报与清理

## 状态

待实现（M2–M4）。

## 参考

- 设备端流程：`docs/OTA.md` 第 4 章
- 安装方案：`docs/OTA.md` 4.8

# verify 是纯验签库，agent 是唯一的设备端编排者

M2 阶段「verify → OTA 代理」的演进让 `verify/` 目录同时装下了验签、适用性判断与
状态机/下载编排，模块地图里 verify 与 agent 的职责边界名存实亡（新增编排能力继续
堆进 verify，还是建 agent/，没有依据可查）。决定：**`verify/` 收缩为纯验签库**，
只拥有「③ 发布清单验签（Ed25519）+ ④ 适用性判断」——纯函数、无状态、不做下载/
发现/安装编排、不依赖 fsd API（monorepo 内经 `fsd_openssl` 接口目标共用 fsd 构建
的同一份 OpenSSL，单独构建用系统 OpenSSL），并提供 `ota-verify --self-test` 自检
（与 fsd 同构：库 + 可执行程序）；**`agent/` 是唯一的状态机与流程编排拥有者**
（② 发现、⑤ 下载编排、⑦ 复核、⑧ 安装、⑨ 确认/回滚、⑩ 上报），也是唯一同时
链接 `fsd` 与 `verify` 的模块。跨模块通信仍只走发布清单 JSON 契约。

## Considered Options

- **继续让 verify 承载代理（M2 现状）**：目录名与职责永久不符；M3 安装、M5 订阅
  形态替换都会继续堆积在 verify 里，最终长成一个说不清边界的「大杂烩」目录。
- **拆分（采用）**：AGENTS.md 模块地图、`docs/OTA.md` 7.2 与目录一一对应；verify
  保持纯函数、易做 gtest；订阅形态 / 安装后端的演进只动 agent，verify 接口稳定。

## Consequences

- 编排类改动（Discovery 替换、安装后端、上报通道）只进 `agent/`；验签/适用性
  规则改动只进 `verify/`。
- **不越界规则**：新能力先归入对应职责的模块；现有接口不足以表达时，先扩接口
  再调用，禁止跨目录「顺手」实现别模块的职责。
- `fsd_openssl` 只暴露头文件与 libcrypto 静态库；任何模块不得借它引用 fsd API。
